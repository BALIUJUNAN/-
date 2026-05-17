/**
 * @file ui.c
 * @brief 超市管理系统 - UI界面模块实现
 */

#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <conio.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #define ISATTY _isatty
    #define FILENO _fileno
#else
    #include <unistd.h>
    #define ISATTY isatty
    #define FILENO fileno
#endif

/* ==================== 内部变量 ==================== */
static int g_table_col_count = 0;
static const TableColumn *g_table_columns = NULL;
static int g_color_enabled = 1;  /* 默认启用颜色 */

/* ==================== 内部函数声明 ==================== */
static void enable_colors(void);
static void disable_colors(void);
static void print_aligned(const char *text, int width, TextAlign align);

/* ==================== UI初始化/清理 ==================== */
void ui_init(void) {
    /* 检测终端是否支持颜色 */
    if (ISATTY(FILENO(stdout))) {
        enable_colors();
    } else {
        disable_colors();
    }
}

void ui_cleanup(void) {
    /* 清理工作 */
}

/* ==================== 颜色控制 ==================== */
static void enable_colors(void) {
    g_color_enabled = 1;
}

static void disable_colors(void) {
    g_color_enabled = 0;
}

/**
 * 控制台是否支持颜色
 */
int is_color_enabled(void) {
    return g_color_enabled;
}

/* ==================== 边框绘制函数 ==================== */
void print_title_box(const char *title) {
    int title_len = 0;
    int i;
    
    if (title == NULL) {
        title = "";
    }
    title_len = 0;
    for (i = 0; title[i]; i++) {
        /* 中文字符占2个宽度 */
        if ((unsigned char)title[i] > 127) {
            title_len += 2;
            i++;
        } else {
            title_len++;
        }
    }
    
    /* 计算总宽度 */
    int width = 36;  /* 最小宽度 */
    int content_width = title_len + 2;
    if (content_width + 4 > width) {
        width = content_width + 4;
    }
    width = (width / 2) * 2;  /* 确保偶数 */
    
    /* 顶部边框 */
    printf("\n");
    printf(BOLD CYAN);
    printf(BOX_DOUBLE_CORNER_TOP_LEFT);
    for (i = 0; i < width - 2; i++) {
        printf(BOX_DOUBLE_LINE_H);
    }
    printf(BOX_DOUBLE_CORNER_TOP_RIGHT);
    printf(RESET "\n");
    
    /* 标题行 */
    int padding = (width - 4 - title_len) / 2;
    printf(BOLD CYAN);
    printf(BOX_DOUBLE_LINE_V " " RESET);
    for (i = 0; i < padding; i++) printf(" ");
    printf(BOLD YELLOW "%s" RESET, title);
    for (i = 0; i < width - 4 - title_len - padding; i++) printf(" ");
    printf(" " BOLD CYAN BOX_DOUBLE_LINE_V RESET "\n");
    
    /* 底部边框 */
    printf(BOX_DOUBLE_CORNER_BOTTOM_LEFT);
    for (i = 0; i < width - 2; i++) {
        printf(BOX_DOUBLE_LINE_H);
    }
    printf(BOX_DOUBLE_CORNER_BOTTOM_RIGHT);
    printf("\n");
}

void print_title_box_single(const char *title) {
    int title_len = 0;
    int i;
    
    if (title == NULL) {
        title = "";
    }
    title_len = 0;
    for (i = 0; title[i]; i++) {
        if ((unsigned char)title[i] > 127) {
            title_len += 2;
            i++;
        } else {
            title_len++;
        }
    }
    
    int width = 36;
    int content_width = title_len + 2;
    if (content_width + 4 > width) {
        width = content_width + 4;
    }
    width = (width / 2) * 2;
    
    printf("\n");
    printf(BOX_SINGLE_CORNER_TOP_LEFT);
    for (i = 0; i < width - 2; i++) {
        printf(BOX_SINGLE_LINE_H);
    }
    printf(BOX_SINGLE_CORNER_TOP_RIGHT);
    printf("\n");
    
    int padding = (width - 4 - title_len) / 2;
    printf(BOX_SINGLE_LINE_V " ");
    for (i = 0; i < padding; i++) printf(" ");
    printf("%s", title);
    for (i = 0; i < width - 4 - title_len - padding; i++) printf(" ");
    printf(" " BOX_SINGLE_LINE_V "\n");
    
    printf(BOX_SINGLE_CORNER_BOTTOM_LEFT);
    for (i = 0; i < width - 2; i++) {
        printf(BOX_SINGLE_LINE_H);
    }
    printf(BOX_SINGLE_CORNER_BOTTOM_RIGHT);
    printf("\n");
}

void print_double_line(int width) {
    int i;
    printf(BOLD CYAN);
    printf(BOX_DOUBLE_LINE_V " ");
    for (i = 0; i < width; i++) {
        printf(BOX_DOUBLE_LINE_H);
    }
    printf(" " BOX_DOUBLE_LINE_V RESET "\n");
}

void print_single_line(int width) {
    int i;
    printf(BOX_SINGLE_LINE_V " ");
    for (i = 0; i < width; i++) {
        printf(BOX_SINGLE_LINE_H);
    }
    printf(" " BOX_SINGLE_LINE_V "\n");
}

/* ==================== 消息显示函数 ==================== */
void print_success(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    if (g_color_enabled) {
        printf(GREEN "✓ " RESET);
    } else {
        printf("[成功] ");
    }
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
}

void print_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    if (g_color_enabled) {
        printf(RED "✗ " RESET);
    } else {
        printf("[错误] ");
    }
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
}

void print_warning(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    if (g_color_enabled) {
        printf(YELLOW "⚠ " RESET);
    } else {
        printf("[警告] ");
    }
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
}

void print_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    if (g_color_enabled) {
        printf(BLUE "ℹ " RESET);
    } else {
        printf("[信息] ");
    }
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
}

void print_hint(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    if (g_color_enabled) {
        printf(CYAN "→ " RESET);
    } else {
        printf("[提示] ");
    }
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
}

/* ==================== 工具函数 ==================== */
char* trim_whitespace(char *str) {
    static char buffer[256];
    int len, i, start, end;
    
    if (str == NULL) return buffer;
    
    len = strlen(str);
    strncpy(buffer, str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    /* 去除两端空白 */
    for (start = 0; start < len && isspace(buffer[start]); start++);
    for (end = len - 1; end >= start && isspace(buffer[end]); end--);
    
    if (start > end) {
        buffer[0] = '\0';
        return buffer;
    }
    
    for (i = start; i <= end; i++) {
        buffer[i - start] = buffer[i];
    }
    buffer[end - start + 1] = '\0';
    
    return buffer;
}

int is_valid_phone(const char *phone) {
    if (!phone || strlen(phone) == 0) {
        return 0;
    }
    
    /* 验证手机号格式：11位数字，以1开头 */
    if (strlen(phone) != 11) {
        return 0;
    }
    
    if (phone[0] != '1') {
        return 0;
    }
    
    /* 检查是否全是数字 */
    for (int i = 0; phone[i]; i++) {
        if (!isdigit((unsigned char)phone[i])) {
            return 0;
        }
    }
    
    return 1;
}

const char* trim_whitespace_const(const char *str) {
    return trim_whitespace((char*)str);
}

static void print_aligned(const char *text, int width, TextAlign align) {
    int text_len = 0;
    int i;
    const char *trimmed = trim_whitespace_const(text);
    
    /* 计算显示宽度（中文占2） */
    for (i = 0; trimmed[i]; i++) {
        if ((unsigned char)trimmed[i] > 127) {
            text_len += 2;
            i++;
        } else {
            text_len++;
        }
    }
    
    int padding = width - text_len;
    if (padding < 0) padding = 0;
    
    if (align == ALIGN_LEFT) {
        printf("%s", trimmed);
        for (i = 0; i < padding; i++) printf(" ");
    } else if (align == ALIGN_RIGHT) {
        for (i = 0; i < padding; i++) printf(" ");
        printf("%s", trimmed);
    } else { /* CENTER */
        int left = padding / 2;
        int right = padding - left;
        for (i = 0; i < left; i++) printf(" ");
        printf("%s", trimmed);
        for (i = 0; i < right; i++) printf(" ");
    }
}

/* ==================== 安全输入函数 ==================== */
int get_safe_int(const char *prompt, int min, int max) {
    char buffer[64];
    char *endptr;
    long value;
    
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            printf("\n");
            return min;
        }
        
        /* 去除换行 */
        buffer[strcspn(buffer, "\n")] = '\0';
        
        /* 去除空白 */
        trim_whitespace(buffer);
        
        /* 检查是否为空 */
        if (strlen(buffer) == 0) {
            print_hint("输入不能为空，请重新输入");
            continue;
        }
        
        /* 尝试解析整数 */
        value = strtol(buffer, &endptr, 10);
        
        /* 检查是否完全转换 */
        if (*endptr != '\0') {
            print_warning("输入包含非法字符: %s", endptr);
            continue;
        }
        
        /* 检查范围 */
        if (value < min || value > max) {
            print_warning("输入超出范围 [%d, %d]，请重新输入", min, max);
            continue;
        }
        
        return (int)value;
    }
}

float get_safe_float(const char *prompt) {
    char buffer[64];
    char *endptr;
    double value;
    
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            printf("\n");
            return 0.0f;
        }
        
        buffer[strcspn(buffer, "\n")] = '\0';
        trim_whitespace(buffer);
        
        if (strlen(buffer) == 0) {
            return 0.0f;
        }
        
        value = strtod(buffer, &endptr);
        
        if (*endptr != '\0') {
            /* 可能是负数的情况 */
            if (endptr == buffer && buffer[0] == '-') {
                print_warning("输入不能为负数");
                continue;
            }
            /* 允许输入百分比格式 */
            if (endptr[0] == '%' && endptr[1] == '\0') {
                return (float)value;
            }
            print_warning("输入包含非法字符: %s", endptr);
            continue;
        }
        
        if (value < 0) {
            print_warning("输入不能为负数");
            continue;
        }
        
        return (float)value;
    }
}

void get_safe_string(const char *prompt, char *out, int size) {
    if (size <= 0 || out == NULL) return;
    
    printf("%s", prompt);
    fflush(stdout);
    
    if (fgets(out, size, stdin) != NULL) {
        /* 去除换行符 */
        out[strcspn(out, "\n")] = '\0';
        /* 去除首尾空白 */
        char *start = out;
        while (isspace(*start)) start++;
        char *end = out + strlen(out) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';
        if (start != out) {
            memmove(out, start, strlen(start) + 1);
        }
    } else {
        out[0] = '\0';
    }
}

void get_password_input(const char *prompt, char *out, int size) {
    int i = 0;
    int ch;
    
    if (size <= 0 || out == NULL) return;
    
    printf("%s", prompt);
    fflush(stdout);
    
    /* Windows 使用 _getch */
#ifdef _WIN32
    i = 0;
    while ((ch = _getch()) != '\r' && ch != '\n' && i < size - 1) {
        if (ch == '\b' && i > 0) {
            printf("\b \b");
            i--;
        } else if (ch != '\b') {
            printf("*");
            out[i++] = (char)ch;
        }
    }
#else
    /* Linux/macOS 使用 termios */
    struct termios old, new;
    tcgetattr(STDIN_FILENO, &old);
    new = old;
    new.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new);
    
    i = 0;
    while ((ch = getchar()) != '\n' && ch != '\r' && i < size - 1) {
        if (ch == 127 || ch == '\b') {  /* Backspace */
            if (i > 0) {
                printf("\b \b");
                i--;
            }
        } else {
            out[i++] = (char)ch;
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
#endif

    out[i] = '\0';
    printf("\n");
}

char get_yes_no(const char *prompt) {
    char buffer[8];
    
    while (1) {
        printf("%s (Y/N): ", prompt);
        fflush(stdout);
        
        if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            buffer[strcspn(buffer, "\n")] = '\0';
            trim_whitespace(buffer);
            
            if (strlen(buffer) == 0) {
                continue;
            }
            
            char ch = toupper(buffer[0]);
            if (ch == 'Y' || ch == 'N') {
                return ch;
            }
            
            print_warning("请输入 Y 或 N");
        }
    }
}

void get_string_input(char *out, int size) {
    if (size <= 0 || out == NULL) return;
    
    if (fgets(out, size, stdin) != NULL) {
        out[strcspn(out, "\n")] = '\0';
    } else {
        out[0] = '\0';
    }
}

/* ==================== 操作确认函数 ==================== */

int confirm_action(const char *msg) {
    char buffer[16];
    int box_width = 50;
    
    printf("\n");
    if (g_color_enabled) {
        /* 顶部边框 */
        printf(YELLOW "┌");
        for (int i = 0; i < box_width - 2; i++) printf("─");
        printf("┐\n" RESET);
        
        /* 消息内容行 */
        printf(YELLOW "│ " RESET);
        printf("%s", msg ? msg : "");
        printf(YELLOW " │\n" RESET);
        
        /* 确认提示行 */
        printf(YELLOW "│ " RESET);
        printf("确认执行此操作吗？");
        printf(YELLOW " │\n" RESET);
        
        /* 输入提示行 */
        printf(YELLOW "│ " RESET);
        printf("输入 Y 确认，输入 N 取消");
        printf(YELLOW " │\n" RESET);
        
        /* 底部边框 */
        printf(YELLOW "└");
        for (int i = 0; i < box_width - 2; i++) printf("─");
        printf("┘\n" RESET);
    } else {
        printf("=");
        for (int i = 0; i < box_width - 2; i++) printf("=");
        printf("=\n");
        printf(" %s\n", msg ? msg : "");
        printf(" 确认执行此操作吗？\n");
        printf(" 输入 Y 确认，输入 N 取消\n");
        printf("=");
        for (int i = 0; i < box_width - 2; i++) printf("=");
        printf("=\n");
    }
    
    while (1) {
        printf("请输入 [Y/N]: ");
        fflush(stdout);
        
        if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            buffer[strcspn(buffer, "\n")] = '\0';
            trim_whitespace(buffer);
            
            if (strlen(buffer) == 0) {
                continue;
            }
            
            char ch = toupper(buffer[0]);
            if (ch == 'Y') {
                return 1;
            } else if (ch == 'N') {
                print_hint("操作已取消");
                return 0;
            }
        }
        
        print_warning("无效输入，请输入 Y 或 N");
    }
}

/* ==================== 表格绘制函数 ==================== */
void table_begin(const TableColumn *columns, int col_count) {
    g_table_columns = columns;
    g_table_col_count = col_count;
}

void table_draw_header(void) {
    int i;
    int total_width = 1;  /* 起始边框 */
    
    if (g_table_columns == NULL || g_table_col_count == 0) return;
    
    /* 顶部边框 */
    printf(BOX_SINGLE_CORNER_TOP_LEFT);
    for (i = 0; i < g_table_col_count; i++) {
        int j;
        for (j = 0; j < g_table_columns[i].width + 2; j++) {
            printf(BOX_SINGLE_LINE_H);
        }
        if (i < g_table_col_count - 1) {
            printf(BOX_SINGLE_T_TOP);
        }
    }
    printf(BOX_SINGLE_CORNER_TOP_RIGHT);
    printf("\n");
    
    /* 表头 */
    printf(BOX_SINGLE_LINE_V);
    for (i = 0; i < g_table_col_count; i++) {
        printf(" ");
        print_aligned(g_table_columns[i].header, g_table_columns[i].width, ALIGN_CENTER);
        printf(" ");
        if (i < g_table_col_count - 1) {
            printf(BOX_SINGLE_LINE_V);
        }
    }
    printf(BOX_SINGLE_LINE_V);
    printf("\n");
    
    /* 分隔线 */
    table_draw_divider(1);
}

void table_draw_divider(int style) {
    int i, j;
    
    if (g_table_columns == NULL || g_table_col_count == 0) return;
    
    if (style == 1) {
        printf(BOX_SINGLE_T_LEFT);
    } else {
        printf(BOX_SINGLE_LINE_V);
    }
    
    for (i = 0; i < g_table_col_count; i++) {
        for (j = 0; j < g_table_columns[i].width + 2; j++) {
            printf(BOX_SINGLE_LINE_H);
        }
        if (i < g_table_col_count - 1) {
            if (style == 1) {
                printf(BOX_SINGLE_T_BOTTOM);
            } else {
                printf(BOX_SINGLE_T_LEFT);
            }
        }
    }
    
    if (style == 1) {
        printf(BOX_SINGLE_T_RIGHT);
    } else {
        printf(BOX_SINGLE_LINE_V);
    }
    printf("\n");
}

void table_draw_row(const char **values) {
    int i;
    
    if (g_table_columns == NULL || g_table_col_count == 0 || values == NULL) return;
    
    printf(BOX_SINGLE_LINE_V);
    for (i = 0; i < g_table_col_count; i++) {
        printf(" ");
        print_aligned(values[i] ? values[i] : "", g_table_columns[i].width, g_table_columns[i].align);
        printf(" ");
        if (i < g_table_col_count - 1) {
            printf(BOX_SINGLE_LINE_V);
        }
    }
    printf(BOX_SINGLE_LINE_V);
    printf("\n");
}

void table_end(void) {
    int i;
    
    if (g_table_columns == NULL || g_table_col_count == 0) return;
    
    /* 底部边框 */
    printf(BOX_SINGLE_CORNER_BOTTOM_LEFT);
    for (i = 0; i < g_table_col_count; i++) {
        int j;
        for (j = 0; j < g_table_columns[i].width + 2; j++) {
            printf(BOX_SINGLE_LINE_H);
        }
        if (i < g_table_col_count - 1) {
            printf(BOX_SINGLE_T_BOTTOM);
        }
    }
    printf(BOX_SINGLE_CORNER_BOTTOM_RIGHT);
    printf("\n");
}

void table_draw(const TableColumn *columns, int col_count, const char **rows, int row_count) {
    table_begin(columns, col_count);
    table_draw_header();
    
    int row_len = col_count;
    for (int i = 0; i < row_count; i++) {
        table_draw_row(rows + i * col_count);
    }
    
    table_end();
}

/* ==================== 分页显示函数 ==================== */
PageContext page_init(int total_rows, int page_size) {
    PageContext ctx;
    ctx.total_rows = total_rows > 0 ? total_rows : 0;
    ctx.page_size = page_size > 0 ? page_size : 10;
    ctx.current_page = 1;
    ctx.total_pages = (ctx.total_rows + ctx.page_size - 1) / ctx.page_size;
    if (ctx.total_pages == 0) ctx.total_pages = 1;
    return ctx;
}

void page_show_navigation(const PageContext *ctx) {
    if (g_color_enabled) {
        printf("\n" CYAN "┌─ 分页导航 ──────────────────────────────────┐\n");
        printf("│ " RESET);
        printf("第 %d/%d 页 | 共 %d 条记录", 
               ctx->current_page, ctx->total_pages, ctx->total_rows);
        printf("                           " CYAN "│\n");
        printf("│ " RESET);
        if (ctx->current_page > 1) {
            printf("[P] 上一页 ");
        }
        if (ctx->current_page < ctx->total_pages) {
            printf("[N] 下一页 ");
        }
        printf("[Q] 退出");
        printf("                               " CYAN "│\n");
        printf("└─────────────────────────────────────────────────┘\n" RESET);
    } else {
        printf("\n--- 第 %d/%d 页 | 共 %d 条记录 ---\n", 
               ctx->current_page, ctx->total_pages, ctx->total_rows);
        if (ctx->current_page > 1) printf("[P] 上一页 ");
        if (ctx->current_page < ctx->total_pages) printf("[N] 下一页 ");
        printf("[Q] 退出\n");
    }
}

int page_get_action(PageContext *ctx) {
    char buffer[8];
    char ch;
    
    printf("\n请选择: ");
    fflush(stdout);
    
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
        trim_whitespace(buffer);
        
        if (strlen(buffer) == 0) {
            return 0;  /* 默认下一页 */
        }
        
        ch = toupper(buffer[0]);
        
        if (ch == 'Q') {
            return -1;
        } else if (ch == 'P' && ctx->current_page > 1) {
            ctx->current_page--;
            return 0;
        } else if (ch == 'N' && ctx->current_page < ctx->total_pages) {
            ctx->current_page++;
            return 1;
        } else {
            return 2;  /* 刷新 */
        }
    }
    
    return 0;
}

/* ==================== 等待按键 ==================== */
void wait_for_key(void) {
    printf("\n");
    if (g_color_enabled) {
        printf(YELLOW "┌─ 提示 ─────────────────────────────────────┐\n");
        printf("│ " RESET);
        printf("按任意键继续...");
        printf("                                    " YELLOW "│\n");
        printf("└─────────────────────────────────────────────────┘\n" RESET);
    } else {
        printf("按任意键继续...");
    }
    
#ifdef _WIN32
    _getch();
#else
    struct termios old, new;
    tcgetattr(STDIN_FILENO, &old);
    new = old;
    new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new);
    getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
#endif
}

void wait_for_key_with_hint(const char *hint) {
    printf("\n");
    if (hint && strlen(hint) > 0) {
        printf("%s\n", hint);
    }
    wait_for_key();
}

/* ==================== 清屏相关 ==================== */
void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}
