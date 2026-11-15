# 🌟 Universal All-in-One Pack (LilyGO T-Display S3 Non-Touch)

> [!IMPORTANT]
> 🔴 **NON-TOUCH VERSION (ВЕРСІЯ БЕЗ ТАЧСКРІНУ)**  
> Ця прошивка та всі файли налаштовані суто для стандартної плати **LilyGO T-Display S3 (Non-Touch, без сенсора)**.  
> Керування здійснюється **виключно двома фізичними кнопками**:  
> - **`BOOT (GPIO 0)`**: Одне натискання — **Вибір / Вхід**. Довге утримання (>800 мс) — **Назад / Вихід / Зупинка**.  
> - **`Кнопка IO14`**: Гортання списку меню вниз (**Scroll Down**).

---

## 📦 Вміст універсальної папки / Directory Contents:

У цій папці зібрано абсолютно всі необхідні файли проєкту в одному місці:

| Файл | Опис |
| :--- | :--- |
| **`esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin`** | Повний монолітний бінарник (All-in-One) для прошивки в 1 клік з адреси `0x0000`. |
| **`esp32_marauder_v1.20.0_lilygo_t_display_s3.bin`** | Основний бінарний файл прошивки Marauder (для адреси `0x10000`). |
| **`bootloader.bin`** | Офіційний Bootloader ESP32-S3 16MB (для адреси `0x0000`). |
| **`partitions.bin`** | Таблиця розмітки Flash-пам'яті (Partition Table 16MB, адреса `0x8000`). |
| **`flash_firmware.sh`** | Готовий скрипт автопрошивки для macOS та Linux. |
| **`flash_firmware.bat`** | Готовий скрипт автопрошивки для Windows. |
| **`User_Setup_lilygo_t_display_s3.h`** | Готовий конфігураційний файл драйвера дисплею TFT_eSPI (8-бітний паралельний ST7789). |
| **`platformio.ini`** | Конфігурація для збірки та прошивки в PlatformIO. |
| **`esp32_marauder_source.zip`** | Повний очищений архів вихідного коду проєкту під LilyGO T-Display S3. |

---

## ⚡ Швидка прошивка:

1. **Через браузер:** Відкрийте [ESP Web Flasher](https://espressif.github.io/esptool-js/), виберіть файл `esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin`, вкажіть адресу `0x0` і натисніть **Program**.
2. **Через термінал (macOS / Linux):**
   ```bash
   chmod +x flash_firmware.sh
   ./flash_firmware.sh
   ```
3. **Через Windows:** Запустіть файл `flash_firmware.bat`.
