# 超市管理系统 Makefile
# 支持增量编译，仅重编译修改过的文件

# 编译器设置
CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic
LDFLAGS =

# 目标文件名
ifeq ($(OS),Windows_NT)
    TARGET = supermarket.exe
    RM     = del /Q
else
    TARGET = supermarket
    RM     = rm -f
endif

# 源文件和目标文件
SRCS = main.c supermarket.c sale.c purchase.c schedule.c report.c \
       marketing.c finance.c store_ops.c utility.c ui.c
OBJS = $(SRCS:.c=.o)

# 默认目标
.PHONY: all clean

all: $(TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译规则（隐式规则 + 依赖）
%.o: %.c supermarket.h hash.h ui.h
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	$(RM) $(OBJS) $(TARGET)
