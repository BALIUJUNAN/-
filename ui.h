/**
 * @file ui.h
 * @brief 超市管理系统 - UI界面模块
 * 
 * 包含所有用户界面相关函数：菜单、输入、表格、颜色等
 */

#ifndef UI_H
#define UI_H

#include <stdio.h>
#include <stdbool.h>

/* ==================== ANSI颜色定义 ==================== */
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"
#define RESET   "\033[0m"

/* ==================== 边框样式定义 ==================== */
#define BOX_SINGLE_CORNER_TOP_LEFT     "┌"
#define BOX_SINGLE_CORNER_TOP_RIGHT     "┐"
#define BOX_SINGLE_CORNER_BOTTOM_LEFT   "└"
#define BOX_SINGLE_CORNER_BOTTOM_RIGHT  "┘"
#define BOX_SINGLE_LINE_H              "─"
#define BOX_SINGLE_LINE_V              "│"
#define BOX_SINGLE_T_LEFT              "├"
#define BOX_SINGLE_T_RIGHT             "┤"
#define BOX_SINGLE_T_TOP               "┬"
#define BOX_SINGLE_T_BOTTOM            "┴"
#define BOX_SINGLE_CROSS               "┼"

#define BOX_DOUBLE_CORNER_TOP_LEFT     "╔"
#define BOX_DOUBLE_CORNER_TOP_RIGHT    "╗"
#define BOX_DOUBLE_CORNER_BOTTOM_LEFT  "╚"
#define BOX_DOUBLE_CORNER_BOTTOM_RIGHT "╝"
#define BOX_DOUBLE_LINE_H             "═"
#define BOX_DOUBLE_LINE_V             "║"
#define BOX_DOUBLE_T_LEFT             "╠"
#define BOX_DOUBLE_T_RIGHT             "╣"
#define BOX_DOUBLE_T_TOP               "╦"
#define BOX_DOUBLE_T_BOTTOM            "╩"
#define BOX_DOUBLE_CROSS              "╬"

/* ==================== 表格列对齐方式 ==================== */
typedef enum {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
} TextAlign;

/* ==================== UI初始化/清理 ==================== */
/**
 * 初始化UI模块
 */
void ui_init(void);

/**
 * 清理UI资源
 */
void ui_cleanup(void);

/* ==================== 边框绘制函数 ==================== */
/**
 * 打印双线标题框
 * @param title 标题文本
 */
void print_title_box(const char *title);

/**
 * 打印单线标题框
 * @param title 标题文本
 */
void print_title_box_single(const char *title);

/**
 * 打印分隔线（双线）
 * @param width 线条宽度
 */
void print_double_line(int width);

/**
 * 打印分隔线（单线）
 * @param width 线条宽度
 */
void print_single_line(int width);

/* ==================== 消息显示函数 ==================== */
/**
 * 打印成功信息（绿色）
 * @param format 格式字符串
 * @param ... 可变参数
 */
void print_success(const char *format, ...);

/**
 * 打印错误信息（红色）
 * @param format 格式字符串
 * @param ... 可变参数
 */
void print_error(const char *format, ...);

/**
 * 打印警告信息（黄色）
 * @param format 格式字符串
 * @param ... 可变参数
 */
void print_warning(const char *format, ...);

/**
 * 打印信息（蓝色）
 * @param format 格式字符串
 * @param ... 可变参数
 */
void print_info(const char *format, ...);

/**
 * 打印提示信息（青色）
 * @param format 格式字符串
 * @param ... 可变参数
 */
void print_hint(const char *format, ...);

/* ==================== 安全输入函数 ==================== */
/**
 * 安全获取整数输入
 * @param prompt 提示文字
 * @param min 最小值
 * @param max 最大值
 * @return 用户输入的整数，超出范围返回边界值
 */
int get_safe_int(const char *prompt, int min, int max);

/**
 * 安全获取浮点数输入
 * @param prompt 提示文字
 * @return 用户输入的浮点数
 */
float get_safe_float(const char *prompt);

/**
 * 安全获取字符串输入
 * @param prompt 提示文字
 * @param out 输出缓冲区
 * @param size 缓冲区大小
 */
void get_safe_string(const char *prompt, char *out, int size);

/**
 * 获取密码输入（不回显）
 * @param prompt 提示文字
 * @param out 输出缓冲区
 * @param size 缓冲区大小
 */
void get_password_input(const char *prompt, char *out, int size);

/**
 * 获取简单字符串输入（不带额外处理）
 * @param prompt 提示文字
 * @param out 输出缓冲区
 * @param size 缓冲区大小
 */
void get_string_input(char *out, int size);

/**
 * 安全获取单个字符（Y/N）
 * @param prompt 提示文字
 * @return 'Y' 或 'N'
 */
char get_yes_no(const char *prompt);

/* ==================== 操作确认函数 ==================== */
/**
 * 操作确认提示
 * @param msg 操作描述信息
 * @return 1=确认执行, 0=取消操作
 */
int confirm_action(const char *msg);

/* ==================== 表格绘制函数 ==================== */
/**
 * 表格列定义
 */
typedef struct {
    const char *header;     // 列标题
    int width;              // 列宽度
    TextAlign align;        // 对齐方式
} TableColumn;

/**
 * 开始绘制表格
 * @param columns 列定义数组
 * @param col_count 列数
 */
void table_begin(const TableColumn *columns, int col_count);

/**
 * 绘制表头
 */
void table_draw_header(void);

/**
 * 绘制分隔线
 * @param style 0=普通, 1=表头下方
 */
void table_draw_divider(int style);

/**
 * 绘制一行数据
 * @param values 值数组
 */
void table_draw_row(const char **values);

/**
 * 结束绘制表格
 */
void table_end(void);

/**
 * 快速绘制简单表格（一次性）
 * @param columns 列定义
 * @param col_count 列数
 * @param rows 行数据数组（字符串指针数组）
 * @param row_count 行数
 */
void table_draw(const TableColumn *columns, int col_count, const char **rows, int row_count);

/* ==================== 分页显示函数 ==================== */
/**
 * 分页上下文
 */
typedef struct {
    int total_rows;         // 总行数
    int page_size;          // 每页行数（默认10）
    int current_page;       // 当前页（从1开始）
    int total_pages;        // 总页数
} PageContext;

/**
 * 初始化分页上下文
 * @param total_rows 总行数
 * @param page_size 每页行数
 * @return 分页上下文
 */
PageContext page_init(int total_rows, int page_size);

/**
 * 显示分页导航
 * @param ctx 分页上下文
 */
void page_show_navigation(const PageContext *ctx);

/**
 * 获取用户分页操作
 * @param ctx 分页上下文
 * @return -1=退出, 0=上一页, 1=下一页, 其他=刷新
 */
int page_get_action(PageContext *ctx);

/**
 * 带分页的表格显示
 * @param columns 列定义
 * @param col_count 列数
 * @param get_row_fn 获取行的回调函数 (index) -> const char**
 * @param total_rows 总行数
 * @param page_size 每页行数
 * @param get_data_fn 获取数据的回调 (index, out_buffer)
 */
typedef void (*RowDataCallback)(int index, char **out);
void table_draw_paged(const TableColumn *columns, int col_count, 
                      RowDataCallback callback, int total_rows, int page_size);

/* ==================== 等待按键 ==================== */
/**
 * 等待用户按键继续
 */
void wait_for_key(void);

/**
 * 等待用户按键（带自定义提示）
 * @param hint 提示文字
 */
void wait_for_key_with_hint(const char *hint);

/* ==================== 清屏相关 ==================== */
/**
 * 清屏
 */
void clear_screen(void);

/**
 * 暂停一段时间
 * @param ms 毫秒数
 */
void sleep_ms(int ms);

/**
 * 去除字符串首尾空白
 * @param str 输入字符串
 * @return 去空白后的字符串（原地修改）
 */
char* trim_whitespace(char *str);

/**
 * 去除字符串首尾空白（const版本）
 * @param str 输入字符串（可const）
 * @return 去空白后的字符串
 */
const char* trim_whitespace_const(const char *str);

/**
 * 验证手机号格式
 * @param phone 手机号字符串
 * @return 1=有效, 0=无效
 */
int is_valid_phone(const char *phone);

#endif /* UI_H */
