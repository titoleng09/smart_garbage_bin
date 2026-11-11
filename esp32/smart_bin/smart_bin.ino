/*
  Smart Garbage Bin - ESP32 + HC-SR04 + 3 LEDs + MQTT
  ----------------------------------------------------
  Library needed (Arduino IDE > Library Manager):
    - PubSubClient by Nick O'Leary
  Board: "ESP32 Dev Module" (install "esp32 by Espressif Systems" in Boards Manager)

  Publishes JSON every PUBLISH_INTERVAL_MS to topic:  smartbin/<BIN_ID>/data
    {"distance":23.4,"fill":45,"status":"LOW"}
*/

#include <WiFi.h>
#include <PubSubClient.h>

// ======== EDIT THESE ========
const char* WIFI_SSID     = "YOUR_WIFI_NAME";     // must be 2.4 GHz
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_HOST     = "192.168.1.100";      // LAN IP of the PC running Docker
const uint16_t MQTT_PORT  = 1883;
const char* BIN_ID        = "bin1";

// Calibrate with your real bin:
const float EMPTY_DISTANCE_CM = 40.0;  // sensor -> bottom of EMPTY bin
const float FULL_DISTANCE_CM  = 5.0;   // sensor -> rubbish when "100% full" (HC-SR04 blind zone ~2-3 cm)

// Status thresholds (percent)
const int YELLOW_FROM = 50;   // 50-79%  -> yellow
const int RED_FROM    = 80;   // 80-100% -> red

const unsigned long PUBLISH_INTERVAL_MS = 3000;
// ============================

// Pins
const int PIN_TRIG   = 5;
const int PIN_ECHO   = 18;   // via 1k/2k voltage divider!
const int PIN_GREEN  = 25;
const int PIN_YELLOW = 26;
const int PIN_RED    = 27;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

char topic[64];
unsigned long lastPublish = 0;
unsigned long lastWifiTry = 0;
unsigned long lastMqttTry = 0;

// ---------- Sensor ----------
float readDistanceOnce() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duration = pulseIn(PIN_ECHO, HIGH, 30000UL);  // 30 ms timeout (~5 m)
  if (duration == 0) return -1.0;                             // no echo
  return duration * 0.0343f / 2.0f;                           // cm
}

// Median of 5 readings filters out random spikes
float readDistanceCm() {
  float r[5];
  int n = 0;
  for (int i = 0; i < 5; i++) {
    float d = readDistanceOnce();
    if (d > 0 && d < 400) r[n++] = d;
    delay(60);  // let echoes die out
  }
  if (n == 0) return -1.0;
  for (int i = 0; i < n - 1; i++)          // simple sort
    for (int j = i + 1; j < n; j++)
      if (r[j] < r[i]) { float t = r[i]; r[i] = r[j]; r[j] = t; }
  return r[n / 2];
}

int calcFillPercent(float distance) {
  float pct = (EMPTY_DISTANCE_CM - distance) / (EMPTY_DISTANCE_CM - FULL_DISTANCE_CM) * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (int)(pct + 0.5f);
}

const char* statusFor(int fill) {
  if (fill >= RED_FROM)    return "FULL";
  if (fill >= YELLOW_FROM) return "HALF";
  return "LOW";
}

// ---------- LEDs ----------
void setLeds(int fill) {
  digitalWrite(PIN_GREEN,  fill <  YELLOW_FROM);
  digitalWrite(PIN_YELLOW, fill >= YELLOW_FROM && fill < RED_FROM);
  digitalWrite(PIN_RED,    fill >= RED_FROM);
}

void showSensorError() {           // all LEDs off, red blinks
  digitalWrite(PIN_GREEN, LOW);
  digitalWrite(PIN_YELLOW, LOW);
  digitalWrite(PIN_RED, (millis() / 300) % 2);
}

// ---------- Network (non-blocking, LEDs keep working offline) ----------
void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiTry < 10000) return;
  lastWifiTry = millis();
  Serial.println("WiFi: connecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void maintainMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (millis() - lastMqttTry < 5000) return;
  lastMqttTry = millis();
  String clientId = String("smartbin-") + BIN_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("MQTT: connecting to "); Serial.print(MQTT_HOST); Serial.print(" ... ");
  if (mqtt.connect(clientId.c_str())) Serial.println("OK");
  else { Serial.print("failed, rc="); Serial.println(mqtt.state()); }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  pinMode(PIN_RED, OUTPUT);

  // LED self-test
  int pins[] = {PIN_GREEN, PIN_YELLOW, PIN_RED};
  for (int p : pins) { digitalWrite(p, HIGH); delay(250); digitalWrite(p, LOW); }

  snprintf(topic, sizeof(topic), "smartbin/%s/data", BIN_ID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiTry = millis();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
}

bool sensorOk = true;

void loop() {
  maintainWifi();
  maintainMqtt();
  mqtt.loop();

  if (!sensorOk) showSensorError();

  if (millis() - lastPublish < PUBLISH_INTERVAL_MS) return;
  lastPublish = millis();

  float distance = readDistanceCm();
  if (distance < 0) {
    sensorOk = false;
    Serial.println("Sensor: no echo (check wiring / divider)");
    return;
  }
  sensorOk = true;

  int fill = calcFillPercent(distance);
  const char* status = statusFor(fill);
  setLeds(fill);

  char payload[96];
  snprintf(payload, sizeof(payload),
           "{\"distance\":%.1f,\"fill\":%d,\"status\":\"%s\"}", distance, fill, status);
  Serial.println(payload);

  if (mqtt.connected()) mqtt.publish(topic, payload);
}
