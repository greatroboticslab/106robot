---

# ESP32 Robot Controller with RC (FlySky FS-i6X) + MQTT (Raspberry Pi)

This is an extended README for any new editing for the ESP32. This ESP32 is the main terminal for controlling the robot movements and pump.

---

## Wiring

This is the current wiring for the ESP32:

<p align="center">
<img src="Images/Wiring.JPG" width="400" align="Center">
</p>

---

## Setup For Editing

If any editing must be done to the ESP32 then:

* Arduino IDE must be downloaded
* The appropriate libraries need to be downloaded (from the library folder):

  * IBusBM (v.2.0.18-arduino.5)
  * PubSubClient (v.2.0.6)
  * roboclaw_arduino_library-master
* The board **"ESP32 Dev Module"** by Espressif Systems (version **2.0.6**) must be installed.

**Important:**
Library versions must match the ones listed above to avoid breaking changes.

To upload code, connect the ESP32 (caution!!: robot must be powered off), select **ESP32 Dev Module**, and upload.

Video if needed:
[https://www.youtube.com/watch?v=CD8VJl27n94&t=225s](https://www.youtube.com/watch?v=CD8VJl27n94&t=225s)

---

# **Code Explanation (Extremely Detailed)**

Below is a complete walkthrough of the program’s architecture, logic, modes, RC controls, MQTT controls, motor mapping, pump logic, and safety rules.
Each section includes **referenced code blocks**.

---

# 1. **Library Imports**

```code
#include <WiFi.h>
#include <PubSubClient.h>
#include <RoboClaw.h>
#include <IBusBM.h>
```

### Purpose of each:

* **WiFi.h** → Connect ESP32 to your wireless network
* **PubSubClient.h** → Handles MQTT subscribe/publish
* **RoboClaw.h** → Communicates with RoboClaw motor controllers
* **IBusBM.h** → Decodes FlySky iBus channels (FS-i6X)

This establishes the two-source hybrid control system:

1. **Manual mode** → FS-i6X RC controller
2. **Automatic mode** → Raspberry Pi sends commands over MQTT

---

# 2. **Pin Assignments & Core Constants**

```code
const int PUMP_PIN = 22;
const int ROBOCLAW_TX_PIN = 17;
const int IBUS_RX_PIN = 16;
```

* **GPIO 22** → Relay controlling the water pump
* **TX2 (GPIO17)** → Sends motor commands to RoboClaw
* **RX2 (GPIO16)** → Reads iBus data from RC receiver

### Motor speed constants:

```code
const int STOP_COMMAND = 64;
const int MAX_COMMAND  = 128;
const int MIN_COMMAND  = 0;
```

RoboClaw uses **0–127** speed values.
Your system defines:

* **64** = stop
* **<64** reverse
* **>64** forward

---

# 3. **Wi-Fi + MQTT Configuration**

```code
const char* ssid = "downRobotRoom";
const char* password = "robotsRcool";
const char* mqtt_server = "192.168.1.145";
const int mqtt_port = 1883;
```

### MQTT Topics:

```code
const char* MQTT_MOVE_CMD = "robot/control";
const char* MQTT_PUMP_CMD = "robot/pump";
```

These topics control:

* robot movement
* pump activation

---

# 4. **RoboClaw Motor Controllers**

You have **two RoboClaws**, one for each side:

```code
#define ROBOCLAW_ADDRESS_LEFT  0x80
#define ROBOCLAW_ADDRESS_RIGHT 0x81
```

Both use:

```code
RoboClaw roboclaw(&Serial1, 38400);
```

This sets a **38400 baud** UART.

---

# 5. **iBus RC Receiver Initialization**

```code
Serial2.begin(115200, SERIAL_8N1, IBUS_RX_PIN, -1);
ibus.begin(Serial2);
```

* Uses **RX-only**
* iBus requires **115200 baud**
* Channels are read using:

```code
ibus.readChannel(channel);
```

---

# 6. **`readChannel()` Mapping Function**

```code
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue)
```

This converts RC stick values from:

* **1000–2000 µs** →
* **your chosen output range (ex: 0–126 or 0–100)**

```code
return map(ch, 1000, 2000, minLimit, maxLimit);
```

If a channel is disconnected (`<100`), the function returns a safe default.

---

# 7. **Setup() – Full System Initialization**

```code
Serial.begin(19200);
pinMode(PUMP_PIN, OUTPUT);

setupWiFi();
client.setServer(mqtt_server, mqtt_port);
client.setCallback(callback);
```

### Important behaviors:

* Stabilization delays help avoid RoboClaw brownouts
* Sends idle bytes to RoboClaw so it wakes cleanly
* Initializes RC receiver
* Ensures motors and pump start OFF:

```code
stopMotors();
digitalWrite(PUMP_PIN, LOW);
```

---

# 8. **Loop() – Dual Operation Modes**

### Mode Selection (via SWB on RC transmitter):

```code
mode = readChannel(5, 0, 100, 0);  
// 0 = MANUAL MODE
// 100 = MQTT MODE
```

---

## **MANUAL MODE (mode == 0)**

Robot is fully operated via RC radio.

```code
ibusLoop();
```

---

## **AUTONOMOUS MODE (mode != 0)**

Robot listens to MQTT commands.

```code
if (!client.connected()) reconnect();
client.loop();
```

---

# 9. **`ibusLoop()` – Manual (RC) Control**

This is where all RC input processing happens.

### Pump activation

```code
bool pump_flag = (pump_channel == 100);
pumpControls(pump_flag);
```

### Reading Forward & Turn sticks

```code
int ch2 = readChannel(1, 0, 126, 64); // Forward/back
int ch1 = readChannel(0, 0, 126, 64); // Turn
```

---

### Differential Drive Math

```code
int turn = map(ch1, 0, 126, -63, 63);
int LSpeed = value + turn;
int RSpeed = value - turn;
```

Meaning:

* Turning left → left wheel slows, right wheel speeds
* Turning right → right wheel slows, left wheel speeds

---

### Deadband Correction

Prevents motor jitter at mid-stick:

```code
if (LSpeed <= 66 && LSpeed >= 62) LSpeed = 64;
if (RSpeed <= 66 && RSpeed >= 62) RSpeed = 64;
```

---

### Constrain Speed

```code
LSpeed = constrain(LSpeed, 1, 127);
RSpeed = constrain(RSpeed, 1, 127);
```

---

### Drive Motors Only When Armed

(RC safety)

```code
if(mode == 0 && arm != 0){
    roboclaw.ForwardBackwardM1(LEFT, LSpeed);
    roboclaw.ForwardBackwardM1(RIGHT, RSpeed);
} else {
    stopMotors();
}
```

---

# 10. **MQTT `callback()` – Autonomous Control**

Triggered when a new MQTT message arrives.

---

## **Pump via MQTT**

```code
if (strcmp(topic, MQTT_PUMP_CMD) == 0) {
    pump_flag = (pump_message == "1");
    pumpControls(pump_flag);
}
```

---

## **Movement via MQTT**

MQTT message format:

```
<turn> <forward>
```

Example:

```
80 64
```

Your code splits the message:

```code
String turnStr = move_message.substring(0, spaceIndex);
String forwardStr = move_message.substring(spaceIndex + 1);

int forwardCmd = forwardStr.toInt();
int turnCmd = turnStr.toInt();
```

After validation, motors are updated:

```code
setMotorSpeeds(forwardCmd, turnCmd);
```

---

# 11. **Motor Speed Mapping**

RoboClaw requires speeds **0–127**, but your logic calculates speeds **-127 → +127**.

### `mapMotorSpeed()` converts correctly:

```code
mappedSpeed = map(speed, -127, 127, 0, 254);
return mappedSpeed / 2;
```

---

# 12. **`setMotorSpeeds()` – Core Differential Drive Logic**

### Convert 0–126 → -64 to +64

```code
int mappedForward = forwardCmd - 64;
int mappedTurn = turnCmd - 64;
```

### Compute raw left/right speeds

```code
int LSpeed = mappedForward + mappedTurn;
int RSpeed = mappedForward - mappedTurn;
```

### Constrain

```code
LSpeed = constrain(LSpeed, -127, 127);
```

### Convert to RoboClaw format

```code
int MappedLSpeed = mapMotorSpeed(LSpeed);
```

### Right Side Inversion

(because your right motors spin reversed)

```code
MappedRSpeed = change_right(MappedRSpeed);
```

### Send to motors

```code
roboclaw.ForwardBackwardM1(LEFT, MappedLSpeed);
roboclaw.ForwardBackwardM1(RIGHT, MappedRSpeed);
```

---

# 13. **Pump Control Function**

```code
digitalWrite(PUMP_PIN, isON ? HIGH : LOW);
```

Simple, reliable, immediate relay toggle.

---

# 14. **Wi-Fi and MQTT Reconnection Logic**

```code
while (WiFi.status() != WL_CONNECTED) { delay(500); }
```

MQTT reconnect:

```code
if (client.connect("ESP32Client1", "robot", "robot1")) {
    client.subscribe(MQTT_MOVE_CMD);
    client.subscribe(MQTT_PUMP_CMD);
}
```

---

# 15. **Safety Features**

### ✔ Manual mode overrides MQTT

If RC switch is UP → MQTT is ignored.

### ✔ Robot must be “armed” to move

RC SWA must be DOWN.

### ✔ Stop motors on any of these:

* Missing RC signal
* Missing MQTT connection
* Invalid MQTT command
* Manual override engaged

### ✔ Pump cannot activate unless allowed by arm + mode logic

Prevents accidents.

