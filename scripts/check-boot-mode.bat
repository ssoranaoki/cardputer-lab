@echo off
rem Ask the Cardputer "are you in download mode?" without resetting it.
rem   Chip is ESP32-S3 ...            -> it is in download mode (ROM bootloader answered)
rem   Failed to connect / No serial   -> it is NOT in download mode (app running or stopped)
rem Usage: double-click, or "check-boot-mode.bat COM5" for another port.

setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM4
set PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe
set ESPTOOL=
for /d %%D in ("%USERPROFILE%\.platformio\packages\tool-esptoolpy*") do (
  if exist "%%D\esptool.py" set ESPTOOL=%%D\esptool.py
)
if "%ESPTOOL%"=="" (
  echo esptool.py not found under %USERPROFILE%\.platformio\packages
  pause
  exit /b 1
)
echo Using %ESPTOOL%
echo Port  %PORT%
echo.
"%PY%" "%ESPTOOL%" --chip esp32s3 --port %PORT% --before no_reset --after no_reset chip_id
echo.
pause
