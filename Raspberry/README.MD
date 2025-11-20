
# **Raspberry Pi Autonomous Robot Controller — Full Documentation**

This README provides an in-depth explanation for all Raspberry Pi components used in the robot control system. The Pi serves as the **main intelligence layer**:

* Handling camera vision
* Receiving and publishing MQTT messages
* Running the Flask UI web controller
* Processing IMU and GPS data
* Executing face-tracking and auto-navigation
* Logging moisture sensor data
* Performing PID-based closed-loop control
* Sending movement commands to the ESP32

---

# **1. Accessing the Raspberry Pi (SSH)**

To develop or modify the robot code, you must SSH into the Raspberry Pi.

### **1. Connect to the robot WiFi:**

* **WiFi name:** `downRobotRoom`
* **Password:** `robotsRcool`

### **2. SSH into the Pi**

Open your computer terminal:

```bash
ssh ormo-bot@192.169.1.145
# Password:
Robot
```

### **3. SSH Tutorial**

If you’re new to SSH:
**Playlist:** [https://www.youtube.com/watch?v=aCGbQB8K8T8&list=PLg8DhjCcwNIUJHBY2gwcLh3A9UxUwOROL](https://www.youtube.com/watch?v=aCGbQB8K8T8&list=PLg8DhjCcwNIUJHBY2gwcLh3A9UxUwOROL)

---

# **2. Project File Structure Overview**

```
/robot-controller/
│── central_script.py        # Main supervisor process (Flask, MQTT, camera, GPS, IMU)
│── face_tracking.py         # PID face-tracking movement controller
│── auto_navigation.py       # EKF + GPS autonomous navigation engine
│── IMU.py                   # IMU hardware driver (BerryIMU v1/v2/v3)
│── GUI.py                   # Full HTML/JS dashboard served by Flask
│── moisture_data.csv        # Logged moisture sensor values
│── Images/                  # Reference images
```

---

# **3. central_script.py — FULL SYSTEM CONTROLLER**

This is the **master file** responsible for:

- MQTT communication
- Camera streaming
- Motion control
- Reading IMU + GPS
- Moisture logging
- Web server (Flask)
- Multi-process face tracking
- Multi-process auto-navigation

---

## **3.1 Imports & Initialization**

```python
import cv2, numpy as np, paho.mqtt.client as mqtt
import threading, time, json, math, sys, base64, gpsd, os
from multiprocessing import Process, Queue, Event
```

Key components:

| Module              | Role                                     |
| ------------------- | ---------------------------------------- |
| **cv2**             | Decode camera frames from ESP32          |
| **paho.mqtt**       | Communicate with ESP32                   |
| **gpsd**            | Read GPS position                        |
| **multiprocessing** | Face tracking & auto nav run in parallel |
| **Queue**           | Pass data between processes              |
| **Flask**           | The robot control dashboard              |

---

## **3.2 MQTT Topics Used**

```python
MQTT_TOPIC_COMMAND = "robot/control"
MQTT_RAIL_TOPIC_COMMAND = "robot/rail"
MQTT_TOPIC_DETECTIONS = "robot/detections"
MQTT_TOPIC_CAMERA = "robot/camera"
MQTT_TOPIC_PUMP = "robot/pump"
MQTT_TOPIC_REMOTE_PUMP = "robot/remotepump"
MQTT_TOPIC_IMU = "imu/data"
MQTT_TOPIC_DATA = "moisture/data"
```

### Topic Purposes:

* `robot/camera` → Pi receives base64-encoded frames
* `robot/detections` → From YOLO face detection model
* `robot/control` → Motor commands (front/back + turn)
* `robot/pump` → Controls large tank pump
* `robot/remotepump` → Controls zone-based pump commands
* `moisture/data` → Moisture sensors publish readings

---

## **3.3 CSV Moisture Logging System**

Every incoming moisture packet is logged:

```python
writer.writerow([timestamp, mac, value])
csv_file.flush()
```

This creates:

```
Timestamp | Mac Address | Value
```

---

## **3.4 Zone-Based Moisture Irrigation**

Zones A, B, C each have:

* A set of **MAC addresses**
* A **threshold**
* An **ON/OFF state**

```python
if value < threshold:
    client.publish(MQTT_TOPIC_REMOTE_PUMP, "A 1")
```

This makes irrigation fully autonomous.

---

## **3.5 Camera Frame Handling**

ESP32 sends JPEG frames as base64.

```python
image_bytes = base64.b64decode(image_b64)
np_arr = np.frombuffer(image_bytes, np.uint8)
image = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
```

Decoded frames are pushed to the `camera_frame_queue` for processing.

---

## **3.6 Main Loop (Core Supervisor Logic)**

### Runs in a dedicated thread:

```python
def main_loop():
    while True:
        # Updates last image frame
        # Draws IMU heading
        # Updates GPS
        # Enforces E-stop
        # Switches between:
        #   - basic movement
        #   - face tracking
        #   - auto navigation
```

### Key Features:

* Every loop updates camera frame
* Displays IMU heading
* Logs GPS into memory
* Handles active autonomous mode
* Publishes STOP if E-stop triggered

---

## **3.7 Flask Dashboard Routes**

The Pi exposes a REST API used by the browser UI.

### Example — Move Forward:

```python
@app.route("/move_forward", methods=['POST'])
def move_forward():
    front_back_command = 126
    side_side_command = 64
    client.publish(MQTT_TOPIC_COMMAND, "126 64")
```

### Categories of routes:

| Category                  | Examples                                     |
| ------------------------- | -------------------------------------------- |
| **Movement**              | `/move_forward`, `/move_left`, `/stop_robot` |
| **Pump**                  | `/pump_on`, `/pump_off`                      |
| **Face Tracking Control** | `/increase_face_area`, `/update_pid`         |
| **Mode Selection**        | `/set_mode`                                  |
| **GPS Data**              | `/get_gps_data`, `/initial_gps`              |

---

## **3.8 Camera Feed Route**

```python
@app.route("/video_feed")
def video_feed():
    return Response(generate(), mimetype="multipart/x-mixed-replace; boundary=frame")
```

This streams MJPEG to the webpage.

---

## **3.9 Starting All Processes**

At the bottom:

```python
t = threading.Thread(target=main_loop)
t.start()

face_track_proc = Process(target=face_tracking_process)
face_track_proc.start()

app.run(host='0.0.0.0', port=5000)
```

This launches:

* main loop
* face tracking engine
* web server

---

# **4. face_tracking.py — PID FACE FOLLOWING ENGINE**

This file computes steering and speed commands based on face position.

---

## **4.1 PID Parameters**

```python
pid_yaw = [0.5, 0.0001, 0.25]
pid_fb  = [0.6, 0.0001, 0.1]
```

* yaw PID → turns robot left/right
* fb PID → moves robot forward/back to maintain size

---

## **4.2 Main Function: track_face()**

```python
def track_face(info, pid_yaw, pid_fb, ...):
```

### Inputs:

* Face center `(x, y)`
* Face bounding box area
* PID coefficients

### Outputs:

* `"front back" "turn"` command pair
* Updated PID error state

### Core logic:

1. Compute yaw error
2. Compute forward/back error (based on desired face area)
3. Apply PID formulas
4. Clamp motor outputs to safe range
5. Convert to **0–126 motor command format**
6. Return final MQTT command

---

## **4.3 face_tracking_process()**

Runs continuously:

```python
while True:
    if command_queue has 'face_tracking':
        - compute PID
        - publish robot/control command
```

Also handles:

* Increasing/decreasing face area
* Center offset shifting
* PID parameter updates

---

# **5. auto_navigation.py — AUTONOMOUS GPS/TRACKING ENGINE**

This is the navigation system that uses:

- GPS
- IMU heading
- Pure-Pursuit path tracking
- Extended Kalman Filter (EKF)

---

## **5.1 Coordinate System Setup**

Uses UTM projection:

```python
proj_utm = Proj(proj='utm', zone=zone_number, ellps='WGS84')
```

Converts:

```
(lat, lon) → (x, y)
```

---

## **5.2 Extended Kalman Filter Initialization**

```python
x_est = [
    x,
    y,
    vx,
    vy,
    ax,
    ay,
    theta
]
```

Covariance starts as:

```python
P_est = identity * 500
```

---

## **5.3 EKF Prediction Step**

```python
x_pred = F @ x_est + u * dt
P_pred = F @ P_est @ F.T + Q
```

Uses IMU acceleration to predict motion.

---

## **5.4 EKF Update Step**

Uses GPS:

```python
z_meas = [x_gps, y_gps, theta]
x_est, P_est = ekf_update(x_pred, P_pred, z_meas)
```

---

## **5.5 Waypoint Handling**

When the UI sends coordinates:

```python
if command == 'set_waypoints':
    waypoints = [...]
    initialize_projection()
    initialize_ekf()
```

---

## **5.6 Pure Pursuit Goal Point Calculation**

```python
goal_point = find_goal_point(path, current_pos, look_ahead_distance)
```

This determines where the robot should drive next along the path.

---

## **5.7 Auto Navigation Loop**

At each loop:

1. Read GPS
2. EKF predict/update
3. Compute steering angle
4. Compute forward speed
5. Publish movement commands
6. Repeat until path completed

---

# **6. IMU.py — BERRYIMU v1/v2/v3 DRIVER**

This file handles **all IMU hardware communication**.

---

## **6.1 Auto-Detection of IMU version**

```python
def detectIMU():
    - Try reading WHO_AM_I register from LSM9DS0
    - Try LSM9DS1
    - Try LSM6DSL + LIS3MDL
```

Sets:

```
BerryIMUversion = 1, 2, or 3
```

---

## **6.2 Reading Accelerometer/Gyro/Magnetometer**

Examples:

```python
def readACCx():
    acc_l = bus.read_byte_data(...)
    acc_h = bus.read_byte_data(...)
    return twos_complement(acc_l | acc_h << 8)
```

Functions include:

* readACCx / y / z
* readGYRx / y / z
* readMAGx / y / z

---

## **6.3 IMU Initialization**

Each IMU version configures different registers:

```python
if BerryIMUversion == 2:
    writeByte(LSM9DS1_ACC_ADDRESS, LSM9DS1_CTRL_REG6_XL, 0b00111000)
```

This sets:

* Output data rate
* Full-scale settings
* Low-pass filters

---

# **7. GUI.py — FULL WEB DASHBOARD FRONT-END**

The Flask server serves the HTML string stored in `html_content`.

---

## **7.1 Key Components**

### Camera stream

```html
<img id="video" src="/video_feed">
```

### Movement controls

Buttons + WASD keyboard mapping.

### Face Tracking UI

* Desired area
* Center offset
* PID parameters

### Auto-Navigation UI

* Draw paths on map
* Send coordinates
* Display robot path (blue)
* Display planned path (green)

### Leaflet Map

Used for:

* GPS visualization
* Robot icon with rotation
* Path planning

---

# **8. Summary of Entire System Workflow**

```
ESP32 → publishes camera + moisture → Raspberry Pi
Raspberry Pi:
    - decodes camera
    - handles face detection
    - runs auto navigation
    - reads IMU + GPS
    - logs all moisture CSV
    - exposes a full web dashboard
User in browser → sends movement/mode commands → Pi → ESP32
```

---

# **9. Final Notes**

* All systems run **simultaneously**
* Multi-processing ensures real-time behavior
* MQTT allows all subsystems to stay loosely coupled
* The Pi can be extended to support:
  - Object detection
  - SLAM
  - Advanced navigation
  - Remote monitoring

---

If you'd like:
✅ A diagram for architecture
✅ A commented version of each Python file
✅ A PDF version of this documentation
Just say the word!
