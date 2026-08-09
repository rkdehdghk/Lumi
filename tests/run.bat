@echo off
REM Lumi example regression tests - runs every .lumi under examples\ (subfolders included).
REM
REM Each file gets a KEY: its path under examples\ with \ turned into _ and .lumi dropped.
REM   examples\hello.lumi          -> hello
REM   examples\graph\menu.lumi     -> graph_menu
REM
REM   expected\<KEY>.txt exists -> compare output byte-for-byte (golden test)
REM   no expected file          -> only check it finished without an error (smoke test)
REM   stdin\<KEY>.txt exists    -> fed to the program's input()
REM   KEYs in nondeterministic.txt get no golden file (time / random output)
REM
REM To create or update the golden files:  record.bat

setlocal enabledelayedexpansion
pushd "%~dp0"

set "LUMI=..\c-interpreter\bin\lumi.exe"
if not exist "%LUMI%" (
  echo [!] lumi.exe not found. Run c-interpreter\build.bat first.
  popd & exit /b 1
)

if not exist actual mkdir actual
set /a PASS=0
set /a FAIL=0

for /r "..\examples" %%F in (*.lumi) do (
  set "REL=%%F"
  set "REL=!REL:*\examples\=!"
  set "KEY=!REL:\=_!"
  set "KEY=!KEY:.lumi=!"
  set "OUT=actual\!KEY!.txt"

  if exist "stdin\!KEY!.txt" (
    "%LUMI%" "%%F" < "stdin\!KEY!.txt" > "!OUT!" 2>&1
  ) else (
    "%LUMI%" "%%F" < nul > "!OUT!" 2>&1
  )

  REM An example must never end in an uncaught error - checked even when a golden
  REM exists, so a bad run can never get frozen into expected\ and stay green.
  findstr /b /c:"Error:" "!OUT!" >nul 2>&1
  if not errorlevel 1 (
    set /a FAIL+=1
    echo [FAIL] !KEY! - error while running ^(see !OUT!^)
  ) else if exist "expected\!KEY!.txt" (
    fc /b "expected\!KEY!.txt" "!OUT!" >nul 2>&1
    if errorlevel 1 (
      set /a FAIL+=1
      echo [FAIL] !KEY! - output differs from the golden file
      echo         fc expected\!KEY!.txt !OUT!
    ) else (
      set /a PASS+=1
    )
  ) else (
    set /a PASS+=1
  )
)

echo.
echo ----------------------------------------
echo   examples: passed !PASS!  /  failed !FAIL!
echo ----------------------------------------

REM The language's own unit tests (tests\lang\*.lumi, run by "lumi test").
echo.
echo Language tests:
"%LUMI%" test lang
if errorlevel 1 set /a FAIL+=1

REM lumi lint. Two halves:
REM   1. examples\ and libraries\ must stay clean - if a change makes lint cry wolf
REM      on ordinary code, that shows up here before anyone else sees it.
REM   2. lint\bad.lumi holds one of each rule on purpose; its output is a golden file.
REM      We cd into that folder so the printed path has no \ or / in it and the
REM      golden file stays byte-identical on Windows and on Linux.
echo.
echo Lint:
"%LUMI%" lint ..\examples > actual\_lint_examples.txt 2>&1
if errorlevel 1 (
  set /a FAIL+=1
  echo [FAIL] lint complained about examples ^(see actual\_lint_examples.txt^)
  type actual\_lint_examples.txt
) else (
  echo   examples clean
)
"%LUMI%" lint ..\libraries > actual\_lint_libraries.txt 2>&1
if errorlevel 1 (
  set /a FAIL+=1
  echo [FAIL] lint complained about libraries ^(see actual\_lint_libraries.txt^)
  type actual\_lint_libraries.txt
) else (
  echo   libraries clean
)

pushd lint
"%~dp0..\c-interpreter\bin\lumi.exe" lint bad.lumi > "%~dp0actual\_lint.txt" 2>&1
popd
if exist expected\_lint.txt (
  fc /b expected\_lint.txt actual\_lint.txt >nul 2>&1
  if errorlevel 1 (
    set /a FAIL+=1
    echo [FAIL] lint\bad.lumi - findings differ from the golden file
    echo         fc expected\_lint.txt actual\_lint.txt
  ) else (
    echo   bad.lumi caught as expected
  )
) else (
  echo [!] expected\_lint.txt missing - run record.bat
)

REM lumi fmt. The whole test is "running it must change nothing", which also
REM proves it is idempotent: examples\ and libraries\ are already formatted, so
REM a formatter that starts reflowing correct code fails right here.
echo.
echo Format:
"%LUMI%" fmt --check ..\examples > actual\_fmt.txt 2>&1
if errorlevel 1 (
  set /a FAIL+=1
  echo [FAIL] fmt would rewrite examples ^(see actual\_fmt.txt^)
  type actual\_fmt.txt
) else (
  echo   examples unchanged
)
"%LUMI%" fmt --check ..\libraries >> actual\_fmt.txt 2>&1
if errorlevel 1 (
  set /a FAIL+=1
  echo [FAIL] fmt would rewrite libraries ^(see actual\_fmt.txt^)
) else (
  echo   libraries unchanged
)

REM fmt\messy.lumi is deliberately badly spaced; tidying it must give the
REM golden file exactly. That is the half that proves fmt actually does something.
pushd fmt
"%~dp0..\c-interpreter\bin\lumi.exe" fmt messy.lumi > nul 2>&1
popd
if exist expected\_fmt_messy.txt (
  fc /b expected\_fmt_messy.txt fmt\messy.lumi >nul 2>&1
  if errorlevel 1 (
    set /a FAIL+=1
    echo [FAIL] fmt\messy.lumi - tidied output differs from the golden file
    echo         fc expected\_fmt_messy.txt fmt\messy.lumi
  ) else (
    echo   messy.lumi tidied as expected
  )
) else (
  echo [!] expected\_fmt_messy.txt missing - run record.bat
)
REM put the messy file back so the next run starts from the same place
if exist fmt\messy.src copy /y fmt\messy.src fmt\messy.lumi >nul

REM The debugger. A canned DAP conversation is replayed into "lumi dap" and the
REM whole answer stream is compared byte for byte. It covers a breakpoint being
REM hit, the call stack, variables inside a function, evaluate, step and continue.
echo.
echo Debugger:
pushd dap
"%~dp0..\c-interpreter\bin\lumi.exe" dap < session.txt > "%~dp0actual\_dap.txt" 2>&1
popd
if exist expected\_dap.txt (
  fc /b expected\_dap.txt actual\_dap.txt >nul 2>&1
  if errorlevel 1 (
    set /a FAIL+=1
    echo [FAIL] lumi dap - the conversation differs from the golden file
    echo         fc expected\_dap.txt actual\_dap.txt
  ) else (
    echo   dap session as expected
  )
) else (
  echo [!] expected\_dap.txt missing - run record.bat
)

REM A .lumi file saved with CRLF must parse exactly like the LF one. The lexer
REM used to see a blank line only as a bare newline, so on CRLF a blank line inside
REM a block read as column 0 and closed the whole block - libraries\sqlite.lumi
REM broke that way and nothing caught it. crlflankline.lumi is CRLF on purpose.
echo.
echo CRLF source:
"%LUMI%" crlflankline.lumi > actual\_crlf.txt 2>&1
findstr /b /c:"Error:" actual\_crlf.txt >nul 2>&1
if not errorlevel 1 (
  set /a FAIL+=1
  echo [FAIL] a CRLF file does not parse ^(see actual\_crlf.txt^)
  type actual\_crlf.txt
) else (
  echo   CRLF file parses
)

REM The little web server needs two programs at once (one serves, one asks),
REM so it cannot live in tests\lang - "lumi test" would sit there waiting.
echo.
echo HTTP server test:
start /b "" "%LUMI%" http\server.lumi > nul 2>&1
REM give the server a moment to open the port
ping -n 2 127.0.0.1 > nul
"%LUMI%" http\client.lumi > actual\_http.txt 2>&1
type actual\_http.txt
findstr /c:"FAIL" actual\_http.txt >nul 2>&1
if not errorlevel 1 set /a FAIL+=1

popd
if !FAIL! gtr 0 exit /b 1
exit /b 0
