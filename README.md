# LARP_BOX

Custom firmware for ESP32 portable multi-tool builds sporting triple nRF24L01+ transceivers and a 128x64 OLED. Combines **nRFBox** (spectrum tools, BLE/WiFi recon, packet injection) and **RF-Clown v2.0** (multi-protocol RF carrier generator) into a single unified build with on-the-fly mode switching.

---

## Pre-compiled Binary (Flash Download Tool)

A single pre-built binary is ready in the [`Pre-compiled Bin`](Pre-compiled%20Bin) folder so you can flash directly without installing development toolchains.

### Flashing with Espressif Flash Download Tool:
1. Open **Flash Download Tool** (Select Chip Type: `ESP32`, WorkMode: `develop`).
2. Load the binary file:

| File | Checkbox | Address Offset |
| :--- | :---: | :--- |
| `Pre-compiled Bin/LARP_BOX_full_0x0000.bin` | `[x]` | `0x0` |

3. Flashing Settings:
   * **SPI SPEED:** `40MHz`
   * **SPI MODE:** `DIO`
   * **FLASH SIZE:** `32Mbit` (or `4MB` / auto-detect)
   * Select your ESP32 **COM Port**, set baud to `921600` (or `115200`), hold `BOOT` if needed, and click **START**.

---

## What It Does

When the board boots (after the startup cat animation), you get a boot screen to pick your operating mode:

### 1. nRFBox (Recon & Analysis Mode)
A custom carousel UI (MOGU OS-style physics, smooth horizontal slide, iris transitions) giving you 12 dedicated tools:

* **Scanner** & **Analyzer**: 2.4 GHz spectrum scanner and live bar graph / waterfall across all 128 RF channels using the nRF24 frontend.
* **BLE Scan**: Sniffs advertising BLE packets around you. Shows device names, MAC addresses, and live RSSI meters.
* **WiFi Scan**: Scans 2.4 GHz BSSIDs with security type, channel, and RSSI (includes text marquee for long SSIDs).
* **Deauther**: ESP32 raw frame injection (`esp_wifi_internal_tx`) sending 802.11 deauth frames to targeted access points.
* **Sour Apple**: Advertises spoofed Apple proximity pairing / AirDrop popups to iOS devices.
* **BLE Spoofer**: Custom BLE advertisement flooder.
* **BLE Jammer**: Fast channel-hopping RF jammer focused on BLE advertising and data frequencies.
* **WLAN Jammer**: Multi-radio 2.4 GHz WiFi channel noise generator across three nRF24 modules.
* **Proto Kill**: Disrupts standard 2.4 GHz proprietary RF protocol packets.
* **Settings & Diagnostics**:
  * Screen brightness adjustment.
  * **OLED Controller Toggle**: Switch between **0.96" SSD1306** and **1.3" SH1106** displays directly from the menu (persisted in EEPROM).
  * Onboard RGB NeoPixel toggle (GPIO 14).
  * **Radio Hardware Self-Test**: Validates SPI comms, registers, PA levels, and carrier detection on all 3 connected nRF24 chips (Radio A, B, and C).
  * **SD Card OTA Updater**: Flash new `firmware.bin` files straight off a FAT32 MicroSD card.
* **Switch Mode**: Dumps you back to the boot menu without touching the physical reset button.

### 2. RF-Clown (Multi-Protocol Carrier Jammer)
A dedicated, card-based UI that drives all three nRF24 radios simultaneously with high-power carrier output across targeted ISM bands:

* **Protocols covered**: WiFi, Video TX (analog 5.8G/2.4G FPV downlinks), RC (drone/plane control links), BLE, Bluetooth Classic, USB Wireless (keyboards/mice), Zigbee (smart home/IoT), and raw NRF24 links.
* **Dual-state toggle**: Flip jamming ON/OFF per protocol with live spectrum bin animation.
* **Quick Exit**: Tap `SELECT` anytime (`Exit:SEL` in the header) to kill all carrier transmissions safely and return to the main launcher.

---

## Hardware Pinout

Built around a standard ESP32-WROOM-32 (30-pin or 38-pin Dev Module).

```
                            ESP32 DEV MODULE
                             +------------+
                        3V3 -|            |- GND
                         EN -|            |- GPIO 23 (SPI MOSI -> All Radios)
                    GPIO 36 -|            |- GPIO 22 (I2C SCL  -> OLED)
                    GPIO 39 -|            |- GPIO 1 (TX)
                    GPIO 34 -|            |- GPIO 3 (RX)
                    GPIO 35 -|            |- GPIO 21 (I2C SDA  -> OLED)
 (BTN UP)           GPIO 26 -|            |- GPIO 19 (SPI MISO -> All Radios)
 (BTN DOWN)         GPIO 33 -|            |- GPIO 18 (SPI SCK  -> All Radios)
 (BTN SELECT)       GPIO 32 -|            |- GPIO 5  (Radio A CE / SD CS)
 (BTN RIGHT)        GPIO 25 -|            |- GPIO 17 (Radio A CSN)
 (BTN LEFT)         GPIO 27 -|            |- GPIO 16 (Radio B CE)
 (RGB LED)          GPIO 14 -|            |- GPIO 4  (Radio B CSN)
 (Radio C CE)       GPIO 15 -|            |- GPIO 0
 (Radio C CSN)      GPIO 2  -|            |- GPIO 2
                             +------------+
```

### Pin Table Summary

| Function | ESP32 GPIO | Connected Component |
| :--- | :--- | :--- |
| **Buttons** | GPIO 26 | Button UP (active LOW, internal pull-up) |
| | GPIO 33 | Button DOWN (active LOW, internal pull-up) |
| | GPIO 27 | Button LEFT (active LOW, internal pull-up) |
| | GPIO 25 | Button RIGHT (active LOW, internal pull-up) |
| | GPIO 32 | Button SELECT (active LOW, internal pull-up) |
| **Display** | GPIO 21 | OLED SDA (SSD1306 / SH1106) |
| | GPIO 22 | OLED SCL (400 kHz Fast I2C) |
| **Status LED** | GPIO 14 | WS2812B / NeoPixel DATA |
| **Shared SPI** | GPIO 18 | SPI SCK (All 3x nRF24 + SD) |
| | GPIO 19 | SPI MISO (All 3x nRF24 + SD) |
| | GPIO 23 | SPI MOSI (All 3x nRF24 + SD) |
| **Radio A** | GPIO 5 | CE |
| | GPIO 17 | CSN |
| **Radio B** | GPIO 16 | CE |
| | GPIO 4 | CSN |
| **Radio C** | GPIO 15 | CE |
| | GPIO 2 | CSN |
| **MicroSD** | GPIO 5 | CS |

> **Power Supply Note:** Running three nRF24L01+ modules at `RF24_PA_MAX` draws significant current spikes on the 3.3V rail. Do **not** power all three modules directly from the onboard ESP32 3.3V pin—use a dedicated 3.3V regulator (AMS1117-3.3 or buck converter) with at least a 10uF - 47uF capacitor across the VCC and GND pins of each radio module to prevent brownouts.

---

## Build & Flash

This project uses **PlatformIO** with a custom partition scheme (`huge_app.csv`) to fit the full BLE stack, WiFi stack, and animation frames inside flash.

### Prerequisites
* [PlatformIO Core (CLI)](https://platformio.org/install/cli) or PlatformIO IDE inside VS Code.
* ESP32 USB cable connected to your machine.

### Build & Upload Commands

```bash
# Clone or enter the project directory
cd nRFBox

# Build the project
pio run

# Flash to your ESP32
pio run --target upload

# Open the serial monitor (115200 baud)
pio run --target monitor
```

If you prefer building inside the Arduino IDE, configure the board settings:
* **Board:** `ESP32 Dev Module`
* **Partition Scheme:** `Huge APP (3MB No OTA / 1MB SPIFFS)`
* **CPU Frequency:** `240MHz`
* **Upload Speed:** `921600`
* Install dependencies via Library Manager: `RF24`, `U8g2`, `Adafruit NeoPixel`.

---

## Controls

| Mode | Control | Action |
| :--- | :--- | :--- |
| **Boot Selector** | `LEFT` / `RIGHT` or `UP` / `DOWN` | Highlight nRFBox or RF-Clown |
| | `SELECT` | Launch selected mode |
| **nRFBox Carousel** | `LEFT` / `RIGHT` | Smooth carousel scroll between tools |
| | `SELECT` | Open active tool |
| | `SELECT` (Inside tool) | Return back to carousel |
| | Scroll to `Switch Mode` + `SELECT` | Return to Boot Selector |
| **RF-Clown** | `LEFT` / `RIGHT` | Select target protocol (WiFi, BLE, Video TX, RC, etc.) |
| | `UP` / `DOWN` | Toggle Jamming ON / OFF (animates toggle pill) |
| | `SELECT` | Immediate RF kill & return to Boot Selector |
| **Idle** | Any Button | Wake up from Scuba Cat screensaver |

---

## Credits & Upstream
* Original nRFBox firmware & concept by [cifertech/nrfbox](https://github.com/cifertech/nrfbox).
* RF-Clown jammer implementation by [cifertech/rfclown](https://github.com/cifertech/rfclown).
* Graphics driver powered by [olikraus/u8g2](https://github.com/olikraus/u8g2).
* 2.4 GHz radio driver powered by [nRF24/RF24](https://github.com/nRF24/RF24).

## Disclaimer
This project is for educational, research, and authorized security testing purposes only. Transmitting interference or deauthenticating clients on networks you do not own or have explicit permission to test is strictly illegal in most jurisdictions. Use responsibly.
