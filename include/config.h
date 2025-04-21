#ifndef CONFIG_H
#define CONFIG_H

// Define display type (uncomment one)
// #define USE_TFT_ST7789
#define USE_OLED_GME128128

#ifdef USE_TFT_ST7789
    #define SCREEN_WIDTH 240
    #define SCREEN_HEIGHT 320
    #define BITMAP_WIDTH 132
    #define BITMAP_HEIGHT 132
    #define STATUS_BAR_HEIGHT 36

    // TFT Pins
    #define TFT_CS    5
    #define TFT_RST   22
    #define TFT_DC    21
    #define TFT_MOSI  23
    #define TFT_SCK   18
    #define TFT_MISO  19
    #define TFT_LED   4   // Backlight LED pin
#endif

#ifdef USE_OLED_GME128128
    #define SCREEN_WIDTH 128
    #define SCREEN_HEIGHT 128
    #define BITMAP_WIDTH 90
    #define BITMAP_HEIGHT 90
    #define STATUS_BAR_HEIGHT 16
#endif

#define LINE_SPACING_OFFSET 5

// BLE Data Frame Configuration
#define FRAME_HEADER    0xAA
#define CMD_NAV_UPDATE  0x01
#define MAX_PAYLOAD     32

#define USE_SPI_DMA

#endif