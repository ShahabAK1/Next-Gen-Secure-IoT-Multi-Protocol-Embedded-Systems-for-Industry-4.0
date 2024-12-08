#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>  // Add ArduinoJson library for easier JSON parsing
#include <Wire.h>  // Required for I2C communication
#include <Adafruit_GFX.h>  // Required for OLED display
#include <Adafruit_SSD1306.h>  // Required for SSD1306 OLED display

// Define UART pins for STM32 communication
#define TX_PIN 17  // TX (ESP32 → STM32 RX)
#define RX_PIN 16  // RX (STM32 TX → ESP32 RX)

// Define DS18B20 sensor pin
#define ONE_WIRE_BUS 2
#define SSD1306_I2C_ADDRESS 0x3C

// UART baud rate
#define BAUD_RATE 9600

// Wi-Fi credentials
const char* ssid = "Internet_2.4GHz@unifi";
const char* password = "1to5&6to10";

// Server URL
const String serverUrl = "http://192.168.0.101:5000/insertData";

// NTP client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// DS18B20 setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// OLED setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // No reset pin
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Variables
String stm32Response = "";
float ds18b20Temperature = 0.0;
unsigned long lastSendTime = 0;
unsigned long lastTempSendTime = 0;
const unsigned long sendInterval = 2000; // 2-second interval
const unsigned long tempSendInterval = 5000; // 5-second interval for STM32

// PIR Sensor pin
#define PIR_PIN 4 // Example pin for PIR sensor (change as needed)

void setup() {
  Serial.begin(115200);                  // Serial monitor
  Serial1.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);  // UART1 for STM32 communication

  sensors.begin();                       // Initialize DS18B20
  pinMode(PIR_PIN, INPUT);               // Initialize PIR sensor pin
  connectToWiFi();                       // Connect to Wi-Fi
  timeClient.begin();                    // Start NTP client
  timeClient.update();

  // OLED initialization
  if (!display.begin(SSD1306_I2C_ADDRESS, 0x3C)) {
    Serial.println(F("OLED initialization failed!"));
    for (;;); // Stay in infinite loop if OLED initialization fails
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Display title on OLED
  display.setCursor(0, 0);
  display.println("LOCAL DATA MONITORING");
  display.display(); // Show title on the OLED
}

void loop() {
  unsigned long currentMillis = millis();

  // Periodic DS18B20 sensor read and send to STM32
  if (currentMillis - lastTempSendTime >= tempSendInterval) {
    ds18b20Temperature = readTemperatureFromDS18B20();
    sendTemperatureToSTM32(ds18b20Temperature);
    lastTempSendTime = currentMillis;
  }

  // Process STM32 data
  while (Serial1.available()) {
    char incomingByte = Serial1.read();
    if (incomingByte == '\n') {  // End of STM32 response
      processSTM32Data(stm32Response);
      stm32Response = "";  // Clear buffer for next response
    } else if (incomingByte >= 32 && incomingByte <= 126) {  // Printable characters
      stm32Response += incomingByte;
    }
  }

  delay(100);  // Small delay to prevent tight loops
}

float readTemperatureFromDS18B20() {
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  if (temp == -127.0) {
    Serial.println("Error reading temperature from DS18B20.");
    return 0.0;  // Return 0.0 if the reading fails
  }
  Serial.println("DS18B20 Temperature: " + String(temp) + " °C");
  return temp;
}

void sendTemperatureToSTM32(float temperature) {
  String tempString = "DS18B20_Temperature:" + String(temperature, 2) + "C\n";
  Serial1.print(tempString);
  Serial.println("Sent to STM32: " + tempString);
}

void processSTM32Data(String response) {
  Serial.println("Received STM32 Data: " + response);

  // Parse STM32 data as JSON
  DynamicJsonDocument doc(1024);  // Allocate space for JSON parsing
  DeserializationError error = deserializeJson(doc, response);
  
  if (error) {
    Serial.println("Error parsing JSON: " + String(error.f_str()));
    return;
  }

  // Extract values from JSON
  String pirStatus = doc["PIR"].as<String>();
  float accelX = doc["Accel_X"].as<float>();
  float accelY = doc["Accel_Y"].as<float>();
  float accelZ = doc["Accel_Z"].as<float>();
  float internalTemp = doc["Temp"].as<float>();  // Internal temperature from STM32
  
  // Debugging the PIR status
  Serial.println("PIR Status: " + pirStatus);

  // Convert PIR status to 1 or 0 (motion or no motion)
  String pirStatusToSend = (pirStatus == "Motion Detected") ? "1" : "0";

  // Get formatted time
  timeClient.update();
  String timestamp = timeClient.getFormattedTime();
  if (timestamp == "00:00:00") { // Fallback if NTP time is unavailable
    timestamp = String(millis() / 1000) + " seconds";
  }

  // Construct JSON payload
  String jsonPayload = "{\"InternalTemperature\": " + String(internalTemp) +
                       ", \"EnvironmentalTemperature\": " + String(ds18b20Temperature) +
                       ", \"PIR_Status\": \"" + pirStatusToSend + "\"" +
                       ", \"Accel_X\": " + String(accelX) +
                       ", \"Accel_Y\": " + String(accelY) +
                       ", \"Accel_Z\": " + String(accelZ) +
                       ", \"timestamp\": \"" + timestamp + "\"}";

  // Send to server
  sendDataToServer(jsonPayload);

  // Display on OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LOCAL DATA MONITORING");

  display.setCursor(0, 20);
  display.print("Temp: ");
  display.print(ds18b20Temperature);
  display.println(" C");

  // Display STM32 internal temperature
  display.setCursor(0, 30);
  display.print("STM32 Temp: ");
  display.print(internalTemp);
  display.println(" C");

  display.setCursor(0, 40);
  display.print("PIR: ");
  display.println(pirStatus);

  display.setCursor(0, 50);
  display.print("Accel: ");
  display.print(accelX); display.print(", ");
  display.print(accelY); display.print(", ");
  display.println(accelZ);

  display.display();  // Update OLED display
}

void sendDataToServer(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi not connected. Skipping data transmission.");
    return;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    Serial.println("Data sent successfully. HTTP Response Code: " + String(httpResponseCode));
  } else {
    Serial.println("Error sending data. HTTP Error: " + String(httpResponseCode));
  }

  http.end();
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");
}
