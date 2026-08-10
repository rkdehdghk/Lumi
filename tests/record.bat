@echo off
REM Writes the golden output files (expected\*.txt).
REM
REM Only run this when the output changed ON PURPOSE (you changed the language).
REM Always run run.bat FIRST and read what changed before recording over it.
REM KEYs listed in nondeterministic.txt are skipped (output differs every run).
REM
REM KEY = the file's path under examples\ with \ turned into _ and .lumi dropped.

setlocal enabledelayedexpansion
pushd "%~dp0"

set "LUMI=..\c-interpreter\bin\lumi.exe"
if not exist "%LUMI%" (
  echo [!] lumi.exe not found. Run c-interpreter\build.bat first.
  popd & exit /b 1
)

if not exist expected mkdir expected
set /a MADE=0
set /a SKIP=0

for /r "..\examples" %%F in (*.lumi) do (
  set "REL=%%F"
  set "REL=!REL:*\examples\=!"
  set "KEY=!REL:\=_!"
  set "KEY=!KEY:.lumi=!"

  findstr /x /c:"!KEY!" nondeterministic.txt >nul 2>&1
  if errorlevel 1 (
    if exist "stdin\!KEY!.txt" (
      "%LUMI%" "%%F" < "stdin\!KEY!.txt" > "expected\!KEY!.txt" 2>&1
    ) else (
      "%LUMI%" "%%F" < nul > "expected\!KEY!.txt" 2>&1
    )
    findstr /b /c:"Error:" "expected\!KEY!.txt" >nul 2>&1
    if not errorlevel 1 (
      echo   [!] !KEY! ended in an ERROR - fix the example ^(or its stdin^) and record again
    )
    set /a MADE+=1
    echo   recorded !KEY!
  ) else (
    set /a SKIP+=1
    echo   skipped  !KEY! ^(nondeterministic^)
  )
)

REM The lint golden. cd into the folder first so the printed path is a bare
REM filename - that keeps the file identical on Windows and on Linux.
pushd lint
"%~dp0..\c-interpreter\bin\lumi.exe" lint bad.lumi > "%~dp0expected\_lint.txt" 2>&1
popd
echo   recorded _lint

REM The fmt golden: tidy the deliberately messy file and keep the result.
copy /y fmt\messy.src fmt\messy.lumi >nul
pushd fmt
"%~dp0..\c-interpreter\bin\lumi.exe" fmt messy.lumi > nul 2>&1
popd
copy /y fmt\messy.lumi expected\_fmt_messy.txt >nul
echo   recorded _fmt_messy

REM The debugger golden: a canned DAP conversation replayed into "lumi dap".
REM cd into the folder so the paths inside stay bare file names.
pushd dap
"%~dp0..\c-interpreter\bin\lumi.exe" dap < session.txt > "%~dp0expected\_dap.txt" 2>&1
popd
echo   recorded _dap

REM The language server golden: a canned LSP conversation replayed into "lumi lsp".
pushd lsp
"%~dp0..\c-interpreter\bin\lumi.exe" lsp < session.txt > "%~dp0expected\_lsp.txt" 2>&1
popd
echo   recorded _lsp

echo.
echo Recorded !MADE! golden files. ^(!SKIP! skipped^)
popd
exit /b 0
