# 106 Robot Control System 
This are all software components that relate to the **106 robot**.
```  
  Arduino/
  ├── ESP32_DEV_CONTROLS.ino
  ├── libraries
  Raspberry/
  ├── main.py
  ├── GUI.py
  ├── IMU.py
  ├── face_tracking.py
  ├── auto_navigation.py
  ├── requirements.txt
  └── README.md
```

## User Interface Controls
IMPORTANT: accommodated for Raspberry's Thonny (Python IDE).

Inside the Raspberry folder it provides the interface for the control system via web-based GUI Control. 

### Features
- Basic Movement
- Autonomous Movement **(In Development)**
- GPS Tracking
- Camera View
- Pump Controls

### Requirements & Dependencies
Communication between the Raspberry Pi and the robot's physical componenet are through a dedicated router.
#### MQTT Configuration
This are the current configuration (change if needed):
- MQTT_SERVER = "192.168.1.145"  
- MQTT_PORT = 1883
- MQTT_TOPIC_COMMAND = "robot/control"
- MQTT_RAIL_TOPIC_COMMAND = "robot/rail"
- MQTT_TOPIC_DETECTIONS = "robot/detections"
- MQTT_TOPIC_CAMERA = "robot/camera"
- MQTT_TOPIC_PUMP = "robot/pump"
- MQTT_TOPIC_REMOTE_PUMP = "robot/remotepump"
- MQTT_TOPIC_IMU = "imu/data"
- MQTT_TOPIC_DATA = "moisture/data"

### Initializing
TO-DO: 
  - Video showcase on how-to
  - Code explantion within a wifi connection and a terminal

## Arduino Component
This is the software that relates to our **ESP32**. Its the **Central Control System** for every component to the robot.
Recieves commands from:
- Remote Control (Ibus/...)
- Raspberry Web User Interface
Sends commands to:
- Motors for movement control
- Relay for Pump toggle

... Put picture of pin locations
... Explain further.

### From Remote Control

### From Rasp Pi GUI
