@echo off
REM Build Lemon Display Flasher as a standalone .exe
REM Requirements: Python 3.8+, pip

echo === Lemon Display Flasher — Build ===
echo.

REM Install dependencies
echo [1/3] Instalando dependencias...
pip install pyinstaller esptool pyserial --quiet

REM Build executable
echo [2/3] Compilando ejecutable...
pyinstaller --onefile --windowed ^
    --name "LemonFlasher" ^
    --hidden-import esptool ^
    --hidden-import serial ^
    --hidden-import serial.tools ^
    --hidden-import serial.tools.list_ports ^
    --collect-all esptool ^
    lemon_flasher.py

echo.
echo [3/3] Listo!
echo.
echo Ejecutable: dist\LemonFlasher.exe
echo.
pause
