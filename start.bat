@echo off
REM SupplyChainX 2.0 - Double-click Launcher
cd /d "%~dp0"
powershell.exe -ExecutionPolicy Bypass -NoProfile -File "%~dp0start.ps1" %*
pause
