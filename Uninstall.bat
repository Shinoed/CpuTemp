@echo off
rem Double-click to uninstall CpuTemp (asks for administrator rights)
rem fltmc only succeeds when elevated and, unlike "net session", does not depend on the Server service
fltmc >nul 2>&1 || (set "SELF=%~f0" & powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath $env:SELF" & exit /b)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
