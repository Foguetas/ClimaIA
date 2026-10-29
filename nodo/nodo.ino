// Nodo del Observatorio de Ocotlán: ESP32 -> Wi-Fi -> MQTT cada 30 s.
// UNA sola plantilla para los cuatro laboratorios. Solo cambias el número de LAB.
//
// Librerías (Herramientas > Administrar bibliotecas):
//   Todos:  PubSubClient
//   LAB 1:  "ENS160 - Adafruit Fork"
//   LAB 2:  "Adafruit BME280 Library", "Adafruit Unified Sensor", "BH1750" (Christopher Laws)
//   LAB 3:  "BH1750" (Christopher Laws)
//   LAB 4:  ninguna adicional
// La ESP32 solo conecta a Wi-Fi de 2.4 GHz con clave normal (WPA2-PSK).

// ===== 1. ELIGE TU LABORATORIO =====
#define LAB 2   // 1 = Aire | 2 = Clima | 3 = Planta | 4 = Red (testigo)
// ===================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <time.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PASS, MQTT_USER, MQTT_PASS (no se sube a GitHub)

#if LAB == 1
  #define NODO "aire-01"
  #include "ScioSense_ENS160.h"
#elif LAB == 2
  #define NODO "clima-01"
  #include <Adafruit_BME280.h>
  #include <BH1750.h>
#elif LAB == 3
  #define NODO "planta-01"
  #include <BH1750.h>
#elif LAB == 4
  #define NODO "testigo-01"
#else
  #error "LAB debe ser 1, 2, 3 o 4"
#endif

#define FW "0.2"
const char* BROKER = "192.168.10.188";
const uint16_t PUERTO = 1883;
const uint32_t INTERVALO_MS = 30000;

WiFiClient red;
PubSubClient mqtt(red);
char topicLectura[48], topicEstado[48];
uint32_t ultimo = 0;

// ---------------------------------------------------------------
// SENSORES (cada laboratorio usa solo su bloque)
// ---------------------------------------------------------------
#if LAB == 1
// Laboratorio 1: ENS160 (0x52 o 0x53) + AHT21 (0x38), todo por I2C (SDA=21, SCL=22)
ScioSense_ENS160* ens160 = nullptr;

bool leerAHT(float &t, float &h) {
  Wire.beginTransmission(0x38);
  Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);   // disparar medición
  if (Wire.endTransmission() != 0) return false;
  delay(80);
  if (Wire.requestFrom((uint8_t)0x38, (uint8_t)7) != 7) return false;
  uint8_t d[7];
  for (int i = 0; i < 7; i++) d[i] = Wire.read();
  if (d[0] & 0x80) return false;                          // sigue ocupado
  uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t rt = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  h = rh * 100.0f / 1048576.0f;
  t = rt * 200.0f / 1048576.0f - 50.0f;
  return true;
}

void iniciarSensores() {
  Wire.begin();
  uint8_t dir = ENS160_I2CADDR_1;                          // 0x53
  Wire.beginTransmission(dir);
  if (Wire.endTransmission() != 0) dir = ENS160_I2CADDR_0; // 0x52
  ens160 = new ScioSense_ENS160(dir);
  ens160->begin();
  if (ens160->available()) ens160->setMode(ENS160_OPMODE_STD);
  else Serial.println("ENS160 no responde: revisa cableado y direccion I2C");
}

bool leerSensores(char* v, size_t n) {
  float t, h;
  if (!leerAHT(t, h) || !ens160 || !ens160->available()) return false;
  ens160->set_envdata(t, h);                               // compensa con temp y humedad
  ens160->measure(true);
  snprintf(v, n, "\"eco2_ppm\":%u,\"tvoc_ppb\":%u,\"aqi\":%u,\"temp_c\":%.1f,\"rh_pct\":%.1f",
           ens160->geteCO2(), ens160->getTVOC(), ens160->getAQI(), t, h);
  return true;
}

#elif LAB == 2
// Laboratorio 2: BME280 (0x76 o 0x77) + BH1750 (0x23), por I2C (SDA=21, SCL=22)
Adafruit_BME280 bme;
BH1750 luzSensor;

void iniciarSensores() {
  Wire.begin();
  if (!bme.begin(0x76) && !bme.begin(0x77)) Serial.println("BME280 no responde");
  luzSensor.begin();
}

bool leerSensores(char* v, size_t n) {
  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0F;                   // hPa
  float lx = luzSensor.readLightLevel();
  if (isnan(t) || isnan(h) || isnan(p) || lx < 0) return false;
  snprintf(v, n, "\"temp_c\":%.1f,\"rh_pct\":%.1f,\"pres_hpa\":%.1f,\"luz\":%.0f", t, h, p, lx);
  return true;
}

#elif LAB == 3
// Laboratorio 3: sensor de suelo analogico en GPIO34 (ADC1) + BH1750 por I2C
const int PIN_SUELO = 34;      // usa solo pines 32-39: el ADC2 choca con el Wi-Fi
const int SUELO_SECO = 3200;   // CALIBRA: lectura con el sensor al aire
const int SUELO_MOJADO = 1400; // CALIBRA: lectura con el sensor en agua (hasta la raya)
BH1750 luzSensor;

void iniciarSensores() {
  Wire.begin();
  luzSensor.begin();
  analogReadResolution(12);
}

bool leerSensores(char* v, size_t n) {
  int crudo = analogRead(PIN_SUELO);
  float pct = (SUELO_SECO - crudo) * 100.0f / (SUELO_SECO - SUELO_MOJADO);
  pct = constrain(pct, 0.0f, 100.0f);
  float lx = luzSensor.readLightLevel();
  Serial.printf("suelo crudo=%d\n", crudo);                // úsalo para calibrar
  if (lx < 0) return false;
  snprintf(v, n, "\"suelo_pct\":%.0f,\"luz\":%.0f", pct, lx);
  return true;
}

#else
// Laboratorio 4: ESP32 testigo, sin sensores. Solo publica su estado.
void iniciarSensores() {}
bool leerSensores(char* v, size_t n) { return false; }
#endif

// ---------------------------------------------------------------
// RED (igual para todos: no lo toques)
// ---------------------------------------------------------------
bool horaUTC(char* out, size_t n) {
  time_t t = time(nullptr);
  if (t < 1700000000) return false;  // el reloj aún no se sincroniza
  struct tm tm;
  gmtime_r(&t, &tm);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return true;
}

void conectarWiFi() {
  Serial.printf("Wi-Fi: conectando a %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() == WL_CONNECTED) Serial.println("Wi-Fi: " + WiFi.localIP().toString());
  else Serial.println("Wi-Fi: no conecto (clave? red de 2.4 GHz?)");
}

void conectarMQTT() {
  if (mqtt.connect(NODO, MQTT_USER, MQTT_PASS)) Serial.println("MQTT: conectado");
  else Serial.printf("MQTT: fallo, rc=%d (-2 sin ruta al broker, 4 o 5 usuario/clave)\n", mqtt.state());
}

void setup() {
  Serial.begin(115200);
  snprintf(topicLectura, sizeof topicLectura, "ito/obs/%s/lectura", NODO);
  snprintf(topicEstado, sizeof topicEstado, "ito/obs/%s/estado", NODO);
  iniciarSensores();
  mqtt.setServer(BROKER, PUERTO);
  mqtt.setBufferSize(512);
  conectarWiFi();
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) conectarMQTT();
  mqtt.loop();

  if (millis() - ultimo < INTERVALO_MS) return;
  ultimo = millis();

  char ts[24], v[160], msg[320];
  if (!horaUTC(ts, sizeof ts)) { Serial.println("Hora: esperando NTP"); return; }
  if (!mqtt.connected()) return;

  if (leerSensores(v, sizeof v)) {
    snprintf(msg, sizeof msg,
             "{\"ts\":\"%s\",\"nodo\":\"%s\",\"fw\":\"%s\",\"v\":{%s}}", ts, NODO, FW, v);
    mqtt.publish(topicLectura, msg);
    Serial.println(msg);
  }
  snprintf(msg, sizeof msg,
           "{\"ts\":\"%s\",\"nodo\":\"%s\",\"rssi\":%d,\"uptime_s\":%lu,\"heap_b\":%lu}",
           ts, NODO, WiFi.RSSI(), millis() / 1000UL, (unsigned long)ESP.getFreeHeap());
  mqtt.publish(topicEstado, msg);
  Serial.println(msg);
}