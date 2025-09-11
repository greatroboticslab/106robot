# central_script.py

import cv2
import numpy as np
import paho.mqtt.client as mqtt
import threading
import time
import json
import math
import sys
import base64
import gpsd
import os
from datetime import datetime
import serial
import csv

#.py files
import IMU  # Importing the IMU module
import GUI

from flask import Flask, Response, jsonify, request, render_template_string
import logging
from multiprocessing import Process, Queue, Event
from face_tracking import face_tracking_process
from auto_navigation import auto_navigation_process

# MQTT Configuration
MQTT_SERVER = "192.168.1.145"  # Update if different
MQTT_PORT = 1883
MQTT_TOPIC_COMMAND = "robot/control"
MQTT_RAIL_TOPIC_COMMAND = "robot/rail"
MQTT_TOPIC_DETECTIONS = "robot/detections"
MQTT_TOPIC_CAMERA = "robot/camera"
MQTT_TOPIC_PUMP = "robot/pump"
MQTT_TOPIC_REMOTE_PUMP = "robot/remotepump"
MQTT_TOPIC_IMU = "imu/data"
MQTT_TOPIC_DATA = "moisture/data"

app = Flask(__name__)

# Global variables
latest_detection = None
latest_camera_frame = None
output_frame = None
lock = threading.Lock()
e_stop_active = False  # E-Stop state
moisture_threshold = 100  # For pump control with moisture

# GPS and heading data
current_lat, current_lon = None, None
robot_heading = 0.0
gps_data = []
gps_data_lock = threading.Lock()

# PID Controller Parameters
w, h = 640, 480  # Frame dimensions for visualization (can be adjusted)
center = w // 2

# Configuration Flags
ENABLE_FRAME_FLIP = True  # Set to False to disable frame flipping
INVERT_YAW_CONTROL = False  # Set to True if robot moves opposite to desired direction

# Mode Control
current_mode = 'basic_movement'  # Default mode

# Queues for inter-process communication
command_queue = Queue()
detection_queue = Queue()
camera_frame_queue = Queue()
gps_data_queue = Queue()
imu_queue = Queue()

# Events to control processes
stop_event = Event()

# CSV section

filename = 'moisture_data.csv'
file_exists = os.path.isfile(filename)

csv_file = open(filename, mode='a', newline='')
writer = csv.writer(csv_file)
if not file_exists:
    writer.writerow(['Timestamp', 'Mac Address', 'Data'])
    

# Mainly for the Remote Pump Controls
zone_A_macs = {"C0:49:EF:69:BF:DC", "BB:BB:BB:BB:BB:BB"} # Change these into different mac addresses or add in the new mac
zone_B_macs = {"AA:AA:AA:AA:AA:AA", "BB:BB:BB:BB:BB:BB"} 
zone_C_macs = {"AA:AA:AA:AA:AA:AA", "BB:BB:BB:BB:BB:BB"} # Not using Zone C for demo 6/16/2025

# Independent Thresholds for moisture
zone_threshold = {
    "A": 10,
    "B": 10,
    "C": 10 
}
zone_macs = {
    "A": zone_A_macs,
    "B": zone_B_macs,
    "C": zone_C_macs 
}

pump_states = {
    "A": False,
    "B": False,
    "C": False
}
    

def on_message(client, userdata, msg):
    try:
        if msg.topic == MQTT_TOPIC_DETECTIONS:
            detection_data = json.loads(msg.payload.decode())
            detection_queue.put(detection_data)
        elif msg.topic == MQTT_TOPIC_CAMERA:
            camera_data = json.loads(msg.payload.decode())
            image_b64 = camera_data.get('image', '')
            if image_b64:
                image_bytes = base64.b64decode(image_b64)
                np_arr = np.frombuffer(image_bytes, np.uint8)
                image = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
                if image is not None:
                    camera_frame_queue.put(image)
                else:
                    print("Failed to decode camera image.")
            else:
                print("No image data found in camera message.")
        elif msg.topic == MQTT_TOPIC_DATA:
            sensor_data = json.loads(msg.payload.decode())
            mac = sensor_data.get("mac")
            value = sensor_data.get("value")
        
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            #print(f"Received | {timestamp}, {mac}, {value}")
            writer.writerow([timestamp, mac, value])
            csv_file.flush()
            
            cmd_value = int(value)
            
            """
            # Not really using this anymore
            if cmd_value < 10:
                client.publish(MQTT_TOPIC_PUMP, "1")
            else:
                client.publish(MQTT_TOPIC_PUMP, "0")
            """
            
            # Make a list of zones for the mac addresses
            # If value in zone A, B, etc is low
                # Set Zone pump on "zone cmd"
            # else
                # Set zones pump off "zone cmd"
                
            zone = None
            for z, macs in zone_macs.items():
                if mac in macs:
                    zone = z
                    break
            
            # Do actions based on findings
            if zone:
                threshold = zone_threshold[zone]
                if value < threshold:
                    pump_cmd = f"{zone} 1"
                    print(f"[ZONE {zone}] Moisture below threshold. Turning Pump ON")
                    client.publish(MQTT_TOPIC_REMOTE_PUMP, pump_cmd)
                    pump_states[zone] = True
                elif value >= threshold:
                    pump_cmd = f"{zone} 0"
                    print(f"[ZONE {zone}] Moisture good. Turning Pump OFF")
                    client.publish(MQTT_TOPIC_REMOTE_PUMP, pump_cmd)
                    pump_states[zone] = False
                
            else:
                print(f"Unknown MAC address {mac}. Ignoring.")

    
    except Exception as e:
        print(f"Error handling message on topic {msg.topic}: {e}")

# Initialize MQTT Client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_SERVER, MQTT_PORT, 60)
client.loop_start()

# Initialize IMU
IMU.detectIMU()
if IMU.BerryIMUversion == 99:
    print("No BerryIMU found... exiting")
    sys.exit()
IMU.initIMU()

# Connect to GPSD
gpsd.connect()

def calculate_heading():
    """
    Calculate the robot's heading using IMU readings.
    Returns the heading in degrees.
    """
    ACCx = IMU.readACCx()
    ACCy = IMU.readACCy()
    ACCz = IMU.readACCz()
    MAGx = IMU.readMAGx()
    MAGy = IMU.readMAGy()
    MAGz = IMU.readMAGz()

    # Normalize accelerometer raw values.
    acc_magnitude = math.sqrt(ACCx ** 2 + ACCy ** 2 + ACCz ** 2)
    if acc_magnitude == 0:
        print("Error: Accelerometer magnitude is zero.")
        return 0
    accXnorm = ACCx / acc_magnitude
    accYnorm = ACCy / acc_magnitude

    pitch = math.asin(accXnorm)
    if math.cos(pitch) == 0:
        roll = 0
    else:
        roll = -math.asin(accYnorm / math.cos(pitch))

    # Tilt compensation
    magXcomp = MAGx * math.cos(pitch) + MAGz * math.sin(pitch)
    magYcomp = (MAGx * math.sin(roll) * math.sin(pitch) +
               MAGy * math.cos(roll) -
               MAGz * math.sin(roll) * math.cos(pitch))

    heading = math.degrees(math.atan2(magYcomp, magXcomp))
    if heading < 0:
        heading += 360

    return heading

def receive_gps_data():
    global current_lat, current_lon
    try:
        packet = gpsd.get_current()
        if packet.mode >= 2:
            current_lat = packet.lat
            current_lon = packet.lon
            return current_lat, current_lon
        else:
            return None, None
    except Exception as e:
        print(f"Error retrieving GPS data: {e}")
        return None, None

def main_loop():
        global latest_detection, latest_camera_frame
        global output_frame, lock, e_stop_active
        global current_lat, current_lon, robot_heading, gps_data
        global current_mode
        check = False

        # Initialize last_img with a black image
        last_img = np.zeros((h, w, 3), dtype=np.uint8)

        try:
            while True:
                # Update last_img if a new frame is available
                while not camera_frame_queue.empty():
                    last_img = camera_frame_queue.get()

                # Use the last available frame
                img = last_img.copy()

                # Optionally flip the frame
                if ENABLE_FRAME_FLIP:
                    img = cv2.flip(img, 1)  # Flip the image horizontally
                # ... (additional processing like drawing text, handling modes, etc.)
                # Acquire lock to set the global output frame
                with lock:
                    output_frame = img.copy()
                # Small sleep to reduce CPU usage
                time.sleep(0.05)

                # Display center offset on the frame
                cv2.putText(img, f"Mode: {current_mode}", (10, 30),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                # Optionally display IMU heading
                imu_heading = calculate_heading()
                cv2.putText(img, f"IMU Heading: {imu_heading:.2f}", (10, 60),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

                # Optionally display E-Stop status
                if e_stop_active:
                    cv2.putText(img, "E-STOP ACTIVE!", (10, 90),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

                # Update GPS data
                current_lat, current_lon = receive_gps_data()
                if current_lat is not None and current_lon is not None:
                    with gps_data_lock:
                        gps_data.append({
                            'GPS_Lat': current_lat,
                            'GPS_Lon': current_lon,
                            'Heading': imu_heading
                        })

                # Check for e-stop activation
                if e_stop_active:
                    print("E-Stop is active. Stopping the robot.")
                    front_back_command = 64  # Stop
                    side_side_command = 64   # Neutral steering
                    command_string = f"{front_back_command} {side_side_command}"
                    client.publish(MQTT_TOPIC_COMMAND, command_string)
                    with lock:
                        output_frame = img.copy()
                    time.sleep(0.1)
                    continue  # Skip the rest of the loop

                # Handle different modes
                if current_mode == 'face_tracking':
                    # Send data to face tracking process
                    check = False
                    if not detection_queue.empty():
                        detection = detection_queue.get()
                        command_queue.put(('face_tracking', detection))
                    else:
                        # No detection received; stop the robot
                        front_back_command = 64  # Stop
                        side_side_command = 64   # Neutral steering
                        command_string = f"{front_back_command} {side_side_command}"
                        print("Stopping (no detection received)")
                        client.publish(MQTT_TOPIC_COMMAND, command_string)
                elif current_mode == 'auto_navigation':
                    # Auto-Navigation Mode
                    check = False
                    pass  # Handled by auto_navigation_process
                elif current_mode == 'basic_movement':
                    # Basic Movement Mode
                    if not check:
                        print("Basic Movement mode is active.")
                        check = True
                    # No automatic commands; manual movement commands are handled via Flask routes
                else:
                    # Unknown mode; stop the robot for safety
                    front_back_command = 64  # Stop
                    side_side_command = 64   # Neutral steering
                    check = False
                    command_string = f"{front_back_command} {side_side_command}"
                    print("Unknown mode. Stopping the robot.")
                    client.publish(MQTT_TOPIC_COMMAND, command_string)

                # Acquire lock to set the global output frame
                with lock:
                    output_frame = img.copy()

                # Small sleep to reduce CPU usage
                time.sleep(0.05)

        except Exception as e:
            print(f"An error occurred: {e}")

        finally:
            # Cleanup
            front_back_command = 64  # Stop
            side_side_command = 64   # Neutral steering
            command_string = f"{front_back_command} {side_side_command}"
            client.publish(MQTT_TOPIC_COMMAND, command_string)
            client.loop_stop()
            client.disconnect()

def generate():
    global output_frame, lock
    # Continuously generate frames
    while True:
        with lock:
            if output_frame is None:
                continue
            # Encode the frame in JPEG format
            (flag, encoded_image) = cv2.imencode(".jpg", output_frame)
            if not flag:
                continue
        # Yield the output frame in byte format
        yield(b'--frame\r\n' b'Content-Type: image/jpeg\r\n\r\n' +
              bytearray(encoded_image) + b'\r\n')
        time.sleep(0.05)  # Adjust to control frame rate


  # (Same as in your original script)

html_content = GUI.html_content

# Define Flask route functions here
@app.route("/")
def index():
    return render_template_string(html_content)

@app.route("/video_feed")
def video_feed():
    return Response(generate(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")

@app.route("/estop", methods=['POST'])
def estop():
    global e_stop_active
    e_stop_active = True
    print("E-Stop activated!")
    front_back_command = 64  # Stop
    side_side_command = 64   # Neutral steering
    command_string = f"{front_back_command} {side_side_command}"
    client.publish(MQTT_TOPIC_COMMAND, command_string)
    return jsonify({"status": "E-Stop activated"})

@app.route("/undo_estop", methods=['POST'])
def undo_estop():
    global e_stop_active
    e_stop_active = False
    print("E-Stop deactivated!")
    return jsonify({"status": "E-Stop deactivated"})

@app.route("/increase_face_area", methods=['POST'])
def increase_face_area():
    # Handle in face_tracking.py
    command_queue.put(('increase_face_area', None))
    return jsonify({"status": "Face area increased"})

@app.route("/decrease_face_area", methods=['POST'])
def decrease_face_area():
    # Handle in face_tracking.py
    command_queue.put(('decrease_face_area', None))
    return jsonify({"status": "Face area decreased"})

@app.route("/move_center_left", methods=['POST'])
def move_center_left():
    # Handle in face_tracking.py
    command_queue.put(('move_center_left', None))
    return jsonify({"status": "Center moved left"})

@app.route("/move_center_right", methods=['POST'])
def move_center_right():
    # Handle in face_tracking.py
    command_queue.put(('move_center_right', None))
    return jsonify({"status": "Center moved right"})

@app.route('/get_gps_data', methods=['GET'])
def get_gps_data_route():
    with gps_data_lock:
        if gps_data:
            sanitized_gps_data = [{
                'GPS_Lat': float(item.get('GPS_Lat', 0)),
                'GPS_Lon': float(item.get('GPS_Lon', 0)),
                'Heading': float(item.get('Heading', 0)),
                'Estimated_Lat': float(item.get('Estimated_Lat', 0)),
                'Estimated_Lon': float(item.get('Estimated_Lon', 0)),
                'Estimated_Theta': float(item.get('Estimated_Theta', 0))
            } for item in gps_data]
        else:
            sanitized_gps_data = []
    return jsonify(sanitized_gps_data)

@app.route('/initial_gps', methods=['GET'])
def initial_gps():
    lat, lon = receive_gps_data()
    if lat is not None and lon is not None:
        return jsonify({"lat": lat, "lon": lon})
    else:
        print("Initial GPS data unavailable.")
        return jsonify({"lat": 0.0, "lon": 0.0})

@app.route("/set_mode", methods=['POST'])
def set_mode():
    global current_mode, stop_event
    data = request.get_json()
    mode = data.get('mode', 'basic_movement')
    if mode in ['basic_movement', 'auto_navigation', 'face_tracking']:
        current_mode = mode
        print(f"Mode set to {current_mode}")
        if current_mode == 'auto_navigation':
            stop_event.clear()
            auto_nav_proc = Process(target=auto_navigation_process, args=(command_queue, client, stop_event))
            auto_nav_proc.start()
        else:
            stop_event.set()  # Stop auto-navigation
        return jsonify({"status": f"Mode set to {current_mode}"})
    else:
        print("Invalid mode selected")
        return jsonify({"status": "Invalid mode selected"}), 400

@app.route("/move_forward", methods=['POST'])
def move_forward():
    global current_mode
    if current_mode == 'basic_movement':
        front_back_command = 126  # Max forward
        side_side_command = 64    # Neutral steering
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving forward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400
    
@app.route("/move_rail_forward", methods=['POST'])
def move_rail_forward():
    global current_mode
    if current_mode == 'basic_movement':
        move_command = 0  # Max forward
        command_string = f"{move_command}"
        client.publish(MQTT_RAIL_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving rail forward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_backward", methods=['POST'])
def move_backward():
    global current_mode
    if current_mode == 'basic_movement':
        front_back_command = 0   # Max backward
        side_side_command = 64    # Neutral steering
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving backward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_rail_backward", methods=['POST'])
def move_rail_backward():
    global current_mode
    if current_mode == 'basic_movement':
        move_command = 126   # Max backward
        command_string = f"{move_command}"
        client.publish(MQTT_RAIL_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Moving rail backward"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_left", methods=['POST'])
def move_left():
    global current_mode
    if current_mode == 'basic_movement':
        front_back_command = 64  # Stop
        side_side_command = 126    # Max left
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Turning left"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/move_right", methods=['POST'])
def move_right():
    global current_mode
    if current_mode == 'basic_movement':
        front_back_command = 64  # Stop
        side_side_command = 0   # Max right
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Turning right"})
    else:
        return jsonify({"status": "Cannot move in current mode"}), 400

@app.route("/stop_robot", methods=['POST'])
def stop_robot():
    global current_mode
    if current_mode == 'basic_movement':
        front_back_command = 64   # Stop
        side_side_command = 64    # Neutral steering
        command_string = f"{front_back_command} {side_side_command}"
        client.publish(MQTT_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Robot stopped"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400

@app.route("/pump_on", methods=['POST'])
def pump_on():
    global current_mode
    if current_mode == 'basic_movement':
        control_command = 1
        command_string = f"{control_command}"
        client.publish(MQTT_TOPIC_PUMP, command_string)
        return jsonify({"status": "Pump ON"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400
    
@app.route("/pump_off", methods=['POST'])
def pump_off():
    global current_mode
    if current_mode == 'basic_movement':
        control_command = 0
        command_string = f"{control_command}"
        client.publish(MQTT_TOPIC_PUMP, command_string)
        return jsonify({"status": "Pump OFF"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400


@app.route("/stop_rail", methods=['POST'])
def stop_rail():
    global current_mode
    if current_mode == 'basic_movement':
        move_command = 64   # Stop
        command_string = f"{move_command}"
        client.publish(MQTT_RAIL_TOPIC_COMMAND, command_string)
        return jsonify({"status": "Rail stopped"})
    else:
        return jsonify({"status": "Cannot stop in current mode"}), 400

@app.route("/update_pid", methods=['POST'])
def update_pid():
    data = request.get_json()
    kp = data.get('kp')
    ki = data.get('ki')
    kd = data.get('kd')

    if kp is not None and ki is not None and kd is not None:
        # Send PID update to face_tracking process
        command_queue.put(('update_pid', (kp, ki, kd)))
        print(f"Updated PID parameters: Kp={kp}, Ki={ki}, Kd={kd}")
        return jsonify({"status": "PID parameters updated"})
    else:
        return jsonify({"status": "Invalid PID parameters"}), 400

@app.route('/send_coordinates', methods=['POST'])
def receive_coordinates():
    data = request.get_json()
    coordinates = data.get('coordinates', [])
    if coordinates:
        command_queue.put(('set_waypoints', coordinates))
        return jsonify({"status": "Coordinates received"})
    else:
        return jsonify({"status": "No coordinates received"}), 400

if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
    # Start the main loop thread
    t = threading.Thread(target=main_loop)
    t.daemon = True
    t.start()
    # Start face tracking process
    face_track_proc = Process(target=face_tracking_process, args=(command_queue, client))
    face_track_proc.start()
    # Start pump data log process
    # Run the Flask app
    app.run(host='0.0.0.0', port=5000)
