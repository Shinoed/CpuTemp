@echo off
rem Requires w64devkit (gcc + windres) on PATH
windres -O coff cputemp.rc -o cputemp_res.o || exit /b 1
gcc -std=c11 -Os -s -municode -mwindows -fno-asynchronous-unwind-tables -Wall -o CpuTempNative.exe cputemp.c cputemp_res.o -luser32 -lgdi32 -lshell32 -ladvapi32 -limm32 || exit /b 1
del cputemp_res.o
