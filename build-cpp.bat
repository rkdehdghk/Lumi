@echo off
REM Build everything:
REM   1) the Lumi interpreter (C)   -> c-interpreter\bin\lumi.exe
REM   2) the Lumina IDE (C++/Qt6)   -> ide-cpp\bin\Lumina.exe + Qt runtime
REM Needs: Visual Studio C++ build tools + Qt6 (C:\Qt). No Node.js, no npm install.

cd /d "%~dp0"

echo [1/2] Building the Lumi interpreter (C)...
call "c-interpreter\build.bat"
if errorlevel 1 goto :failed

echo.
echo [2/2] Building the Lumina IDE (C++)...
taskkill /f /im Lumina.exe >nul 2>&1
call "ide-cpp\build.bat"
if errorlevel 1 goto :failed

copy /y "c-interpreter\bin\lumi.exe" "ide-cpp\bin\lumi.exe" >nul

echo.
echo Build complete. Run: Lumina\Lumina.exe
if not "%1"=="--no-pause" pause
exit /b 0

:failed
echo.
echo [!] Build failed. See the messages above.
if not "%1"=="--no-pause" pause
exit /b 1