@echo off

echo [%date% %time%] ����㥬 ������ NOX

cd "C:\Program Files\Nox\bin" && nox_adb.exe connect 127.0.0.1:62001 && nox_adb.exe logcat -s smallapp

pause