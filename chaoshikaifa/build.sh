#!/bin/bash
# 超市管理系统 - Linux/macOS 编译脚本 (精简版)
# 使用 GCC 编译器

echo "================================"
echo "  超市管理系统 - Linux 编译"
echo "================================"

# 源文件 (已精简至10个文件)
SOURCES="main.c supermarket.c marketing.c store_ops.c utility.c sale.c purchase.c schedule.c finance.c report.c"

# 输出文件
OUTPUT="supermarket"

# 编译
echo ""
echo "正在编译..."
gcc $SOURCES -o $OUTPUT -Wall -std=c99

if [ $? -eq 0 ]; then
    echo ""
    echo "================================"
    echo "  编译成功！"
    echo "  输出文件: ./$OUTPUT"
    echo "================================"
    echo ""
    echo "运行系统: ./$OUTPUT"
    echo ""
else
    echo ""
    echo "编译失败，请检查错误信息"
    echo ""
fi
