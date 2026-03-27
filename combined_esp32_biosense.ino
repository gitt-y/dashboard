#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/*
  =========================================================
  BioSense combined sketch for 2 ESP32 boards + Firebase
  =========================================================

  Upload this SAME file to both boards.

  1. Set DEVICE_ROLE to DEVICE_ROLE_A
     Upload to the ESP32 with:
     - MH-Z19E
     - HS-S26P temperature
     - HS-S26P humidity

  2. Set DEVICE_ROLE to DEVICE_ROLE_B
     Upload to the ESP32 with:
     - DS18B20
     - PE-03 pH

  Both boards connect to your phone hotspot and PATCH their data
  directly into Firebase Realtime Database.

  Dashboard reads from:
  https://biosense-32860-default-rtdb.asia-southeast1.firebasedatabase.app/readings.json
*/

#define DEVICE_ROLE_A 1
#define DEVICE_ROLE_B 2

// Change this before each upload.
#define DEVICE_ROLE DEVICE_ROLE_A

// =========================
// Wi-Fi and Firebase
// =========================
const char* WIFI_SSID = "CMF by Nothing Phone 1";
const char* WIFI_PASSWORD = "qwertyuiop";
const char* FIREBASE_READINGS_URL = "https://biosense-32860-default-rtdb.asia-southeast1.firebasedatabase.app/readings.json";

// =========================
// Role A pin mapping
// =========================
const int MHZ19_RX_PIN = 25;       // ESP32 RX <- MH-Z19E TX
const int MHZ19_TX_PIN = 26;       // ESP32 TX -> MH-Z19E RX
const int HS_S26P_TEMP_PIN = 34;   // analog in
const int HS_S26P_HUM_PIN = 33;    // analog in
const int LDR_PIN = 32;            // analog in

// =========================
// Role B pin mapping
// =========================
const int ONE_WIRE_BUS = 4;        // DS18B20 data
const int PH_PIN = 35;             // PE-03 analog output

// =========================
// Timing
// =========================
const unsigned long ROLE_A_SEND_INTERVAL_MS = 4000;
const unsigned long ROLE_B_SEND_INTERVAL_MS = 4000;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;

// =========================
// Analog calibration
// =========================
const float ADC_REF_VOLTAGE = 3.3f;
const int ADC_MAX = 4095;

// HS-S26P output tuning.
// Adjust these if your transmitter range is different.
const float HS_OUTPUT_DIVIDER_RATIO = 1.0f;
const float HS_TEMP_OUTPUT_MIN_V = 0.0f;
const float HS_TEMP_OUTPUT_MAX_V = 5.0f;
const float HS_TEMP_RANGE_MIN_C = 0.0f;
const float HS_TEMP_RANGE_MAX_C = 50.0f;
const float HS_HUM_OUTPUT_MIN_V = 0.0f;
const float HS_HUM_OUTPUT_MAX_V = 5.0f;
const float HS_HUM_RANGE_MIN = 0.0f;
const float HS_HUM_RANGE_MAX = 1.0f;       // sensor returns 0.00 to 1.00
const float HS_HUMIDITY_SCALE = 100.0f;    // convert 0.41 to 41%
const float LDR_DARK_PERCENT = 0.0f;
const float LDR_BRIGHT_PERCENT = 100.0f;

// PE-03 pH tuning.
const float PH_NEUTRAL_VOLTAGE = 2.50f;
const float PH_SLOPE = -5.70f;
const float PH_DISPLAY_DIVISOR = 2.0f;     // keeps dashboard and serial aligned

#if DEVICE_ROLE == DEVICE_ROLE_A
HardwareSerial mhzSerial(2);
unsigned long lastRoleASendMs = 0;
#endif

#if DEVICE_ROLE == DEVICE_ROLE_B
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
unsigned long lastRoleBSendMs = 0;
#endif

unsigned long lastWifiAttemptMs = 0;

float mapFloat(float value, float inMin, float inMax, float outMin, float outMax) {
  if (value < inMin) {
    value = inMin;
  }
  if (value > inMax) {
    value = inMax;
  }
  return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

float readAverageVoltage(int pin) {
  long total = 0;
  const int samples = 16;

  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(4);
  }

  float adc = total / (float)samples;
  return (adc / ADC_MAX) * ADC_REF_VOLTAGE;
}

void connectToWifi(const char* roleLabel) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(1000);

  Serial.print(roleLabel);
  Serial.print(" connecting to Wi-Fi ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(roleLabel);
    Serial.println(" connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.print(roleLabel);
    Serial.print(" Wi-Fi failed, status=");
    Serial.println(WiFi.status());
  }
}

void ensureWifiConnected(const char* roleLabel) {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (millis() - lastWifiAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiAttemptMs = millis();
    connectToWifi(roleLabel);
  }
}

bool patchFirebase(JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skipping Firebase upload, Wi-Fi disconnected");
    return false;
  }

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(FIREBASE_READINGS_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  int responseCode = http.sendRequest("PATCH", payload);
  Serial.print("Firebase PATCH response: ");
  Serial.println(responseCode);

  if (responseCode > 0) {
    Serial.println(http.getString());
  } else {
    Serial.println("Firebase PATCH failed");
  }

  http.end();
  return responseCode > 0;
}

#if DEVICE_ROLE == DEVICE_ROLE_A
bool readMHZ19Raw(int &ppm) {
  uint8_t command[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};
  uint8_t response[9];

  while (mhzSerial.available()) {
    mhzSerial.read();
  }

  mhzSerial.write(command, 9);
  mhzSerial.flush();
  delay(120);

  if (mhzSerial.available() < 9) {
    return false;
  }

  for (int i = 0; i < 9; i++) {
    response[i] = mhzSerial.read();
  }

  if (response[0] != 0xFF || response[1] != 0x86) {
    return false;
  }

  ppm = (response[2] << 8) | response[3];
  return true;
}

float readHSS26PTemperatureC() {
  float measuredVoltage = readAverageVoltage(HS_S26P_TEMP_PIN);
  float sensorVoltage = measuredVoltage * HS_OUTPUT_DIVIDER_RATIO;

  return mapFloat(
    sensorVoltage,
    HS_TEMP_OUTPUT_MIN_V,
    HS_TEMP_OUTPUT_MAX_V,
    HS_TEMP_RANGE_MIN_C,
    HS_TEMP_RANGE_MAX_C
  );
}

float readHSS26PHumidityRH() {
  float measuredVoltage = readAverageVoltage(HS_S26P_HUM_PIN);
  float sensorVoltage = measuredVoltage * HS_OUTPUT_DIVIDER_RATIO;

  float humidityFraction = mapFloat(
    sensorVoltage,
    HS_HUM_OUTPUT_MIN_V,
    HS_HUM_OUTPUT_MAX_V,
    HS_HUM_RANGE_MIN,
    HS_HUM_RANGE_MAX
  );

  float humidityPercent = humidityFraction * HS_HUMIDITY_SCALE;
  if (humidityPercent < 0.0f) {
    humidityPercent = 0.0f;
  }
  if (humidityPercent > 100.0f) {
    humidityPercent = 100.0f;
  }
  return humidityPercent;
}

float readLightPercent() {
  float measuredVoltage = readAverageVoltage(LDR_PIN);

  return mapFloat(
    measuredVoltage,
    0.0f,
    ADC_REF_VOLTAGE,
    LDR_DARK_PERCENT,
    LDR_BRIGHT_PERCENT
  );
}

void setupNodeA() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  mhzSerial.begin(9600, SERIAL_8N1, MHZ19_RX_PIN, MHZ19_TX_PIN);
  connectToWifi("ROLE_A");
}

void loopNodeA() {
  ensureWifiConnected("ROLE_A");

  if (millis() - lastRoleASendMs < ROLE_A_SEND_INTERVAL_MS) {
    return;
  }
  lastRoleASendMs = millis();

  int ppm = 0;
  bool co2Ok = readMHZ19Raw(ppm);
  float ambientTemp = readHSS26PTemperatureC();
  float humidity = readHSS26PHumidityRH();
  float light = readLightPercent();

  Serial.println();
  Serial.println("===== ROLE_A READINGS =====");
  if (co2Ok) {
    Serial.print("CO2 ppm: ");
    Serial.println(ppm);
  } else {
    Serial.println("CO2 ppm: read failed");
  }
  Serial.print("Ambient temp C: ");
  Serial.println(ambientTemp, 2);
  Serial.print("Humidity %RH: ");
  Serial.println(humidity, 2);
  Serial.print("Light %: ");
  Serial.println(light, 2);
  Serial.println("===========================");

  StaticJsonDocument<256> doc;
  if (co2Ok) {
    doc["co2"] = ppm;
  }
  doc["ambientTemp"] = ambientTemp;
  doc["humidity"] = humidity;
  doc["light"] = light;
  doc["nodeAOnline"] = true;
  doc["roleAUpdatedAtMs"] = millis();

  patchFirebase(doc);
}
#endif

#if DEVICE_ROLE == DEVICE_ROLE_B
float readProbeTemperatureC() {
  ds18b20.requestTemperatures();
  return ds18b20.getTempCByIndex(0);
}

float readPh() {
  long total = 0;
  const int samples = 20;

  for (int i = 0; i < samples; i++) {
    total += analogRead(PH_PIN);
    delay(10);
  }

  float adc = total / (float)samples;
  float voltage = (adc / ADC_MAX) * ADC_REF_VOLTAGE;
  float rawPh = 7.0f + ((voltage - PH_NEUTRAL_VOLTAGE) * PH_SLOPE);
  float correctedPh = rawPh / PH_DISPLAY_DIVISOR;

  if (correctedPh < 0.0f) {
    correctedPh = 0.0f;
  }
  if (correctedPh > 14.0f) {
    correctedPh = 14.0f;
  }
  return correctedPh;
}

void setupNodeB() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  ds18b20.begin();
  connectToWifi("ROLE_B");
}

void loopNodeB() {
  ensureWifiConnected("ROLE_B");

  if (millis() - lastRoleBSendMs < ROLE_B_SEND_INTERVAL_MS) {
    return;
  }
  lastRoleBSendMs = millis();

  float probeTemp = readProbeTemperatureC();
  float phValue = readPh();

  bool probeValid = probeTemp != DEVICE_DISCONNECTED_C && probeTemp > -100.0f && probeTemp < 125.0f;

  Serial.println();
  Serial.println("===== ROLE_B READINGS =====");
  if (probeValid) {
    Serial.print("Probe temp C: ");
    Serial.println(probeTemp, 2);
  } else {
    Serial.println("Probe temp C: sensor disconnected");
  }
  Serial.print("pH: ");
  Serial.println(phValue, 2);
  Serial.println("===========================");

  StaticJsonDocument<192> doc;
  if (probeValid) {
    doc["probeTemp"] = probeTemp;
  }
  doc["ph"] = phValue;
  doc["nodeBOnline"] = true;
  doc["roleBUpdatedAtMs"] = millis();

  patchFirebase(doc);
}
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("BioSense Firebase uploader booting...");

#if DEVICE_ROLE == DEVICE_ROLE_A
  Serial.println("Configured as DEVICE_ROLE_A");
  setupNodeA();
#elif DEVICE_ROLE == DEVICE_ROLE_B
  Serial.println("Configured as DEVICE_ROLE_B");
  setupNodeB();
#else
  Serial.println("Invalid DEVICE_ROLE selected");
#endif
}

void loop() {
#if DEVICE_ROLE == DEVICE_ROLE_A
  loopNodeA();
#elif DEVICE_ROLE == DEVICE_ROLE_B
  loopNodeB();
#endif
}
