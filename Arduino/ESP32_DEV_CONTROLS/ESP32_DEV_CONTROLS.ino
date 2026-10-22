/*=========================================
  ========== Motor & Pump Controls ========
  =========================================*/

// Libraries:
#include <WiFi.h>
#include <PubSubClient.h>
#include <RoboClaw.h>

// Description: This is the program for an Esp 32 Dev. It gets signals from
//              a remote control(FS-iA10B, one PWM wire per channel) or a
//              server(MQTT). SWB picks which.
// Aditional:   To upload we use the board "ESP32 Dev Module",
//              Pin Locations:  Pump Relay:        see PUMP_PIN
//                              Motors(RoboClaw):  GPIO5 -> S1
//                              Receiver (signal row "S" of each channel):
//                                CH1 -> GPIO18   CH2 -> GPIO4
//                                CH5 -> GPIO19   CH6 -> GPIO23
//                                CH8 -> GPIO22   (CH7 -> GPIO21, unused)
//                              Receiver ground (-) MUST be tied to ESP32 GND.
//              LEAVE THE STICKS CENTRED AT BOOT, they are calibrated there.

// Constants & Global Variables
//    Pins (Constants)
const int PUMP_PIN        = -1;   // CONFIRM: relay IN pin. -1 = pump disabled
const int ROBOCLAW_TX_PIN = 5;    // -> RoboClaw S1
const int RC_STEER_PIN    = 18;   // CH1  Left / Right
const int RC_DRIVE_PIN    = 4;    // CH2  Forward / Back
const int RC_ARM_PIN      = 19;   // CH5  SWA
const int RC_MODE_PIN     = 23;   // CH6  SWB  (up = manual/remote, down = web/MQTT)
const int RC_PUMP_PIN     = 22;   // CH8  SWD

//    Motor Commands (Constants)
const int STOP_COMMAND = 64;      // RoboClaw 0-127, 64 is stop
const int MAX_COMMAND  = 128;
const int MIN_COMMAND  = 0;
const long ROBOCLAW_BAUD = 38400;

//    Receiver channel indexes into rc[] (Constants)
const byte CH_STEER = 0;
const byte CH_DRIVE = 1;
const byte CH_ARM   = 2;
const byte CH_MODE  = 3;
const byte CH_PUMP  = 4;
const byte CH_COUNT = 5;

//    Tuning (Constants)
const int RAW_CENTER     = 1500;  // fallback centre if calibration fails
const int RAW_DEADZONE   = 40;    // us either side of centre, widen if jittery
const int CENTER_MAX_ERR = 250;   // reject a calibration further than this from 1500
const int ARM_ON_LEVEL   = 50;    // armed when mapped value exceeds this
const int PUMP_ON_LEVEL  = 50;    // pump on when mapped value exceeds this
const int MODE_ON_LEVEL  = 60;    // web mode above this, see note in loop()
const int LINK_TIMEOUT   = 200;   // ms without a pulse before failsafe
const int RC_MIN_US      = 800;   // pulses outside this range are glitches
const int RC_MAX_US      = 2200;
const bool INVERT_RIGHT  = false; // set true if the right motor runs backwards

//    MQTT timing (Constants)
const uint32_t MQTT_RETRY_MS   = 5000;  // gap between reconnect attempts
const uint32_t MQTT_TIMEOUT_MS = 1500;  // no command for this long -> stop

//    Stick centres (Global Variables) - measured at boot by calibrateSticks()
int centerSteer = RAW_CENTER;
int centerDrive = RAW_CENTER;

//    Mode state (Global Variables)
bool webMode     = false;   // false = RC remote, true = MQTT server
bool lastWebMode = false;   // used to detect a mode change

// ==================== Wifi & MQTT Server details ====================
// WiFi credentials
const char* ssid = "downRobotRoom";         // Replace with your WiFi SSID
const char* password = "robotsRcool";       // Replace with your WiFi password
// MQTT server details
const char* mqtt_server = "192.168.1.145";  // Replace with the IP address of your Raspberry Pi
const int mqtt_port = 1883;                 // Default MQTT port

// Define the MQTT topic to subscribe to
const char* MQTT_MOVE_CMD = "robot/control";
const char* MQTT_PUMP_CMD = "robot/pump";

WiFiClient espClient;
PubSubClient client(espClient);

bool     wifiStarted     = false;  // WiFi is started lazily, see startWiFi()
uint32_t lastMqttAttempt = 0;      // millis of the last reconnect attempt
uint32_t lastMqttCommand = 0;      // millis of the last accepted move command
// ====================================================================

// ======================== RoboClaw details ==========================
RoboClaw roboclaw(&Serial1, 10000);  // 2nd arg is TIMEOUT in microseconds

// RoboClaw addresses
#define ROBOCLAW_ADDRESS_LEFT  0x80  // Address for the left motor's RoboClaw
#define ROBOCLAW_ADDRESS_RIGHT 0x81  // Address for the right motor's RoboClaw
// ====================================================================

// ======================== Receiver (PWM) details ====================
// Each channel is its own wire carrying a 1000-2000 us pulse ~50 times a
// second. An interrupt times every pulse so loop() never has to wait.
struct RcChannel {
  uint8_t pin;
  volatile uint32_t riseUs;       // micros() at the last rising edge
  volatile uint16_t widthUs;      // last in-range pulse width
  volatile uint32_t lastValidUs;  // micros() when widthUs was updated
};

RcChannel rc[CH_COUNT] = {
  {RC_STEER_PIN}, {RC_DRIVE_PIN}, {RC_ARM_PIN}, {RC_MODE_PIN}, {RC_PUMP_PIN}
};

void IRAM_ATTR onRcEdge(void* arg)
{
  RcChannel* c = (RcChannel*)arg;
  uint32_t now = micros();
  if (digitalRead(c->pin)) { c->riseUs = now; return; }

  uint32_t w = now - c->riseUs;
  if (w >= RC_MIN_US && w <= RC_MAX_US) {   // drop glitches
    c->widthUs = w;
    c->lastValidUs = now;
  }
}

// Description: Latest pulse width of a channel
// Input:   Channel index (CH_*)
// Output:  Width in microseconds, or 0 if no valid pulse within LINK_TIMEOUT
uint16_t readRc(byte ch)
{
  noInterrupts();
  uint16_t w = rc[ch].widthUs;
  uint32_t t = rc[ch].lastValidUs;
  interrupts();
  if (w == 0 || micros() - t > (uint32_t)LINK_TIMEOUT * 1000) { return 0; }
  return w;
}

// RC control for reading switches (sticks use stickToSpeed)
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue)
{
    uint16_t ch = readRc(channelInput);
    if (ch < 100) return defaultValue;
    return map(ch, 1000, 2000, minLimit, maxLimit);
}
// ====================================================================

// --------------------- Function prototypes --------------------------
void calibrateSticks();
void rcLoop();
void mqttLoop();
void startWiFi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
int  stickToSpeed(uint16_t raw, int center);
bool rcLinkOk();
void setMotorSpeeds(int forwardCmd, int turnCmd);
void pumpControls(bool isON);
int  change_right(int speed);
void stopMotors();
void printDebug(int drive, int turn, int arm, int LSpeed, int RSpeed, bool pumpOn, bool linkOk);


// ================== Setup, Loop, and Callback =======================
void setup()
{
  /* Debugging: RoboClaw is on Serial1 (GPIO17), so Serial 0 is free.
                MONITOR MUST BE SET TO 115200.
  */
  Serial.begin(115200);
  // ---------------------------------------------------------------

  if (PUMP_PIN >= 0) {
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW); // Ensure pump is off at boot
  }

  delay(3000); // Allow power stabilization

  // Initialize RoboClaw (TX-only)
  // NOTE: do NOT call roboclaw.begin() on ESP32. It calls Serial1.begin()
  //       with the default pins (GPIO9/10), which are wired to the SPI
  //       flash on most modules and will boot-loop the board.
  Serial1.begin(ROBOCLAW_BAUD, SERIAL_8N1, -1, ROBOCLAW_TX_PIN);
  delay(500);
  stopMotors();
  Serial.println("RoboClaw initialized.");

  // Initialize receiver inputs (one PWM wire per channel)
  for (int i = 0; i < CH_COUNT; i++) {
    pinMode(rc[i].pin, INPUT_PULLDOWN);   // a loose wire reads 0, not noise
    attachInterruptArg(digitalPinToInterrupt(rc[i].pin), onRcEdge, &rc[i], CHANGE);
  }
  Serial.println("Receiver inputs initialized.");

  calibrateSticks();

  // NOTE: WiFi is deliberately NOT started here. It is started the first
  //       time SWB selects web mode. Nothing in the network stack is
  //       allowed to run while the remote is driving.
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setSocketTimeout(2);   // seconds, keeps a dead broker from stalling us
  Serial.println("MQTT configured (idle until web mode).");

  stopMotors();                 // Ensure motors are stopped
  Serial.println("Motors stopped. System ready.");
}

void loop()
{
  // NOTE: the threshold is 60, not 0. An UNASSIGNED channel sits at 1500,
  //       which maps to exactly 50, so a mistakenly unassigned SWB leaves
  //       us safely in remote mode instead of silently handing control
  //       to a server that may not be there.
  webMode = (readChannel(CH_MODE, 0, 100, 0) > MODE_ON_LEVEL);

  // Stop everything on a mode change so nothing carries over
  if (webMode != lastWebMode) {
    stopMotors();
    pumpControls(false);
    lastWebMode = webMode;
    Serial.println(webMode ? "--> WEB (MQTT) mode" : "--> CONTROLLER (RC) mode");
  }

  if (webMode) {
    mqttLoop();   // Check callback() for MQTT interaction with components
  } else {
    rcLoop();     // Check rcLoop() for full remote interaction with components
  }
}
// ====================================================================

// Description: Samples the resting stick positions at boot so that a
//              mistrimmed transmitter still maps centre to exactly stop.
//              Falls back to RAW_CENTER if the reading looks implausible,
//              which usually means a stick was held during boot.
// Input:   NONE
// Output:  NONE (sets centerSteer / centerDrive)
void calibrateSticks()
{
  Serial.println("Calibrating, keep the sticks centred...");

  // Let pulses arrive before sampling
  delay(1000);

  long sumSteer = 0;
  long sumDrive = 0;
  int  samples  = 0;

  for (int i = 0; i < 50; i++) {
    uint16_t s = readRc(CH_STEER);
    uint16_t d = readRc(CH_DRIVE);
    if (s > 100 && d > 100) { sumSteer += s; sumDrive += d; samples++; }
    delay(10);
  }

  if (samples < 10) {
    Serial.println("WARNING: no receiver signal, using default centres.");
    centerSteer = RAW_CENTER;
    centerDrive = RAW_CENTER;
    return;
  }

  int s = sumSteer / samples;
  int d = sumDrive / samples;

  // A centre far from 1500 means a stick was held, do not trust it
  if (abs(s - RAW_CENTER) > CENTER_MAX_ERR || abs(d - RAW_CENTER) > CENTER_MAX_ERR) {
    Serial.println("WARNING: sticks were not centred, using defaults.");
    centerSteer = RAW_CENTER;
    centerDrive = RAW_CENTER;
    return;
  }

  centerSteer = s;
  centerDrive = d;
  Serial.print("Centres: steer="); Serial.print(centerSteer);
  Serial.print("  drive=");        Serial.println(centerDrive);
}

// Description: RC remote interaction with components (movement, pump)
// Input:   NONE
// Output:  NONE
void rcLoop()
{
  // Sticks are read RAW against their measured centre so that a released
  // stick maps to exactly zero. Going through readChannel() with a 0-126
  // range puts centre at 63, one count off stop, which leaves both motors
  // permanently crawling.
  int drive = stickToSpeed(readRc(CH_DRIVE), centerDrive); // Forward / Back
  int turn  = stickToSpeed(readRc(CH_STEER), centerSteer); // Left / Right

  int arm    = readChannel(CH_ARM,  0, 100, 0);   // CH5 SWA
  int pumpCh = readChannel(CH_PUMP, 0, 100, 0);   // CH8 SWD

  int LSpeed = constrain(STOP_COMMAND + drive + turn, 1, 127);
  int RSpeed = constrain(STOP_COMMAND + drive - turn, 1, 127);

  // Snap anything within 1 count of centre to exactly stop
  if (abs(LSpeed - STOP_COMMAND) <= 1) { LSpeed = STOP_COMMAND; }
  if (abs(RSpeed - STOP_COMMAND) <= 1) { RSpeed = STOP_COMMAND; }

  if (INVERT_RIGHT) { RSpeed = change_right(RSpeed); }

  // NOTE: arm uses a threshold, not "!= 0". PWM timing jitters by a few us,
  //       so a disarmed switch can read 1005 and map to 0-1.
  bool linkOk = rcLinkOk();
  bool active = (arm > ARM_ON_LEVEL && linkOk);
  bool pumpOn = (active && pumpCh > PUMP_ON_LEVEL);

  // IMPORTANT: the robot only moves when armed (SWA) and the RC link is up
  // FOR: pump
  pumpControls(pumpOn);

  // FOR: drive
  if (active) {
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT,  LSpeed);
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, RSpeed);
  }
  else {
    stopMotors();
  }

  printDebug(drive, turn, arm, LSpeed, RSpeed, pumpOn, linkOk);
}

// Description: Non-blocking MQTT service. Keeps the connection alive and
//              stops the robot if commands go quiet. Only ever called
//              while SWB selects web mode.
// Input:   NONE
// Output:  NONE
void mqttLoop()
{
  startWiFi();   // no-op after the first call

  if (WiFi.status() != WL_CONNECTED) {
    stopMotors();
    pumpControls(false);
    return;                       // WiFi library keeps retrying on its own
  }

  if (!client.connected()) {
    stopMotors();
    pumpControls(false);
    // Retry on a timer instead of every pass, connect() can still stall
    // for up to the socket timeout when the broker is unreachable.
    if (millis() - lastMqttAttempt > MQTT_RETRY_MS) {
      lastMqttAttempt = millis();
      reconnect();
    }
    return;
  }

  client.loop();   // Check callback() for MQTT interaction with components

  // Failsafe: the server must keep sending, otherwise we stop
  if (millis() - lastMqttCommand > MQTT_TIMEOUT_MS) {
    stopMotors();
  }
}

// Description: Brings up WiFi the first time web mode is selected.
//              Deliberately does not wait for a connection.
// Input:   NONE
// Output:  NONE
void startWiFi()
{
  if (wifiStarted) { return; }
  wifiStarted = true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("WiFi started (connecting in background).");
}

// Description: MQTT interaction with components (movement, pump)
// Input:   topic's message, its payload, and length of message
// Output:  NONE
void callback(char* topic, byte* payload, unsigned int length)
{
  // IMPORTANT: RC (Remote Control) takes priority. SWB must be down
  //            for MQTT to take over control.
  if (!webMode) { return; }

  // Convert payload to String once
  String message;
  for (unsigned int i = 0; i < length; i++) { message += (char)payload[i]; }

  // FOR: pump
  if (strcmp(topic, MQTT_PUMP_CMD) == 0) {
    pumpControls(message == "1");
    return;
  }

  // FOR: motor
  if (strcmp(topic, MQTT_MOVE_CMD) == 0) {
    // NOTE: the payload is "forward side", matching central_script.py
    //       (f"{front_back_command} {side_side_command}"). The Pi uses
    //       side 126 = left, 0 = right, which is the mirror of the stick,
    //       so it is flipped here to steer the same way as the remote.
    int spaceIndex = message.indexOf(' ');
    if (spaceIndex == -1) {
      Serial.println("Invalid message format. Expected 'forward side'.");
      stopMotors();
      return;
    }

    int forwardCmd = message.substring(0, spaceIndex).toInt();
    int sideCmd    = message.substring(spaceIndex + 1).toInt();
    int turnCmd    = 2 * STOP_COMMAND - sideCmd;

    // Validate command ranges
    if (forwardCmd < MIN_COMMAND || forwardCmd > MAX_COMMAND ||
        turnCmd    < MIN_COMMAND || turnCmd    > MAX_COMMAND) {
      Serial.println("Command values out of range. Expected 0-128.");
      stopMotors();
      return;
    }

    lastMqttCommand = millis();   // feed the failsafe
    setMotorSpeeds(forwardCmd, turnCmd);
  }
}

// Description: Converts a raw stick pulse width into a signed speed offset
// Input:   Raw channel value in microseconds, and that stick's centre
// Output:  Signed speed -63 to +63, where 0 is stop
int stickToSpeed(uint16_t raw, int center)
{
  if (raw < 100) { return 0; }              // no signal -> stop
  int v = (int)raw - center;
  if (abs(v) < RAW_DEADZONE) { return 0; }  // inside the deadzone -> stop
  v = constrain(v, -500, 500);
  return map(v, -500, 500, -63, 63);
}

// Description: Reports whether every receiver wire is still delivering
//              valid pulses. One unplugged wire counts as a lost link.
// Input:   NONE
// Output:  True if all channels had a valid pulse within LINK_TIMEOUT
bool rcLinkOk()
{
  for (byte i = 0; i < CH_COUNT; i++) {
    if (readRc(i) == 0) { return false; }
  }
  return true;
}

// Description: Sets the motor speeds based on forward and turn commands.
//              Uses the same mixing as the RC path so both control
//              modes behave identically.
// Input:   forwardCmd Forward command value (0-128), where 64 is stop.
//          turnCmd Turn command value (0-128), where 64 is no turn.
// Output:  NONE
void setMotorSpeeds(int forwardCmd, int turnCmd)
{
  int drive = forwardCmd - STOP_COMMAND;   // Range: -64 to +64
  int turn  = turnCmd    - STOP_COMMAND;   // Range: -64 to +64

  int LSpeed = constrain(STOP_COMMAND + drive + turn, 1, 127);
  int RSpeed = constrain(STOP_COMMAND + drive - turn, 1, 127);

  if (abs(LSpeed - STOP_COMMAND) <= 1) { LSpeed = STOP_COMMAND; }
  if (abs(RSpeed - STOP_COMMAND) <= 1) { RSpeed = STOP_COMMAND; }

  if (INVERT_RIGHT) { RSpeed = change_right(RSpeed); }

  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT,  LSpeed);
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, RSpeed);
}

// Description: Turns the relay for the pump ON or OFF
// Input:   Boolean, is it ON or OFF
// Output:  NONE
void pumpControls(bool isON)
{
  if (PUMP_PIN < 0) { return; }   // relay pin not configured yet
  digitalWrite(PUMP_PIN, isON ? HIGH : LOW);
}

// Description: Inverts a 0-127 motor command around the stop point
// Input:   Speed value 0-127, where 64 is stop
// Output:  The mirrored speed value
int change_right(int speed)
{
  int temp = speed - STOP_COMMAND;
  temp *= -1;
  return temp + STOP_COMMAND;
}

// Description: Stop motors
// Input:   NONE
// Output:  NONE
void stopMotors()
{
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT,  STOP_COMMAND);
  roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, STOP_COMMAND);
}

// Description: Reconnects to the MQTT broker if disconnected.
// Input:   NONE
// Output:  NONE
void reconnect()
{
  Serial.print("Attempting MQTT connection... ");

  if (client.connect("ESP32Client1", "robot", "robot1")) {
    Serial.println("connected");
    client.subscribe(MQTT_MOVE_CMD);
    client.subscribe(MQTT_PUMP_CMD);
  } else {
    Serial.print("failed, rc=");
    Serial.println(client.state());
  }
}

// Description: Throttled debug output, does not block the main loop
// Input:   The mixed drive values and current state flags
// Output:  NONE
void printDebug(int drive, int turn, int arm, int LSpeed, int RSpeed, bool pumpOn, bool linkOk)
{
  static uint32_t last = 0;
  if (millis() - last < 300) { return; }
  last = millis();

  Serial.print("ch1="); Serial.print(readRc(CH_STEER));
  Serial.print(" ch2=");  Serial.print(readRc(CH_DRIVE));
  Serial.print(" ch5=");  Serial.print(readRc(CH_ARM));
  Serial.print(" ch6=");  Serial.print(readRc(CH_MODE));
  Serial.print(" ch8=");  Serial.print(readRc(CH_PUMP));
  Serial.print(" | drive="); Serial.print(drive);
  Serial.print(" turn=");    Serial.print(turn);
  Serial.print(" | L=");     Serial.print(LSpeed);
  Serial.print(" R=");       Serial.print(RSpeed);
  Serial.print(pumpOn ? "  PUMP" : "  ----");
  Serial.print(arm > ARM_ON_LEVEL ? "  ARMED" : "  DISARMED");
  Serial.println(linkOk ? "  LINK" : "  NO-LINK");
}