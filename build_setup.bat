@echo off
rem Builds dist\CpuTemp_Setup_<version>.exe. Requires Inno Setup (ISCC.exe) on PATH
rem and CpuTempNative.exe already built with build_c.bat.
ISCC.exe /Q "%~dp0CpuTemp.iss" || exit /b 1
