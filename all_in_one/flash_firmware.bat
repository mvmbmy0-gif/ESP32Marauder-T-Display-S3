@echo off
echo ==========================================================
echo   ESP32 Marauder Flasher for LilyGO T-Display S3
echo   [Non-Touch Version / Двокнопкове физичне керування]
echo ==========================================================

set BIN=esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin

if not exist "%BIN%" (
    set BIN=..\firmware_binaries\esp32_marauder_v1.20.0_lilygo_t_display_s3_merged.bin
)

if not exist "%BIN%" (
    echo [ERROR] Binary file not found!
    pause
    exit /b 1
)

set /p COM_PORT="Enter COM port (e.g. COM3): "
if "%COM_PORT%"=="" (
    echo [ERROR] COM port cannot be empty.
    pause
    exit /b 1
)

echo Flashing Marauder to %COM_PORT%...
esptool.py --chip esp32s3 -p %COM_PORT% -b 921600 write_flash 0x0000 "%BIN%"

echo.
echo ==========================================================
echo Flash completed successfully!
echo NOTE: This is the NON-TOUCH version.
echo Controls:
echo  - BOOT (IO0): Short press = Select / Long press = Back
echo  - IO14: Scroll down
echo ==========================================================
pause
