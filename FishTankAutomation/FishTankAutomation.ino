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
bool   g_led             = false;
bool   g_pump            = false;
String g_servo = "idle";  // "idle", "activate", or "auto"

const char* ntpServer = "pool.ntp.org";


// ─── Pin & Calibration Constants ───────────────────────────────────────────
constexpr int PH_PIN = A0;            // Analog pin for pH sensor
constexpr float PH_OFFSET = 32.878f;  // Calibration offset (b in pH = m·V + b)
constexpr float PH_SLOPE = -5.70f;    // Calibration slope (m in pH = m·V + b)

// Ultrasonic (water level)
constexpr int TRIG_PIN = 18;
constexpr int ECHO_PIN = 19;
constexpr float TANK_HEIGHT_IN = 12.0f;  // total tank height in inches

// ADC & sampling
constexpr float VREF = 5.0f;
constexpr int ADC_MAX = 1023;
constexpr int NUM_SAMPLES = 10;

// Temperature (DS18B20)
#define ONE_WIRE_BUS 4

// ─── Global Variable ───────────────────────────────────────────────────────
float g_phValue = 0.0f;  // holds the latest pH reading
float g_waterLevelIN = 0.0f;
float g_waterLevelPercent = 0.0f;
float g_tempC = 0.0f;


// ─── OneWire & DallasTemperature Setup ─────────────────────────────────────
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);



#define SERVO_PIN        18
constexpr int OPEN_ANGLE    = 90;      // adjust to your feeder’s “open” position
constexpr int IDLE_ANGLE    =  0;      // “closed” position
constexpr int FEED_DURATION = 3000;    // milliseconds to hold open

// Flags to avoid double-feeding
bool fedMorning = false;
bool fedEvening = false;

// Servo object
Servo feederServo;


// ─── At the top with your other pin defs ────────────────────────────────────
constexpr int LED_PIN = 2;   // change to whichever GPIO you wired your LED to




void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  sensors.begin();


  // Servo setup — 50 Hz, with pulse widths 500–2400 μs
  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(IDLE_ANGLE);

    // LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize Wifi Server
  initWiFi();
  configTime(6 * 3600, 0, ntpServer);
  initFirebase();
}


// ─── Trimmed-Mean Filter ───────────────────────────────────────────────────
float readTrimmedMean(int pin) {
  int raw;
  long sum = 0;
  int minVal = ADC_MAX, maxVal = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    raw = analogRead(pin);
    sum += raw;
    if (raw < minVal) minVal = raw;
    if (raw > maxVal) maxVal = raw;
    delay(30);
  }
  // discard one min + one max sample
  sum -= (minVal + maxVal);
  return float(sum) / float(NUM_SAMPLES - 2);
}

// Read and compute pH
float readPH() {
  float avgRaw = readTrimmedMean(PH_PIN);
  float voltage = avgRaw * VREF / float(ADC_MAX);
  // linear calibration: pH = m·V + b  (here m = –5.70)
  return -5.70f * voltage + PH_OFFSET;
}

// ─── Read water level (in inches & %) ──────────────────────────────────────
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
  float levelIN = TANK_HEIGHT_IN - distanceIN;
  levelIN = constrain(levelIN, 0.0f, TANK_HEIGHT_IN);

  g_waterLevelIN = levelIN;
  g_waterLevelPercent = (levelIN / TANK_HEIGHT_IN) * 100.0f;
}


// ─── Read temperature (DS18B20) ─────────────────────────────────────────────
void readTemperature() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp == DEVICE_DISCONNECTED_C) {
    // handle error if needed
    g_tempC = NAN;
  } else {
    g_tempC = temp;
  }
}


// ─── Read all sensors and store in globals ──────────────────────────────────
void readAllSensors() {
  g_phValue = readPH();
  readWaterLevel();
  readTemperature();
}

void showSensorsValue() {
  Serial.print("pH: ");
  Serial.println(g_phValue, 2);

  Serial.print("Water Level: ");
  Serial.print(g_waterLevelIN, 2);
  Serial.print(" in (");
  Serial.print(g_waterLevelPercent, 1);
  Serial.println("%)");

  if (isnan(g_tempC)) {
    Serial.println("Temperature: Error reading sensor");
  } else {
    Serial.print("Temperature: ");
    Serial.print(g_tempC, 2);
    Serial.println(" °C");
  }

  Serial.println();
}



void loop() {
  readAllSensors();
  showSensorsValue();
  fetchActuatorsStatus();


  updateStatusToFirebase(); // overwrite the status node
  addLogToFirebase();       // push a new log entry


  // If commanded, feed the fish
  feedFish();
  feedFishAuto();
  controlLED();


  // String dateTime = getDateTimeString();
  // String date = dateTime.substring(0, 10);
  // String dateString = getDateString(dateTime);

  // // === Update status ===
  // FirebaseJson status;
  // // status.set("led", false);     // Default value
  // // status.set("pump", false);    // Default value
  // // status.set("servo", "idle");  // Default value
  // status.set("pH", g_phValue);
  // status.set("temperature", g_tempC);
  // status.set("waterLevel", g_waterLevelPercent);

  // if (Firebase.RTDB.setJSON(&fbdo, statusPath.c_str(), &status)) {
  //   Serial.println("Status updated.");
  // } else {
  //   Serial.println(fbdo.errorReason());
  // }

  // // === Store logs ===
  // FirebaseJson log;
  // log.set("led", false);
  // log.set("pump", false);
  // log.set("servo", "idle");
  // log.set("pH", g_phValue);
  // log.set("temperature", g_tempC);
  // log.set("waterLevel", g_waterLevelPercent);


  // if (Firebase.RTDB.pushJSON(&fbdo, logsPath.c_str(), &log)) {
  //   Serial.println("Log saved.");
  // } else {
  //   Serial.println(fbdo.errorReason());
  // }
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
  String ledPath   = statusPath + "/led";
  String pumpPath  = statusPath + "/pump";
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
  Serial.printf("LED:   %s\n", g_led   ? "ON"  : "OFF");
  Serial.printf("Pump:  %s\n", g_pump  ? "ON"  : "OFF");
  Serial.printf("Servo: %s\n", g_servo.c_str());
  Serial.println();
}


void updateStatusToFirebase() {
  FirebaseJson status;
  // Uncomment or use fetched actuator globals if you want to write them:
  // status.set("led", g_led);
  // status.set("pump", g_pump);
  // status.set("servo", g_servo);

  status.set("pH",          g_phValue);
  status.set("temperature", g_tempC);
  status.set("waterLevel",  g_waterLevelPercent);

  if (Firebase.RTDB.setJSON(&fbdo, statusPath.c_str(), &status)) {
    Serial.println("Status updated.");
  } else {
    Serial.println("Status update failed: " + fbdo.errorReason());
  }
}


void addLogToFirebase() {
  FirebaseJson log;
  log.set("led",          g_led);
  log.set("pump",         g_pump);
  log.set("servo",        g_servo);
  log.set("pH",           g_phValue);
  log.set("temperature",  g_tempC);
  log.set("waterLevel",   g_waterLevelPercent);

  if (Firebase.RTDB.pushJSON(&fbdo, logsPath.c_str(), &log)) {
    Serial.println("Log saved.");
  } else {
    Serial.println("Log save failed: " + fbdo.errorReason());
  }
}


// ————— feedFish(): perform a one-time feeding cycle —————————————————
void feedFish() {
  if (g_servo == "activate") {
    Serial.println("Feeding fish: opening feeder");

    // Move to open position
    feederServo.write(OPEN_ANGLE);
    delay(FEED_DURATION);

    // Return to idle/closed
    feederServo.write(IDLE_ANGLE);
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


// ─── New helper: directly reflect g_led to the hardware LED ───────────────
void controlLED() {
  // HIGH when g_led==true, LOW otherwise
  digitalWrite(LED_PIN, g_led ? HIGH : LOW);
}


void feedFishAuto() {
  if (g_servo != "auto") return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int H = timeinfo.tm_hour;
  int M = timeinfo.tm_min;

  // Reset flags at midnight
  if (H == 0 && M == 0) {
    fedMorning = fedEvening = false;
  }

  // Morning 08:00
  if (isTime(8, 0) && !fedMorning) {
    Serial.println("Auto-feed: Morning");
    feederServo.write(OPEN_ANGLE);
    delay(FEED_DURATION);
    feederServo.write(IDLE_ANGLE);
    fedMorning = true;
  }

  // Evening 18:00
  if (isTime(18, 0) && !fedEvening) {
    Serial.println("Auto-feed: Evening");
    feederServo.write(OPEN_ANGLE);
    delay(FEED_DURATION);
    feederServo.write(IDLE_ANGLE);
    fedEvening = true;
  }
}

// ————————————— Helper: check exact hh:mm —————————————————————————
bool isTime(int h, int m) {
  struct tm t;
  if (!getLocalTime(&t)) return false;
  return (t.tm_hour == h && t.tm_min == m);
}

