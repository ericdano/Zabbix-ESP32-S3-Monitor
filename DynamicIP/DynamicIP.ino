#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>
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
#define UPS_PIN 5      
#define RGB_PIN 48     

// --- Network & Zabbix Configuration ---
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xAA }; // Changed last byte to avoid conflicts
IPAddress zabbixServer(192, 168, 1, 10); 
const int zabbixPort = 10051;
const char* hostName = "ESP32-S3-Sensor";

// --- Global Objects ---
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
EthernetClient client;
Adafruit_NeoPixel statusLED(1, RGB_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastSend = 0;
const int sendInterval = 30000; 

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(0, statusLED.Color(r, g, b));
  statusLED.show();
}

void sendToZabbix(String key, String value) {
  if (client.connect(zabbixServer, zabbixPort)) {
    String data = "{\"request\":\"sender data\",\"data\":[{\"host\":\"" + String(hostName) + "\",\"key\":\"" + key + "\",\"value\":\"" + value + "\"}]}";
    uint64_t len = data.length();
    client.print("ZBXD\1");
    client.write((uint8_t*)&len, 8);
    client.print(data);
    client.stop();
    Serial.println(">> Zabbix Sent: " + key + " = " + value);
  } else {
    Serial.println("!! Zabbix Connection Failed");
  }
}

void setup() {
  Serial.begin(115200);
  
  // 1. Kill Radios Immediately (Reduces heat/noise/power)
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  // 2. Initialize RGB LED
  statusLED.begin();
  setLED(50, 20, 0); // Dim Orange: Booting

  // 3. Hardware Watchdog (15s)
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = 15000,
      .idle_core_mask = (1 << 0), 
      .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);

  // 4. Internal Temp Sensor
  temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
  temp_sensor_set_config(temp_sensor);
  temp_sensor_start();

  // 5. Hardware Initialization Delay
  // Giving the W5500 and the Switch/Router time to see each other
  Serial.println("Waiting for hardware stabilization...");
  for(int i=0; i<5; i++) {
    delay(1000);
    setLED(50, 20, 0); delay(100); setLED(0,0,0); // Blink Orange
    esp_task_wdt_reset();
  }

  // 6. Setup SPI and Ethernet
  SPI.begin(S3_SCLK, S3_MISO, S3_MOSI, S3_CS);
  Ethernet.init(S3_CS);
  
  Serial.println("Starting DHCP Request...");
  setLED(0, 0, 50); // Blue: DHCP Request

  int dhcp_retries = 0;
  while (Ethernet.begin(mac) == 0 && dhcp_retries < 3) {
    Serial.print("DHCP Failed, retry ");
    Serial.println(dhcp_retries + 1);
    delay(3000);
    dhcp_retries++;
    esp_task_wdt_reset();
  }

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("CRITICAL: W5500 not detected. Check SPI wiring!");
    while (true) { setLED(100, 0, 0); delay(100); setLED(0,0,0); delay(100); esp_task_wdt_reset(); }
  }

  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("CRITICAL: Ethernet cable not detected!");
  }

  Serial.print("SUCCESS! IP: ");
  Serial.println(Ethernet.localIP());
  
  sensors.begin();
  pinMode(UPS_PIN, INPUT_PULLUP);
  setLED(0, 50, 0); // Green: System Online
}

void loop() {
  esp_task_wdt_reset();
  Ethernet.maintain(); // Keep the DHCP lease active

  // UPS Status Logic
  int upsStatus = digitalRead(UPS_PIN);
  if (upsStatus == 0) {
    setLED(150, 0, 0); // Solid Red: Power Out
  } else {
    // Faint Green Heartbeat every 2 seconds
    int pulse = (millis() / 1000) % 2 == 0 ? 2 : 8;
    setLED(0, pulse, 0); 
  }

  // Send to Zabbix
  if (millis() - lastSend > sendInterval) {
    sensors.requestTemperatures();
    float roomTemp = sensors.getTempCByIndex(0);
    float chipTemp = 0;
    temp_sensor_read_celsius(&chipTemp);

    sendToZabbix("room.temp", String(roomTemp));
    sendToZabbix("ups.status", String(upsStatus));
    sendToZabbix("chip.health", String(chipTemp));
    Serial.println(Ethernet.localIP());
    lastSend = millis();
  }
}