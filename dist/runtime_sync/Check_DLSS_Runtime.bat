@echo off
setlocal
chcp 65001 >nul

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

title OptiScaler Aurora - DLSS / Streamline Runtime Self-check

echo.
echo ============================================================
echo  OptiScaler Aurora DLSS / Streamline Self-check
echo ============================================================
echo.
echo If your launcher just verified or updated the game,
echo wait until it shows "Start Game", then run this check.
echo 如果启动器刚刚验证或更新了游戏，请等待出现“开始游戏”后再运行本工具。
echo.

if not exist "%ROOT%\runtime_sync.ps1" (
    echo [FAIL] runtime_sync.ps1 was not found beside this BAT file.
    echo.
    pause
    exit /b 2
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\runtime_sync.ps1" -Mode Check -InstallDir "%ROOT%"

echo.
pause
endlocal
