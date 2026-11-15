# 📦 LilyGO T-Display S3 (Non-Touch) Firmware Binaries

> [!IMPORTANT]
> **ПРИЗНАЧЕНО ДЛЯ ВЕРСІЇ БЕЗ ТАЧСКРІНУ (NON-TOUCH VERSION)**  
> Ця збірка створена спеціально для звичайної плати **LilyGO T-Display S3 (Standard Non-Touch)**.  
> Керування здійснюється **виключно двома фізичними кнопками**:
> - **Кнопка BOOT (IO0)**: Одне натискання — вибір пункту меню / Довге утримання (>800 мс) — вихід / повернення назад (Back / Exit).
> - **Кнопка IO14**: Натискання — гортання списку пунктів меню вниз (Scroll Down).

---

## 📁 Файли у цій папці / Files in this directory:

| Файл | Адреса прошивки (Offset) | Призначення |
| :--- | :--- | :--- |
| **`esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin`** | `0x0000` | **Готовий монолітний образ (All-in-One)**. Містить завантажувач, таблицю розділів і прошивку. Ідеальний для швидкої прошивки в 1 клік через веб-флешер або esptool. |
| **`esp32_marauder_v1.20.0_lilygo_t_display_s3.bin`** | `0x10000` | Основний бінарник програми Marauder (App partition). |
| **`bootloader.bin`** | `0x0000` | Офіційний Bootloader ESP32-S3 для 16MB Flash (QIO/OPI). |
| **`partitions.bin`** | `0x8000` | Таблиця розмітки Flash-пам'яті (Partition Table 16MB). |

---

## ⚡ Як прошити (Quick Flash Guide):

### Варіант 1: В один клік монолітним файлом (Рекомендовано)
```bash
esptool.py --chip esp32s3 write_flash 0x0 esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin
```

### Варіант 2: Окремими бінарниками
```bash
esptool.py --chip esp32s3 write_flash \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 esp32_marauder_v1.20.0_lilygo_t_display_s3.bin
```
