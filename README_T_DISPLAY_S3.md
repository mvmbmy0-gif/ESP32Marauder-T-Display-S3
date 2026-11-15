# ESP32Marauder — LilyGO T-Display S3 Port & Enhancements

Цей репозиторій містить повноцінний порт **ESP32Marauder** для плати **LilyGO T-Display S3** (ESP32-S3 + паралельний 8-бітний дисплей ST7789 320x170), а також систему фонового автосканування мереж та покращений захист пам'яті.

---

## 🖥️ 1. Драйвер дисплею (LilyGO T-Display S3)

Плата LilyGO T-Display S3 використовує контролер дисплею **ST7789V** з паралельною 8-бітною шиною даних Intel 8080 (8080-I), що забезпечує надзвичайно високу швидкість оновлення кадрів без навантаження на SPI.

### Розпіновка дисплею (Pinout)

| Сигнал | GPIO (ESP32-S3) | Опис |
| :--- | :---: | :--- |
| **TFT_CS** | `GPIO 6` | Chip Select (активний LOW) |
| **TFT_DC** | `GPIO 7` | Data / Command select |
| **TFT_RST** | `GPIO 5` | Hardware Reset |
| **TFT_WR** | `GPIO 8` | Write strobe |
| **TFT_RD** | `GPIO 9` | Read strobe (використовується для зчитування скріншотів) |
| **TFT_D0** | `GPIO 39` | Шина даних: Bit 0 |
| **TFT_D1** | `GPIO 40` | Шина даних: Bit 1 |
| **TFT_D2** | `GPIO 41` | Шина даних: Bit 2 |
| **TFT_D3** | `GPIO 42` | Шина даних: Bit 3 |
| **TFT_D4** | `GPIO 45` | Шина даних: Bit 4 |
| **TFT_D5** | `GPIO 46` | Шина даних: Bit 5 |
| **TFT_D6** | `GPIO 47` | Шина даних: Bit 6 |
| **TFT_D7** | `GPIO 48` | Шина даних: Bit 7 |
| **TFT_BL** | `GPIO 38` | Backlight (підсвічування екрана, активний HIGH) |
| **POWER_ON**| `GPIO 15` | Головний ключ живлення плати (обов'язково `HIGH` для старту дисплею) |

### Конфігурація TFT_eSPI (`User_Setup_lilygo_t_display_s3.h`)
```c
#define USER_SETUP_ID 206
#define ST7789_DRIVER
#define TFT_RGB_ORDER TFT_BGR
#define TFT_WIDTH  170
#define TFT_HEIGHT 320
#define TFT_PARALLEL_8_BIT

#define TFT_CS    6
#define TFT_DC    7
#define TFT_RST   5
#define TFT_WR    8
#define TFT_RD    9

#define TFT_D0   39
#define TFT_D1   40
#define TFT_D2   41
#define TFT_D3   42
#define TFT_D4   45
#define TFT_D5   46
#define TFT_D6   47
#define TFT_D7   48

#define TFT_BL   38
#define TFT_BACKLIGHT_ON HIGH
```

### Патч для зняття скріншотів (`TFT_eSPI.cpp`)
У стандартній бібліотеці `TFT_eSPI` зчитування пікселів (`readPixel` та `readRect`) не підтримувало паралельний ST7789. У нашому драйвері додано підтримку `ST7789_DRIVER`, що дозволяє через команду `screen` або веб-інтерфейс знімати повноколірні BMP-скріншоти з пам'яті дисплея на льоту.

---

## 🕹️ 2. Двокнопкове керування (Two-Button UI)

Оскільки екран без тачскріну, все керування повністю реалізовано на 2 фізичних кнопках:
- **Ліва кнопка / Boot Button (`GPIO 0`)**:
  - *Коротке натискання*: Вибір / Вхід / Підтвердження (`OK`).
  - *Довге утримання (Hold)*: Назад / Вихід у попереднє меню / Скасування сканування (`Exit / Back`).
- **Права кнопка (`GPIO 14`)**:
  - *Коротке натискання*: Переміщення курсору вниз по меню (`Down / Scroll`).

---

## ⚡ 3. Фонове автосканування (Background Scan)

Раніше у багатьох меню (наприклад, `AP Clone Spam`, `Deauth Flood`, `Select APs`) при вході відображалося порожнє вікно з помилкою `No APs found. Run scan first`.

Тепер:
- При відкритті будь-якого меню, що потребує точок доступу (`AP Clone Spam`, `Probe Req Flood`, `SAE Commit`, `CSA`, `Quiet Time`, `Deauth Flood`, `Bad Msg`, `Karma`, `Select APs`, `View AP Info`), автоматично відкривається **стокова панель керування** (з кнопками `Back`, `Review`, `Rescan APs`, `Select All`, `Clear All`).
- Кнопка сканування оформлена в єдиному стилі (зелений колір як `Select All`, без зайвих символів `+`).
- У фоновому режимі (`background_scan = true`) відразу стартує сканування ефіру на всіх каналах 1–14 без виведення сміття в буфер екрана.
- Усі знайдені мережі динамічно з'являються знизу списку в реальному часі.

---

## 📦 4. Готові скомпільовані бінарники (Release Bins)

У папці `Release Bins/` доступні готові файли:

1. **`esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin`** (Повний об'єднаний бінарник):
   - Містить `bootloader`, `partitions`, `boot_app0` та `firmware`.
   - Прошивається за **1 клік** за адресою `0x0000`:
   ```bash
   esptool.py --chip esp32s3 -p <PORT> write_flash 0x0000 esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin
   ```
   Або через онлайн веб-флешер (ESP Web Flasher, Adafruit WebSerial).

2. **`esp32_marauder_v1.20.0_lilygo_t_display_s3.bin`** (Лише прошивка):
   - Прошивається за адресою `0x10000`:
   ```bash
   esptool.py --chip esp32s3 -p <PORT> write_flash 0x10000 esp32_marauder_v1.20.0_lilygo_t_display_s3.bin
   ```

---

## 🛠️ 5. Збірка з вихідного коду через PlatformIO

```bash
# Клонування репозиторію
git clone https://github.com/<username>/ESP32Marauder.git
cd ESP32Marauder

# Компіляція
pio run -e lilygo-t-display-s3

# Прошивка на плату
pio run -e lilygo-t-display-s3 -t upload
```
