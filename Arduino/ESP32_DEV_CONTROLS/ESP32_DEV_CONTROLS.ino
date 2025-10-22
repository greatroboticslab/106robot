  /*=========================================
  ========== Motor & Pump Controls ==========
  =========================================*/

// Libraries:
#include <WiFi.h>
#include <PubSubClient.h>
#include <RoboClaw.h>
#include <IBusBM.h>

// Description: This is the program for an Esp 32 Dev. It can gets signals 
//              from a remote control(IBUS) or a server(MQTT).
// Aditional:   To upload we use the board "ESP32 Dev Module",
//              Pin Locations:  Pump Relay:        GPIO22
//                              Motors(RoboClaw):  TX0
//                              Ibus:              RX2

// Constants & Global Varuable
//    Pins (Constants)
const int PUMP_PIN = 22;
//    Motor Comands (Constants)
const int STOP_COMMAND = 64;
const int MAX_COMMAND  = 128;
const int MIN_COMMAND  = 0;
//    Ibus Controls (Global Varibales)
int mode = 0;

// ==================== Wifi & MQTT Server details ====================
// WiFi credentials
const char* ssid = "downRobotRoom";         // Replace with your WiFi SSID
const char* password = "robotsRcool";       // Replace with your WiFi password
// MQTT server details
const char* mqtt_server = "192.168.1.145";  // Replace with the IP address of your Raspberry Pi
const int mqtt_port = 1883;                 // Default MQTT port

WiFiClient espClient;
PubSubClient client(espClient);

// Define the MQTT topic to subscribe to
const char* MQTT_MOVE_CMD = "robot/control";
const char* MQTT_PUMP_CMD = "robot/pump";
// ====================================================================

// ======================== RoboClaw details ==========================
RoboClaw roboclaw(&Serial, 10000);  // Serial(Serial0/TX0) communication to motors

// RoboClaw addresses
#define ROBOCLAW_ADDRESS_LEFT  0x80  // Address for the left motor's RoboClaw
#define ROBOCLAW_ADDRESS_RIGHT 0x81  // Address for the right motor's RoboClaw
// ====================================================================

// ========================== Ibus details ============================
IBusBM ibus;

// IBus control for reading RC controls
int readChannel(byte channelInput, int minLimit, int maxLimit, int defaultValue) 
{
    uint16_t ch = ibus.readChannel(channelInput);
    if (ch < 100) return defaultValue;
    return map(ch, 1000, 2000, minLimit, maxLimit);
}
// ====================================================================

// --------------------- Function prototypes --------------------------
void setupWiFi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
void ibusLoop();
uint8_t mapMotorSpeed(int speed);
void setMotorSpeeds(int forwardCmd, int turnCmd);
void pumpControls(bool isON);
int change_right(int speed);
void stopMotors();


// ================== Setup, Loop, and Callback =======================
void setup() 
{
    pinMode(PUMP_PIN, OUTPUT);

    // Initialize RoboClaw
    roboclaw.begin(38400);
    delay(5000);

    /* Debugging: Due to both roboclaw and serial monitor using Serial 0,
           - Comment our instances of roboclaw
           - Uncomment that that contain serial.print or .println
    */
    // Serial.begin(115200);
    // ---------------------------------------------------------------

    // Initialize WiFi
    setupWiFi();
    delay(500);

    // Initialize MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    delay(500);
    
    // Initialize Ibus
    ibus.begin(Serial2);
    delay(500);

    // Stop all operation for START-UP
    stopMotors();                 // Ensure motors are stopped
    digitalWrite(PUMP_PIN, LOW);  // Ensure pump are stopped
}

void loop() 
{
  // Reconnect to MQTT broker if not connected
  if (!client.connected()) { reconnect(); }

  // Handle incoming MQTT messages
  client.loop();  // Check Callback() for MQTT interaction with components
  // Handle incoming Ibus messages
  ibusLoop(); // Check ibusLoop() for full ibus interaction with components
}
// ====================================================================

// Description: ibus interaction with components (movement, pump)
// Input:   NONE
// Output:  NONE
void ibusLoop()
{
  bool pump_flag = false;
  int pump_channel = readChannel(7, 0, 100, 0);  // IBus SWD
  int ch2 = readChannel(1, 0, 126, 64); // Forward and back
  int ch1 = readChannel(0, 0, 126, 64); // Left and right
  int arm = readChannel(4, 0, 100, 0);  // Channel 5 on IBus SWA
  mode = readChannel(5, 0, 100, 0); // Channel 6 on IBus SWB (Manual = 0 (UP),  Auto = 100 (Down))

  // Below write any Ibus interaction with components
  // FOR: pump
  if(mode = 0){
    if (pump_channel == 100){
      pump_flag = true;
    } 
    else if (pump_channel == 0){
      pump_flag = false;
    }
    else{
      pump_flag = false;
      // Serial.print("Pump_Channel is using the wrong Channel");
    }
    pumpControls(pump_flag);
  }
  //FOR: drive
  int turn = map(ch1, 0, 126, -63, 63);
  int value = ch2;
  int LSpeed = value + turn;
  int RSpeed = value - turn;

  if (value > 62 && value < 66) { value = 64; }
  if (LSpeed <= 66 && LSpeed >= 62) { LSpeed = 64; }
  if (RSpeed <= 66 && RSpeed >= 62) { RSpeed = 64; }
  
  LSpeed = constrain(LSpeed, 1, 127);
  RSpeed = constrain(RSpeed, 1, 127);


  /* Debugging Print Statements
    // Serial.println("DEBUG: ");
    // Serial.print("ch1 Left/Right): "); Serial.print(ch1);
    // Serial.print(" | ch2 (Fwd/Back): "); Serial.println(ch2);
    // Serial.print("arm (CH5 SWA): "); Serial.print(arm);
    // Serial.print(" | mode (CH6 SWB): "); Serial.println(mode);
    // Serial.print("Mapped Turn: "); Serial.print(turn);
    // Serial.print(" | Value: "); Serial.println(value);
    // Serial.print("Left Speed: "); Serial.print(LSpeed);
    // Serial.print("  | Right Speed: "); Serial.println(RSpeed);
    // Serial.println();
  */

  // IMPORTANT: For Ibus Controller to move the robot
  //    it must be armed(100 - down) and MANUAL mode(100 - down)
  if(arm == 0 & mode != 0){
    stopMotors();
  } 
  else {
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, LSpeed);
    roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, RSpeed);
  }
  delay(30);
}

// Description: MQTT interaction with components (movement, pump)
// Input:   topic's message, its payload, and length of message
// Output:  NONE
void callback(char* topic, byte* payload, unsigned int length) 
{
  bool pump_flag = false

  // IMPORTAN: Ibus (Remote Control) takes priority. Mode's Switch must 
  //            output 100 for MQTT to take over control
  if (mode != 0){
    // Serial.print("Message arrived ["); Serial.print(topic); Serial.print("]: ");

    // Below write any MQTT interaction with components
    // FOR: pump
    // Recieved payload from topic (PUMP)
    if (strcmp(topic, MQTT_PUMP_CMD) == 0){
      // Convert payload to String
      String pump_message;
      for (unsigned int i = 0; i < length; i++) {
        pump_message += (char)payload[i];
      }
      // Serial.println(pump_message);

      // Activate on pump message
      if (pump_message == "1"){
        pump_flag = true;
      } 
      else if (pump_message == "0"){
        pump_flag = false;
      }
      else{
        pump_flag = false;
        // Serial.print("Uknown command");
      }
      pumpControls(pump_flag);
    }

    // FOR: motor
    // Recieved payload from topic (MOTOR)
    if (strcmp(topic, MQTT_MOVE_CMD) == 0){
      // Convert payload to String
      String move_message;
      for (unsigned int i = 0; i < length; i++) {
        move_message += (char)payload[i];
      }

      // Serial.println(move_message);

      // Parse the message assuming format "forward_command side_command"
      int spaceIndex = move_message.indexOf(' ');
      if (spaceIndex == -1) {
        //Serial.println("Invalid message format. Expected 'forward_command side_command'.");
        stopMotors();
        return;
      }

      String forwardStr = move_message.substring(0, spaceIndex);
      String turnStr = move_message.substring(spaceIndex + 1);

      // Convert strings to integers
      int forwardCmd = forwardStr.toInt();
      int turnCmd = turnStr.toInt();

      // Validate command ranges
      if (forwardCmd < MIN_COMMAND || forwardCmd > MAX_COMMAND ||
          turnCmd < MIN_COMMAND || turnCmd > MAX_COMMAND) {
        // Serial.println("Command values out of range. Expected 0-126.");
        stopMotors();
        return;
      }

      // Debugging output
      // Serial.print("Forward Command: ");
      // Serial.print(forwardCmd);
      // Serial.print(" | Turn Command: ");
      // Serial.println(turnCmd);

      // Determine if the robot should stop
      if (forwardCmd == STOP_COMMAND && turnCmd == STOP_COMMAND) {
        stopMotors();
      } else {
        setMotorSpeeds(forwardCmd, turnCmd);
      }
    }
  }
}

// Description: Turns the relay for the pump ON or OFF
// Input:   Boolean, is it ON or OFF
// Output:  NONE
void pumpControls(bool isON)
{
  if (isON) {
    digitalWrite(PUMP_PIN, HIGH);
    //Serial.println("Pump ON");
  } else {
    digitalWrite(PUMP_PIN, LOW);
    //Serial.println("Pump OFF");
  }
}

// Description: Maps a speed value from -127 to +127 to 0-127 for RoboClaw.
// Input:   Speed values ranging from -127 to +127.
// Output:  A uint8_t Mapped speed value ranging from 0 to 127.
uint8_t mapMotorSpeed(int speed) 
{
  int mappedSpeed = speed + 64; // Map from -127~+127 to 0~254
  mappedSpeed = map(speed, -127, 127, 0, 254);
  return (uint8_t)(mappedSpeed / 2); // Scale down to 0~127
}

// Description: Sets the motor speeds based on forward and turn commands.
// Input:   forwardCmd Forward command value (0-126), where 64 is stop.
//          turnCmd Turn command value (0-126), where 64 is no turn.
// Output:  NONE
void setMotorSpeeds(int forwardCmd, int turnCmd) 
{
  // Check if controller is armed to execute commands
  if( mode == 0) { return; }

  // Map forward and turn commands from 0-128 to -64 to +64
  int mappedForward = forwardCmd - STOP_COMMAND; // Range: -64 to +64
  int mappedTurn = turnCmd - STOP_COMMAND;       // Range: -64 to +64

  // Calculate left and right motor speeds
  int LSpeed = mappedForward + mappedTurn;
  int RSpeed = mappedForward - mappedTurn;

  // Constrain motor speeds to valid range (-127 to +127)
  LSpeed = constrain(LSpeed, -127, 127);
  RSpeed = constrain(RSpeed, -127, 127);

  // Map speeds to 0-127 for RoboClaw
  int MappedLSpeed = mapMotorSpeed(LSpeed);
  int MappedRSpeed = mapMotorSpeed(RSpeed);

  // Debugging output
  // Serial.print("Mapped Forward: ");
  // Serial.print(mappedForward);
  // Serial.print(" | Mapped Turn: ");
  // Serial.print(mappedTurn);
  // Serial.print(" | LSpeed: ");
  // Serial.print(LSpeed);
  // Serial.print(" | RSpeed: ");
  // Serial.print(RSpeed);
  // Serial.print(" | MappedLSpeed: ");
  // Serial.print(MappedLSpeed);
  // Serial.print(" | MappedRSpeed: ");
  // Serial.println(MappedRSpeed);

  MappedRSpeed = change_right(MappedRSpeed);

  // Set motor speeds on M1 of each RoboClaw controller
  if (!roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, MappedLSpeed)) {
    //Serial.println("Left RoboClaw Command Failed!");
  }
  if (!roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, MappedRSpeed)) {
    //Serial.println("Right RoboClaw Command Failed!");
  }
}

// Description: Inverts
int change_right(int speed)
{
  int temp = speed - 64;
  temp *= -1;
  int mapped = temp + 64;

  return mapped;
}

// Description: Stop motors
void stopMotors() 
{

  //Serial.println("Stopping Motors");
  if (!roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_LEFT, STOP_COMMAND)) {
    //Serial.println("Left RoboClaw Command Failed!");
  }
  if (!roboclaw.ForwardBackwardM1(ROBOCLAW_ADDRESS_RIGHT, STOP_COMMAND)) {
    //Serial.println("Right RoboClaw Command Failed!");
  }
}

// Description: Connects the ESP32 to the WiFi
void setupWiFi() 
{
  // Serial.print("Connecting to ");
  // Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    // Serial.print(".");
  }

  // Serial.println("\nConnected to WiFi");
  // Serial.print("IP Address: ");
  // Serial.println(WiFi.localIP());
}

// Description: Reconnects to the MQTT broker if disconnected.
void reconnect() 
{
  while (!client.connected()) {
    // Serial.print("Attempting MQTT connection...");

    // Attempt to connect with username and password
    if (client.connect("ESP32Client1", "robot", "robot1")) {
      // Serial.println("connected");
      client.subscribe(MQTT_MOVE_CMD);
      client.subscribe(MQTT_PUMP_CMD);
      // Serial.print("Subscribed to topic: ");
      // Serial.println(topic);
    } else {
      // Serial.print("failed, rc=");
      // Serial.print(client.state());
      // Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


