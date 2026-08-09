@echo off
REM Build and run the IDE's self-checks (Qt6Core only, no test framework).
setlocal
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "QT=C:\Qt\6.9.3\msvc2022_64"
call "%VS%" >nul
pushd "%~dp0"
cl /nologo /std:c++17 /Zc:__cplusplus /permissive- /EHsc /utf-8 /MD /DQT_NO_DEBUG ^
   /I"%QT%\include" /I"%QT%\include\QtCore" ^
   streammarkers_test.cpp /Fe:streammarkers_test.exe ^
   /link /LIBPATH:"%QT%\lib" Qt6Core.lib
if errorlevel 1 (echo [!] compile failed & popd & exit /b 1)
set "PATH=%QT%\bin;%PATH%"
"%~dp0streammarkers_test.exe"
set RC=%ERRORLEVEL%
del /q streammarkers_test.obj streammarkers_test.exe 2>nul
popd
exit /b %RC%
