<div align="center">

# 🇺🇦 Проєкт розроблений українцем / Project developed by a Ukrainian 🇺🇦

[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/badges/StandWithUkraine.svg)](https://stand-with-ukraine.pp.ua)

<p align="center">
  <img src="pictures/marauder_skull_patch_04_full_final.png" alt="ESP32 Marauder Logo" width="220">
</p>

# ESP32Marauder for LilyGO T-Display S3
### 📟 Standard Non-Touch Version + 📡 IR Blaster Edition

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
├── 💻 esp32_marauder/       <- Чистий вихідний код прошивки під LilyGO T-Display S3 (+ IR Blaster)
├── 🖼️ pictures/             <- Логотип проєкту
├── 📄 platformio.ini        <- Конфігурація для збірки в PlatformIO
└── 📄 README.md             <- Головна документація
```

---

<a name="english"></a>
## 🇬🇧 English

### ⚡ Overview
This is a dedicated, fully optimized port of **ESP32Marauder** specifically crafted for the **LilyGO T-Display S3 (Standard Non-Touch version)**. It features a parallel 8-bit ST7789 IPS LCD driver, two-button hardware navigation, protected RAM allocation, silent background Wi-Fi scanning, and a built-in **IR Blaster transmitter on GPIO 20**.

> [!NOTE]
> **No touch screen hardware is required or used.** All menus and functions are fully accessible via the 2 physical buttons.

### 🎮 Hardware Controls (Non-Touch):
- **BOOT Button (`GPIO 0`):**
  - **Single Click:** Select / Toggle / Confirm action.
  - **Long Press (>800ms):** Back / Exit current view / Stop attack.
- **Side Button (`GPIO 14`):**
  - **Single Click:** Scroll down to the next menu item.

### ✨ Key Features:
- **📡 Hardware IR Blaster Transmitter (GPIO 20):**
  - Connect your IR LED through a transistor to **GPIO 20** for maximum range.
  - **Universal TV-B-Gone:** Multi-brand power-off database (Samsung, LG, Sony, Panasonic, Philips, TCL, Hisense, etc.).
  - **IR Jammer:** High-power multi-frequency IR jamming noise generator.
  - **Panasonic TV Control:** Power toggle, Mute, Volume +/-, Channel +/-, Input/AV, OK, Menu (Kaseikyo 48-bit protocol at 36.7 kHz).
  - **Window Fairy Lights ("Гірлянда на вікно"):** Full control over all 8 lighting modes (1..8), Brightness DIM +/-, Power ON/OFF, and Timer (NEC 38 kHz with dual burst compatibility).
  - **SP611E Smart Pixel LED Strip:** 38-key remote emulation (Power, 142 dynamic modes, sound-reactive Music Mode, Speed, Brightness, Colors).
  - **3-Second Periodic Test Loop:** Automatically sends test signals every 3 seconds to test range and receiver responsiveness without touching buttons.
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
| **`IR_TX`** | **`GPIO 20`** | **Infrared LED Transmitter output (Hardware 38kHz PWM)** |
| **`IR_RX`** | **`GPIO 16`** | **Infrared Receiver input (Pulse decoder / Sniffer / Replayer)** |

---

### 💻 IR CLI Commands (Serial Terminal)
```text
ir tv <power|mute|vol+|vol-|ch+|ch-|input|ok|menu>
ir fairy <on|off|1..8|dim+|dim-|timer>
ir led <on|off|red|green|blue|white|flash|fade|strobe|smooth|bright+|bright->
ir nec <32-bit hex>              (e.g. ir nec 0x00FF02FD)
ir panasonic <48-bit hex>        (e.g. ir panasonic 0x40040100BCBD)
ir loop start [fairy|led|tv|all] [interval_ms]  (starts automated loop, default 3000ms)
ir loop stop
```

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
Це спеціалізований, повністю оптимізований порт **ESP32Marauder** для стандартної плати **LilyGO T-Display S3 (версія БЕЗ сенсорного екрану / Non-Touch)** з підтримкою **ІЧ-передавача на GPIO 20**.  
Прошивка містить драйвер 8-бітного паралельного IPS-екрану ST7789 (320x170), надійне двокнопкове керування, безшумне фонове сканування Wi-Fi мереж та меню ІЧ-пульта.

> [!NOTE]
> **Тачскрін не потрібен!** Усе керування оптимізовано під дві зручні фізичні кнопки на корпусі плати.

### 🎮 Керування кнопками (Non-Touch):
- **Кнопка BOOT (`GPIO 0`):**
  - **Короткий клік:** Вибір / Вхід у меню / Підтвердження.
  - **Довге утримання (>800 мс):** Назад / Вихід з поточного меню / Зупинка сканування чи атаки.
- **Права кнопка (`GPIO 14`):**
  - **Короткий клік:** Гортання списку пунктів меню вниз (Scroll Down).

---

### 📡 Можливості ІЧ-системи (TX: GPIO 20, RX: GPIO 16):
- **Універсальний TV-B-Gone:**
  - Об'єднаний режим `All TVs (EU -> NA -> Univ)`: спочатку транслюються всі коди Європи (`EUpowerCodes`), потім коди Північної Америки (`NApowerCodes`), а потім універсальні коди.
  - Окремі режими `Region EU (+ Univ)` та `Region NA (+ Univ)`.
  - Можливість зупинки в будь-який момент кнопкою **`BOOT` (IO 0)**.
- **ІЧ-глушилка (IR Jammer):**
  - Режими: `Sweep Jam (36-56k)`, `Basic Jam (38k)` та `Random Jitter Jam`.
  - Екран атаки з лічильником надісланих пакетів.
- **Зчитувач та клонер сигналів (IR Read / Sniffer на GPIO 16):**
  - Приймає сигнали з будь-якого пульта в реальному часі.
  - Декодує протоколи `NEC`, `Sony SIRC`, `Raw Pulses` з відображенням Hex Address, Command та 32-bit Raw.
  - **Клонування / Replay:** Натискання кнопки `IO 14` одразу ретранслює щойно зчитаний код через передавач на GPIO 20!
- **Папка `Custom Remotes`:**
  - **`SP611E LED Remote`:** Повний пульт для адресної LED стрічки з точними кодами з оригінального пульта (Power ON `0xFFA25D`, OFF `0xFFE21D`, Brightness +/- `0xFF5AA5`/`0xFF10EF`, Flash `0xFF5EA1`, Strobe `0xFF0CF3`, Fade `0xFF16E9`, Smooth `0xFF0DF2`, Speed, кольори Red/Green/Blue/White, режими Music та Auto).
  - **`Panasonic TV`:** Повноцінний пульт (Power, Mute, гучність +/-, канали +/-, AV/Input, OK, Menu) за протоколом Kaseikyo 48-bit.
  - **`Window Lights` (Гірлянда на вікно):** Усі 8 режимів, яскравість DIM +/-, таймер.
  - **Тестовий цикл кожні 3 секунди (3s Loop)** та **IR Blink Test**.
- **Меню `IR Config` (з автозбереженням в NVS пам'ять):**
  - Перемикання `TX Pin` (за замовчуванням **GPIO 20**).
  - Перемикання `RX Pin` (за замовчуванням **GPIO 16**).
  - Налаштування кількості повторів `TX Repeats` (0, 1, 2, 3, 5, 10).
  - Вибір несучої частоти `Carrier` (38k, 36k, 40k, 56k).
  - Вибір режиму глушилки `Jam Mode`.

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
