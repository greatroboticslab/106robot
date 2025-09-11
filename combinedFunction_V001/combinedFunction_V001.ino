#include <WiFi.h>
#include <PubSubClient.h>
#include <RoboClaw.h>
#include <IBusBM.h>

// ---------------- WiFi & MQTT ----------------
const char* ssid = "downRobotRoom";
const char* password = "robotsRcool";

const char* mqtt_server = "192.168.1.145"; 
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

const char* topic_motors = "robot/control";
const char* topic_pump   = "robot/pump";

// ---------------- RoboClaw ----------------
RoboClaw roboclaw(&Serial2, 10000);
#define ROBOCLAW_ADDRESS_LEFT  0x80
#define ROBOCLAW_ADDRESS_RIGHT 0x81

// ---------------- Pump ----------------
const int pumpPin = 22;  // Relay for pump

// ---------------- RC (iBus) ----------------
IBusBM ibus;
bool rcActive = false;  // flag if RC is alive

// Mapping constants
#define STOP_COMMAND 64
#define MAX_COMMAND 128
#define MIN_COMMAND 0

// ---------------- Helpers ----------------
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) {
  uint16_t ch = ibus.readChannel(channelInput);
  if (ch < 100) return defaultValue;  // No signal → default
  rcActive = true;                    // We saw RC signal
  return map(ch, 1000, 2000, minLimit, maxLimit);
}

uint8_t mapMotorSpeed(int speed) {
  int mappedSpeed = map(speed, -127, 127, 0, 254);
  return (uint8_t)(mappedSpeed / 2);  // 0–127
}

void setMotorSpeeds(int forwardCmd, int turnCmd) {
  int mappedForward = forwardCmd - STOP_COMMAND;
  int mappedTurn    = turnCmd - STOP_COMMAND;

  int LSpeed = constrain(mappedForward + mappedTurn, -127, 127);
  int RSpeed = constrain(mappedForward - mappedTurn, -127, 127);

  uint8_t MappedLSpeed = mapMotorSpeed(LSpeed);
  uint8_t MappedRSpeed = mapMotorSpeed(RSpeed);

  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, MappedLSpeed);
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, MappedRSpeed);
}

void stopMotors() {
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, STOP_COMMAND);
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, STOP_COMMAND);
}

// ---------------- WiFi / MQTT ----------------
void setupWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32Client1")) {
      client.subscribe(topic_motors);
      client.subscribe(topic_pump);
    } else {
      delay(5000);
    }
  }
}

// ---------------- MQTT callback ----------------
void callback(char* topic, byte* payload, unsigned int length) {
  if (rcActive) return;  // Ignore MQTT if RC is active

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == topic_motors) {
    int spaceIndex = message.indexOf(' ');
    if (spaceIndex == -1) {
      stopMotors();
      return;
    }

    int forwardCmd = message.substring(0, spaceIndex).toInt();
    int turnCmd    = message.substring(spaceIndex + 1).toInt();

    if (forwardCmd < MIN_COMMAND || forwardCmd > MAX_COMMAND ||
        turnCmd < MIN_COMMAND || turnCmd > MAX_COMMAND) {
      stopMotors();
      return;
    }

    if (forwardCmd == STOP_COMMAND && turnCmd == STOP_COMMAND) {
      stopMotors();
    } else {
      setMotorSpeeds(forwardCmd, turnCmd);
    }
  }

  else if (String(topic) == topic_pump) {
    if (message == "1") {
      digitalWrite(pumpPin, HIGH);
    } else if (message == "0") {
      digitalWrite(pumpPin, LOW);
    }
  }
}

// ---------------- Setup ----------------
void setup() {
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);

  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial2.begin(38400, SERIAL_8N1, -1, 17);  // RoboClaw
  roboclaw.begin(38400);

  ibus.begin(Serial);  // iBus RX

  stopMotors();
}

// ---------------- Loop ----------------
void loop() {
  rcActive = false;  // reset every loop, will be set true if signal seen

  // Check RC input
  int ch2 = readChannel(1, 0, 126, 64); // throttle
  int ch1 = readChannel(0, 0, 126, 64); // steering
  int pumpSW = readChannel(6, 0, 1, 0); // SWD → channel index may vary

  if (rcActive) {
    // Motors
    int turn = map(ch1, 0, 126, -63, 63);
    int value = ch2;
    int LSpeed = value + turn;
    int RSpeed = value - turn;

    LSpeed = constrain(LSpeed, 1, 126);
    RSpeed = constrain(RSpeed, 1, 126);

    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, LSpeed);
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, RSpeed);

    // Pump
    if (pumpSW == 1) {
      digitalWrite(pumpPin, HIGH);
    } else {
      digitalWrite(pumpPin, LOW);
    }
  }

  else {
    // Fall back to MQTT
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }

  delay(10);
}
