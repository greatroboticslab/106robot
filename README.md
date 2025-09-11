# RapsPi GUI Controls - Autonomous Robot Control System

An autonomous robot control system built for Raspberry Pi that provides web-based GUI controls, face tracking, auto-navigation, and sensor monitoring capabilities. This system is designed for agricultural robotics applications with features like moisture monitoring, pump control, and GPS-based navigation.

## Features

### Core Functionality
- **Web-based GUI Dashboard**: Real-time robot control interface accessible via web browser
- **Live Camera Feed**: Video streaming from robot's camera with real-time display
- **Multiple Operation Modes**:
  - Basic Movement: Manual robot control
  - Face Tracking: Autonomous face detection and following
  - Auto-Navigation: GPS waypoint-based autonomous navigation

### Sensor Integration
- **IMU Support**: Compatible with BerryIMU v1, v2, and v3 (LSM9DS0, LSM9DS1, LSM6DSL/LIS3MDL)
- **GPS Integration**: Real-time location tracking and navigation
- **Moisture Sensors**: Wireless moisture monitoring with automatic pump control
- **Camera**: OpenCV-based computer vision processing

### Control Systems
- **MQTT Communication**: Wireless command and telemetry system
- **PID Controllers**: Precision control for face tracking and navigation
- **Emergency Stop**: Safety system with immediate robot shutdown
- **Multi-zone Pump Control**: Automated irrigation based on moisture levels

### Navigation & Mapping
- **Interactive Maps**: Leaflet-based mapping with real-time robot position
- **Waypoint Planning**: Draw paths on map for autonomous navigation
- **Extended Kalman Filter**: Advanced state estimation for precise navigation
- **GPS Coordinate Conversion**: UTM projection for accurate positioning

## Hardware Requirements

### Essential Components
- Raspberry Pi (3B+ or newer recommended)
- BerryIMU (v1, v2, or v3) for orientation sensing
- USB Camera or Raspberry Pi Camera Module
- GPS Module (compatible with gpsd)
- MQTT-enabled motor controllers
- Wireless moisture sensors (optional)
- Pump control hardware (optional)

### Network Requirements
- WiFi connection for web interface
- MQTT broker (default: 192.168.1.145:1883)

## Software Dependencies

### Python Packages
- **Computer Vision**: opencv-python, numpy
- **Web Framework**: Flask
- **MQTT Communication**: paho-mqtt
- **GPS**: gpsd-py3
- **Navigation**: pyproj (for coordinate transformations)
- **Hardware Interface**: smbus (for I2C communication)
- **Data Processing**: csv, json, base64, datetime
- **Concurrency**: threading, multiprocessing

### System Requirements
- Python 3.7+
- I2C enabled on Raspberry Pi
- GPS daemon (gpsd) running
- Camera interface enabled

## Installation

### 1. System Setup
```bash
# Enable I2C and Camera
sudo raspi-config
# Navigate to Interfacing Options and enable I2C and Camera

# Install system dependencies
sudo apt update
sudo apt install python3-pip gpsd gpsd-clients i2c-tools
```

### 2. Python Dependencies
```bash
# Navigate to the project directory
cd /path/to/RapsPi_GUIcontrols

# Install required Python packages using requirements.txt
pip3 install -r requirements.txt

# Alternative: Install with user permissions (if you get permission errors)
pip3 install --user -r requirements.txt

# For virtual environment (recommended for development)
python3 -m venv robot_env
source robot_env/bin/activate  # On Linux/Mac
# robot_env\Scripts\activate   # On Windows
pip install -r requirements.txt
```

#### Using Virtual Environment (Recommended)
```bash
# Create a virtual environment
python3 -m venv robot_control_env

# Activate the virtual environment
source robot_control_env/bin/activate

# Install dependencies in the virtual environment
pip install -r requirements.txt

# When done, deactivate the environment
deactivate
```

#### Troubleshooting Package Installation
```bash
# If you encounter permission issues
sudo pip3 install -r requirements.txt

# Update pip first if you get version warnings
pip3 install --upgrade pip
pip3 install -r requirements.txt

# Install specific packages individually if requirements.txt fails
pip3 install opencv-python numpy Flask paho-mqtt gpsd-py3 pyproj smbus RPi.GPIO
```

### 3. GPS Configuration
```bash
# Configure GPS daemon
sudo systemctl enable gpsd
sudo systemctl start gpsd
```

### 4. I2C Verification
```bash
# Check if IMU is detected
sudo i2cdetect -y 1
```

## Configuration

### MQTT Settings
Edit the MQTT configuration in `central_script.py`:
```python
MQTT_SERVER = "192.168.1.145"  # Your MQTT broker IP
MQTT_PORT = 1883
```

### Moisture Sensor Zones
Configure MAC addresses for different irrigation zones:
```python
zone_A_macs = {"C0:49:EF:69:BF:DC", "BB:BB:BB:BB:BB:BB"}
zone_B_macs = {"AA:AA:AA:AA:AA:AA", "BB:BB:BB:BB:BB:BB"}
```

### Camera Settings
Adjust frame dimensions and processing options:
```python
w, h = 640, 480  # Frame dimensions
ENABLE_FRAME_FLIP = True  # Flip camera feed
```

## Usage

### Starting the System
```bash
# Run the main control script
python3 central_script.py
```

### Accessing the Web Interface
1. Open a web browser
2. Navigate to `http://[RASPBERRY_PI_IP]:5000`
3. The dashboard will display camera feed and controls

### Operating Modes

#### Basic Movement Mode
- Use arrow buttons or WASD keys for manual control
- Emergency stop available at all times
- Pump controls for irrigation system

#### Face Tracking Mode
- Robot automatically follows detected faces
- Adjustable PID parameters for smooth tracking
- Configurable face area and center offset

#### Auto-Navigation Mode
- Click on map to set waypoints
- Robot follows GPS-based path planning
- Real-time position tracking with heading display

### Emergency Procedures
- **E-Stop Button**: Immediately stops all robot movement
- **Resume Button**: Restores normal operation after E-Stop
- **Mode Switching**: Always returns robot to safe state

## File Structure

```
RapsPi_GUIcontrols/
├── central_script.py      # Main control system
├── GUI.py                 # Web interface HTML/CSS/JS
├── face_tracking.py       # Face detection and tracking
├── auto_navigation.py     # GPS navigation and path planning
├── IMU.py                 # IMU sensor interface
├── LSM9DS0.py            # BerryIMU v1 definitions
├── LSM9DS1.py            # BerryIMU v2 definitions
├── LSM6DSL.py            # BerryIMU v3 accelerometer/gyro
├── LIS3MDL.py            # BerryIMU v3 magnetometer
├── moisture_data.csv     # Sensor data logging
├── README.md             # This file
└── requirements.txt      # Python dependencies
```

## MQTT Topics

### Command Topics
- `robot/control`: Movement commands (format: "front_back side_side")
- `robot/rail`: Rail movement commands
- `robot/pump`: Main pump control (0/1)
- `robot/remotepump`: Zone-specific pump control

### Data Topics
- `robot/detections`: Face detection results
- `robot/camera`: Camera feed data
- `moisture/data`: Sensor readings
- `imu/data`: IMU sensor data

## Troubleshooting

### Common Issues

#### IMU Not Detected
```bash
# Check I2C connection
sudo i2cdetect -y 1
# Verify IMU wiring and power
```

#### GPS Not Working
```bash
# Check GPS daemon status
sudo systemctl status gpsd
# Test GPS reception
cgps -s
```

#### Camera Feed Issues
```bash
# Test camera
raspistill -o test.jpg
# Check camera interface is enabled
```

#### MQTT Connection Problems
- Verify broker IP address and port
- Check network connectivity
- Ensure MQTT broker is running

### Performance Optimization
- Adjust frame rate in `generate()` function
- Modify PID update rates for smoother control
- Optimize sensor polling intervals

## Development

### Adding New Features
1. Sensor Integration: Add new sensor modules following IMU.py pattern
2. Control Modes: Extend mode handling in central_script.py
3. Web Interface: Modify GUI.py for new controls

### Testing
- Use basic movement mode for initial testing
- Verify sensor readings before autonomous operation
- Test emergency stop functionality regularly

## Safety Considerations

- Always test in safe, open areas
- Keep emergency stop easily accessible
- Monitor battery levels during operation
- Verify GPS accuracy before autonomous navigation
- Check sensor calibration regularly

## License

This project is designed for educational and research purposes in agricultural robotics.

## Contributing

When contributing to this project:
1. Test all changes in basic movement mode first
2. Verify sensor integration doesn't break existing functionality
3. Update documentation for any new features
4. Follow existing code structure and naming conventions

## Support

For technical issues:
1. Check hardware connections
2. Verify software dependencies
3. Review MQTT broker configuration
4. Test individual components separately
