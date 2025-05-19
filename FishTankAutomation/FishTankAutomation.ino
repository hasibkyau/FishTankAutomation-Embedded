#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "utils.h"
#include "json.h"
#include <ESP32Servo.h>

// Temperature
#include <OneWire.h>
#include <DallasTemperature.h>

// Firebase
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define API_KEY "AIzaSyDTX_U9v_x9fVE6RoTbiVNRRlZYYeJt7rg"
#define USER_EMAIL "shaemsakib@gmail.com"
#define USER_PASSWORD "@shaemsakib"
#define DATABASE_URL "https://fish-tank-automation-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Firebase Paths
String uid;
String basePath = "/FISH-TANK";
String statusPath = basePath + "/status";
String logsPath = basePath + "/logs";

// Global Actuator States
bool g_led = false;
bool g_pump = false;
String g_servo = "idle";  // "idle", "activate", or "auto"

// Last known states to detect changes
bool last_led = false;
bool last_pump = false;
String last_servo = "";

// NTP
const char* ntpServer = "pool.ntp.org";

// Temperature (DS18B20)
#define ONE_WIRE_BUS 4

// Sensor Readings (dummy initialized)
float g_phValue = 0.0f;
float g_waterLevelIN = 0.0f;
float g_waterLevelPercent = 0.0f;
float g_tempC = 0.0f;

// ESP Pin Distrubution
constexpr int LED_PIN = 2;
constexpr int WATER_PUMP_PIN = 16;

//Servo pin and variables
#define SERVO_PIN        17
constexpr int open_angle    = 90;      // adjust to your feeder’s “open” position
constexpr int idle_angle    =  0;      // “closed” position
constexpr int feed_duration = 3000;    // milliseconds to hold open

// Servo object
Servo feederServo;


// Ultrasonic (water level)
constexpr int TRIG_PIN = 14;
constexpr int ECHO_PIN = 27;
constexpr float tank_height_inch = 12.0f;  // total tank height in inches

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  digitalWrite(WATER_PUMP_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // Sonor Sensor
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Servo setup — 50 Hz, with pulse widths 500–2400 μs
  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(idle_angle);

  initWiFi();
  configTime(6 * 3600, 0, ntpServer);
  initFirebase();
}

// Fish Feed
void feedFish() {
  if (g_servo == "activate") {
    Serial.println("Feeding fish: opening feeder");

    // Move to open position
    feederServo.write(open_angle);
    delay(feed_duration);

    // Return to idle/closed
    feederServo.write(idle_angle);
    delay(200);

    Serial.println("Feeding complete. Resetting servo status to idle");

    // 1) Update Firebase so it won’t keep retriggering
    FirebaseJson upd;
    upd.set("servo", "idle");
    if (!Firebase.RTDB.updateNode(&fbdo, statusPath.c_str(), &upd)) {
      Serial.println("Error resetting servo status: " + fbdo.errorReason());
    }

    // 2) Update local global
    g_servo = "idle";
  }
}


void readWaterLevel() {
  // trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // measure echo (timeout 30 ms)
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float distanceCM = (duration * 0.034f) / 2.0f;
  float distanceIN = distanceCM / 2.54f;

  // compute water height
  float levelIN = tank_height_inch - distanceIN;
  levelIN = constrain(levelIN, 0.0f, tank_height_inch);

  g_waterLevelIN = levelIN;
  g_waterLevelPercent = (levelIN / tank_height_inch) * 100.0f;
}


void loop() {
  fetchActuatorsStatus();
  updateStatusToFirebase();
  addLogToFirebase();
  ControlActuators();
  feedFish();

  readWaterLevel();

  delay(1000); // Wait 5 seconds before next loop
}

// ========== Firebase Setup ==========

void initFirebase() {
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);
  config.token_status_callback = tokenStatusCallback;
  config.max_token_generation_retry = 5;

  Firebase.begin(&config, &auth);

  Serial.println("Getting User UID...");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }

  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);
}

// ========== Actuator State Fetching ==========

void fetchActuatorsStatus() {
  String ledPath = statusPath + "/led";
  String pumpPath = statusPath + "/pump";
  String servoPath = statusPath + "/servo";

  if (Firebase.RTDB.getBool(&fbdo, ledPath.c_str())) {
    g_led = fbdo.boolData();
  } else {
    Serial.println("Error reading LED: " + fbdo.errorReason());
    return;
  }

  if (Firebase.RTDB.getBool(&fbdo, pumpPath.c_str())) {
    g_pump = fbdo.boolData();
  } else {
    Serial.println("Error reading Pump: " + fbdo.errorReason());
    return;
  }

  if (Firebase.RTDB.getString(&fbdo, servoPath.c_str(), &g_servo)) {
    // Successfully read
  } else {
    Serial.println("Error reading Servo: " + fbdo.errorReason());
    return;
  }

  Serial.println("=== Actuator Status ===");
  Serial.printf("LED:   %s\n", g_led ? "ON" : "OFF");
  Serial.printf("Pump:  %s\n", g_pump ? "ON" : "OFF");
  Serial.printf("Servo: %s\n\n", g_servo.c_str());
}

// ========== Firebase Update (Only on Change) ==========

void updateStatusToFirebase() {
  if (g_led == last_led && g_pump == last_pump && g_servo == last_servo) {
    // No change in state, skip writing to Firebase
    return;
  }

  FirebaseJson status;
  status.set("led", g_led);
  status.set("pump", g_pump);
  status.set("servo", g_servo);
  status.set("pH", g_phValue);
  status.set("temperature", g_tempC);
  status.set("waterLevel", g_waterLevelPercent);

  if (Firebase.RTDB.setJSON(&fbdo, statusPath.c_str(), &status)) {
    Serial.println("Status updated to Firebase.");
    // Update local memory
    last_led = g_led;
    last_pump = g_pump;
    last_servo = g_servo;
  } else {
    Serial.println("Status update failed: " + fbdo.errorReason());
  }
}

// ========== Firebase Logging ==========

void addLogToFirebase() {
  FirebaseJson log;
  log.set("led", g_led);
  log.set("pump", g_pump);
  log.set("servo", g_servo);
  log.set("pH", g_phValue);
  log.set("temperature", g_tempC);
  log.set("waterLevel", g_waterLevelPercent);

  if (Firebase.RTDB.pushJSON(&fbdo, logsPath.c_str(), &log)) {
    Serial.println("Log saved to Firebase.");
  } else {
    Serial.println("Log save failed: " + fbdo.errorReason());
  }
}

// ========== Hardware Control ==========

void ControlActuators() {
  digitalWrite(LED_PIN, g_led ? HIGH : LOW);
  digitalWrite(WATER_PUMP_PIN, g_pump ? HIGH : LOW); //ternary operator
}

// ========== Time Utility ==========

bool isTime(int h, int m) {
  struct tm t;
  if (!getLocalTime(&t)) return false;
  return (t.tm_hour == h && t.tm_min == m);
}
