# ----------------------------------
# ------- central_script.py --------
# ----------------------------------

# ----------- Libraries ------------
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
import csv
import logging
from multiprocessing import Process, Queue, Event
from flask import Flask, Response, jsonify, request, render_template_string

# -------- Internal Modules --------
import IMU
import GUI
from face_tracking import face_tracking_process
from auto_navigation import auto_navigation_process

# ----------------------------------
# ------- MQTT Configuration -------
MQTT_SERVER = "192.168.1.145"  # Update if different
MQTT_PORT   = 1883

# ------------ Channels ------------
MQTT_TOPIC_COMMAND      = "robot/control"
MQTT_RAIL_TOPIC_COMMAND = "robot/rail"
MQTT_TOPIC_DETECTIONS   = "robot/detections"
MQTT_TOPIC_CAMERA       = "robot/camera"
MQTT_TOPIC_PUMP         = "robot/pump"
MQTT_TOPIC_IMU          = "imu/data"
MQTT_TOPIC_REMOTE_PUMP  = "robot/remotepump"
MQTT_TOPIC_DATA         = "moisture/data"
# ----------------------------------

# ----------- Constants ------------
# PID CONTROLLER PARAMETES
FRAME_WIDTH         = 640
FRAME_HEIGHT        = 480
FRAME_CENTER_X      = FRAME_WIDTH // 2
# CONFIGURATION FLAGS
ENABLE_FRAME_FLIP   = True  # False: Disable frame flipping
INVERT_YAW_CONTROL  = False # True: if robot moves opposing direction
# ----------------------------------

# ------------ Globals -------------
latest_detection    = None
latest_camera_frame = None
output_frame        = None
gps_data            = []
current_lat         = None  # GPS latitude
current_lon         = None  # GPS longitude
robot_heading       = 0.0   # Heading data
e_stop_active       = False # E-Stop state
moisture_threshold  = 100   # For pump control with moisture

# MOVEMENTS MODE CONTROLS - DEFAULT
current_mode        = 'BASIC_MOV'

# LOCKS
lock                = threading.Lock()
gps_data_lock       = threading.Lock()

# QUEUES for inter-process communication
command_queue       = Queue()
detection_queue     = Queue()
camera_frame_queue  = Queue()
gps_data_queue      = Queue()
imu_queue           = Queue()

# EVENT to control processes
stop_event          = Event()

# Flask App
app = Flask(__name__)
logging.basicConfig(level=logging.INFO)
# ----------------------------------

# MQTT setup and functions
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to MQTT server {MQTT_SERVER} successfully.")
        #SUCRIBE TO TOPICS
        client.subscribe([(MQTT_TOPIC_DETECTIONS, 0), 
                          (MQTT_TOPIC_CAMERA, 0), 
                          (MQTT_TOPIC_DATA, 0)])
    else:
        print(f"Failed to connect to MQTT server, return code {rc}")

# CSV & Moisture info
# TO-DO

def on_message(client, userdata, msg):
    try:
        if msg.topic == MQTT_TOPIC_CAMERA:
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