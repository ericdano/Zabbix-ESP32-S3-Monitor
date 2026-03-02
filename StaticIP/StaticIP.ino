#include <SPI.h>
#include <Ethernet.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <esp_task_wdt.h>
#include <Adafruit_NeoPixel.h>
#include "driver/temp_sensor.h"

// --- Hardware Pins for ESP32-S3 ---
#define S3_SCLK 12
#define S3_MISO 13
#define S3_MOSI 11
#define S3_CS   10
#define ONE_WIRE_BUS 4
#define UPS_PIN 5      // Optocoupler input
#define RGB_PIN 48     // Built-in S3 RGB LED

// --- Network Configuration (Adjust to your network) ---
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 50);      
IPAddress dns(192, 168, 1, 1);     
IPAddress gateway(192, 168, 1, 1); 
IPAddress subnet(255, 255, 255, 0);

// --- Zabbix Configuration ---
IPAddress zabbixServer(192, 168, 1, 10);
const int zabbixPort = 10051;
const char* hostName = "ESP32-S3-Sensor";

// --- Global Objects ---
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
EthernetClient client;
Adafruit_NeoPixel statusLED(1, RGB_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastSend = 0;
const int sendInterval = 30000; // 30 seconds

// Helper function for LED colors
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(0, statusLED.Color(r, g, b));
  statusLED.show();
}

// Function to send data to Zabbix using Trapper Protocol
void sendToZabbix(String key, String value) {
  if (client.connect(zabbixServer, zabbixPort)) {
    String data = "{\"request\":\"sender data\",\"data\":[{\"host\":\"" + String(hostName) + "\",\"key\":\"" + key + "\",\"value\":\"" + value + "\"}]}";
    
    // Zabbix Header: 'ZBXD' + version(1) + data length(8 bytes)
    uint64_t len = data.length();
    client.print("ZBXD\1");
    client.write((uint8_t*)&len, 8);
    client.print(data);
    
    client.stop();
    Serial.println("Zabbix Sync: " + key + " = " + value);
  } else {
    Serial.println("Zabbix Connection Failed!");
  }
}

void setup() {
  Serial.begin(115200);
  
  // 1. Initialize RGB LED
  statusLED.begin();
  setLED(50, 50, 0); // Yellow: Booting

  // 2. Initialize Hardware Watchdog (Core 3.x Fix)
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = 10000,
      .idle_core_mask = (1 << 0), 
      .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  // 3. Initialize S3 Internal Temp Sensor (Chip Health)
  temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
  temp_sensor_set_config(temp_sensor);
  temp_sensor_start();

  // 4. Setup SPI and Ethernet
  SPI.begin(S3_SCLK, S3_MISO, S3_MOSI, S3_CS);
  Ethernet.init(S3_CS);
  
  Serial.println("Starting Ethernet (Static IP)...");
  Ethernet.begin(mac, ip, dns, gateway, subnet);

  // Check Ethernet hardware
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("W5500 not found!");
    while (true) { setLED(100, 0, 0); delay(200); setLED(0,0,0); delay(200); } // Flash Red
  }

  sensors.begin();
  pinMode(UPS_PIN, INPUT_PULLUP);
  
  Serial.println("System Ready!");
  setLED(0, 50, 0); // Green: Ready
}

void loop() {
  // Feed the Watchdog
  esp_task_wdt_reset();

  // Instant Visual Feedback for UPS
  int upsStatus = digitalRead(UPS_PIN);
  if (upsStatus == 0) {
    setLED(100, 0, 0); // Bright Red: Power Out
  } else {
    // Pulse Dim Green to show it's alive
    int pulse = (millis() / 1000) % 2 == 0 ? 5 : 15;
    setLED(0, pulse, 0); 
  }

// Periodic Zabbix Update
  if (millis() - lastSend > sendInterval) {
    sensors.requestTemperatures();
    float roomTemp = sensors.getTempCByIndex(0);
    
    float chipTemp = 0;
    // FIXED: Changed from the weird translated text to the proper C++ function
    temp_sensor_read_celsius(&chipTemp); 

    // Push data
    sendToZabbix("room.temp", String(roomTemp));
    sendToZabbix("ups.status", String(upsStatus));
    sendToZabbix("chip.health", String(chipTemp));

    lastSend = millis();
  }
}