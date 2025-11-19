# ESP32 Robot Controller with RC (FlySky FS-i6X) + MQTT (Raspberry Pi)

This is an extended README for any new editing for the ESP32. This ESP32 is the main terminal for controlling the robot movements and pump.

## Wiring
This is the current Wiring for the Esp32
...Photo Here...

## Setup For editing
If any editing must be done to the ESP32 then:
- Arduino IDE most be downloaded
- The appropiate libraries need to be downloaded(Given in the library folder):
  - IBusBM (v.2.0.18-arduino.5)
  - PubSubClient (v.2.0.6)
  - roboclaw_arduino_library-master
- The board "esp 32" by Espressif System at version 2.0.6 must be install.

**Important**: The libraries should not be always be at their latest version, but the specified version detailed here.

Then to actually upload any code you must connect the ESP32 (WARNING: THE ROBOT or ESP32 must be off). Applied "esp32 dev1" board and hit send.

Video if needed: https://www.youtube.com/watch?v=CD8VJl27n94&t=225s
