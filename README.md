<div align="center">

# 🇺🇦 Проєкт розроблений українцем / Project developed by a Ukrainian 🇺🇦

[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/badges/StandWithUkraine.svg)](https://stand-with-ukraine.pp.ua)

**ESP32Marauder port for LilyGO T-Display S3 (ESP32-S3 + Parallel 8-bit ST7789 Display)**

[English Documentation](#-english) | [Українська версія](#-українська-версія)

---
</div>

<a name="english"></a>
## 🇬🇧 English

### ⚡ Overview
This is a dedicated, fully optimized port of **ESP32Marauder** specifically crafted for the **LilyGO T-Display S3** (ESP32-S3). It features a parallel 8-bit display driver for the ST7789 IPS LCD (320x170), two-button hardware navigation, and silent real-time background Wi-Fi scanning across all attack and sniffer menus.

### ✨ Key Features
- **Parallel 8-bit ST7789 Display Driver:** Fast frame rate via 8080-I 8-bit bus, custom landscape layout, and hardware framebuffer readback for screenshots over serial (`screen` command).
- **Two-Button UI Navigation:** Intuitive navigation designed for non-touch screens:
  - **Left / Boot Button (`GPIO 0`):** Short press = `OK / Select / Toggle`, Long press (>800ms) = `Back / Exit`.
  - **Right Button (`GPIO 14`):** Short press = `Down / Scroll`.
- **Silent Background Auto-Scanning:** Menus (`AP Clone Spam`, `Probe Req Flood`, `Deauth Flood`, `Select APs`, `Karma`, `SAE Commit`, etc.) open directly into the stock control view with green buttons, silently scan Wi-Fi channels in the background, and dynamically populate discovered Access Points in real time.
- **RuntimeMemoryGuard:** Internal heap monitoring and memory safety guard ensuring zero crashes and constant >200KB free heap.
- **Evil Portal:** Embedded captive portal with local LittleFS file management and CLI support (`evilportal -c list`).

---

### 🖥️ Display Driver & Pinout (LilyGO T-Display S3)
The onboard **ST7789V** panel operates on an Intel 8080 8-bit parallel bus:

| Signal | GPIO (ESP32-S3) | Description |
| :--- | :---: | :--- |
| **`BOARD_POWERON_PIN`** | `GPIO 15` | Main board power enable (**MUST be HIGH** for display to turn on) |
| **`TFT_BL`** | `GPIO 38` | Backlight control (Active HIGH) |
| **`TFT_CS`** | `GPIO 6` | Chip Select (Active LOW) |
| **`TFT_DC`** | `GPIO 7` | Data / Command register select |
| **`TFT_RST`**| `GPIO 5` | Hardware reset |
| **`TFT_WR`** | `GPIO 8` | Write strobe clock |
| **`TFT_RD`** | `GPIO 9` | Read strobe clock (used for screenshot capture) |
| **`TFT_D0..D7`** | `GPIO 39, 40, 41, 42, 45, 46, 47, 48` | 8-bit parallel data bus |

Configuration file: `User_Setup_lilygo_t_display_s3.h` (used by `TFT_eSPI`).

---

### 📦 Pre-compiled Release Binaries
Available in the [GitHub Releases](https://github.com/mvmbmy0-gif/ESP32Marauder-T-Display-S3/releases/latest) section and in `Release Bins/`:

1. **`esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin`** (⭐ **Recommended**)
   - Complete 1-click image (`bootloader` + `partitions` + `boot_app0` + `firmware`).
   - Flash at offset **`0x0000`** in [ESP Web Flasher](https://espressif.github.io/esptool-js/) or using esptool:
     ```bash
     esptool.py --chip esp32s3 -p <PORT> write_flash 0x0000 esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin
     ```
2. **`esp32_marauder_v1.20.0_lilygo_t_display_s3.bin`** — Firmware only (flash offset `0x10000`).
3. **`bootloader.bin`** — Offset `0x0000`.
4. **`partitions.bin`** — Offset `0x8000`.
5. **`esp32_marauder_lilygo_t_display_s3_source.zip`** — Clean source code package for LilyGO T-Display S3 only (no legacy bloat).

---

### 🛠️ Building with PlatformIO
```bash
git clone https://github.com/mvmbmy0-gif/ESP32Marauder-T-Display-S3.git
cd ESP32Marauder-T-Display-S3
pio run -e lilygo-t-display-s3 -t upload
```

---
---

<a name="українська-версія"></a>
## 🇺🇦 Українська версія

### ⚡ Огляд
Це спеціалізований, повністю оптимізований порт **ESP32Marauder** для плати **LilyGO T-Display S3** (ESP32-S3). Прошивка включає підтримку 8-бітного паралельного IPS-екрану ST7789 (320x170), повноцінне двокнопкове керування та безшумне фонове сканування Wi-Fi мереж у реальному часі.

### ✨ Основні можливості:
- **Драйвер паралельного дисплею ST7789 (8-bit):** Швидкісне оновлення через шину 8080-I, кастомна розмітка landscape 320x170 та апаратне зчитування пікселів для зняття скріншотів через серійний порт (`screen`).
- **Двокнопкова навігація:** Повний контроль без сенсорного екрана:
  - **Ліва кнопка / Boot (`GPIO 0`):** Короткий клік — `OK / Вибір`, довге утримання (>800 мс) — `Назад / Вихід / Зупинка`.
  - **Права кнопка (`GPIO 14`):** Короткий клік — `Вниз / Прокрутка списку`.
- **Фонове автосканування мереж:** Меню (`AP Clone Spam`, `Probe Req Flood`, `Deauth Flood`, `Select APs`, `Karma` тощо) відразу відкривають стокову панель із зеленими кнопками, сканують ефір у фоні та автоматично додають знайдені точки доступу в реальному часі.
- **RuntimeMemoryGuard:** Захист від падінь через переповнення пам'яті, контроль внутрішньої купи (постійно >200 КБ вільної RAM).
- **Evil Portal:** Підтримка локального флеш-сховища LittleFS для сторінок порталу та CLI-команда `evilportal -c list`.

---

### 🖥️ Розпіновка дисплею (LilyGO T-Display S3):
| Сигнал | GPIO (ESP32-S3) | Призначення |
| :--- | :---: | :--- |
| **`BOARD_POWERON_PIN`** | `GPIO 15` | Головне живлення плати (**обов'язково HIGH**, інакше екран не вмикається) |
| **`TFT_BL`** | `GPIO 38` | Підсвічування екрана (Active HIGH) |
| **`TFT_CS`** | `GPIO 6` | Chip Select (Active LOW) |
| **`TFT_DC`** | `GPIO 7` | Data / Command |
| **`TFT_RST`**| `GPIO 5` | Скид (Reset) |
| **`TFT_WR`** | `GPIO 8` | Такт запису |
| **`TFT_RD`** | `GPIO 9` | Такт читання (для зняття скріншотів) |
| **`TFT_D0..D7`** | `GPIO 39, 40, 41, 42, 45, 46, 47, 48` | 8-бітна паралельна шина даних |

---

### 📦 Завантаження та прошивка
Усі бінарники доступні у вкладці **[Releases](https://github.com/mvmbmy0-gif/ESP32Marauder-T-Display-S3/releases/latest)**:
1. **`esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin`** (⭐ **Рекомендовано**) — готовий повний образ для прошивки в 1 клік з адреси **`0x0000`** через браузерний [ESP Web Flasher](https://espressif.github.io/esptool-js/) або `esptool`.
2. **`esp32_marauder_v1.20.0_lilygo_t_display_s3.bin`** — файл прошивки для адреси `0x10000`.
3. **`bootloader.bin`** — адреса `0x0000`.
4. **`partitions.bin`** — адреса `0x8000`.
5. **`esp32_marauder_lilygo_t_display_s3_source.zip`** — очищений архів з вихідним кодом тільки для LilyGO T-Display S3.
