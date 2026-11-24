/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/nrfbox
   ________________________________________ */

#include "config.h"
#include "icon.h"
#include "setting.h"
#include "Frames.h"
#include "rfclown.h"
#include <Arduino.h>
#include <math.h>

// Forward declarations
void drawMenu();
bool readButton(int pin);
bool readAnyButton();
void flushInput();
void menuStep(int dir);
void menuUpdate();
void menuOpenTransition();
void menuCloseTransition();
void runScubaIdleApp();
void playStartupAnimation(int loops = 1);
void drawModeSelectScreen(int selected);

enum AppMode {
  APP_MODE_SELECT = 0,
  APP_MODE_NRFBOX = 1,
  APP_MODE_RFCLOWN = 2
};

AppMode currentAppMode = APP_MODE_SELECT;
int selectedMode = 0;
bool modeSelectDirty = true;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_NeoPixel pixels(1, 14, NEO_GRB + NEO_KHZ800);
bool neoPixelActive = false;
uint8_t oledBrightness = 100;
uint8_t oledType = 0;

void applyOledType(uint8_t type) {
  if (type == 1) {
    u8g2_Setup_sh1106_i2c_128x64_noname_f(u8g2.getU8g2(), U8G2_R0, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
  } else {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2.getU8g2(), U8G2_R0, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
  }
  u8x8_SetPin_HW_I2C(u8g2.getU8x8(), U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.setContrast(oledBrightness);
}

const int NUM_ITEMS = 14;
const int SWITCH_MODE_ITEM_INDEX = 13;
const int MAX_ITEM_LENGTH = 20;

const unsigned char *bitmap_icons[NUM_ITEMS] = {
    bitmap_icon_scanner,     bitmap_icon_analyzer,   bitmap_icon_jammer,
    bitmap_icon_kill,        bitmap_icon_ble_jammer, bitmap_icon_spoofer,
    bitmap_icon_apple,       bitmap_icon_ble,        bitmap_icon_wifi,
    bitmap_icon_wifi_jammer, bitmap_icon_skull,      bitmap_icon_about,
    bitmap_icon_setting,     bitmap_icon_go_back};

char menu_items[NUM_ITEMS][MAX_ITEM_LENGTH] = {
    "Scanner",    "Analyzer",    "WLAN Jammer", "Proto Kill",
    "BLE Jammer", "BLE Spoofer", "Sour Apple",  "BLE Scan",
    "WiFi Scan",  "Deauther",    "RF-Clown",    "About",
    "Setting",    "Switch Mode"};

void (*menu_functions[NUM_ITEMS])() = {Scanner::scannerSetup,
                                       Analyzer::analyzerSetup,
                                       Jammer::jammerSetup,
                                       ProtoKill::blackoutSetup,
                                       BleJammer::blejammerSetup,
                                       Spoofer::spooferSetup,
                                       SourApple::sourappleSetup,
                                       BleScan::blescanSetup,
                                       WifiScan::wifiscanSetup,
                                       Deauther::deautherSetup,
                                       RFClown::rfclownSetup,
                                       utils,
                                       Setting::settingSetup,
                                       nullptr};

void (*menu_loop_functions[NUM_ITEMS])() = {Scanner::scannerLoop,
                                            Analyzer::analyzerLoop,
                                            Jammer::jammerLoop,
                                            ProtoKill::blackoutLoop,
                                            BleJammer::blejammerLoop,
                                            Spoofer::spooferLoop,
                                            SourApple::sourappleLoop,
                                            BleScan::blescanLoop,
                                            WifiScan::wifiscanLoop,
                                            Deauther::deautherLoop,
                                            RFClown::rfclownLoop,
                                            nullptr,
                                            Setting::settingLoop,
                                            nullptr};

int item_selected = 0;
int current_screen = 0;
unsigned long last_button_time = 0;
unsigned long last_user_activity = 0;
const unsigned long DEBOUNCE_DELAY = 160;
const unsigned long IDLE_MS = 30000UL;

// Carousel Menu Layout & Physics (MOGU OS style)
#define MENU_CX 64
#define MENU_ICON_CY 27
#define MENU_SPACING 38
#define MENU_NAME_Y 52
#define MENU_DOTS_Y 61

#define MENU_EASE_TAU_MS 60.0f
#define MENU_MAX_ITEMS_PER_S 14.0f
#define MENU_SNAP 0.02f
#define MENU_REFRESH_MS 500

long menuTarget = 0;
float menuScroll = 0.0f;
unsigned long menuLastDraw = 0;
bool menuDirty = true;

static int menuWrap(long v) {
  long m = v % NUM_ITEMS;
  if (m < 0)
    m += NUM_ITEMS;
  return (int)m;
}

void menuStep(int dir) {
  menuTarget += dir;
  item_selected = menuWrap(menuTarget);
  last_user_activity = millis();
  menuDirty = true;
}

static bool menuAnimate(unsigned long now) {
  static unsigned long last = 0;
  float dt = last ? (float)(now - last) : 16.0f;
  last = now;
  if (dt > 60.0f)
    dt = 60.0f;

  float diff = (float)menuTarget - menuScroll;
  if (diff == 0.0f)
    return false;
  if (fabsf(diff) < MENU_SNAP) {
    menuScroll = (float)menuTarget;
    return true;
  }
  float step = diff * (1.0f - expf(-dt / MENU_EASE_TAU_MS));
  float maxStep = MENU_MAX_ITEMS_PER_S * dt / 1000.0f;
  if (step > maxStep)
    step = maxStep;
  else if (step < -maxStep)
    step = -maxStep;
  menuScroll += step;
  return true;
}

// Draw a 16x16 icon (XBMP) scaled s times, centered on (cx, cy)
// If faded is true, only every other pixel is drawn (checkerboard dither)
static void menuDrawIcon(int idx, int cx, int cy, int s, bool faded) {
  const unsigned char *bmp = bitmap_icons[idx];
  if (!bmp)
    return;
  int x0 = cx - (16 * s) / 2;
  int y0 = cy - (16 * s) / 2;
  for (int r = 0; r < 16; r++) {
    uint16_t rowBits = (uint16_t)pgm_read_byte(bmp + r * 2) |
                       ((uint16_t)pgm_read_byte(bmp + r * 2 + 1) << 8);
    for (int c = 0; c < 16; c++) {
      if (!(rowBits & (1 << c)))
        continue;
      if (faded && ((r + c) & 1))
        continue;
      if (s == 1) {
        int px = x0 + c;
        int py = y0 + r;
        if (px >= 0 && px < 128 && py >= 0 && py < 64)
          u8g2.drawPixel(px, py);
      } else {
        int px = x0 + c * s;
        int py = y0 + r * s;
        if (px + s > 0 && px < 128 && py + s > 0 && py < 64)
          u8g2.drawBox(px, py, s, s);
      }
    }
  }
}

static void menuDrawCarousel(float scroll) {
  long base = (long)floorf(scroll);
  for (long i = base - 2; i <= base + 3; i++) {
    float d = (float)i - scroll;
    int x = MENU_CX + (int)lroundf(d * MENU_SPACING);
    if (x < -24 || x > 152)
      continue;
    float ad = fabsf(d);
    int s = (ad < 0.5f) ? 2 : 1;
    menuDrawIcon(menuWrap(i), x, MENU_ICON_CY, s, ad >= 0.5f);
  }
}

static void menuDrawName(float scroll) {
  int idx = menuWrap((long)lroundf(scroll));
  const char *name = menu_items[idx];
  u8g2.setFont(u8g2_font_7x14_tf);
  int w = u8g2.getUTF8Width(name);
  if (w > 124) {
    u8g2.setFont(u8g2_font_5x8_tf);
    w = u8g2.getUTF8Width(name);
  }
  int x = (128 - w) / 2;
  u8g2.drawStr(x, MENU_NAME_Y, name);
}

static void menuDrawDots(float scroll) {
  int sel = menuWrap((long)lroundf(scroll));
  const int gap = 7;
  int x0 = MENU_CX - ((NUM_ITEMS - 1) * gap) / 2;
  for (int i = 0; i < NUM_ITEMS; i++) {
    int x = x0 + i * gap;
    if (i == sel)
      u8g2.drawBox(x - 2, MENU_DOTS_Y, 5, 2);
    else
      u8g2.drawPixel(x, MENU_DOTS_Y + 1);
  }
}

static void menuDrawHeader() {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 7, txt_n);
  int vw = u8g2.getUTF8Width(txt_v);
  u8g2.drawStr(128 - vw - 2, 7, txt_v);
  u8g2.drawHLine(0, 9, 128);
}

static const uint8_t menuIris[] = {1, 4, 9, 16, 24, 32};

void menuRender(int iris) {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  menuDrawCarousel(menuScroll);
  menuDrawName(menuScroll);
  menuDrawDots(menuScroll);
  menuDrawHeader();

  if (iris < 32) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 0, 128, 32 - iris);
    u8g2.drawBox(0, 32 + iris, 128, 32 - iris);
    u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

void menuOpenTransition() {
  menuScroll = (float)menuTarget;
  for (uint8_t i = 0; i < sizeof(menuIris); i++) {
    menuRender(menuIris[i]);
    delay(10);
  }
  menuLastDraw = millis();
  menuDirty = false;
  last_user_activity = millis();
}

void menuCloseTransition() {
  for (int i = (int)sizeof(menuIris) - 2; i >= 0; i--) {
    menuRender(menuIris[i]);
    delay(10);
  }
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  flushInput();
}

void menuUpdate() {
  unsigned long now = millis();
  bool moving = menuAnimate(now);
  if (menuDirty || moving || now - menuLastDraw >= MENU_REFRESH_MS) {
    menuRender(32);
    menuLastDraw = millis();
    menuDirty = false;
  }
  if (!moving && menuScroll == (float)menuTarget && labs(menuTarget) >= 1000) {
    long s = (menuTarget / NUM_ITEMS) * NUM_ITEMS;
    menuTarget -= s;
    menuScroll -= (float)s;
  }
}

void drawMenu() {
  menuDirty = true;
  menuRender(32);
  setRadiosNeutralState();
}

bool readButton(int pin) {
  if (digitalRead(pin) == LOW &&
      (millis() - last_button_time > DEBOUNCE_DELAY)) {
    last_button_time = millis();
    last_user_activity = millis();
    return true;
  }
  return false;
}

bool readAnyButton() {
  return (digitalRead(BUTTON_SELECT_PIN) == LOW ||
          digitalRead(BUTTON_UP_PIN) == LOW ||
          digitalRead(BUTTON_DOWN_PIN) == LOW ||
          digitalRead(BTN_PIN_LEFT) == LOW ||
          digitalRead(BTN_PIN_RIGHT) == LOW);
}

void flushInput() {
  unsigned long t = millis();
  while (readAnyButton() && millis() - t < 1000) {
    delay(5);
  }
  delay(30);
  last_button_time = millis();
  last_user_activity = millis();
}

void runScubaIdleApp() {
  uint8_t i = 0;
  unsigned long nextFrameAt = millis();

  while (true) {
    if (readAnyButton())
      break;

    unsigned long now = millis();
    if ((long)(now - nextFrameAt) >= 0) {
      const uint8_t *bmp = (const uint8_t *)pgm_read_ptr(&frames[i]);
      u8g2.clearBuffer();
      u8g2.drawBitmap(0, 0, 16, 64, bmp);
      u8g2.sendBuffer();

      i = (i + 1) % FRAME_COUNT;
      nextFrameAt += FRAME_DELAY;
      if ((long)(millis() - nextFrameAt) > 0)
        nextFrameAt = millis();
    }
    delay(1);
  }
  flushInput();
  menuOpenTransition();
}

void playStartupAnimation(int loops) {
  for (int l = 0; l < loops; l++) {
    for (uint8_t i = 0; i < FRAME_COUNT; i++) {
      if (readAnyButton())
        return;
      unsigned long t0 = millis();
      const uint8_t *bmp = (const uint8_t *)pgm_read_ptr(&frames[i]);
      u8g2.clearBuffer();
      u8g2.drawBitmap(0, 0, 16, 64, bmp);
      u8g2.sendBuffer();
      long elapsed = (long)(millis() - t0);
      if (elapsed < FRAME_DELAY) {
        delay(FRAME_DELAY - elapsed);
      }
    }
  }
}

void drawModeSelectScreen(int selected) {
  u8g2.clearBuffer();
  u8g2.setFontPosTop();

  // Top header bar
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 11);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setDrawColor(0);
  u8g2.drawStr(12, 2, "select OS :3");
  u8g2.setDrawColor(1);

  for (int i = 0; i < 2; i++) {
    int y = 14 + i * 25;
    bool isSel = (selected == i);

    if (isSel) {
      u8g2.drawRFrame(2, y, 124, 23, 2);
      u8g2.drawRFrame(3, y + 1, 122, 21, 2);
      u8g2.setFont(u8g2_font_5x8_tr);
      u8g2.drawStr(6, y + 7, ">");
    } else {
      u8g2.drawRFrame(4, y + 1, 120, 21, 2);
    }

    const unsigned char *icon = (i == 0) ? bitmap_icon_satelite : bitmap_icon_skull;
    u8g2.drawXBMP(15, y + 3, 16, 16, icon);

    u8g2.setFont(u8g2_font_7x14_tf);
    const char *title = (i == 0) ? "nRFBox" : "RF-Clown";
    u8g2.drawStr(36, y + 2, title);

    u8g2.setFont(u8g2_font_4x6_tr);
    const char *sub = (i == 0) ? "Tools for larping" : "RF Multi-Jammer v2.0";
    u8g2.drawStr(36, y + 15, sub);
  }

  u8g2.setFontPosBaseline();
  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_PIN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_PIN_LEFT, INPUT_PULLUP);
  neopixelSetup();
  initAllRadios();
  EEPROM.begin(512);
  oledBrightness = EEPROM.read(EEPROM_ADDRESS_BRIGHTNESS);
  if (oledBrightness > 255) oledBrightness = 128;
  oledType = EEPROM.read(EEPROM_ADDRESS_OLED_TYPE);
  if (oledType > 1) oledType = 0;
  applyOledType(oledType);

  conf();
  playStartupAnimation(1);
  setRadiosNeutralState();
  flushInput();

  currentAppMode = APP_MODE_SELECT;
  modeSelectDirty = true;
}

void loop() {
  if (currentAppMode == APP_MODE_SELECT) {
    if (modeSelectDirty) {
      drawModeSelectScreen(selectedMode);
      modeSelectDirty = false;
    }

    if (readButton(BUTTON_UP_PIN) || readButton(BTN_PIN_LEFT)) {
      selectedMode = (selectedMode == 0) ? 1 : 0;
      modeSelectDirty = true;
    }
    if (readButton(BUTTON_DOWN_PIN) || readButton(BTN_PIN_RIGHT)) {
      selectedMode = (selectedMode == 0) ? 1 : 0;
      modeSelectDirty = true;
    }

    if (readButton(BUTTON_SELECT_PIN)) {
      flushInput();
      if (selectedMode == 0) {
        currentAppMode = APP_MODE_NRFBOX;
        current_screen = 0;
        menuOpenTransition();
      } else {
        currentAppMode = APP_MODE_RFCLOWN;
        RFClown::rfclownSetup();
      }
      modeSelectDirty = true;
    }
    delay(1);
    return;
  }

  if (currentAppMode == APP_MODE_RFCLOWN) {
    RFClown::rfclownLoop();
    if (readButton(BUTTON_SELECT_PIN)) {
      RFClown::cleanup();
      setRadiosNeutralState();
      u8g2.setFontPosBaseline();
      flushInput();
      currentAppMode = APP_MODE_SELECT;
      modeSelectDirty = true;
    }
    delay(1);
    return;
  }

  if (currentAppMode == APP_MODE_NRFBOX) {
    if (current_screen == 0) {
      if (millis() - last_user_activity > IDLE_MS) {
        runScubaIdleApp();
      }

      if (readButton(BTN_PIN_LEFT) || readButton(BUTTON_UP_PIN)) {
        menuStep(-1);
      }
      if (readButton(BTN_PIN_RIGHT) || readButton(BUTTON_DOWN_PIN)) {
        menuStep(+1);
      }
      if (readButton(BUTTON_SELECT_PIN)) {
        if (item_selected == SWITCH_MODE_ITEM_INDEX) {
          menuCloseTransition();
          setRadiosNeutralState();
          u8g2.setFontPosBaseline();
          flushInput();
          currentAppMode = APP_MODE_SELECT;
          modeSelectDirty = true;
          return;
        }

        current_screen = 1;
        menuCloseTransition();
        menu_functions[item_selected]();

        while (current_screen == 1) {
          if (menu_loop_functions[item_selected]) {
            menu_loop_functions[item_selected]();
          }
          if (readButton(BUTTON_SELECT_PIN)) {
            current_screen = 0;
            break;
          }
          delay(1);
        }
        setRadiosNeutralState();
        u8g2.setFontPosBaseline();
        flushInput();
        menuOpenTransition();
      }

      menuUpdate();
      delay(1);
    }
  }
}
