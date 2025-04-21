#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "config.h"

#ifdef USE_TFT_ST7789
#include "Adafruit_ST7789.h"
#include "U8g2_for_Adafruit_GFX.h"
U8G2_FOR_ADAFRUIT_GFX u8g2;
#endif
#ifdef USE_OLED_GME128128
  #include <U8g2lib.h>
#endif

#include "NimBLEDevice.h"
#include "myfont.h"
#include "esp_crc.h"
#include "disconnected_icon_9.h"


#ifdef USE_TFT_ST7789
  Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
  #define DISPLAY_COLOR_WHITE ST77XX_WHITE
  #define DISPLAY_COLOR_BLACK ST77XX_BLACK
  #define DISPLAY_COLOR_GREEN ST77XX_GREEN
  #define DISPLAY_COLOR_RED ST77XX_RED
#endif
#ifdef USE_OLED_GME128128
U8G2_SH1107_SEEED_128X128_F_HW_I2C u8g2_oled(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
  #define DISPLAY_COLOR_WHITE 1
  #define DISPLAY_COLOR_BLACK 0
  #define DISPLAY_COLOR_GREEN 1
  #define DISPLAY_COLOR_RED 1
#endif

extern const uint8_t u8g2_font_unifont_t_vietnamese2[15330] U8G2_FONT_SECTION("u8g2_font_unifont_t_vietnamese2");
extern const uint8_t u8g2_font_inr33_mf[11616] U8G2_FONT_SECTION("u8g2_font_inr33_mf");
extern const uint8_t u8g2_font_helvB18_tf[4956] U8G2_FONT_SECTION("u8g2_font_helvB18_tf");
extern const uint8_t u8g2_font_unifont_t_vietnamese1[4308] U8G2_FONT_SECTION("u8g2_font_unifont_t_vietnamese1");

// BLE Variables (unchanged)
static NimBLEServer* pServer;
static NimBLECharacteristic* pCharacteristic;
static bool deviceConnected = false;
static bool lastConnectionStt = false;
static bool displayNeedsUpdate = true;
static String connectedDeviceAddress = "";

// Scrolling state
static bool isScrolling = false;
static uint32_t scrollStartTime = 0;
static int16_t scrollTextWidth = 0;

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

// Function Prototypes
void processReceivedData(uint8_t* data, size_t length);
void parseData();
void drawBitmap(int16_t x, int16_t y, uint8_t* bitmap, int16_t w, int16_t h);
void updateDisplay();
void drawUnicodeString(int16_t x, int16_t y, const char *text, uint16_t color, const uint8_t *font);
void drawBitmapScaled(U8G2 &u8g2, int x, int y, const uint8_t *bitmap, int width, int height, int scale);

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

void drawBitmapScaled(U8G2 &u8g2, int x, int y, const uint8_t *bitmap, int width, int height, int scale) {
  if (!bitmap || scale < 1) return;

  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      int byteIndex = row * ((width + 7) / 8) + (col / 8);
      int bitMask = 0x80 >> (col % 8);
      bool pixelOn = bitmap[byteIndex] & bitMask;

      if (pixelOn) {
        u8g2.drawBox(x + col * scale, y + row * scale, scale, scale);
      }
    }
  }
}

void drawBitmap(int16_t x, int16_t y, uint8_t* bitmap, int16_t w, int16_t h) {
  if (!bitmap) {
    Serial.println("Invalid bitmap: null pointer");
    return;
  }

#ifdef USE_TFT_ST7789
  // TFT rendering (unchanged, 132x132)
  for (int16_t j = 0; j < h; j++) {
    int16_t startX = x;
    int16_t lineStart = 0;
    bool lastPixel = false;
    for (int16_t i = 0; i < w; i++) {
      int32_t pixelIndex = j * w + i;
      int32_t byteIndex = pixelIndex / 8;
      int32_t bitIndex = 7 - (pixelIndex % 8);
      bool currentPixel = (bitmap[byteIndex] >> bitIndex) & 0x01;
      if (currentPixel != lastPixel || i == w - 1) {
        if (i == w - 1 && currentPixel == lastPixel) {
          i++;
        }
        int16_t segmentWidth = i - lineStart;
        if (segmentWidth > 0) {
          uint16_t color = lastPixel ? DISPLAY_COLOR_WHITE : DISPLAY_COLOR_BLACK;
          tft.drawFastHLine(startX, y + j, segmentWidth, color);
        }
        startX = x + i;
        lineStart = i;
        lastPixel = currentPixel;
      }
    }
  }
#endif
#ifdef USE_OLED_GME128128
u8g2_oled.setDrawColor(1); // Set color to white
for (int16_t j = 0; j < h; j++) {
  for (int16_t i = 0; i < w; i++) {
    int32_t pixelIndex = j * w + i;
    int32_t byteIndex = pixelIndex / 8;
    int32_t bitIndex = 7 - (pixelIndex % 8);
    bool pixelOn = (bitmap[byteIndex] >> bitIndex) & 0x01;
    if (pixelOn) {
      u8g2_oled.drawPixel(x + i, y + j);
    }
  }
}
#endif
}

void drawUnicodeString(int16_t x, int16_t y, const char *text, uint16_t color, const uint8_t *font) {
#ifdef USE_TFT_ST7789
  // Unchanged TFT code
  u8g2.begin(tft);
  u8g2.setFont(font);
  u8g2.setForegroundColor(color);
  u8g2.setFontMode(1);
  const int16_t maxWidth = SCREEN_WIDTH - x;
  const int16_t lineHeight = u8g2.getFontAscent() - u8g2.getFontDescent();
  int16_t currentX = x;
  int16_t currentY = y;
  String currentLine = "";

  const char *p = text;
  while (*p) {
    currentLine += *p;
    int16_t textWidth = u8g2.getUTF8Width(currentLine.c_str());
    if (textWidth > maxWidth && currentLine.length() > 1) {
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
      currentY += lineHeight + LINE_SPACING_OFFSET;
    }
    p++;
  }
  if (currentLine.length() > 0) {
    u8g2.setCursor(currentX, currentY);
    u8g2.print(currentLine.c_str());
  }
#endif
#ifdef USE_OLED_GME128128
  u8g2_oled.setFont(font);
  u8g2_oled.setDrawColor(color);
  int16_t textWidth = u8g2_oled.getUTF8Width(text);
  const int16_t maxWidth = SCREEN_WIDTH - x; // 126 at x=2

  if (textWidth <= maxWidth) {
    // Static text
    u8g2_oled.drawUTF8(x, y, text);
    isScrolling = false;
  } else {
    // Scrolling text
    isScrolling = true;
    scrollTextWidth = textWidth;
    if (scrollStartTime == 0) {
      scrollStartTime = millis();
    }
    // Scroll right to left, 5s cycle
    uint32_t elapsed = millis() - scrollStartTime;
    // Add 500ms pause at start
    int16_t offset;
    if (elapsed < 500) {
      offset = 0; // Pause
    } else {
      uint32_t scrollTime = elapsed - 500;
      offset = (scrollTime % 8000) * (textWidth + SCREEN_WIDTH) / 8000;
    }
    int16_t drawX = SCREEN_WIDTH - offset;
    u8g2_oled.setClipWindow(x, y - u8g2_oled.getFontAscent() - 6, x + maxWidth, y + u8g2_oled.getFontDescent()+ 6);
    u8g2_oled.drawUTF8(drawX, y, text);
    u8g2_oled.setClipWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); // Reset clip
  }
#endif
}

void updateDisplay() {
  int yOffset = 0;
#ifdef USE_TFT_ST7789
  int xOffset = (SCREEN_WIDTH - BITMAP_WIDTH) / 2;
#endif
#ifdef USE_OLED_GME128128
  int xOffset = 2; // Bitmap on left
  u8g2_oled.clearBuffer();
#endif

  if (deviceConnected) {
    yOffset = 0;
    // Status bar
#ifdef USE_TFT_ST7789
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, deviceConnected ? DISPLAY_COLOR_GREEN : DISPLAY_COLOR_RED);
    drawUnicodeString(5, 20, deviceConnected ? "Connected" : "Disconnected", DISPLAY_COLOR_BLACK, u8g2_font_unifont_t_vietnamese2);
#endif

    // Navigation data
    yOffset += 2; // y=18
#ifdef USE_TFT_ST7789
    drawBitmap(xOffset, yOffset, bitmapData, 132, 132);
    yOffset = 200;
    tft.fillRect(0, BITMAP_HEIGHT + STATUS_BAR_HEIGHT - 10, SCREEN_WIDTH, SCREEN_HEIGHT - yOffset, DISPLAY_COLOR_BLACK);
    drawUnicodeString(xOffset, yOffset, distance.c_str(), DISPLAY_COLOR_GREEN, u8g2_font_inr33_mf);
    yOffset += 40;
    tft.fillRect(0, yOffset, SCREEN_WIDTH, SCREEN_HEIGHT, DISPLAY_COLOR_BLACK);
    drawUnicodeString(5, yOffset, title.c_str(), DISPLAY_COLOR_WHITE, myfont);
    yOffset = 304;
    drawUnicodeString(5, yOffset, eta.c_str(), DISPLAY_COLOR_WHITE, myfont);
#endif
#ifdef USE_OLED_GME128128
    drawBitmap(xOffset, yOffset, bitmapData, BITMAP_WIDTH, BITMAP_HEIGHT);
    // ETA bound (right side of bitmap)
    int etaX = 20; // x=74
    int etaWidth = SCREEN_WIDTH - etaX - 2; // 52
    int etaHeight = BITMAP_HEIGHT; // 70
    // u8g2_oled.drawFrame(etaX, yOffset, etaWidth, etaHeight);
    // Center ETA text
    u8g2_oled.setFont(u8g2_font_helvB18_tf);
    int textWidth = u8g2_oled.getUTF8Width(eta.c_str());
    int textX = etaX + (etaWidth - textWidth) / 2;
    int textY = yOffset + BITMAP_WIDTH + 10;
    drawUnicodeString(textX, textY, distance.c_str(), DISPLAY_COLOR_WHITE, u8g2_font_helvB18_tf);
    // Direction at bottom (with wrapping downward)
    drawUnicodeString(0, 124, title.c_str(), DISPLAY_COLOR_WHITE, u8g2_font_unifont_t_vietnamese1);
#endif
    lastConnectionStt = deviceConnected;
  } else {
#ifdef USE_TFT_ST7789
    tft.fillScreen(DISPLAY_COLOR_BLACK);
    tft.fillRect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, deviceConnected ? DISPLAY_COLOR_GREEN : DISPLAY_COLOR_RED);
    drawUnicodeString(5, 20, deviceConnected ? "Connected" : "Disconnected", DISPLAY_COLOR_WHITE, u8g2_font_unifont_t_vietnamese2);
    drawBitmap(54, 70, disconnected_icon_9, BITMAP_WIDTH, BITMAP_HEIGHT);
#endif
#ifdef USE_OLED_GME128128
    u8g2_oled.drawBox(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT);
    drawUnicodeString(5, 14, deviceConnected ? "Connected" : "Disconnected", DISPLAY_COLOR_BLACK, u8g2_font_helvB12_tf);
    drawBitmap(19, 39, disconnected_icon_90, BITMAP_WIDTH, BITMAP_HEIGHT);
#endif
  }

#ifdef USE_OLED_GME128128
  u8g2_oled.sendBuffer();
#endif
}

void setup() {
  Serial.begin(115200);

  // Initialize Display
#ifdef USE_TFT_ST7789
  tft.init(240, 320);
  tft.setSPISpeed(80000000);
  tft.setRotation(0);
  tft.fillScreen(DISPLAY_COLOR_BLACK);
#endif
#ifdef USE_OLED_GME128128
  u8g2_oled.begin();
  u8g2_oled.clearBuffer();
  u8g2_oled.setPowerSave(0);
#endif

  // BLE Setup (unchanged)
  NimBLEDevice::init("WeNav_OLED_ESP32C3");
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
#ifdef USE_TFT_ST7789
  drawBitmap(54, 70, disconnected_icon_9, BITMAP_WIDTH, BITMAP_HEIGHT);
#endif
#ifdef USE_OLED_GME128128
  drawBitmap(29, 29, disconnected_icon_9, BITMAP_WIDTH, BITMAP_HEIGHT);
  u8g2_oled.sendBuffer();
#endif
}

void loop() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (isScrolling && (now - lastUpdate >= 100)) { // Update every 100ms
    displayNeedsUpdate = true;
    lastUpdate = now;
  }
  if (displayNeedsUpdate) {
    updateDisplay();
    displayNeedsUpdate = false;
  }
  delay(10);
}