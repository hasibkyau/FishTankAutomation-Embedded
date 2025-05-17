#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "utils.h"
#include "json.h"
#include <ESP32Servo.h>

// Temperature
#include <OneWire.h>
#include <DallasTemperature.h>


// firebase
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


// Variables to store Firebase data
String uid;
String basePath = "/FISH-TANK";
String statusPath = basePath + "/status";
String logsPath = basePath + "/logs";

// ——— Global “status” variables ——————————————————————————————————
bool g_led = false;
bool g_pump = false;
String g_servo = "idle";  // "idle", "activate", or "auto"

const char* ntpServer = "pool.ntp.org";

// Temperature (DS18B20)
#define ONE_WIRE_BUS 4

// ─── Global Variable ───────────────────────────────────────────────────────
float g_phValue = 0.0f;  // holds the latest pH reading
float g_waterLevelIN = 0.0f;
float g_waterLevelPercent = 0.0f;
float g_tempC = 0.0f;


// LED PIN
constexpr int LED_PIN = 2;  



void setup() {
  Serial.begin(115200);

  // LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize Wifi Server
  initWiFi();
  configTime(6 * 3600, 0, ntpServer);
  initFirebase();
}




void loop() {
  fetchActuatorsStatus();
  updateStatusToFirebase();  
  addLogToFirebase();       
  controlLED();

  delay(5000);
}



// Function to initialize Firebase
void initFirebase() {
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the RTDB URL
  config.database_url = DATABASE_URL;
  Firebase.reconnectWiFi(true);

  // Set response size
  fbdo.setResponseSize(4096);
  config.token_status_callback = tokenStatusCallback;

  // Assign max retry for token generation
  config.max_token_generation_retry = 5;

  // Start Firebase
  Firebase.begin(&config, &auth);

  // Wait until UID is retrieved
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);
}


// ————— Fetch into globals & print —————————————————————————————————
void fetchActuatorsStatus() {
  // Build C-strings for each child path
  String ledPath = statusPath + "/led";
  String pumpPath = statusPath + "/pump";
  String servoPath = statusPath + "/servo";

  // 1) LED
  // overload: bool getBool(FirebaseData*, const char*)
  if (Firebase.RTDB.getBool(&fbdo, ledPath.c_str())) {
    g_led = fbdo.boolData();
  } else {
    Serial.println("Error reading LED: " + fbdo.errorReason());
    return;
  }

  // 2) Pump
  if (Firebase.RTDB.getBool(&fbdo, pumpPath.c_str())) {
    g_pump = fbdo.boolData();
  } else {
    Serial.println("Error reading Pump: " + fbdo.errorReason());
    return;
  }

  // 3) Servo
  // we can use the 3-argument overload that writes directly into a String
  if (Firebase.RTDB.getString(&fbdo, servoPath.c_str(), &g_servo)) {
    /* g_servo is now populated */
  } else {
    Serial.println("Error reading Servo: " + fbdo.errorReason());
    return;
  }

  // Print the globals
  Serial.println("=== Actuator Status ===");
  Serial.printf("LED:   %s\n", g_led ? "ON" : "OFF");
  Serial.printf("Pump:  %s\n", g_pump ? "ON" : "OFF");
  Serial.printf("Servo: %s\n", g_servo.c_str());
  Serial.println();
}


void updateStatusToFirebase() {
  FirebaseJson status;
  // Uncomment or use fetched actuator globals if you want to write them:
  status.set("led", g_led);
  status.set("pump", g_pump);
  status.set("servo", g_servo);
  status.set("pH", g_phValue);
  status.set("temperature", g_tempC);
  status.set("waterLevel", g_waterLevelPercent);

  if (Firebase.RTDB.setJSON(&fbdo, statusPath.c_str(), &status)) {
    Serial.println("Status updated.");
  } else {
    Serial.println("Status update failed: " + fbdo.errorReason());
  }
}


void addLogToFirebase() {
  FirebaseJson log;
  log.set("led", g_led);
  log.set("pump", g_pump);
  log.set("servo", g_servo);
  log.set("pH", g_phValue);
  log.set("temperature", g_tempC);
  log.set("waterLevel", g_waterLevelPercent);

  if (Firebase.RTDB.pushJSON(&fbdo, logsPath.c_str(), &log)) {
    Serial.println("Log saved.");
  } else {
    Serial.println("Log save failed: " + fbdo.errorReason());
  }
}



void controlLED() {
  // HIGH when g_led==true, LOW otherwise
  digitalWrite(LED_PIN, g_led ? HIGH : LOW);
}


bool isTime(int h, int m) {
  struct tm t;
  if (!getLocalTime(&t)) return false;
  return (t.tm_hour == h && t.tm_min == m);
}
