@echo off
chcp 65001 >nul
echo ========================================
echo   超市管理系统 Web 版
echo ========================================
echo.

cd /d "%~dp0"

echo 正在检查 Python...
python --version >nul 2>&1
if errorlevel 1 (
    echo 错误: 未安装 Python
    echo 请访问 https://www.python.org/downloads/ 下载安装
    pause
    exit /b 1
)

echo 检查依赖...
pip show flask >nul 2>&1
if errorlevel 1 (
    echo 正在安装 Flask...
    pip install flask -q
)

echo.
echo ========================================
echo   启动服务...
echo   访问地址: http://127.0.0.1:5000
echo ========================================
echo.
echo 按 Ctrl+C 停止服务
echo.

python app.py

pause
