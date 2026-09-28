#include <Wire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <RTClib.h>
#include <PCA9536D.h>
#include <ModbusMaster.h>

// --- Hardware Pin Definitions ---
#define SDA_PIN 8
#define SCL_PIN 9
#define ETH_CS 1
#define SD_CS 42
#define MISO_PIN 13
#define MOSI_PIN 11
#define SCLK_PIN 12

// RS485 Pins
#define RS485_RXD 16
#define RS485_TXD 15
#define RS485_FC  41 // Flow Control (RE/DE)

// PCA9536 I/O Mapping (Onboard Buttons and LEDs)
#define IO_PB1  0
#define IO_LED1 1
#define IO_LED2 2
#define IO_PB3  3

// --- Objects ---
RTC_DS3231 rtc;
PCA9536 io;
ModbusMaster node;

// Ethernet MAC Address
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Timing and State Variables
unsigned long lastDisplayTime = 0;
bool led1State = false;
bool led2State = false;
bool lastPb1State = HIGH;
bool lastPb3State = HIGH;

// --- Modbus Flow Control Callbacks ---
void preTransmission() {
  digitalWrite(RS485_FC, HIGH); // Set to Transmit
}

void postTransmission() {
  digitalWrite(RS485_FC, LOW);  // Set to Receive
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize I2C and SPI
  Wire.begin(SDA_PIN, SCL_PIN);
  SPI.begin(SCLK_PIN, MISO_PIN, MOSI_PIN);

  // Initialize RS485 Flow Control Pin
  pinMode(RS485_FC, OUTPUT);
  digitalWrite(RS485_FC, LOW);

  // Initialize RS485 Serial (XY-MD02 defaults to 9600 baud)
  Serial2.begin(9600, SERIAL_8N1, RS485_RXD, RS485_TXD);

  // Initialize Modbus (XY-MD02 default address is 1)
  node.begin(1, Serial2);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // Initialize PCA9536 (Buttons & LEDs)
  if (!io.begin()) {
    Serial.println("PCA9536 not found!");
  } else {
    io.pinMode(IO_PB1, INPUT);
    io.pinMode(IO_PB3, INPUT);
    io.pinMode(IO_LED1, OUTPUT);
    io.pinMode(IO_LED2, OUTPUT);
    io.digitalWrite(IO_LED1, LOW);
    io.digitalWrite(IO_LED2, LOW);
  }

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
  } else if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize Ethernet (No blocking DHCP loop, just checking link)
  Ethernet.init(ETH_CS);
  Ethernet.begin(mac); 
}

void loop() {
  // --- 1. Button & LED Toggle Logic ---
  // Read current states (buttons are typically pulled HIGH, pressed is LOW)
  bool currentPb1 = io.digitalRead(IO_PB1);
  bool currentPb3 = io.digitalRead(IO_PB3);

  // Toggle LED 1 on PB1 press
  if (currentPb1 == LOW && lastPb1State == HIGH) {
    led1State = !led1State;
    io.digitalWrite(IO_LED1, led1State ? HIGH : LOW);
    delay(10); // Simple debounce
  }
  lastPb1State = currentPb1;

  // Toggle LED 2 on PB3 press
  if (currentPb3 == LOW && lastPb3State == HIGH) {
    led2State = !led2State;
    io.digitalWrite(IO_LED2, led2State ? HIGH : LOW);
    delay(10); // Simple debounce
  }
  lastPb3State = currentPb3;

  // --- 2. Periodic Sensor & Status Reporting (Every 2 seconds) ---
  if (millis() - lastDisplayTime >= 2000) {
    lastDisplayTime = millis();
    Serial.println("\n--- NORVI X System Status ---");

    // Print Time
    DateTime now = rtc.now();
    Serial.print("Time: ");
    Serial.print(now.year(), DEC); Serial.print('/');
    Serial.print(now.month(), DEC); Serial.print('/');
    Serial.print(now.day(), DEC); Serial.print(" ");
    Serial.print(now.hour(), DEC); Serial.print(':');
    Serial.print(now.minute(), DEC); Serial.print(':');
    Serial.println(now.second(), DEC);

    // Check SD Card
    if (SD.begin(SD_CS)) {
      Serial.println("SD Card: Inserted and Mounted");
    } else {
      Serial.println("SD Card: NOT Inserted / Failed to Mount");
    }

    // Check Ethernet Status
    if (Ethernet.linkStatus() == LinkON) {
      Serial.println("Ethernet: Connected");
      Serial.print("IP Address: ");
      Serial.println(Ethernet.localIP());
    } else {
      Serial.println("Ethernet: Disconnected");
    }

    // Read Modbus XY-MD02 (Temperature & Humidity)
    // XY-MD02 usually stores Temp at register 1 and Humidity at register 2 (Input Registers 0x04)
    uint8_t result = node.readInputRegisters(1, 2); 
    
    if (result == node.ku8MBSuccess) {
      float temperature = node.getResponseBuffer(0) / 10.0;
      float humidity = node.getResponseBuffer(1) / 10.0;
      
      Serial.print("XY-MD02 Temperature: ");
      Serial.print(temperature);
      Serial.println(" °C");
      
      Serial.print("XY-MD02 Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");
    } else {
      Serial.print("Modbus Error Reading XY-MD02: 0x");
      Serial.println(result, HEX);
    }
  }
}