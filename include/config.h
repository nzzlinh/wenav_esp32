#ifndef CONFIG_H
#define CONFIG_H

// TFT Pins
#define TFT_CS    5
#define TFT_RST   22
#define TFT_DC    21
#define TFT_MOSI  23
#define TFT_SCK   18
#define TFT_MISO  19
#define TFT_LED   4   // Backlight LED pin

// BLE Data Frame Configuration
#define FRAME_HEADER    0xAA
#define CMD_NAV_UPDATE  0x01
#define MAX_PAYLOAD     32

#define USE_SPI_DMA

#endif