import paho.mqtt.client as mqtt
import time
import json
import threading
import math
import sys
import cv2
import serial
import pynmea2
import IMU
import base64

# ─────────────────────────────────────────────────────────────────────────────
# CLOUD MQTT Configuration
# ─────────────────────────────────────────────────────────────────────────────
CLOUD_MQTT_SERVER   = "100.119.46.15"   
CLOUD_MQTT_PORT     = 1883
CLOUD_MQTT_USERNAME = None              
CLOUD_MQTT_PASSWORD = None              

CLOUD_TOPIC_STATUS  = "cloud/robot/status"
CLOUD_TOPIC_GPS     = "cloud/robot/gps"
CLOUD_TOPIC_CAMERA  = "cloud/robot/camera"
CLOUD_TOPIC_IMU     = "cloud/robot/imu"
CLOUD_TOPIC_CMD     = "cloud/robot/cmd"

# ─────────────────────────────────────────────────────────────────────────────
# LOCAL MQTT Configuration (Pi <-> ESP32, same broker/topic central_script.py uses)
# ─────────────────────────────────────────────────────────────────────────────
# Same address/port as MQTT_SERVER/MQTT_PORT in central_script.py -- this is the
# robot's own local broker, not the cloud one above.
LOCAL_MQTT_SERVER    = "192.168.1.145"
LOCAL_MQTT_PORT      = 1883
LOCAL_TOPIC_COMMAND  = "robot/control"   # same topic the ESP32 subscribes to

# Client for the outgoing side of the bridge: cloud command -> local ESP32 command.
# Kept separate from the `client` object (which talks to the cloud broker) since
# they're two different brokers.
local_client = mqtt.Client(client_id="rpi_cloud_bridge_local")

# The ESP32 has a deadman timeout: MQTT_TIMEOUT_MS = 1500 in
# ESP32_DEV_CONTROLS.ino, i.e. it stops the motors if no robot/control message
# arrives for 1.5s. The dashboard's GamePad is "sticky" -- it only sends when
# the command CHANGES -- so a single press would drive for 1.5s and then halt.
# We therefore re-send the last command continuously, the same way
# central_script.py's main loop does for the local website.
COMMAND_REPEAT_S = 0.25          # 4Hz, comfortably inside the 1.5s failsafe
MOTOR_STOP       = "64 64"       # neutral drive + neutral steering

last_motor_cmd = None            # None until the dashboard sends something
last_motor_cmd_lock = threading.Lock()

# ─────────────────────────────────────────────────────────────────────────────
# HARDWARE GLOBALS & THREADS
# ─────────────────────────────────────────────────────────────────────────────
current_lat, current_lon = None, None
latest_camera_frame = None
camera_frame_lock = threading.Lock()

# 1. GPS Thread
GPS_PORT_CANDIDATES = ['/dev/ttyACM0', '/dev/ttyACM1', '/dev/ttyUSB0', '/dev/ttyUSB1']
GPS_BAUD = 9600

def gps_serial_thread():
    global current_lat, current_lon
    port = None
    for candidate in GPS_PORT_CANDIDATES:
        try:
            ser = serial.Serial(candidate, baudrate=GPS_BAUD, timeout=1)
            port = candidate
            print(f"[GPS] Opened VFan GPS on {port} at {GPS_BAUD} baud")
            break
        except Exception:
            continue

    if port is None:
        print(f"[GPS] ERROR: Could not open GPS on any of {GPS_PORT_CANDIDATES}")
        return

    while True:
        try:
            line = ser.readline().decode('ascii', errors='replace').strip()
            if not line:
                continue

            if 'RMC' in line or 'GGA' in line:
                try:
                    msg = pynmea2.parse(line)
                    if hasattr(msg, 'status') and msg.status == 'A':
                        current_lat = msg.latitude
                        current_lon = msg.longitude
                    elif hasattr(msg, 'gps_qual') and msg.gps_qual and int(msg.gps_qual) > 0:
                        current_lat = msg.latitude
                        current_lon = msg.longitude
                except pynmea2.ParseError:
                    pass
        except Exception as e:
            time.sleep(1)

# 2. Camera Thread
def camera_capture_thread():
    global latest_camera_frame
    cap = cv2.VideoCapture(0, cv2.CAP_V4L2)
    if not cap.isOpened():
        print("[CAMERA] ERROR: Could not open USB camera.")
        return
    
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    cap.set(cv2.CAP_PROP_FPS, 24)
    
    # Try to force Auto-Exposure ON (For V4L2, '3' usually means Auto, '1' means Manual)
    #cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 3) 
    # Force a higher default brightness (usually a scale from 0 to 255)
    #cap.set(cv2.CAP_PROP_BRIGHTNESS, 150)

    print("[CAMERA] Active and reading frames.")
    while True:
        ret, frame = cap.read()
        if ret:
            with camera_frame_lock:
                latest_camera_frame = frame

# 3. IMU Helper
def calculate_heading():
    ACCx, ACCy, ACCz = IMU.readACCx(), IMU.readACCy(), IMU.readACCz()
    MAGx, MAGy, MAGz = IMU.readMAGx(), IMU.readMAGy(), IMU.readMAGz()

    acc_magnitude = math.sqrt(ACCx ** 2 + ACCy ** 2 + ACCz ** 2)
    if acc_magnitude == 0:
        return 0, 0, 0
    
    accXnorm = ACCx / acc_magnitude
    accYnorm = ACCy / acc_magnitude

    pitch = math.asin(accXnorm)
    roll = 0 if math.cos(pitch) == 0 else -math.asin(accYnorm / math.cos(pitch))

    magXcomp = MAGx * math.cos(pitch) + MAGz * math.sin(pitch)
    magYcomp = (MAGx * math.sin(roll) * math.sin(pitch) +
               MAGy * math.cos(roll) - MAGz * math.sin(roll) * math.cos(pitch))

    heading = math.degrees(math.atan2(magYcomp, magXcomp))
    if heading < 0:
        heading += 360

    return heading, math.degrees(pitch), math.degrees(roll)

# ─────────────────────────────────────────────────────────────────────────────
# MQTT CALLBACKS
# ─────────────────────────────────────────────────────────────────────────────
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("[SUCCESS] Connected to Cloud MQTT Broker!")
        # Subscribe to the GamePad commands
        client.subscribe(CLOUD_TOPIC_CMD)
        print(f"[*] Listening for GamePad commands on {CLOUD_TOPIC_CMD}...")
    else:
        print(f"[ERROR] Failed to connect. Return code: {rc}")

def on_disconnect(client, userdata, rc):
    # If we can no longer hear the dashboard we must not keep driving on its
    # behalf. Clear the repeat and explicitly command a stop.
    global last_motor_cmd
    print(f"\n[MQTT] Lost the cloud connection (rc={rc}) -- stopping the robot.")
    with last_motor_cmd_lock:
        last_motor_cmd = None
    try:
        local_client.publish(LOCAL_TOPIC_COMMAND, MOTOR_STOP)
    except Exception as e:
        print(f"[ERROR] Could not send stop after disconnect: {e}")


def on_message(client, userdata, msg):
    # Decode the GamePad text (e.g., "forward", "stop")
    command = msg.payload.decode('utf-8')
    print(f"\n[GAMEPAD] Received: {command}")
    
    # Translate to your robot's specific front_back/side_side values
    if command == "forward":
        motor_cmd = "126 64"
    elif command == "back":
        motor_cmd = "0 64"
    elif command == "left":
        motor_cmd = "64 126"
    elif command == "right":
        motor_cmd = "64 0"
    elif command == "stop":
        motor_cmd = "64 64"
    else:
        return # Ignore unknown commands

    print(f"[*] Translated to Motor Command: {motor_cmd}")

    # Forward to the ESP32 over the LOCAL broker -- same topic/format
    # central_script.py's own Flask movement routes already publish to.
    # The ESP32 firmware only acts on this when SWA is armed and SWB is in
    # web/MQTT mode, so this respects the robot's existing physical safety
    # switches exactly like the local website does.
    global last_motor_cmd
    with last_motor_cmd_lock:
        last_motor_cmd = motor_cmd   # the repeater keeps re-sending this

    try:
        local_client.publish(LOCAL_TOPIC_COMMAND, motor_cmd)
    except Exception as e:
        print(f"[ERROR] Could not forward command to local broker: {e}")



# ─────────────────────────────────────────────────────────────────────────────
# MAIN EXECUTION
# ─────────────────────────────────────────────────────────────────────────────
def main():
    print("[*] Starting Hardware-to-Cloud MQTT Script...")
    
    # Init IMU.
    # NOT fatal: the point of this script is the cloud link, so a missing or
    # unreadable IMU should not stop the robot from connecting. If the IMU
    # isn't usable we just skip IMU telemetry and everything else still runs.
    imu_ok = False
    try:
        IMU.detectIMU()
        if IMU.BerryIMUversion == 99:
            print("[IMU] No BerryIMU found -- continuing WITHOUT IMU telemetry.")
        else:
            IMU.initIMU()
            imu_ok = True
            print("[IMU] Initialized successfully.")
    except Exception as e:
        print(f"[IMU] Init failed ({e}) -- continuing WITHOUT IMU telemetry.")

    # Start Hardware Threads
    threading.Thread(target=gps_serial_thread, daemon=True).start()
    threading.Thread(target=camera_capture_thread, daemon=True).start()

    # Init MQTT
    client = mqtt.Client(client_id="rpi_hardware_telemetry")
    if CLOUD_MQTT_USERNAME and CLOUD_MQTT_PASSWORD:
        client.username_pw_set(CLOUD_MQTT_USERNAME, CLOUD_MQTT_PASSWORD)

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    
    client.will_set(CLOUD_TOPIC_STATUS, "Status: INACTIVE | Connection Lost", retain=True)

    try:
        client.connect(CLOUD_MQTT_SERVER, CLOUD_MQTT_PORT, 60)
        client.loop_start()
        print(f"[*] Connected to Cloud MQTT at {CLOUD_MQTT_SERVER}")
    except Exception as e:
        print(f"[ERROR] Could not connect to cloud broker: {e}")
        return

    try:
        local_client.connect(LOCAL_MQTT_SERVER, LOCAL_MQTT_PORT, 60)
        local_client.loop_start()
        print(f"[*] Connected to local broker at {LOCAL_MQTT_SERVER} (for forwarding commands to the ESP32)")
    except Exception as e:
        # Non-fatal: telemetry to the cloud can still work even if the local
        # bridge side is down, we just won't be able to drive the robot.
        print(f"[ERROR] Could not connect to local broker: {e}")

    time.sleep(2)
    print("[*] Beginning telemetry stream...")

    last_imu_publish = 0.0
    IMU_PUBLISH_INTERVAL_S = 0.5  # ~2Hz, see note above on I2C contention
    imu_read_failures = 0
    IMU_MAX_READ_FAILURES = 5     # give up on IMU after this many in a row
    last_command_repeat = 0.0

    try:
        while True:
            # 0. Keep the last drive command alive.
            # Without this the ESP32's 1.5s failsafe stops the robot between
            # GamePad presses, because the dashboard only sends on change.
            repeat_now = time.monotonic()
            if repeat_now - last_command_repeat >= COMMAND_REPEAT_S:
                last_command_repeat = repeat_now
                with last_motor_cmd_lock:
                    cmd_to_repeat = last_motor_cmd
                if cmd_to_repeat is not None:
                    try:
                        local_client.publish(LOCAL_TOPIC_COMMAND, cmd_to_repeat)
                    except Exception as e:
                        print(f"[ERROR] Could not repeat command: {e}")

            # 1. Real GPS Data
            if current_lat is not None and current_lon is not None:
                gps_payload = json.dumps({
                    "lat": str(current_lat),
                    "lon": str(current_lon),
                    "alt": "0.0"
                })
                client.publish(CLOUD_TOPIC_GPS, gps_payload)

            # 2. Real IMU Data
            # NOTE: central_script.py also reads this same BerryIMU directly
            # over I2C. Running both processes at once means two readers on
            # the same bus -- short polling reads are usually fine, but this
            # is throttled to ~2Hz (vs the 20Hz loop rate) to reduce
            # contention. If this ever causes flaky IMU reads in either
            # script, the real fix is to have central_script.py publish IMU
            # data locally so only one process touches the bus.
            now = time.monotonic()
            if imu_ok and now - last_imu_publish >= IMU_PUBLISH_INTERVAL_S:
                last_imu_publish = now
                try:
                    h, p, r = calculate_heading()
                    try:
                        gx, gy, gz = str(IMU.readGYRx()), str(IMU.readGYRy()), str(IMU.readGYRz())
                    except AttributeError:
                        gx = gy = gz = "0.00"

                    imu_payload = json.dumps({
                        "acc": {"x": str(IMU.readACCx()), "y": str(IMU.readACCy()), "z": str(IMU.readACCz())},
                        "gyro": {"x": gx, "y": gy, "z": gz},
                        "roll": str(round(r, 2)),
                        "pitch": str(round(p, 2)),
                        "yaw": str(round(h, 2))
                    })
                    client.publish(CLOUD_TOPIC_IMU, imu_payload)
                    imu_read_failures = 0
                except Exception as e:
                    # A bad I2C read must not take the whole bridge down.
                    imu_read_failures += 1
                    print(f"[IMU] Read failed ({e}) "
                          f"[{imu_read_failures}/{IMU_MAX_READ_FAILURES}]")
                    if imu_read_failures >= IMU_MAX_READ_FAILURES:
                        imu_ok = False
                        print("[IMU] Too many consecutive failures -- "
                              "disabling IMU telemetry for this run.")

            # 3. Real Camera Live Feed (Base64 Encoded)
            with camera_frame_lock:
                frame_to_send = latest_camera_frame.copy() if latest_camera_frame is not None else None

            if frame_to_send is not None:
                # Resize to make the MQTT payload smaller and faster
                small_frame = cv2.resize(frame_to_send, (320, 240))
                # Encode as JPEG with 50% quality
                _, buffer = cv2.imencode('.jpg', small_frame, [cv2.IMWRITE_JPEG_QUALITY, 50])
                # Convert the bytes to a Base64 string
                jpg_as_text = base64.b64encode(buffer).decode('utf-8')
                
                # Format exactly how HTML expects an image source
                cam_payload = f"data:image/jpeg;base64,{jpg_as_text}"
            else:
                cam_payload = "Camera Offline: Awaiting frame..."
                
            client.publish(CLOUD_TOPIC_CAMERA, cam_payload)

            # 4. Status
            client.publish(CLOUD_TOPIC_STATUS, "Status: ACTIVE | Telemetry Streaming")

            time.sleep(0.05) # Send at ~20 updates per second

    except KeyboardInterrupt:
        print("\n[*] Stopping Script...")
    finally:
        client.publish(CLOUD_TOPIC_STATUS, "Status: INACTIVE | Script Stopped", retain=True)
        time.sleep(0.5) # Give it half a second to actually send the message before cutting the connection

        client.loop_stop()
        client.disconnect()
        local_client.loop_stop()
        local_client.disconnect()
        print("[*] Disconnected.")

if __name__ == "__main__":
    main()