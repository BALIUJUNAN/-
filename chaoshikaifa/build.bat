# 超市管理系统 - Windows 编译脚本 (精简版)
# 使用 MinGW 编译器

@echo off
chcp 65001 >nul
echo ================================
echo   超市管理系统 - Windows 编译
echo ================================

REM 设置源文件 (已精简至10个文件)
set SOURCES=main.c supermarket.c marketing.c store_ops.c utility.c sale.c purchase.c schedule.c finance.c report.c

REM 设置输出文件
set OUTPUT=supermarket.exe

REM 编译
echo.
echo 正在编译...
gcc %SOURCES% -o %OUTPUT% -Wall -std=c99

if %errorlevel% equ 0 (
    echo.
    echo ================================
    echo   编译成功！
    echo   输出文件: %OUTPUT%
    echo ================================
    echo.
    echo 运行系统: %OUTPUT%
    echo.
) else (
    echo.
    echo 编译失败，请检查错误信息
    echo.
)

pause
