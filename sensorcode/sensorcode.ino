#include <SPI.h>
#include <Ethernet.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_task_wdt.h>
#include <Adafruit_NeoPixel.h>
#include "driver/temp_sensor.h"

// --- Hardware Pins ---
#define S3_SCLK 12
#define S3_MISO 13
#define S3_MOSI 11
#define S3_CS   10
#define ONE_WIRE_BUS 4
#define UPS_PIN 5 
#define RGB_PIN 48 // Default for most S3 DevKits

// --- Network Configuration ---
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 50);      // Static IP for your Sensor
IPAddress dns(192, 168, 1, 1);     // Your Router/DNS
IPAddress gateway(192, 168, 1, 1); 
IPAddress subnet(255, 255, 255, 0);
IPAddress zabbixServer(192, 168, 1, 10);
const int zabbixPort = 10051;
const char* hostName = "ahs-100-idf-sensor";

// --- Objects ---
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
EthernetClient client;
Adafruit_NeoPixel statusLED(1, RGB_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastSend = 0;

void setLED(uint32_t color) {
  statusLED.setPixelColor(0, color);
  statusLED.show();
}

void setup() {
  Serial.begin(115200);
  statusLED.begin();
  setLED(statusLED.Color(50, 50, 0)); // Yellow: Booting

  // 1. Hardware Watchdog (10s)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 10000,
    .idle_core_mask = (1 << 0),
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  // 2. Internal Temp Sensor
  temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
  temp_sensor_set_config(temp_sensor);
  temp_sensor_start();

  // 3. Ethernet Setup
  SPI.begin(S3_SCLK, S3_MISO, S3_MOSI, S3_CS);
  Ethernet.init(S3_CS);
  
  // Use Static IP
  Ethernet.begin(mac, ip, dns, gateway, subnet);
  
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    while (true) { setLED(statusLED.Color(255, 0, 0)); delay(500); setLED(0); delay(500); } // Blinking Red: HW Fail
  }

  sensors.begin();
  pinMode(UPS_PIN, INPUT_PULLUP);
  setLED(statusLED.Color(0, 50, 0)); // Green: Ready
}

void sendToZabbix(String key, String value) {
  if (client.connect(zabbixServer, zabbixPort)) {
    String data = "{\"request\":\"sender data\",\"data\":[{\"host\":\"" + String(hostName) + "\",\"key\":\"" + key + "\",\"value\":\"" + value + "\"}]}";
    uint64_t len = data.length();
    client.print("ZBXD\1");
    client.write((uint8_t*)&len, 8);
    client.print(data);
    client.stop();
  }
}

void loop() {
  esp_task_wdt_reset();

  // UPS Status Check (Instant Visual Feedback)
  int upsStatus = digitalRead(UPS_PIN);
  if (upsStatus == 0) {
    setLED(statusLED.Color(255, 0, 0)); // Red if power is out
  } else {
    setLED(statusLED.Color(0, 20, 0)); // Dim Green if OK
  }

  if (millis() - lastSend > 30000) {
    sensors.requestTemperatures();
    float roomTemp = sensors.getTempCByIndex(0);
    float chipTemp = 0;
    temp_sensor_read_攝氏(&chipTemp);

    sendToZabbix("room.temp", String(roomTemp));
    sendToZabbix("ups.status", String(upsStatus));
    sendToZabbix("chip.health", String(chipTemp));

    lastSend = millis();
    Serial.println("Zabbix Update Sent.");
  }
}