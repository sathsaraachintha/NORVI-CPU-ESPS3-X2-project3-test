#include <Wire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <RTClib.h>
#include <PCA9536D.h>
#include <ModbusMaster.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// --- LovyanGFX Display Configuration for NORVI X ---
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      // SPI Pins from Datasheet
      cfg.pin_sclk = 12; 
      cfg.pin_mosi = 11; 
      cfg.pin_miso = 13; 
      cfg.pin_dc   = 46; 
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 45; 
      cfg.pin_rst          = 47; 
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = true; // ST7789 usually requires inverted colors
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true; // MUST be true as Ethernet and SD share this SPI bus
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft; // Instantiate the display object

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

  // Initialize I2C and Standard SPI
  Wire.begin(SDA_PIN, SCL_PIN);
  SPI.begin(SCLK_PIN, MISO_PIN, MOSI_PIN);

  // Initialize TFT Display
  tft.init();
  tft.setRotation(1); // Landscape mode
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK); // Text color, background color (prevents text overlapping)
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Booting System...");

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
    tft.println("PCA9536 Error");
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
    tft.println("RTC Error");
  } else if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Initialize Ethernet
  Ethernet.init(ETH_CS);
  Ethernet.begin(mac); 
  
  delay(1000);
  tft.fillScreen(TFT_BLACK); // Clear boot screen
}

void loop() {
  // --- 1. Button & LED Toggle Logic ---
  bool currentPb1 = io.digitalRead(IO_PB1);
  bool currentPb3 = io.digitalRead(IO_PB3);

  if (currentPb1 == LOW && lastPb1State == HIGH) {
    led1State = !led1State;
    io.digitalWrite(IO_LED1, led1State ? HIGH : LOW);
    delay(50); 
  }
  lastPb1State = currentPb1;

  if (currentPb3 == LOW && lastPb3State == HIGH) {
    led2State = !led2State;
    io.digitalWrite(IO_LED2, led2State ? HIGH : LOW);
    delay(50); 
  }
  lastPb3State = currentPb3;

  // --- 2. Periodic Sensor & Status Reporting (Every 2 seconds) ---
  if (millis() - lastDisplayTime >= 2000) {
    lastDisplayTime = millis();
    
    // Reset Cursor to Top-Left for continuous overwrite
    tft.setCursor(0, 5); 
    
    // --- Header ---
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("  NORVI X System Status  ");
    tft.println("-------------------------");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    // --- Time ---
    DateTime now = rtc.now();
    tft.print(" Time: ");
    tft.printf("%04d/%02d/%02d %02d:%02d:%02d  \n", 
               now.year(), now.month(), now.day(), 
               now.hour(), now.minute(), now.second());

    // --- SD Card ---
    tft.print(" SD Card: ");
    if (SD.begin(SD_CS)) {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.println("Mounted        ");
    } else {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Not Found      ");
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    // --- Ethernet ---
    tft.print(" Ethernet: ");
    if (Ethernet.linkStatus() == LinkON) {
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.println("Connected      ");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.print(" IP: ");
      tft.print(Ethernet.localIP());
      tft.println("       "); // Padding to clear old characters
    } else {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Disconnected   ");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println(" IP: 0.0.0.0            ");
    }

    // --- Modbus XY-MD02 ---
    tft.println("-------------------------");
    uint8_t result = node.readInputRegisters(1, 2); 
    
    if (result == node.ku8MBSuccess) {
      float temperature = node.getResponseBuffer(0) / 100.0;
      float humidity = node.getResponseBuffer(1) / 100.0;
      
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.print(" Temp: ");
      tft.print(temperature, 1);
      tft.println(" C       ");
      
      tft.print(" Hum : ");
      tft.print(humidity, 1);
      tft.println(" %       ");
    } else {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.print(" Modbus Error: 0x");
      tft.print(result, HEX);
      tft.println("     ");
      tft.println("                         "); // Padding
    }
  }
}