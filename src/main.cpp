#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_GFX.h"        // Joey Castillo's fork
#include "Adafruit_ST7789.h"
#include "NimBLEDevice.h"
#include "config.h"
#include "roboto.h"
#include <U8g2_for_Adafruit_GFX.h>
#include "esp_crc.h"  // ESP32 Hardware CRC
#include "disconnected_icon_9.h" // Include the bitmap header file
#include "u8g2_fonts.h" // Include the U8g2 font header file

// U8g2 for Adafruit_GFX instance
U8G2_FOR_ADAFRUIT_GFX u8g2;

#define LINE_SPACING_OFFSET  5
// ST7789 TFT Display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

extern const uint8_t u8g2_font_unifont_t_vietnamese2[15330] U8G2_FONT_SECTION("u8g2_font_unifont_t_vietnamese2");
extern const uint8_t u8g2_font_inr33_mf[11616] U8G2_FONT_SECTION("u8g2_font_inr33_mf");

// BLE Variables (unchanged)
static NimBLEServer* pServer;
static NimBLECharacteristic* pCharacteristic;
static bool deviceConnected = false;
static bool lastConnectionStt = false;
static bool displayNeedsUpdate = true;
static String connectedDeviceAddress = "";

// Data Buffer (unchanged)
#define MAX_BUFFER_SIZE 60000
uint8_t dataBuffer[MAX_BUFFER_SIZE];
uint32_t dataIndex = 0;
bool receivingData = false;

// Parsed Data (unchanged)
uint8_t* bitmapData = nullptr;
uint32_t bitmapSize = 0;
String title = "N/A";
String eta = "N/A";
String distance = "N/A";

// Display Constants (unchanged)
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
// Helper function to render UTF-8 strings with U8g2 fonts
void drawUnicodeString(Adafruit_ST7789 &tft, U8G2_FOR_ADAFRUIT_GFX &u8g2, 
  int16_t x, int16_t y, const char *text, 
  uint16_t color, const uint8_t *font);

// BLE Callbacks (unchanged)
class MyCharacteristicCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      processReceivedData((uint8_t*)value.data(), value.length());
    }
  }
};

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) override {
    deviceConnected = true;
    displayNeedsUpdate = true;
  }

  void onDisconnect(NimBLEServer* pServer) override {
    deviceConnected = false;
    displayNeedsUpdate = true;
    connectedDeviceAddress = "";
    Serial.println("Device disconnected");
    NimBLEDevice::startAdvertising();
  }
};

// Process Received Data (unchanged)
void processReceivedData(uint8_t* data, size_t length) {
  // Serial.println("Received data: " + String((char*)data, length));
  for (size_t i = 0; i < length; i++) {
    dataBuffer[dataIndex++] = data[i];
    if (!receivingData && dataIndex >= 5) {
      if (strncmp((char*)dataBuffer, ">>>>>", 5) == 0) {
        receivingData = true;
        memmove(dataBuffer, dataBuffer + 5, dataIndex - 5);
        dataIndex -= 5;
      }
    }
    if (receivingData && dataIndex >= 5) {
      if (strncmp((char*)(dataBuffer + dataIndex - 5), "<<<<<", 5) == 0) {
        receivingData = false;
        dataIndex -= 5;
        parseData();
        dataIndex = 0;
        displayNeedsUpdate = true;
        // Serial.println("Received 1 frame");
      }
    }
    if (dataIndex >= MAX_BUFFER_SIZE) {
      dataIndex = 0;
      receivingData = false;
      Serial.println("Buffer overflow, resetting");
    }
  }
}

// Parse Data (unchanged)
void parseData() {
  uint8_t* separator = (uint8_t*)memchr(dataBuffer, ';', dataIndex);
  if (!separator) {
    Serial.println("Invalid data: separator not found");
    return;
  }
  bitmapSize = separator - dataBuffer;
  bitmapData = dataBuffer;
  uint32_t textStart = bitmapSize + 1;
  uint32_t textLength = dataIndex - textStart;
  String textData = String((char*)(dataBuffer + textStart), textLength);
  int firstPipe = textData.indexOf('|');
  int secondPipe = textData.indexOf('|', firstPipe + 1);
  if (firstPipe != -1 && secondPipe != -1) {
    title = textData.substring(0, firstPipe);
    eta = textData.substring(firstPipe + 1, secondPipe);
    distance = textData.substring(secondPipe + 1);
  } else {
    title = "N/A";
    eta = "N/A";
    distance = "N/A";
  }
}

void drawBitmap(int16_t x, int16_t y, uint8_t* bitmap, int16_t w, int16_t h) {
  // Define buffer for 132x132 bitmap (assuming 1-bit per pixel)
  const int16_t DISPLAY_WIDTH = 132;
  const int16_t DISPLAY_HEIGHT = 132;
  const int16_t BUFFER_SIZE = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8; // 2178 bytes
  uint8_t renderBuffer[BUFFER_SIZE] = {0}; // Initialize buffer to all black
  
  // Validate input dimensions
  if (w > DISPLAY_WIDTH || h > DISPLAY_HEIGHT) {
      return; // Input bitmap too large
  }

  // Step 1: Parse bitmap into render buffer
  for (int16_t j = 0; j < h; j++) {
      for (int16_t i = 0; i < w; i++) {
          int32_t pixelIndex = j * w + i;
          int32_t byteIndex = pixelIndex / 8;
          int32_t bitIndex = 7 - (pixelIndex % 8); // MSB to LSB

          if (byteIndex >= bitmapSize) continue;

          uint8_t byteValue = bitmap[byteIndex];
          bool pixelValue = (byteValue >> bitIndex) & 0x01;

          // Calculate position in render buffer
          int32_t bufferPixelIndex = j * DISPLAY_WIDTH + i;
          int32_t bufferByteIndex = bufferPixelIndex / 8;
          int32_t bufferBitIndex = 7 - (bufferPixelIndex % 8);

          if (pixelValue) {
              renderBuffer[bufferByteIndex] |= (1 << bufferBitIndex);
          }
      }
  }

  // Step 2: Draw buffer to display using horizontal lines
  for (int16_t j = 0; j < DISPLAY_HEIGHT; j++) {
      int16_t startX = x;
      int16_t lineStart = 0;
      bool lastPixel = false;

      for (int16_t i = 0; i < DISPLAY_WIDTH; i++) {
          int32_t pixelIndex = j * DISPLAY_WIDTH + i;
          int32_t byteIndex = pixelIndex / 8;
          int32_t bitIndex = 7 - (pixelIndex % 8);
          
          bool currentPixel = (renderBuffer[byteIndex] >> bitIndex) & 0x01;

          // If pixel changes or we're at the end, draw the previous segment
          if (currentPixel != lastPixel || i == DISPLAY_WIDTH - 1) {
              if (i == DISPLAY_WIDTH - 1 && currentPixel == lastPixel) {
                  // Include the last pixel if it matches
                  i++;
              }
              
              int16_t segmentWidth = i - lineStart;
              if (segmentWidth > 0) {
                  uint16_t color = lastPixel ? ST77XX_WHITE : ST77XX_BLACK;
                  tft.drawFastHLine(startX, y + j, segmentWidth, color);
              }
              
              startX = x + i;
              lineStart = i;
              lastPixel = currentPixel;
          }
      }
  }
}

// Draw Unicode string with line wrapping
void drawUnicodeString(Adafruit_ST7789 &tft, U8G2_FOR_ADAFRUIT_GFX &u8g2, 
  int16_t x, int16_t y, const char *text, 
  uint16_t color, const uint8_t *font) {
    u8g2.begin(tft);              
    u8g2.setFont(font);           
    u8g2.setForegroundColor(color);
    u8g2.setFontMode(1);          

    const int16_t maxWidth = SCREEN_WIDTH - x; // Maximum width before wrapping
    const int16_t lineHeight = u8g2.getFontAscent() - u8g2.getFontDescent(); // Approx 16 for this font
    int16_t currentX = x;
    int16_t currentY = y;
    const char *p = text;
    String currentLine = "";

    while (*p) {
        currentLine += *p;
        int16_t textWidth = u8g2.getUTF8Width(currentLine.c_str());

        // If the current line exceeds the max width, wrap it
        if (textWidth > maxWidth && currentLine.length() > 1) {
            // Backtrack to the last space (if any) or just wrap
            int lastSpace = currentLine.lastIndexOf(' ');
            if (lastSpace != -1) {
                String lineToDraw = currentLine.substring(0, lastSpace);
                u8g2.setCursor(currentX, currentY);
                u8g2.print(lineToDraw.c_str());
                currentLine = currentLine.substring(lastSpace + 1);
            } else {
                u8g2.setCursor(currentX, currentY);
                u8g2.print(currentLine.c_str());
                currentLine = "";
            }
            currentY += lineHeight + LINE_SPACING_OFFSET; // Move to next line
        }
        p++;
    }

    // Draw any remaining text
    if (currentLine.length() > 0) {
        u8g2.setCursor(currentX, currentY);
        u8g2.print(currentLine.c_str());
    }
}

void updateDisplay() {
  // tft.fillScreen(ST77XX_BLACK);
  int yOffset = 0;
  // Only redraw if the CRC has changed
  int xOffset = (SCREEN_WIDTH - BITMAP_WIDTH) / 2;
  if (deviceConnected) {
    yOffset = 0;
    // Status bar
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, deviceConnected ? ST77XX_GREEN : ST77XX_RED);
    drawUnicodeString(tft, u8g2, 5, 20, deviceConnected ? "Connected" : "Disconnected", 
      ST77XX_BLACK, u8g2_font_unifont_t_vietnamese2);
      // Navigation data
      yOffset = STATUS_BAR_HEIGHT + 10;
      drawBitmap(xOffset, yOffset, bitmapData, BITMAP_WIDTH, BITMAP_HEIGHT);
      
    yOffset = 200;
    tft.fillRect(0, BITMAP_HEIGHT + STATUS_BAR_HEIGHT - 10 , SCREEN_WIDTH, SCREEN_HEIGHT - yOffset, ST77XX_BLACK);
    
    drawUnicodeString(tft, u8g2, xOffset, yOffset, (distance).c_str(), ST77XX_GREEN, u8g2_font_inr33_mf);
    
    yOffset += 40;
    tft.fillRect(0, yOffset, SCREEN_WIDTH, SCREEN_HEIGHT, ST77XX_BLACK);
    drawUnicodeString(tft, u8g2, 5, yOffset, (title).c_str(), ST77XX_WHITE, myfont);
    yOffset = 304;
    drawUnicodeString(tft, u8g2, 5, yOffset, (eta).c_str(), ST77XX_WHITE, myfont);
    lastConnectionStt = deviceConnected; // Update last connection status
  } else {
    tft.fillScreen(ST77XX_BLACK);
    yOffset = 0;
    // Status bar
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, deviceConnected ? ST77XX_GREEN : ST77XX_RED);
    drawUnicodeString(tft, u8g2, 5, 20, deviceConnected ? "Connected" : "Disconnected", 
                      ST77XX_WHITE, u8g2_font_unifont_t_vietnamese2);

    // Navigation data
    yOffset = STATUS_BAR_HEIGHT + 10;
    drawBitmap(54, 70, disconnected_icon_9, BITMAP_WIDTH, BITMAP_HEIGHT);
  }
}


void setup() {
  Serial.begin(115200);

  // Initialize TFT
  tft.init(240, 320);
  tft.setSPISpeed(800000000);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  // BLE Setup (unchanged)
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
  drawBitmap(54, 70, disconnected_icon_9, BITMAP_WIDTH, BITMAP_HEIGHT);
}

void loop() {
  if (displayNeedsUpdate) {
    updateDisplay();
    displayNeedsUpdate = false;
  }
  delay(50); // Adjust delay as needed
}
