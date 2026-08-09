@echo off
REM Build the Lumina IDE (C++ / Qt6) into bin\Lumina.exe using MSVC + CMake + Ninja.
setlocal

set "VS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS%" (
  echo [!] Visual Studio C++ build tools not found at: %VS%
  exit /b 1
)

set "CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "QT=C:\Qt\6.9.3\msvc2022_64"

call "%VS%" >nul

pushd "%~dp0"
if not exist bin mkdir bin

echo [1/4] CMake configure...
"%CMAKE%" -B build -S . -G "Ninja" ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_PREFIX_PATH="%QT%" ^
  -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :failed

echo [2/4] Build...
"%CMAKE%" --build build --config Release
if errorlevel 1 goto :failed

echo [3/4] windeployqt...
"%QT%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler ^
    --no-opengl-sw --no-quick-import bin\Lumina.exe
if errorlevel 1 goto :failed

echo [4/4] Done.
popd
echo.
echo Build complete: ide-cpp\bin\Lumina.exe
exit /b 0

:failed
echo.
echo [!] Build failed.
popd
exit /b 1
