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
For the Remote control we utilize **4 inputs**:

  **IMPORTANT**: When turning on the power-switch all other switches must be facing up.
<p align="center">
<img src="Images/Control_Overview.png" width="650" align="Center">
</p>

#### Power-switch: 
Located at the bottom-right of the controller. This is what turns on/off the controller.
#### Armed-switch: 
Located as the first switch at the top of the controller. This is the safety switch that allow to use the components.
  - Labeled as: **SWA** 
  - Armed ON: is switch up(100)
  - Armed OFF: is switch down(0)
#### Mode-switch: 
Located as the second switch at the top of the controller. Allows who has priority of the robot, Either Controller or Website but not both.
  - Labeled as: **SWB**
  - Mode Controller: is switch up(100)
  - Mode Website:    is switch down(0)
#### Pump-switch: 
Located as the fourth switch at the top of the controller. This is what turns on/off the pump.
  - Labeled as: **SWD**
  - Pump ON: is switch up(100)
  - Pump OFF: is switch down(0)
#### Movement-Stick: 
Located at the middle-right of the controller. It is a joystick that control the movements of the robot. 
### From Rasp Pi GUI
