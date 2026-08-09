@echo off
cd /d "%~dp0"
if not exist "ide-cpp\bin\Lumina.exe" (
  echo.
  echo [!] ide-cpp\bin\Lumina.exe not found.
  echo     Build it with:  build-cpp.bat
  pause
  exit /b 1
)
start "" "ide-cpp\bin\Lumina.exe"