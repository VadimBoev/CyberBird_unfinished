@echo off

echo [%date% %time%] ����㥬 ������

cd "C:\adb" && adb.exe logcat -s smallapp

pause