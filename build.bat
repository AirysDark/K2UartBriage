@echo off
title PlatformIO Build Script - ESP32S3

REM Move to script directory
cd /d "%~dp0"

echo ============================================
echo   PlatformIO Full Build - ESP32S3
echo ============================================
echo.

REM Launch PowerShell and run everything inside it
powershell -NoExit -Command ^
    "Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser -Force;" ^
    "Write-Host 'Execution policy set to RemoteSigned (CurrentUser)';" ^
    "Write-Host 'Running: Full Clean';" ^
    "pio run --target fullclean --environment esp32s3;" ^
    "Write-Host 'Running: Erase Flash';" ^
    "pio run --target erase --environment esp32s3;" ^
    "Write-Host 'Running: Upload Firmware';" ^
    "pio run --target upload --environment esp32s3;" ^
    "Write-Host 'Running: Upload Filesystem';" ^
    "pio run --target uploadfs --environment esp32s3;" ^
    "Write-Host '--- BUILD COMPLETE ---';" ^
    "pause"

exit
