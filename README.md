# iPod ESP - ESP32-P4 iPod Classic Media Player

A retro recreation of the Apple iPod Classic running on the ESP32-P4 RISC-V SoC with LVGL 9, MIPI-DSI 480x800 Display, and a Virtual Polar-Math Click Wheel.

## Key Features

* Authentic Split-Screen UI: 480x400 top display featuring a 2-column menu with iPod blue gradient selection and real-time contextual preview pane.
* Virtual Polar Touch Click Wheel: 480x400 bottom chassis translating capacitive touch into polar coordinates, enabling rotary swirling and button clicks.
* Dual-Core FreeRTOS Architecture:
  * Core 0: LVGL 9 rendering loop, touch processing, and state synchronization.
  * Core 1: Dedicated audio decoding task (minimp3_ex) streaming directly to the ES8311 I2S DMA codec.
* Multi-Format Media Support:
  * Music: MP3, WAV, FLAC, M4A, AAC from MicroSD card with metadata parsing.
  * Photos: JPEG, PNG, BMP image viewer with 4-second slideshow.
  * Videos: 480x360 MJPEG video playback with scrub slider and timestamp indicators.

## Hardware Requirements

* Board: Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3
* Display: 4.3-inch ST7701 MIPI-DSI (480x800 portrait)
* Touch: Goodix GT911 capacitive touch over I2C (GPIO 7 / 8)
* Audio: ES8311 I2S Audio Codec with 3.5mm headphone jack
* Storage: MicroSD Card (FAT32 or exFAT) in 4-bit SDMMC slot

## Repository Structure

```text
ipodesp/
├── ipod_player/               Main ESP-IDF firmware project
│   ├── main/
│   │   ├── main.c             System entry and dual-core task dispatcher
│   │   ├── bsp_init.c         Hardware init (MIPI-DSI, GT911, SDMMC, I2S)
│   │   ├── audio/             Audio playback engine and minimp3_ex decoder
│   │   ├── media/             SD card media scanner and video engine
│   │   └── ui/                LVGL 9 UI (Click wheel, split menu, now playing)
│   ├── partitions.csv         Custom partition table
│   └── CMakeLists.txt         ESP-IDF project build configuration
├── schematic/                 Hardware schematic
└── README.md                  Project overview
```

## Quick Start

### 1. Set up ESP-IDF (v5.3+)
```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
. ./export.sh
```

### 2. Clone & Build
```bash
git clone https://github.com/AahanDoesGit/Esp-iPod.git
cd Esp-iPod/ipod_player
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### 3. Setup MicroSD Card
Create the following folders on your FAT32/exFAT MicroSD card:
* `/Music/` - Copy your `.mp3`, `.wav`, or `.flac` audio tracks.
* `/Photos/` - Copy your `.jpg` or `.png` images.
* `/Videos/` - Copy your `.mjpeg` video files.
