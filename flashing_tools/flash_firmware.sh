#!/bin/bash
set -e

echo "=========================================================="
echo "  ESP32 Marauder Flasher for LilyGO T-Display S3"
echo "  [Non-Touch Version / Двокнопкове фізичне керування]"
echo "=========================================================="

PORT="${1}"
BIN="../firmware_binaries/esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin"

if [ ! -f "$BIN" ]; then
    echo "❌ Помилка: бінарний файл не знайдено за шляхом $BIN"
    exit 1
fi

if [ -z "$PORT" ]; then
    echo "🔍 Пошук підключеної плати..."
    DETECTED_PORT=$(ls /dev/cu.usbmodem* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1 || true)
    if [ -n "$DETECTED_PORT" ]; then
        PORT="$DETECTED_PORT"
        echo "✅ Знайдено порт: $PORT"
    else
        echo "⚠️  Порт не визначено автоматично. Будь ласка, вкажіть його:"
        echo "   Використання: ./flash_firmware.sh /dev/cu.usbmodem14301"
        exit 1
    fi
fi

echo "🚀 Починаємо прошивку на $PORT..."
esptool.py --chip esp32s3 -p "$PORT" -b 921600 write_flash 0x0000 "$BIN"

echo ""
echo "🎉 Прошивка успішно завершена!"
echo "📌 Пам'ятайте: це версія БЕЗ тачскріну."
echo "   Керування здійснюється двома фізичними кнопками:"
echo "   - BOOT (IO0): Вибір / Довге утримання — Вихід (Back/Exit)"
echo "   - IO14: Гортання меню вниз"
