@echo off

echo [%date% %time%] стартуем логкат

cd "C:\adb" && adb.exe logcat -s smallapp

pause