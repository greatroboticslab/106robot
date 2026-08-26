/*=========================================
  ========== Motor & Pump Controls ========
  =========================================*/

// Libraries:
#include <RoboClaw.h>
#include <IBusBM.h>

// Description: Remote-control-only build for an Esp 32 Dev. Reads the RC
//              remote over iBus and drives the two RoboClaws plus the pump
//              relay. WiFi / MQTT are added in a later step.
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
const long ROBOCLAW_BAUD = 38400;

//    Ibus channel indexes (Constants) - ZERO based, index 0 = physical CH1
const byte CH_STEER = 0;   // CH1  Left / Right
const byte CH_DRIVE = 1;   // CH2  Forward / Back
const byte CH_ARM   = 4;   // CH5  SWA
const byte CH_PUMP  = 7;   // CH8  SWD

//    Tuning (Constants)
const int RAW_CENTER     = 1500;  // fallback centre if calibration fails
const int RAW_DEADZONE   = 40;    // us either side of centre, widen if jittery
const int CENTER_MAX_ERR = 250;   // reject a calibration further than this from 1500
const int PUMP_ON_LEVEL  = 50;    // pump on when mapped value exceeds this
const int LINK_TIMEOUT   = 200;   // ms without a frame before failsafe
const bool INVERT_RIGHT  = false; // set true if the right motor runs backwards

//    Stick centres (Global Variables) - measured at boot by calibrateSticks()
int centerSteer = RAW_CENTER;
int centerDrive = RAW_CENTER;

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
int  stickToSpeed(uint16_t raw, int center);
bool ibusLinkOk();
void pumpControls(bool isON);
int  change_right(int speed);
void stopMotors();
void printDebug(int drive, int turn, int arm, int LSpeed, int RSpeed, bool pumpOn, bool linkOk);


// ======================== Setup and Loop ============================
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

  stopMotors();                 // Ensure motors are stopped
  Serial.println("Motors stopped. System ready.");
}

void loop()
{
  ibus.loop();   // FIRST, every iteration, no exceptions
  ibusLoop();    // Check ibusLoop() for full ibus interaction with components
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