# 🚜 ESP32 Robot Controller with RC (FlySky FS-i6X) + MQTT (Raspberry Pi)

This project allows an **ESP32** to control both **motors (via RoboClaw)** and a **pump relay (GPIO 22)**.  
It supports **two control methods**:

- **FlySky FS-i6X + FS-iA10B receiver (iBus protocol)** → **priority control**  
- **MQTT via Raspberry Pi broker** → used only if RC controller is **off** or disconnected  

---

## 🔧 Features
- **Dual Control**
  - Motors + Pump via RC (priority when controller is ON)  
  - Motors + Pump via MQTT (fallback when RC is OFF)  
- **Failsafe**
  - If RC disconnects, control automatically switches to MQTT  
- **Pump Control**
  - Controlled by **SWD switch (CH7)** on the FS-i6X  
  - Controlled by MQTT topic `"robot/pump"`  
- **Motor Control**
  - Left/Right motors driven via RoboClaw controllers  
  - Controlled by joystick (CH1 = steering, CH2 = throttle)  
  - Controlled by MQTT topic `"robot/control"`  

---

## ⚡ Wiring

### FS-iA10B Receiver → ESP32
| Receiver Pin | ESP32 Pin |
|--------------|-----------|
| **iBus (S)** | GPIO 3 (RX) → `Serial` for iBus |
| **VCC**      | 5V (VIN) |
| **GND**      | GND |

### Pump Relay → ESP32
| Relay Pin | ESP32 Pin |
|-----------|-----------|
| IN        | GPIO 22 |
| VCC       | 5V |
| GND       | GND |

### RoboClaw → ESP32
| RoboClaw Pin | ESP32 Pin |
|--------------|-----------|
| S1 (RX)      | GPIO 17 (TX2) |
| S2 (TX)      | GPIO 16 (RX2) |
| GND          | GND |

---

## 📡 MQTT Topics

- `robot/control` → `"forward turn"` (two integers 0–126, space separated)  
  - Example: `"64 64"` → stop  
  - Example: `"80 64"` → forward  
- `robot/pump` → `"1"` (ON) or `"0"` (OFF)  

---

## ⚙️ Setup

1. Connect hardware:
   - FS-iA10B receiver → ESP32 (iBus RX)  
   - Pump relay → GPIO 22  
   - RoboClaw motor controllers → ESP32 Serial2  

2. Update code with:
   - Your WiFi **SSID** and **password**  
   - Your MQTT **broker IP**  

3. Upload sketch to ESP32 via Arduino IDE  

4. Power system:
   - When **RC controller is ON** → joystick + SWD switch control robot  
   - When **RC controller is OFF** → MQTT messages control robot  

---

## 📝 Notes
- **RC has priority** → MQTT commands are ignored as long as RC signal is active.  
- **Failsafe** → If transmitter is off, iBus signal stops, ESP32 automatically switches to MQTT control.  
- Ensure **RoboClaw** address matches (`0x80` left, `0x81` right) or update code.  
- Adjust **RC channel mapping** if your FS-i6X assigns CH7 (SWD) differently.  
