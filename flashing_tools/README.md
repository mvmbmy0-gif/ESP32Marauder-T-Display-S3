# ⚡ Інструменти та інструкція з прошивки (Flashing Tools)

> [!IMPORTANT]
> **ПРИЗНАЧЕНО ДЛЯ ВЕРСІЇ БЕЗ ТАЧСКРІНУ (NON-TOUCH)**  
> Прошивка адаптована для **звичайної версії LilyGO T-Display S3 без сенсора**.  
> Керування в меню здійснюється виключно **двома фізичними кнопками**:
> - `BOOT (IO0)`: Вибір (Select) / Довге утримання (>800 мс) для повернення назад або виходу (Back/Exit).
> - `IO14`: Гортання вниз по списку (Scroll).

---

## 🛠 Спосіб 1: Через веб-браузер (Без встановлення програм — Найпростіший)

1. Відкрийте браузер Google Chrome або Edge та перейдіть на:
   👉 **[ESP Web Tool / ESP Flasher](https://espressif.github.io/esptool-js/)** або **[Web ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)**
2. Підключіть вашу плату LilyGO T-Display S3 USB-кабелем до комп'ютера.
3. Натисніть **Connect** та виберіть порт пристрою (наприклад, `USB Single Serial` або `cu.usbmodem...`).
4. Вкажіть файл для прошивки:
   - Файл: `firmware_binaries/esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin`
   - Адреса (Offset): **`0x0`** (або `0x0000`)
5. Натисніть **Program / Flash**. Зачекайте 15-20 секунд — готово!

---

## 💻 Спосіб 2: Через готовий скрипт у терміналі

### Для macOS / Linux:
```bash
chmod +x flash_firmware.sh
./flash_firmware.sh
```
*(Скрипт автоматично знайде підключену плату і запустить esptool)*.

### Для Windows:
Двічі клацніть на `flash_firmware.bat` та вкажіть ваш COM-порт (наприклад, `COM3`).

---

## ⚙️ Спосіб 3: Через esptool.py вручну
```bash
esptool.py --chip esp32s3 write_flash 0x0000 ../firmware_binaries/esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin
```
