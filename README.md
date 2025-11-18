# Overview
This 106 robot is designed specifically for agricultural applications. Its primary function is to serve as a mobile tank and irrigation system, capable of transporting and dispensing water or other liquids as needed. The robot features a [basic control system](#User-Controls) that enables reliable operation in various field conditions, making it a practical tool for supporting farm-level automation and improving irrigation efficiency. 
IMPORTANT: accommodated for Raspberry's Thonny (Python IDE) and the FlySky FS-i6X(controller).
### Features
- Basic Movement
- Pump Controls
- Website Features:
  - Autonomous Movement **(In Development)**
  - GPS Tracking
  - Camera View

## 106 Robot Control System 
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

## Arduino Component
This is the software that relates to our **ESP32**. Its the **Central Control System** for every component to the robot.
Recieves commands from:
- Remote Control (Ibus/FlySky FS-i6X)
- Raspberry Web User Interface
Sends commands to:
- Motors for movement control
- Relay for Pump toggle

**For More Detail** to handle and edit arduino go to:
  [Arduino Folder](Arduino)

## User Controls
### Using the Remote Control
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

### Using the Website Interface
To be able to use the GUI we must first connect to its WiFi connection
#### WiFi and Connections
- **WiFi name**: downRobotRoom
- **Password**:  robotsRcool
Since the robot's GUI website runs on a local server created by the Raspberry Pi we just use this link:
- (http://192.168.1.145:5000/)[http://192.168.1.145:5000/]
#### User Interface
