@echo off
REM Build the Lumi interpreter (C) into bin\lumi.exe using MSVC.
REM Usage: double-click this file, or run  build.bat

setlocal
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS%" (
  echo [!] Visual Studio C++ build tools not found at:
  echo     %VS%
  echo     Install "Desktop development with C++", or edit the path above.
  exit /b 1
)

pushd "%~dp0"
call "%VS%" >nul
if not exist bin mkdir bin
if not exist build mkdir build

REM /we4013 : an undeclared function is an ERROR, not a warning. Twice now a missing
REM           declaration truncated a 64-bit pointer to int and broke things quietly.
cl /nologo /std:c17 /O2 /W3 /we4013 /utf-8 /D_CRT_SECURE_NO_WARNINGS ^
   /Fe:bin\lumi.exe /Fo:build\ ^
   src\platform.c src\value.c src\regex.c src\unicode.c src\lexer.c src\parser.c src\interp.c src\pkg.c src\lsp.c src\dap.c src\lint.c src\fmt.c src\ffi.c src\bundle.c src\main.c ^
   /link /STACK:8388608 ws2_32.lib
set ERR=%ERRORLEVEL%
popd

if %ERR% neq 0 (
  echo.
  echo [!] Build failed.
  exit /b %ERR%
)
echo.
echo Build complete: c-interpreter\bin\lumi.exe
