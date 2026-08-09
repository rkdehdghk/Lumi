@echo off
REM Lumi 명령어(lumi, lumipm)를 환경변수 PATH에 자동 등록합니다.
echo Lumi 명령어를 사용자 환경변수 PATH에 등록하고 있습니다...
powershell -Command "$old = [Environment]::GetEnvironmentVariable('Path', 'User'); $dir = '%~dp0c-interpreter\bin'; if ($old -notlike '*c-interpreter\bin*') { [Environment]::SetEnvironmentVariable('Path', $old + ';' + $dir, 'User'); echo 'PATH 등록 완료!' } else { echo '이미 PATH에 등록되어 있습니다.' }"
echo.
echo 이제 새 터미널 창을 열면 어디서나 'lumi' 및 'lumipm' 명령어를 바로 사용할 수 있습니다!
pause
