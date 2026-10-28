/**
 * Smart Patient Monitor - ESP32 Firmware
 * Compliant with CONTRACT.md
 * Tested for Wokwi Simulation & Physical ESP32 Hardware
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// -------------------------------------------------------------
// PIN ASSIGNMENTS
// -------------------------------------------------------------
#define PIN_POT_HR       34  // Heart Rate Potentiometer (ADC)
#define PIN_POT_SPO2     32  // SpO2 Potentiometer (ADC)
#define PIN_DHT          15  // DHT22 Temperature & Humidity Sensor
#define PIN_BTN_SOS       4  // Bedside Emergency SOS Button (Active LOW)
#define PIN_LED_ALARM     2  // Red Critical Alarm LED
#define PIN_LED_WIFI     19  // Green WiFi & Heartbeat LED
#define PIN_BUZZER        5  // Medical Audio Alarm Buzzer

#define DHTTYPE       DHT22

// OLED Display Configuration
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64
#define OLED_RESET       -1
#define SCREEN_ADDRESS 0x3C

// -------------------------------------------------------------
// NETWORK & MQTT CONFIGURATION
// -------------------------------------------------------------
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_BROKER   = "broker.emqx.io";
const int   MQTT_PORT     = 1883;
const char* DEVICE_ID     = "esp32-spm-bed01";
const char* PATIENT_ID    = "PT-9042";
const char* PATIENT_NAME  = "Eleanor Vance";

// MQTT Topics
const char* TOPIC_VITALS   = "hospital/icu/bed-01/vitals";
const char* TOPIC_ALERTS   = "hospital/icu/bed-01/alerts";
const char* TOPIC_STATUS   = "hospital/icu/bed-01/status";
const char* TOPIC_COMMANDS = "hospital/icu/bed-01/commands";
const char* TOPIC_ACK      = "hospital/icu/bed-01/commands/ack";

// -------------------------------------------------------------
// OBJECTS & RUNTIME VARIABLES
// -------------------------------------------------------------
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(PIN_DHT, DHTTYPE);

// Telemetry Timing
unsigned long lastTelemetryTime = 0;
unsigned long telemetryInterval = 2000; // ms

// Hardware Alert States
bool buzzerMuted = false;
bool sosActive = false;
bool prevSosButtonState = HIGH;
unsigned long lastSosDebounceTime = 0;

// Vitals Cache
int currentHeartRate = 72;
float currentSpO2 = 98.0;
float currentTempC = 36.8;
int currentSystolic = 118;
int currentDiastolic = 76;
String currentStatus = "NORMAL";
String previousStatus = "NORMAL";

// Alarm Tone Timing
unsigned long lastAlarmToneTime = 0;
bool alarmToneState = false;

// -------------------------------------------------------------
// FUNCTION DECLARATIONS
// -------------------------------------------------------------
void setupWiFi();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void readSensors();
void evaluateTriage();
void publishTelemetry();
void publishAlert(const char* alertType, const char* severity, const char* message, const char* metric, float value, float threshold);
void updateOLED();
void handleAlarms();

// -------------------------------------------------------------
// SETUP
// -------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=============================================");
  Serial.println("  SMART PATIENT MONITOR - ESP32 INITIALIZING ");
  Serial.println("=============================================");

  // Initialize GPIOs
  pinMode(PIN_BTN_SOS, INPUT_PULLUP);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_LED_WIFI, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_ALARM, LOW);
  digitalWrite(PIN_LED_WIFI, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // Initialize Sensors & Display
  dht.begin();

  Wire.begin(21, 22); // SDA=21, SCL=22
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 OLED allocation failed. Continuing without display.");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println("SMART PATIENT");
    display.setCursor(10, 34);
    display.println("MONITOR v1.0");
    display.setCursor(10, 48);
    display.println("Connecting WiFi...");
    display.display();
  }

  setupWiFi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);

  reconnectMQTT();
}

// -------------------------------------------------------------
// MAIN LOOP
// -------------------------------------------------------------
void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Read sensors continuously for responsive SOS & alerts
  readSensors();
  evaluateTriage();
  handleAlarms();

  // Periodic Telemetry Publishing
  unsigned long now = millis();
  if (now - lastTelemetryTime >= telemetryInterval) {
    lastTelemetryTime = now;
    publishTelemetry();
    updateOLED();
  }

  // Heartbeat LED (blinks briefly every 2s)
  if ((now % 2000) < 100) {
    digitalWrite(PIN_LED_WIFI, HIGH);
  } else {
    digitalWrite(PIN_LED_WIFI, LOW);
  }

  delay(20);
}

// -------------------------------------------------------------
// WIFI MANAGEMENT
// -------------------------------------------------------------
void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400);
    digitalWrite(PIN_LED_WIFI, !digitalRead(PIN_LED_WIFI));
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_LED_WIFI, HIGH);
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Failed! Will retry in loop.");
  }
}

// -------------------------------------------------------------
// MQTT CONNECTION & LWT
// -------------------------------------------------------------
void reconnectMQTT() {
  static unsigned long lastReconnectAttempt = 0;
  unsigned long now = millis();

  if (now - lastReconnectAttempt < 4000) {
    return;
  }
  lastReconnectAttempt = now;

  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
    return;
  }

  Serial.print("Connecting to MQTT Broker (");
  Serial.print(MQTT_BROKER);
  Serial.print(")... ");

  String clientId = String(DEVICE_ID) + "-" + String(random(0xffff), HEX);

  // LWT payload according to CONTRACT.md
  StaticJsonDocument<256> lwtDoc;
  lwtDoc["device_id"] = DEVICE_ID;
  lwtDoc["status"] = "OFFLINE";
  lwtDoc["reason"] = "HARDWARE_DISCONNECT_OR_CRASH";
  lwtDoc["timestamp"] = millis() / 1000;
  char lwtBuffer[256];
  serializeJson(lwtDoc, lwtBuffer);

  // Connect with LWT (qos=1, retain=true)
  if (mqttClient.connect(clientId.c_str(), nullptr, nullptr, TOPIC_STATUS, 1, true, lwtBuffer)) {
    Serial.println("CONNECTED!");

    // Publish Retained ONLINE Status
    StaticJsonDocument<300> statusDoc;
    statusDoc["device_id"] = DEVICE_ID;
    statusDoc["status"] = "ONLINE";
    statusDoc["ip_address"] = WiFi.localIP().toString();
    statusDoc["wifi_rssi"] = WiFi.RSSI();
    statusDoc["firmware_version"] = "1.0.4-wokwi";
    statusDoc["uptime_seconds"] = millis() / 1000;
    statusDoc["buzzer_muted"] = buzzerMuted;
    statusDoc["telemetry_interval_ms"] = telemetryInterval;

    char statusBuffer[300];
    serializeJson(statusDoc, statusBuffer);
    mqttClient.publish(TOPIC_STATUS, statusBuffer, true);

    // Subscribe to commands
    mqttClient.subscribe(TOPIC_COMMANDS, 1);
    Serial.print("Subscribed to command channel: ");
    Serial.println(TOPIC_COMMANDS);
  } else {
    Serial.print("Failed, rc=");
    Serial.println(mqttClient.state());
  }
}

// -------------------------------------------------------------
// MQTT COMMAND HANDLER
// -------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_COMMANDS) != 0) return;

  StaticJsonDocument<384> cmdDoc;
  DeserializationError error = deserializeJson(cmdDoc, payload, length);

  if (error) {
    Serial.print("Command JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  const char* action = cmdDoc["action"] | "";
  const char* cmdId = cmdDoc["command_id"] | "CMD-UNKNOWN";
  Serial.print("Received Remote Command: ");
  Serial.println(action);

  String ackStatus = "SUCCESS";
  String ackMsg = "";

  if (strcmp(action, "MUTE_BUZZER") == 0) {
    buzzerMuted = true;
    digitalWrite(PIN_BUZZER, LOW);
    ackMsg = "Physical alarm buzzer silenced";
  } else if (strcmp(action, "UNMUTE_BUZZER") == 0) {
    buzzerMuted = false;
    ackMsg = "Physical alarm buzzer enabled";
  } else if (strcmp(action, "TRIGGER_SOS_ACK") == 0) {
    sosActive = false;
    ackMsg = "Emergency SOS alarm acknowledged and reset";
  } else if (strcmp(action, "SET_INTERVAL") == 0) {
    unsigned long newInt = cmdDoc["params"]["interval_ms"] | 0;
    if (newInt >= 500 && newInt <= 10000) {
      telemetryInterval = newInt;
      ackMsg = String("Interval updated to ") + String(telemetryInterval) + "ms";
    } else {
      ackStatus = "ERROR";
      ackMsg = "Interval must be between 500 and 10000 ms";
    }
  } else if (strcmp(action, "TRIGGER_ALARM_TEST") == 0) {
    // 2-second diagnostic test
    digitalWrite(PIN_LED_ALARM, HIGH);
    tone(PIN_BUZZER, 2600, 1000);
    delay(1000);
    digitalWrite(PIN_LED_ALARM, LOW);
    ackMsg = "Hardware self-test finished successfully";
  } else {
    ackStatus = "UNKNOWN_ACTION";
    ackMsg = String("Action not implemented: ") + action;
  }

  // Publish ACK
  StaticJsonDocument<256> ackDoc;
  ackDoc["command_id"] = cmdId;
  ackDoc["action"] = action;
  ackDoc["status"] = ackStatus;
  ackDoc["message"] = ackMsg;
  ackDoc["timestamp"] = millis() / 1000;

  char ackBuffer[256];
  serializeJson(ackDoc, ackBuffer);
  mqttClient.publish(TOPIC_ACK, ackBuffer);
}

// -------------------------------------------------------------
// SENSOR ACQUISITION
// -------------------------------------------------------------
void readSensors() {
  // Read Potentiometers
  int rawHr = analogRead(PIN_POT_HR);      // 0 - 4095
  int rawSpo2 = analogRead(PIN_POT_SPO2);  // 0 - 4095

  // Map to realistic medical vitals
  // Heart Rate: 40 BPM to 180 BPM
  int targetHr = map(rawHr, 0, 4095, 40, 180);
  // Moving average smoothing for pulse
  currentHeartRate = (currentHeartRate * 3 + targetHr) / 4;

  // SpO2: 75% to 100%
  float targetSpo2 = 75.0 + ((float)rawSpo2 / 4095.0) * 25.0;
  currentSpO2 = (currentSpO2 * 2.0 + targetSpo2) / 3.0;

  // Read DHT22 Temperature
  float t = dht.readTemperature();
  if (!isnan(t) && t > 20.0 && t < 50.0) {
    currentTempC = t;
  }

  // Correlate BP roughly with heart rate
  currentSystolic = 110 + (currentHeartRate - 70) / 2;
  currentDiastolic = 70 + (currentHeartRate - 70) / 4;

  // SOS Pushbutton with debouncing (Active LOW)
  int reading = digitalRead(PIN_BTN_SOS);
  if (reading == LOW && prevSosButtonState == HIGH && (millis() - lastSosDebounceTime > 200)) {
    sosActive = true;
    lastSosDebounceTime = millis();
    Serial.println(">>> PHYSICAL SOS BUTTON PRESSED! <<<");
    publishAlert("SOS_PANIC_BUTTON", "CRITICAL", "Physical SOS button pressed at patient bedside!", "sos", 1.0, 0.0);
  }
  prevSosButtonState = reading;
}

// -------------------------------------------------------------
// TRIAGE & ALERT EVALUATION
// -------------------------------------------------------------
void evaluateTriage() {
  previousStatus = currentStatus;

  if (sosActive) {
    currentStatus = "CRITICAL";
    return;
  }

  if (currentHeartRate > 120 || currentHeartRate < 50 || currentSpO2 < 90.0 || currentTempC >= 38.5) {
    currentStatus = "CRITICAL";
  } else if (currentHeartRate > 100 || currentHeartRate < 60 || currentSpO2 < 95.0 || currentTempC >= 37.6) {
    currentStatus = "WARNING";
  } else {
    currentStatus = "NORMAL";
  }

  // Trigger alert messages on state transition to CRITICAL
  if (currentStatus == "CRITICAL" && previousStatus != "CRITICAL") {
    if (currentSpO2 < 90.0) {
      publishAlert("CRITICAL_HYPOXIA", "CRITICAL", "Arterial SpO2 critically low (< 90%)", "spo2", currentSpO2, 90.0);
    }
    if (currentHeartRate > 120) {
      publishAlert("CRITICAL_TACHYCARDIA", "CRITICAL", "Heart rate critically high (> 120 BPM)", "heart_rate", currentHeartRate, 120.0);
    }
    if (currentHeartRate < 50) {
      publishAlert("CRITICAL_BRADYCARDIA", "CRITICAL", "Heart rate critically low (< 50 BPM)", "heart_rate", currentHeartRate, 50.0);
    }
    if (currentTempC >= 38.5) {
      publishAlert("HIGH_FEVER", "CRITICAL", "Core body temperature dangerously elevated (>= 38.5 C)", "temperature_c", currentTempC, 38.5);
    }
  }
}

// -------------------------------------------------------------
// ACTUATORS: LEDS & BUZZER
// -------------------------------------------------------------
void handleAlarms() {
  if (currentStatus == "CRITICAL") {
    // Rapid flashing Red Alarm LED
    bool blink = (millis() / 250) % 2;
    digitalWrite(PIN_LED_ALARM, blink ? HIGH : LOW);

    // Audio Buzzer Beep Sequence (Unless muted)
    if (!buzzerMuted) {
      if (millis() - lastAlarmToneTime >= 300) {
        lastAlarmToneTime = millis();
        alarmToneState = !alarmToneState;
        if (alarmToneState) {
          tone(PIN_BUZZER, 2800, 200);
        }
      }
    } else {
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, LOW);
    }
  } else if (currentStatus == "WARNING") {
    // Slower blink, no buzzer
    bool blink = (millis() / 800) % 2;
    digitalWrite(PIN_LED_ALARM, blink ? HIGH : LOW);
    noTone(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
  } else {
    // Normal: Alarm LED off, buzzer silent
    digitalWrite(PIN_LED_ALARM, LOW);
    noTone(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// -------------------------------------------------------------
// OLED UI DISPLAY
// -------------------------------------------------------------
void updateOLED() {
  display.clearDisplay();

  // Invert display if critical SOS
  if (sosActive && ((millis() / 300) % 2)) {
    display.invertDisplay(true);
  } else {
    display.invertDisplay(false);
  }

  // Header Bar
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("BED-01 [");
  display.print(currentStatus);
  display.print("]");

  display.setCursor(105, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "WIFI" : "--");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Vitals Grid
  // Heart Rate
  display.setCursor(0, 16);
  display.print("HR:  ");
  display.setTextSize(2);
  display.print(currentHeartRate);
  display.setTextSize(1);
  display.print(" bpm");

  // SpO2
  display.setCursor(0, 36);
  display.print("SpO2:");
  display.setTextSize(2);
  display.print(currentSpO2, 1);
  display.setTextSize(1);
  display.print("%");

  // Temp & BP
  display.setCursor(0, 54);
  display.print("T:");
  display.print(currentTempC, 1);
  display.print("C BP:");
  display.print(currentSystolic);
  display.print("/");
  display.print(currentDiastolic);

  display.display();
}

// -------------------------------------------------------------
// TELEMETRY PUBLISH
// -------------------------------------------------------------
void publishTelemetry() {
  float tempF = (currentTempC * 9.0 / 5.0) + 32.0;

  StaticJsonDocument<450> doc;
  doc["device_id"] = DEVICE_ID;
  doc["patient_id"] = PATIENT_ID;
  doc["patient_name"] = PATIENT_NAME;
  doc["timestamp"] = millis() / 1000;

  JsonObject vitals = doc.createNestedObject("vitals");
  vitals["heart_rate"] = currentHeartRate;
  vitals["spo2"] = serialized(String(currentSpO2, 1));
  vitals["temperature_c"] = serialized(String(currentTempC, 1));
  vitals["temperature_f"] = serialized(String(tempF, 2));

  JsonObject bp = vitals.createNestedObject("blood_pressure");
  bp["systolic"] = currentSystolic;
  bp["diastolic"] = currentDiastolic;

  vitals["respiration_rate"] = 16;
  vitals["ecg_lead_connected"] = true;

  doc["status"] = currentStatus;

  char buffer[450];
  serializeJson(doc, buffer);

  mqttClient.publish(TOPIC_VITALS, buffer, false);
  Serial.print("Published Vitals: ");
  Serial.println(buffer);
}

// -------------------------------------------------------------
// ALERT PUBLISH
// -------------------------------------------------------------
void publishAlert(const char* alertType, const char* severity, const char* message, const char* metric, float value, float threshold) {
  StaticJsonDocument<384> doc;
  char alertId[32];
  snprintf(alertId, sizeof(alertId), "ALT-%lu", millis());

  doc["alert_id"] = alertId;
  doc["device_id"] = DEVICE_ID;
  doc["patient_id"] = PATIENT_ID;
  doc["timestamp"] = millis() / 1000;
  doc["alert_type"] = alertType;
  doc["severity"] = severity;
  doc["message"] = message;
  doc["metric"] = metric;
  doc["value"] = value;
  doc["threshold"] = threshold;
  doc["acknowledged"] = false;

  char buffer[384];
  serializeJson(doc, buffer);

  mqttClient.publish(TOPIC_ALERTS, buffer, true);
  Serial.print(">>> ALERT SENT: ");
  Serial.println(buffer);
}

