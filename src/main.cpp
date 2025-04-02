#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <NimBLEDevice.h>
#include "config.h"

// ST7789 TFT Display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);

// BLE Variables
static NimBLEServer* pServer;
static NimBLECharacteristic* pCharacteristic;
static bool deviceConnected = false;
static bool displayNeedsUpdate = true;
static String connectedDeviceAddress = "";

// Data Buffer for Incoming BLE Data
#define MAX_BUFFER_SIZE 60000 // Enough for 132x132x3 bitmap + text
uint8_t dataBuffer[MAX_BUFFER_SIZE];
uint32_t dataIndex = 0;
bool receivingData = false;

// Parsed Data
uint8_t* bitmapData = nullptr;
uint32_t bitmapSize = 0;
String title = "N/A";
String eta = "N/A";
String distance = "N/A";

// Display Constants
const int STATUS_BAR_HEIGHT = 36;
const int SCREEN_WIDTH = 240;
const int SCREEN_HEIGHT = 320;
const int BITMAP_WIDTH = 132;
const int BITMAP_HEIGHT = 132;

// Function Prototypes
void processReceivedData(uint8_t* data, size_t length);
void parseData();
void drawBitmap(int16_t x, int16_t y, uint8_t* bitmap, int16_t w, int16_t h);
void updateDisplay();

// BLE Callback for Characteristic
class MyCharacteristicCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      processReceivedData((uint8_t*)value.data(), value.length());
    }
  }
};

// BLE Callback for Server
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) override {
    deviceConnected = true;
    displayNeedsUpdate = true;
    // Get the peer device address from the connected server
    // NimBLEInfo peerAddress = pServer->getPeerInfo(0);
    // connectedDeviceAddress = peerAddress.toString().c_str();
    // Serial.println("Device connected: " + connectedDeviceAddress);
  }

  void onDisconnect(NimBLEServer* pServer) override {
    deviceConnected = false;
    displayNeedsUpdate = true;
    connectedDeviceAddress = "";
    Serial.println("Device disconnected");
    NimBLEDevice::startAdvertising();
  }
};

void processReceivedData(uint8_t* data, size_t length) {
  Serial.println("Received data: " + String((char*)data, length));
  for (size_t i = 0; i < length; i++) {
    dataBuffer[dataIndex++] = data[i];

    // Check for start marker
    if (!receivingData && dataIndex >= 5) {
      if (strncmp((char*)dataBuffer, ">>>>>", 5) == 0) {
        receivingData = true;
        // Shift buffer to remove marker
        memmove(dataBuffer, dataBuffer + 5, dataIndex - 5);
        dataIndex -= 5;
      }
    }

    // Check for end marker
    if (receivingData && dataIndex >= 5) {
      if (strncmp((char*)(dataBuffer + dataIndex - 5), "<<<<<", 5) == 0) {
        receivingData = false;
        dataIndex -= 5; // Remove end marker
        parseData();
        dataIndex = 0; // Reset buffer
        displayNeedsUpdate = true;
      }
      else {
        Serial.println("Data not complete yet, waiting for end marker...");
      }
    }

    // Prevent buffer overflow
    if (dataIndex >= MAX_BUFFER_SIZE) {
      dataIndex = 0;
      receivingData = false;
      Serial.println("Buffer overflow, resetting");
    }
  }
}

void parseData() {
  // Find the separator between bitmap and text
  uint8_t* separator = (uint8_t*)memchr(dataBuffer, ';', dataIndex);
  if (!separator) {
    Serial.println("Invalid data: separator not found");
    return;
  }

  // Extract bitmap data
  bitmapSize = separator - dataBuffer;
  bitmapData = dataBuffer;

  // Extract text data
  uint32_t textStart = bitmapSize + 1;
  uint32_t textLength = dataIndex - textStart;
  String textData = String((char*)(dataBuffer + textStart), textLength);

  Serial.println("Bitmap size: " + String(bitmapSize) + ", Text data: " + textData);

  // Split text into title, eta, distance
  int firstPipe = textData.indexOf('|');
  int secondPipe = textData.indexOf('|', firstPipe + 1);

  if (firstPipe != -1 && secondPipe != -1) {
    title = textData.substring(0, firstPipe);
    eta = textData.substring(firstPipe + 1, secondPipe);
    distance = textData.substring(secondPipe + 1);
  } else {
    Serial.println("Invalid text data format");
    title = "N/A";
    eta = "N/A";
    distance = "N/A";
  }

  Serial.println("Parsed data - Title: " + title + ", ETA: " + eta + ", Distance: " + distance);
}

void drawBitmap(int16_t x, int16_t y, uint8_t* bitmap, int16_t w, int16_t h) {
  // Assuming bitmap is in RGB888 format (3 bytes per pixel)
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      int32_t idx = (j * w + i) * 3;
      if (idx + 2 >= bitmapSize) continue; // Safety check
      uint8_t r = bitmap[idx];
      uint8_t g = bitmap[idx + 1];
      uint8_t b = bitmap[idx + 2];
      // Convert RGB888 to RGB565
      uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      tft.drawPixel(x + i, y + j, color);
    }
  }
}

void updateDisplay() {
  tft.fillScreen(ST77XX_BLACK);

  // Draw status bar
  tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, deviceConnected ? ST77XX_GREEN : ST77XX_RED);
  tft.setCursor(5, 5);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.print(deviceConnected ? "Connected" : "Disconnected");

  // Draw navigation data
  int yOffset = STATUS_BAR_HEIGHT + 10;

  // Draw bitmap (centered)
  if (bitmapData && bitmapSize > 0) {
    int xOffset = (SCREEN_WIDTH - BITMAP_WIDTH) / 2;
    drawBitmap(xOffset, yOffset, bitmapData, BITMAP_WIDTH, BITMAP_HEIGHT);
    yOffset += BITMAP_HEIGHT + 10;
  }

  // Draw text
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, yOffset);
  tft.println("Next: " + title);
  yOffset += 20;
  tft.setCursor(5, yOffset);
  tft.println("ETA: " + eta);
  yOffset += 20;
  tft.setCursor(5, yOffset);
  tft.println("Dist: " + distance);
}

void setup() {
  Serial.begin(115200);

  // Initialize TFT
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  // Initialize BLE
  NimBLEDevice::init("ESP32_Navigation");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService* pService = pServer->createService(NimBLEUUID("18199909-f923-426c-9fdd-1e7a884d8aa2"));
  pCharacteristic = pService->createCharacteristic(
      NimBLEUUID("a37b8b6d-00e9-41db-ad37-9808464cba1b"),
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pCharacteristic->setCallbacks(new MyCharacteristicCallback());

  pService->start();
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(NimBLEUUID("18199909-f923-426c-9fdd-1e7a884d8aa2"));
  pAdvertising->start();

  Serial.println("BLE Server started");
  updateDisplay();
}

void loop() {
  if (displayNeedsUpdate) {
    updateDisplay();
    displayNeedsUpdate = false;
  }
  delay(100);
}