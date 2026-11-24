/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/nrfbox
   ________________________________________ */
   
#ifndef setting_H
#define setting_H

#include <BLEDevice.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <RF24.h>
#include <vector>
#include <string>
#include <SD.h>
#include <Update.h>
#include <SPI.h>

void neopixelSetup();
void neopixelLoop();

void setNeoPixelColour(const std::string& colour);
void flash(int numberOfFlashes, const std::vector<std::string>& colors, const std::string& finalColour);

#define EEPROM_ADDRESS_NEOPIXEL 0
#define EEPROM_ADDRESS_BRIGHTNESS 1
#define EEPROM_ADDRESS_OLED_TYPE 2

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern Adafruit_NeoPixel pixels;

extern bool neoPixelActive;
extern uint8_t oledBrightness;
extern uint8_t oledType;

void applyOledType(uint8_t type);

extern RF24 RadioA;
extern RF24 RadioB;
extern RF24 RadioC;

void configureNrf(RF24 &radio);

void setRadiosNeutralState();

void setupRadioA();
void setupRadioB();
void setupRadioC();

void initAllRadios();

void Str(uint8_t x, uint8_t y, const char* str);
void CenteredStr(uint8_t screenWidth, uint8_t y, const char* str, const uint8_t* font);
void utils();
void conf();

namespace Setting {
  void settingSetup();
  void settingLoop();
}

#endif
