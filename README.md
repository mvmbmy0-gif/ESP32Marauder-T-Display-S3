<div align="center">

# 🇺🇦 Проєкт розроблений українцем / Project developed by a Ukrainian 🇺🇦

[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/badges/StandWithUkraine.svg)](https://stand-with-ukraine.pp.ua)

<p align="center">
  <img src="pictures/marauder_skull_patch_04_full_final.png" alt="ESP32 Marauder Logo" width="220">
</p>

# ESP32Marauder for LilyGO T-Display S3
### 📟 Standard Non-Touch Version / Звичайна версія БЕЗ тачскріну

> [!IMPORTANT]
> 🔴 **NON-TOUCH VERSION (ВЕРСІЯ БЕЗ СЕНСОРНОГО ЕКРАНУ)**  
> This firmware is tailored specifically for the standard **LilyGO T-Display S3 (Non-Touch)**.  
> Navigation is performed **exclusively via 2 physical onboard buttons**:  
> - **`BOOT (GPIO 0)`**: Short press = Select / Enter. Long press (>800ms) = Back / Exit.  
> - **`GPIO 14`**: Scroll down through menu items.  
>  
> Ця прошивка створена суто для звичайної плати **LilyGO T-Display S3 (Non-Touch, без сенсора)**.  
> Все керування здійснюється **виключно двома фізичними кнопками**!

[English Documentation](#-english) | [Українська версія](#-українська-версія)

---
</div>

## 📂 Структура репозиторію / Repository Structure

Репозиторій очищено від усіх сторонніх плат (залишено тільки **LilyGO T-Display S3**) та організовано у зручні папки:

```text
├── 🌟 all_in_one/           <- Універсальна папка: абсолютно всі файли проєкту в одному місці (бінарники, скрипти, код)
├── 📦 firmware_binaries/    <- Готові бінарники (merged.bin, firmware.bin, bootloader, partitions)
├── ⚡ flashing_tools/       <- Усе для прошивки: готові скрипти (.sh, .bat) та Web Flasher гід
├── 💻 esp32_marauder/       <- Чистий вихідний код прошивки під LilyGO T-Display S3
├── 🖼️ pictures/             <- Логотип проєкту
├── 📄 platformio.ini        <- Конфігурація для збірки в PlatformIO
└── 📄 README.md             <- Головна документація
```

---

<a name="english"></a>
## 🇬🇧 English

### ⚡ Overview
This is a dedicated, fully optimized port of **ESP32Marauder** specifically crafted for the **LilyGO T-Display S3 (Standard Non-Touch version)**. It features a parallel 8-bit ST7789 IPS LCD driver, two-button hardware navigation, protected RAM allocation, and silent background Wi-Fi scanning.

> [!NOTE]
> **No touch screen hardware is required or used.** All menus and functions are fully accessible via the 2 physical buttons.

### 🎮 Hardware Controls (Non-Touch):
- **BOOT Button (`GPIO 0`):**
  - **Single Click:** Select / Toggle / Confirm action.
  - **Long Press (>800ms):** Back / Exit current view / Stop attack.
- **Side Button (`GPIO 14`):**
  - **Single Click:** Scroll down to the next menu item.

### ✨ Key Features:
- **Parallel 8-bit ST7789 Display Driver:** High frame rate via 8080-I 8-bit bus, custom 320x170 landscape layout, and hardware framebuffer readback for screenshots over serial (`screen` command).
- **Silent Background Auto-Scanning:** Sniffer and attack menus (`AP Clone Spam`, `Probe Req Flood`, `Deauth Flood`, `Select APs`, etc.) immediately launch in operational mode, scanning channels in the background and populating AP lists automatically.
- **RuntimeMemoryGuard:** Internal memory protection ensuring stable operation and >200KB free heap during continuous scanning.
- **Evil Portal:** Embedded captive portal with local LittleFS file management and CLI support (`evilportal -c list`).

---

### 🖥️ Pinout (LilyGO T-Display S3)
| Signal | GPIO (ESP32-S3) | Description |
| :--- | :---: | :--- |
| **`BOARD_POWERON_PIN`** | `GPIO 15` | Main board power enable (**MUST be HIGH** for display) |
| **`TFT_BL`** | `GPIO 38` | Backlight control (Active HIGH) |
| **`TFT_CS`** | `GPIO 6` | Chip Select (Active LOW) |
| **`TFT_DC`** | `GPIO 7` | Data / Command register select |
| **`TFT_RST`**| `GPIO 5` | Hardware reset |
| **`TFT_WR`** | `GPIO 8` | Write strobe clock |
| **`TFT_RD`** | `GPIO 9` | Read strobe clock (screenshot readback) |
| **`TFT_D0..D7`** | `GPIO 39, 40, 41, 42, 45, 46, 47, 48` | 8-bit parallel data bus |
| **`BUTTON_1`** | `GPIO 0` | BOOT button (Select / Back) |
| **`BUTTON_2`** | `GPIO 14` | Navigation button (Scroll down) |

---

### 🚀 1-Click Flashing (Web Browser)
1. Open [ESP Web Flasher](https://espressif.github.io/esptool-js/) in Chrome / Edge.
2. Connect your LilyGO T-Display S3 via USB-C.
3. Choose `firmware_binaries/esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin` (or from `all_in_one/`) at offset **`0x0`**.
4. Click **Program**. Done!

---

<a name="українська-версія"></a>
## 🇺🇦 Українська версія

### ⚡ Огляд
Це спеціалізований, повністю оптимізований порт **ESP32Marauder** для стандартної плати **LilyGO T-Display S3 (версія БЕЗ сенсорного екрану / Non-Touch)**.  
Прошивка містить драйвер 8-бітного паралельного IPS-екрану ST7789 (320x170), надійне двокнопкове керування та безшумне фонове сканування Wi-Fi мереж.

> [!NOTE]
> **Тачскрін не потрібен!** Усе керування оптимізовано під дві зручні фізичні кнопки на корпусі плати.

### 🎮 Керування кнопками (Non-Touch):
- **Кнопка BOOT (`GPIO 0`):**
  - **Короткий клік:** Вибір / Вхід у меню / Підтвердження.
  - **Довге утримання (>800 мс):** Назад / Вихід з поточного меню / Зупинка сканування чи атаки.
- **Права кнопка (`GPIO 14`):**
  - **Короткий клік:** Гортання списку пунктів меню вниз (Scroll Down).

---

### 📁 Структура папок:
1. **`all_in_one/`** (⭐ **Універсальна папка**):
   - Містить абсолютно всі файли проєкту разом: готові бінарники, скрипти прошивки, конфігурацію дисплея, PlatformIO конфіг та повний архів вихідного коду.
2. **`firmware_binaries/`**:
   - `esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin` — повний злитий образ прошивки для запису з адреси `0x0`.
   - `esp32_marauder_v1.20.0_lilygo_t_display_s3.bin` — бінарник програми для адреси `0x10000`.
   - `bootloader.bin` (адреса `0x0000`) та `partitions.bin` (адреса `0x8000`).
3. **`flashing_tools/`**:
   - Скрипти для швидкої прошивки в один клік: `flash_firmware.sh` (macOS/Linux) та `flash_firmware.bat` (Windows).
   - Детальна інструкція з прошивки через веб-браузер або термінал.
4. **`esp32_marauder/`**:
   - Повний вихідний код, очищений від усіх зайвих плат і налаштований суто під LilyGO T-Display S3 Non-Touch.

---

### 🛠️ Збірка проекту з вихідного коду (PlatformIO)
```bash
git clone https://github.com/mvmbmy0-gif/ESP32Marauder-T-Display-S3.git
cd ESP32Marauder-T-Display-S3
pio run -e lilygo-t-display-s3 -t upload
```

---

### 🙏 Подяки та ліцензія
- Базується на оригінальному проекті **[ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder)** від **[JustCallMeKoko](https://github.com/justcallmekoko)**.
- Проєкт поширюється за відкритою ліцензією **MIT License** — вільне використання, модифікація та розповсюдження із зазначенням авторства.
