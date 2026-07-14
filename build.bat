@echo off
chcp 65001 >nul
make all %*
if errorlevel 1 exit /b %errorlevel%
echo Build completed: supermarket.exe
