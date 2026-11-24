/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/rfclown
   ________________________________________ */

#include "rfclown.h"
#include "config.h"
#include "setting.h"
#include <math.h>

namespace RFClown {

enum OperationMode {
  WiFi_MODULE,
  VIDEO_TX_MODULE,
  RC_MODULE,
  BLE_MODULE,
  Bluetooth_MODULE,
  USB_WIRELESS_MODULE,
  ZIGBEE_MODULE,
  NRF24_MODULE
};

enum Operation { DEACTIVE_MODE, ACTIVE_MODE };

static OperationMode current_Mode = WiFi_MODULE;
static Operation current = DEACTIVE_MODE;

static const byte channelGroup_1[] = {2, 5, 8, 11};
static const byte channelGroup_2[] = {26, 29, 32, 35};
static const byte channelGroup_3[] = {80, 83, 86, 89};

static const byte bluetooth_channels[] = {32, 34, 46, 48, 50, 52, 0,
                                          1,  2,  4,  6,  8,  22, 24,
                                          26, 28, 30, 74, 76, 78, 80};
static const byte ble_channels[] = {2, 26, 80};
static const byte WiFi_channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const byte usbWireless_channels[] = {40, 50, 60};
static const byte videoTransmitter_channels[] = {70, 75, 80};
static const byte rc_channels[] = {1, 3, 5, 7};
static const byte zigbee_channels[] = {11, 15, 20, 25};
static const byte nrf24_channels[] = {76, 78, 79};

static unsigned long lastPressTime = 0;
static const unsigned long debounceDelay = 220;

static const uint8_t HEADER_H = 12;
static const uint8_t PAD = 6;
static const uint8_t CARD_H = 36;
static const uint8_t RADIUS = 3;
static const uint8_t DOT_Y = 64 - 6;

#define SCREEN_W 128
#define SCREEN_H 64
#define FONT_SMALL u8g2_font_5x8_tr
#define FONT_MEDIUM u8g2_font_7x14_tf

static const char *kMenuLabels[] = {"WiFi",   "Video TX",  "RC",
                                    "BLE",    "Bluetooth", "USB Wireless",
                                    "Zigbee", "NRF24"};
static const int kMenuCount = sizeof(kMenuLabels) / sizeof(kMenuLabels[0]);

static int menuIndexFromMode(OperationMode m) {
  switch (m) {
  case WiFi_MODULE:
    return 0;
  case VIDEO_TX_MODULE:
    return 1;
  case RC_MODULE:
    return 2;
  case BLE_MODULE:
    return 3;
  case Bluetooth_MODULE:
    return 4;
  case USB_WIRELESS_MODULE:
    return 5;
  case ZIGBEE_MODULE:
    return 6;
  case NRF24_MODULE:
    return 7;
  default:
    return 0;
  }
}

static OperationMode modeFromMenuIndex(int idx) {
  switch (idx) {
  case 0:
    return WiFi_MODULE;
  case 1:
    return VIDEO_TX_MODULE;
  case 2:
    return RC_MODULE;
  case 3:
    return BLE_MODULE;
  case 4:
    return Bluetooth_MODULE;
  case 5:
    return USB_WIRELESS_MODULE;
  case 6:
    return ZIGBEE_MODULE;
  case 7:
    return NRF24_MODULE;
  default:
    return WiFi_MODULE;
  }
}

static void configure_Radio(RF24 &radio, const byte *channels, size_t size) {
  configureNrf(radio);
  for (size_t i = 0; i < size; i++) {
    radio.setChannel(channels[i]);
    radio.startConstCarrier(RF24_PA_MAX, channels[i]);
  }
}

static void initialize_MultiMode() {
  if (RadioA.begin()) {
    configure_Radio(RadioA, channelGroup_1, sizeof(channelGroup_1));
  }
  if (RadioB.begin()) {
    configure_Radio(RadioB, channelGroup_2, sizeof(channelGroup_2));
  }
  if (RadioC.begin()) {
    configure_Radio(RadioC, channelGroup_3, sizeof(channelGroup_3));
  }
}

static void initialize_Radios() {
  if (current == ACTIVE_MODE) {
    initialize_MultiMode();
  } else if (current == DEACTIVE_MODE) {
    RadioA.powerDown();
    RadioB.powerDown();
    RadioC.powerDown();
    delay(10);
  }
}

static void drawHeaderBar() {
  const uint8_t W = 128;
  const uint8_t H = HEADER_H;
  const uint8_t TXT_Y = 2;
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, W, H);
  u8g2.setFont(FONT_SMALL);
  u8g2.setDrawColor(0);
  u8g2.drawStr(PAD, TXT_Y, "RF-Clown");
  const char *ver = "Exit:SEL";
  int vw = u8g2.getStrWidth(ver);
  u8g2.drawStr(W - vw - 4, TXT_Y, ver);
  u8g2.setDrawColor(1);
  u8g2.drawHLine(0, H - 1, W);
}

static void drawPillOutline(int x, int y, int w, int h) {
  if (h & 1)
    h--;
  if (y & 1)
    y--;
  u8g2.drawRFrame(x, y, w, h, h / 2);
}

static void drawPillKnob(int x, int y, int w, int h, float pos) {
  if (h & 1)
    h--;
  if (y & 1)
    y--;
  int r = h / 2, cxL = x + r, cxR = x + w - r - 1, cy = y + r;
  int cx = (int)lroundf(cxL + (cxR - cxL) * pos);
  int knob = max(2, r - 3);
  u8g2.drawDisc(cx, cy, knob);
}

static void drawPillToggle(int x, int y, int w, int h, bool on) {
  drawPillOutline(x, y, w, h);
  drawPillKnob(x, y, w, h, on ? 0.0f : 1.0f);
}

static void drawPaginationDots(int active) {
  int totalW = (kMenuCount * 6) - 2;
  int startX = (128 - totalW) / 2;
  for (int i = 0; i < kMenuCount; i++) {
    int x = startX + i * 6;
    if (i == active)
      u8g2.drawDisc(x, DOT_Y, 2);
    else
      u8g2.drawCircle(x, DOT_Y, 2);
  }
}

static void drawCardAtCenter(int centerX, int menuIndex) {
  const char *label = kMenuLabels[menuIndex];
  const int W = 128;
  const int w = W - PAD * 2;
  const int x = centerX - w / 2;
  const int y = HEADER_H + 4;
  u8g2.setDrawColor(0);
  u8g2.drawBox(x + 1, y + 1, w - 2, CARD_H - 2);
  u8g2.setDrawColor(1);
  u8g2.drawRFrame(x, y, w, CARD_H, RADIUS);
  u8g2.setFont(FONT_MEDIUM);
  u8g2.setCursor(x + 10, y + 4);
  u8g2.print(label);
  bool isThisFocus = (modeFromMenuIndex(menuIndex) == current_Mode);
  bool isActive = (isThisFocus && current == ACTIVE_MODE);
  const char *status = isActive ? "ACTIVE" : "DEACTIVE";
  u8g2.setFont(FONT_SMALL);
  u8g2.setCursor(x + 10, y + 20);
  u8g2.print(status);
  bool isOn = (modeFromMenuIndex(menuIndex) == current_Mode) &&
              (current == ACTIVE_MODE);
  const int pillW = 24, pillH = 14;
  const int pillY = (y + ((CARD_H - pillH) / 2)) & ~1;
  drawPillToggle(x + w - pillW - 8, pillY, pillW, pillH, isOn);
}

static void renderStaticMenu(int focusIndex) {
  u8g2.clearBuffer();
  drawHeaderBar();
  drawCardAtCenter(128 / 2, focusIndex);
  drawPaginationDots(focusIndex);
  u8g2.sendBuffer();
}

static void animateToMenu(int fromIdx, int toIdx) {
  const int W = 128;
  const int STEPS = 8;
  const uint8_t DT = 8;
  const int dir = (toIdx > fromIdx) ? -1 : 1;
  for (int s = 0; s <= STEPS; s++) {
    float t = (float)s / (float)STEPS;
    float e =
        (t < 0.5f) ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3) / 2.0f;
    int shift = (int)(e * W + 0.5f);
    int centerFrom = (W / 2) + (-dir * shift);
    int centerTo = (W / 2) + (dir * (W - shift));
    u8g2.clearBuffer();
    drawHeaderBar();
    drawCardAtCenter(centerFrom, fromIdx);
    drawCardAtCenter(centerTo, toIdx);
    drawPaginationDots(toIdx);
    u8g2.sendBuffer();
    delay(DT);
  }
  renderStaticMenu(toIdx);
}

static void animateToggleKnobForFocus(int focusIdx, bool fromActive,
                                      bool toActive) {
  float start = (fromActive ? 0.0f : 1.0f);
  float end = (toActive ? 0.0f : 1.0f);
  const int STEPS = 8;
  const uint8_t FRAME_MS = 10;
  for (int s = 0; s <= STEPS; s++) {
    float t = (float)s / (float)STEPS;
    float e =
        (t < 0.5f) ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2) / 2.0f;
    float p = start + (end - start) * e;
    u8g2.clearBuffer();
    drawHeaderBar();
    const int W = 128;
    const int w = W - PAD * 2;
    const int x = W / 2 - w / 2;
    const int y = HEADER_H + 4;
    u8g2.setDrawColor(0);
    u8g2.drawBox(x + 1, y + 1, w - 2, CARD_H - 2);
    u8g2.setDrawColor(1);
    u8g2.drawRFrame(x, y, w, CARD_H, RADIUS);
    u8g2.setFont(FONT_MEDIUM);
    u8g2.setCursor(x + 10, y + 4);
    u8g2.print(kMenuLabels[focusIdx]);
    u8g2.setFont(FONT_SMALL);
    u8g2.setCursor(x + 10, y + 20);
    u8g2.print(toActive ? "ACTIVE" : "DEACTIVE");
    const int pillW = 24, pillH = 14;
    const int pillY = (y + ((CARD_H - pillH) / 2)) & ~1;
    const int pillX = x + w - pillW - 8;
    drawPillOutline(pillX, pillY, pillW, pillH);
    drawPillKnob(pillX, pillY, pillW, pillH, p);
    drawPaginationDots(focusIdx);
    u8g2.sendBuffer();
    delay(FRAME_MS);
  }
  renderStaticMenu(focusIdx);
}

static void menuPrev() {
  int fromIdx = menuIndexFromMode(current_Mode);
  int toIdx = (fromIdx == 0) ? (kMenuCount - 1) : (fromIdx - 1);
  current_Mode = modeFromMenuIndex(toIdx);
  animateToMenu(fromIdx, toIdx);
}

static void menuNext() {
  int fromIdx = menuIndexFromMode(current_Mode);
  int toIdx = (fromIdx == (kMenuCount - 1)) ? 0 : (fromIdx + 1);
  current_Mode = modeFromMenuIndex(toIdx);
  animateToMenu(fromIdx, toIdx);
}

static void menuToggleActive() {
  int focus = menuIndexFromMode(current_Mode);
  bool wasActive = (current == ACTIVE_MODE);
  current = wasActive ? DEACTIVE_MODE : ACTIVE_MODE;
  initialize_Radios();
  animateToggleKnobForFocus(focus, wasActive, !wasActive);
}

void rfclownSetup() {
  Serial.begin(115200);
  esp_bt_controller_deinit();
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_wifi_disconnect();

  u8g2.setFontPosTop();
  current = DEACTIVE_MODE;
  current_Mode = WiFi_MODULE;

  initialize_Radios();
  renderStaticMenu(0);
}

void rfclownLoop() {
  unsigned long now = millis();
  if (now - lastPressTime > debounceDelay) {
    if (digitalRead(BTN_PIN_LEFT) == LOW) {
      lastPressTime = now;
      menuPrev();
    } else if (digitalRead(BTN_PIN_RIGHT) == LOW) {
      lastPressTime = now;
      menuNext();
    } else if (digitalRead(BUTTON_UP_PIN) == LOW ||
               digitalRead(BUTTON_DOWN_PIN) == LOW) {
      lastPressTime = now;
      menuToggleActive();
    }
  }

  if (current == ACTIVE_MODE) {
    if (current_Mode == BLE_MODULE) {
      int randomIndex =
          random(0, sizeof(ble_channels) / sizeof(ble_channels[0]));
      byte channel = ble_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    } else if (current_Mode == Bluetooth_MODULE) {
      int randomIndex =
          random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
      byte channel = bluetooth_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    } else if (current_Mode == WiFi_MODULE) {
      int randomIndex =
          random(0, sizeof(WiFi_channels) / sizeof(WiFi_channels[0]));
      byte channel = WiFi_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    } else if (current_Mode == USB_WIRELESS_MODULE) {
      int randomIndex = random(0, sizeof(usbWireless_channels) /
                                      sizeof(usbWireless_channels[0]));
      byte channel = usbWireless_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    } else if (current_Mode == VIDEO_TX_MODULE) {
      int randomIndex = random(0, sizeof(videoTransmitter_channels) /
                                      sizeof(videoTransmitter_channels[0]));
      byte channel = videoTransmitter_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    } else if (current_Mode == RC_MODULE) {
      int randomIndex = random(0, sizeof(rc_channels) / sizeof(rc_channels[0]));
      byte channel = rc_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    } else if (current_Mode == ZIGBEE_MODULE) {
      int randomIndex =
          random(0, sizeof(zigbee_channels) / sizeof(zigbee_channels[0]));
      byte channel = zigbee_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    } else if (current_Mode == NRF24_MODULE) {
      int randomIndex =
          random(0, sizeof(nrf24_channels) / sizeof(nrf24_channels[0]));
      byte channel = nrf24_channels[randomIndex];
      RadioA.setChannel(channel);
      RadioB.setChannel(channel);
      RadioC.setChannel(channel);
    }
  }
}

void cleanup() {
  current = DEACTIVE_MODE;
  initialize_Radios();
  u8g2.setFontPosBaseline();
}

} // namespace RFClown
