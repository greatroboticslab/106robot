/*=========================================
  ========== Motor & Pump Controls ========
  =========================================*/

// Libraries:
#include <WiFi.h>
#include <PubSubClient.h>
#include <RoboClaw.h>
#include <IBusBM.h>

// Description: This is the program for an Esp 32 Dev. It gets signals from
//              a remote control(IBUS) or a server(MQTT). SWB picks which.
// Aditional:   To upload we use the board "ESP32 Dev Module",
//              Pin Locations:  Pump Relay:        GPIO22
//                              Motors(RoboClaw):  TX2 (GPIO17)
//                              Ibus:              RX2 (GPIO16)
//              LEAVE THE STICKS CENTRED AT BOOT, they are calibrated there.

// Constants & Global Variables
//    Pins (Constants)
const int PUMP_PIN        = 22;
const int ROBOCLAW_TX_PIN = 17;   // TX2 -> RoboClaw RX
const int IBUS_RX_PIN     = 16;   // RX2 <- iBus signal

//    Motor Commands (Constants)
const int STOP_COMMAND = 64;      // RoboClaw 0-127, 64 is stop
const int MAX_COMMAND  = 128;
const int MIN_COMMAND  = 0;
const long ROBOCLAW_BAUD = 38400;

//    Ibus channel indexes (Constants) - ZERO based, index 0 = physical CH1
const byte CH_STEER = 0;   // CH1  Left / Right
const byte CH_DRIVE = 1;   // CH2  Forward / Back
const byte CH_ARM   = 4;   // CH5  SWA
const byte CH_MODE  = 5;   // CH6  SWB  (up = manual/iBus, down = web/MQTT)
const byte CH_PUMP  = 7;   // CH8  SWD

//    Tuning (Constants)
const int RAW_CENTER     = 1500;  // fallback centre if calibration fails
const int RAW_DEADZONE   = 40;    // us either side of centre, widen if jittery
const int CENTER_MAX_ERR = 250;   // reject a calibration further than this from 1500
const int PUMP_ON_LEVEL  = 50;    // pump on when mapped value exceeds this
const int MODE_ON_LEVEL  = 60;    // web mode above this, see note in loop()
const int LINK_TIMEOUT   = 200;   // ms without a frame before failsafe
const bool INVERT_RIGHT  = false; // set true if the right motor runs backwards

//    MQTT timing (Constants)
const uint32_t MQTT_RETRY_MS   = 5000;  // gap between reconnect attempts
const uint32_t MQTT_TIMEOUT_MS = 1500;  // no command for this long -> stop

//    Stick centres (Global Variables) - measured at boot by calibrateSticks()
int centerSteer = RAW_CENTER;
int centerDrive = RAW_CENTER;

//    Mode state (Global Variables)
bool webMode     = false;   // false = iBus remote, true = MQTT server
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

// ========================== Ibus details ============================
IBusBM ibus;

// IBus control for reading RC controls (switches only, see stickToSpeed)
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue)
{
    uint16_t ch = ibus.readChannel(channelInput);
    if (ch < 100) return defaultValue;
    return map(ch, 1000, 2000, minLimit, maxLimit);
}
// ====================================================================

// --------------------- Function prototypes --------------------------
void calibrateSticks();
void ibusLoop();
void mqttLoop();
void startWiFi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
int  stickToSpeed(uint16_t raw, int center);
bool ibusLinkOk();
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

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);   // Ensure pump is off at boot

  delay(3000); // Allow power stabilization

  // Initialize RoboClaw (TX-only)
  // NOTE: do NOT call roboclaw.begin() on ESP32. It calls Serial1.begin()
  //       with the default pins (GPIO9/10), which are wired to the SPI
  //       flash on most modules and will boot-loop the board.
  Serial1.begin(ROBOCLAW_BAUD, SERIAL_8N1, -1, ROBOCLAW_TX_PIN);
  delay(500);
  stopMotors();
  Serial.println("RoboClaw initialized.");

  // Initialize iBus (RX-only)
  Serial2.setRxBufferSize(512);   // headroom, frames arrive every ~7 ms
  Serial2.begin(115200, SERIAL_8N1, IBUS_RX_PIN, -1);
  delay(100);  // small pause helps stabilize
  ibus.begin(Serial2, IBUSBM_NOTIMER);   // no timer ISR, avoids the wdt panic
  Serial.println("iBus receiver initialized.");

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
  ibus.loop();   // FIRST, every iteration, in BOTH modes, no exceptions

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
    Serial.println(webMode ? "--> WEB (MQTT) mode" : "--> CONTROLLER (iBus) mode");
  }

  if (webMode) {
    mqttLoop();   // Check callback() for MQTT interaction with components
  } else {
    ibusLoop();   // Check ibusLoop() for full ibus interaction with components
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

  // Let frames accumulate before sampling
  for (int i = 0; i < 100; i++) { ibus.loop(); delay(10); }

  long sumSteer = 0;
  long sumDrive = 0;
  int  samples  = 0;

  for (int i = 0; i < 50; i++) {
    ibus.loop();
    uint16_t s = ibus.readChannel(CH_STEER);
    uint16_t d = ibus.readChannel(CH_DRIVE);
    if (s > 100 && d > 100) { sumSteer += s; sumDrive += d; samples++; }
    delay(10);
  }

  if (samples < 10) {
    Serial.println("WARNING: no iBus signal, using default centres.");
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

// Description: ibus interaction with components (movement, pump)
// Input:   NONE
// Output:  NONE
void ibusLoop()
{
  // Sticks are read RAW against their measured centre so that a released
  // stick maps to exactly zero. Going through readChannel() with a 0-126
  // range puts centre at 63, one count off stop, which leaves both motors
  // permanently crawling.
  int drive = stickToSpeed(ibus.readChannel(CH_DRIVE), centerDrive); // Forward / Back
  int turn  = stickToSpeed(ibus.readChannel(CH_STEER), centerSteer); // Left / Right

  int arm    = readChannel(CH_ARM,  0, 100, 0);   // CH5 SWA
  int pumpCh = readChannel(CH_PUMP, 0, 100, 0);   // CH8 SWD

  int LSpeed = constrain(STOP_COMMAND + drive + turn, 1, 127);
  int RSpeed = constrain(STOP_COMMAND + drive - turn, 1, 127);

  // Snap anything within 1 count of centre to exactly stop
  if (abs(LSpeed - STOP_COMMAND) <= 1) { LSpeed = STOP_COMMAND; }
  if (abs(RSpeed - STOP_COMMAND) <= 1) { RSpeed = STOP_COMMAND; }

  if (INVERT_RIGHT) { RSpeed = change_right(RSpeed); }

  bool linkOk = ibusLinkOk();
  bool active = (arm != 0 && linkOk);
  bool pumpOn = (active && pumpCh > PUMP_ON_LEVEL);

  // IMPORTANT: the robot only moves when armed (SWA) and the iBus link is up
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
  // no delay() here, ibus.loop() must keep up with 7 ms frames
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
  // IMPORTANT: Ibus (Remote Control) takes priority. SWB must be down
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
    // NOTE: the payload is "turn forward", the turn value comes FIRST.
    //       Confirm the Raspberry Pi publishes it in that order.
    int spaceIndex = message.indexOf(' ');
    if (spaceIndex == -1) {
      Serial.println("Invalid message format. Expected 'turn forward'.");
      stopMotors();
      return;
    }

    int turnCmd    = message.substring(0, spaceIndex).toInt();
    int forwardCmd = message.substring(spaceIndex + 1).toInt();

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

// Description: Converts a raw iBus stick value into a signed speed offset
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

// Description: Reports whether iBus frames are still arriving
// Input:   NONE
// Output:  True if a new frame was seen within LINK_TIMEOUT
bool ibusLinkOk()
{
  static uint16_t lastCnt = 0;
  static uint32_t lastCntTime = 0;

  if (ibus.cnt_rec != lastCnt) {
    lastCnt = ibus.cnt_rec;
    lastCntTime = millis();
  }
  return (millis() - lastCntTime) < LINK_TIMEOUT;
}

// Description: Sets the motor speeds based on forward and turn commands.
//              Uses the same mixing as the iBus path so both control
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
{ digitalWrite(PUMP_PIN, isON ? HIGH : LOW); }

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

// Description: Throttled debug output, does not block the ibus loop
// Input:   The mixed drive values and current state flags
// Output:  NONE
void printDebug(int drive, int turn, int arm, int LSpeed, int RSpeed, bool pumpOn, bool linkOk)
{
  static uint32_t last = 0;
  if (millis() - last < 300) { return; }
  last = millis();

  Serial.print("cnt=");    Serial.print(ibus.cnt_rec);
  Serial.print("  raw: ");
  for (int i = 0; i < 10; i++) { Serial.print(ibus.readChannel(i)); Serial.print(' '); }
  Serial.print(" | drive="); Serial.print(drive);
  Serial.print(" turn=");    Serial.print(turn);
  Serial.print(" | L=");     Serial.print(LSpeed);
  Serial.print(" R=");       Serial.print(RSpeed);
  Serial.print(pumpOn ? "  PUMP" : "  ----");
  Serial.print(arm != 0 ? "  ARMED" : "  DISARMED");
  Serial.println(linkOk ? "  LINK" : "  NO-LINK");
}