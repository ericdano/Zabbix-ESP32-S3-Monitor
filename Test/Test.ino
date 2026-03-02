void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for Mac to connect
  Serial.println("ESP32-S3 Native USB Serial Active");
}

void loop() {
  Serial.print("Up time: ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
  delay(1000);
}