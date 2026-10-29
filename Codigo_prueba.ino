#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <BH1750.h>
// Definición de pines I2C para ESP32
#define SDA_PIN 21
#define SCL_PIN 22
// Instancias de los sensores
Adafruit_BME280 bme;
BH1750 lightMeter;
void setup() {
  // Inicialización de la comunicación serie
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // Esperar a que abra el monitor serie
  }
  Serial.println("\n--- Iniciando prueba de sensores ---");
  // Inicialización del bus I2C en los pines especificados
  Wire.begin(SDA_PIN, SCL_PIN);
  // Inicializar el BME280 (Prueba con dirección 0x76 o 0x77)
  bool statusBME = bme.begin(0x76, &Wire);
  if (!statusBME) {
    // Si falla en 0x76, intentar en 0x77
    statusBME = bme.begin(0x77, &Wire);
  }
  if (!statusBME) {
    Serial.println("❌ No se encontró el sensor BME280. Verifica la dirección I2C/conexiones.");
  } else {
    Serial.println("✅ Sensor BME280 detectado correctamente.");
  }
  // Inicializar el BH1750
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire)) {
    Serial.println("✅ Sensor BH1750 detectado correctamente.");
  } else {
    Serial.println("❌ No se encontró el sensor BH1750.");
  }
  Serial.println("------------------------------------\n");
}
void loop() {
  Serial.println("=== LECTURA DE SENSORES ===");
  // --- Lectura del BH1750 ---
  float lux = lightMeter.readLightLevel();
  if (lux < 0) {
    Serial.println("Luz (BH1750): Error de lectura");
  } else {
    Serial.print("Luminosidad: ");
    Serial.print(lux);
    Serial.println(" lx");
  }
  // --- Lectura del BME280 ---
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float press = bme.readPressure() / 100.0F; // Convertir Pa a hPa
  if (isnan(temp) || isnan(hum) || isnan(press)) {
    Serial.println("BME280: Error de lectura");
  } else {
    Serial.print("Temperatura: ");
    Serial.print(temp);
    Serial.println(" °C");
    Serial.print("Humedad:     ");
    Serial.print(hum);
    Serial.println(" %");
    Serial.print("Presión:     ");
    Serial.print(press);
    Serial.println(" hPa");
  }
  Serial.println("---------------------------\n");
  delay(2000); // Esperar 2 segundos entre lecturas
}