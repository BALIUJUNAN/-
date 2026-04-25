/**
 * @file utility.c
 * @brief 工具模块合并文件
 * 
 * 合并了以下模块：
 * - 小票打印 (printer)
 * - 数据看板 (dashboard)
 */

#include "supermarket.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

// ==================== 打印模块 ====================

// ESC/POS commands
#define ESC_INIT       "\x1B\x40"
#define ESC_ALIGN_CENTER "\x1B\x61\x01"
#define ESC_ALIGN_LEFT   "\x1B\x61\x00"
#define ESC_BOLD_ON    "\x1B\x45\x01"
#define ESC_BOLD_OFF   "\x1B\x45\x00"
#define ESC_DOUBLE_HEIGHT "\x1B\x21\x10"
#define ESC_NORMAL     "\x1B\x21\x00"
#define ESC_CUT        "\x1B\x6D"

// Global printer handle
#ifdef _WIN32
static HANDLE g_printer = INVALID_HANDLE_VALUE;
#else
static int g_printer_fd = -1;
#endif

static PrinterConfig g_printer_config;
static int g_initialized = 0;

// Forward declaration
static int printer_save_to_file(const Receipt *receipt);

// Get default printer configuration
PrinterConfig printer_get_default_config(void) {
    PrinterConfig config;
#ifdef _WIN32
    strncpy(config.device_path, "LPT1", sizeof(config.device_path) - 1);
#else
    strncpy(config.device_path, "/dev/usb/lp0", sizeof(config.device_path) - 1);
#endif
    config.device_path[sizeof(config.device_path) - 1] = '\0';
    config.paper_width = 32;
    config.codepage = 0;
    return config;
}

// Set printer device path
int printer_set_device(const char *device_path) {
    if (!device_path) return -1;
    strncpy(g_printer_config.device_path, device_path, sizeof(g_printer_config.device_path) - 1);
    g_printer_config.device_path[sizeof(g_printer_config.device_path) - 1] = '\0';
    return 0;
}

// Initialize printer connection
int printer_init(const PrinterConfig *config) {
    if (config) {
        memcpy(&g_printer_config, config, sizeof(PrinterConfig));
    } else {
        g_printer_config = printer_get_default_config();
    }

#ifdef _WIN32
    // Open printer device (COM port or LPT port)
    g_printer = CreateFileA(
        g_printer_config.device_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (g_printer == INVALID_HANDLE_VALUE) {
        // Try COM port if LPT not found
        if (strncmp(g_printer_config.device_path, "LPT", 3) == 0) {
            char com_path[256];
            snprintf(com_path, sizeof(com_path), "\\\\.\\%s", g_printer_config.device_path);
            g_printer = CreateFileA(
                com_path,
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );
        }
        if (g_printer == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Failed to open printer: %s (Error: %lu)\n",
                    g_printer_config.device_path, GetLastError());
            return -1;
        }

        // Set COM port timeouts
        COMMTIMEOUTS timeout = {0, 0, 500, 0, 0};
        SetCommTimeouts(g_printer, &timeout);

        // Set baud rate to 9600 for ESC/POS printers
        DCB dcb;
        memset(&dcb, 0, sizeof(dcb));
        dcb.DCBlength = sizeof(dcb);
        dcb.BaudRate = 9600;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(g_printer, &dcb);
    }
#else
    // Open device file (USB printer or parallel port)
    g_printer_fd = open(g_printer_config.device_path, O_WRONLY | O_NOCTTY);
    if (g_printer_fd < 0) {
        // Try alternative paths
        const char *alt_paths[] = {
            "/dev/usb/lp1",
            "/dev/usb/lp2",
            "/dev/lp0",
            "/dev/lp1",
            NULL
        };
        for (int i = 0; alt_paths[i]; i++) {
            if (strcmp(g_printer_config.device_path, alt_paths[i]) == 0) continue;
            g_printer_fd = open(alt_paths[i], O_WRONLY | O_NOCTTY);
            if (g_printer_fd >= 0) {
                strncpy(g_printer_config.device_path, alt_paths[i], sizeof(g_printer_config.device_path) - 1);
                break;
            }
        }
        if (g_printer_fd < 0) {
            fprintf(stderr, "Failed to open printer: %s (%s)\n",
                    g_printer_config.device_path, strerror(errno));
            return -1;
        }
    }
#endif

    g_initialized = 1;
    return 0;
}

// Close printer connection
int printer_close(void) {
    if (!g_initialized) return 0;

#ifdef _WIN32
    if (g_printer != INVALID_HANDLE_VALUE) {
        CloseHandle(g_printer);
        g_printer = INVALID_HANDLE_VALUE;
    }
#else
    if (g_printer_fd >= 0) {
        close(g_printer_fd);
        g_printer_fd = -1;
    }
#endif

    g_initialized = 0;
    return 0;
}

// Internal function to send data to printer
static int printer_write_data(const void *data, size_t len) {
    if (!g_initialized) {
        if (printer_init(NULL) != 0) return -1;
    }

#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(g_printer, data, (DWORD)len, &written, NULL)) {
        return -1;
    }
    FlushFileBuffers(g_printer);
#else
    ssize_t written = write(g_printer_fd, data, len);
    if (written != (ssize_t)len) {
        return -1;
    }
    fsync(g_printer_fd);
#endif
    return 0;
}

// Print text line (handles Chinese encoding)
static int printer_print_line(const char *text) {
    if (!text) return -1;
    return printer_write_data(text, strlen(text));
}

// Print formatted line with alignment
static int printer_format_line(const char *left, const char *right, int width) {
    if (!left || !right) return -1;

    char line[128];
    int left_len = 0;
    int right_len = 0;

    // Calculate visible length (simple ASCII-only for now)
    for (const char *p = left; *p; p++) {
        if ((*p & 0x80) == 0) left_len++;  // ASCII
        else if ((*p & 0xE0) == 0xC0) { p++; left_len += 2; }  // 2-byte UTF-8
        else if ((*p & 0xF0) == 0xE0) { p += 2; left_len += 2; }  // 3-byte UTF-8
    }
    for (const char *p = right; *p; p++) {
        if ((*p & 0x80) == 0) right_len++;
        else if ((*p & 0xE0) == 0xC0) { p++; right_len += 2; }
        else if ((*p & 0xF0) == 0xE0) { p += 2; right_len += 2; }
    }

    int spaces = width - left_len - right_len;
    if (spaces < 1) spaces = 1;

    snprintf(line, sizeof(line), "%s%*s%s\n", left, spaces, "", right);
    return printer_write_data(line, strlen(line));
}

// Print receipt
int printer_print_receipt(const Receipt *receipt) {
    if (!receipt) return -1;

    char buf[256];
    struct tm *tm_info = localtime(&receipt->print_time);

    // Initialize printer if needed
    if (!g_initialized) {
        if (printer_init(NULL) != 0) {
            // Fallback: save to file if printer not available
            return printer_save_to_file(receipt);
        }
    }

    // Initialize printer
    printer_write_data(ESC_INIT, 2);

    // Shop header
    printer_write_data(ESC_ALIGN_CENTER, 3);
    printer_write_data(ESC_BOLD_ON ESC_DOUBLE_HEIGHT, 4);
    printer_print_line(receipt->shop_name);
    printer_write_data(ESC_NORMAL, 3);

    printer_write_data(ESC_BOLD_ON, 3);
    printer_print_line(receipt->shop_address);
    printer_print_line(receipt->shop_phone);
    printer_write_data(ESC_BOLD_OFF, 3);

    printer_print_line("--------------------------------");

    // Receipt info
    printer_write_data(ESC_ALIGN_LEFT, 3);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    printer_format_line("时间:", buf, g_printer_config.paper_width);

    snprintf(buf, sizeof(buf), "%08d", receipt->receipt_no);
    printer_format_line("小票号:", buf, g_printer_config.paper_width);

    printer_format_line("收银员:", receipt->cashier, g_printer_config.paper_width);

    if (receipt->member_phone[0]) {
        printer_format_line("会员:", receipt->member_phone, g_printer_config.paper_width);
        snprintf(buf, sizeof(buf), "%d", receipt->member_points);
        printer_format_line("积分:", buf, g_printer_config.paper_width);
    }

    printer_print_line("--------------------------------");

    // Items header
    printer_write_data(ESC_BOLD_ON, 3);
    printer_format_line("商品", "金额", g_printer_config.paper_width);
    printer_write_data(ESC_BOLD_OFF, 3);
    printer_print_line("--------------------------------");

    // Items
    for (int i = 0; i < receipt->item_count && i < 100; i++) {
        const ReceiptItem *item = &receipt->items[i];
        char price_str[32], qty_str[32];

        snprintf(price_str, sizeof(price_str), "%.2f", item->price);
        snprintf(qty_str, sizeof(qty_str), "x%.2f", item->quantity);

        // Item name with quantity
        printer_format_line(item->name, price_str, g_printer_config.paper_width);
        if (item->quantity > 1) {
            snprintf(buf, sizeof(buf), "  %s 小计:%.2f", qty_str, item->subtotal);
            printer_print_line(buf);
        }

        // Discount for this item
        if (item->discount > 0.001f) {
            snprintf(buf, sizeof(buf), "  -折扣: %.2f", item->discount);
            printer_print_line(buf);
        }
    }

    printer_print_line("--------------------------------");

    // Totals
    printer_write_data(ESC_BOLD_ON, 3);

    snprintf(buf, sizeof(buf), "%.2f", receipt->subtotal);
    printer_format_line("合计:", buf, g_printer_config.paper_width);

    if (receipt->discount > 0.001f) {
        snprintf(buf, sizeof(buf), "-%.2f", receipt->discount);
        printer_format_line("优惠:", buf, g_printer_config.paper_width);
    }

    snprintf(buf, sizeof(buf), "%.2f", receipt->total);
    printer_format_line("实收:", buf, g_printer_config.paper_width);

    printer_write_data(ESC_BOLD_OFF, 3);

    // Points
    if (receipt->points_earned > 0) {
        snprintf(buf, sizeof(buf), "%d", receipt->points_earned);
        printer_format_line("获得积分:", buf, g_printer_config.paper_width);
    }
    if (receipt->points_used > 0) {
        snprintf(buf, sizeof(buf), "%d", receipt->points_used);
        printer_format_line("使用积分:", buf, g_printer_config.paper_width);
    }

    printer_print_line("--------------------------------");

    // Payment
    snprintf(buf, sizeof(buf), "%.2f", receipt->cash);
    printer_format_line("现金:", buf, g_printer_config.paper_width);

    snprintf(buf, sizeof(buf), "%.2f", receipt->change);
    printer_format_line("找零:", buf, g_printer_config.paper_width);

    printer_print_line("--------------------------------");

    // Footer
    printer_write_data(ESC_ALIGN_CENTER, 3);
    printer_write_data(ESC_BOLD_ON, 3);
    printer_print_line("谢谢惠顾，欢迎下次光临!");
    printer_write_data(ESC_BOLD_OFF, 3);
    printer_print_line("");

    // Cut paper
    printer_write_data(ESC_CUT, 3);

    return 0;
}

// Fallback: Save receipt to file when printer is not available
static int printer_save_to_file(const Receipt *receipt) {
    char filepath[256];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    snprintf(filepath, sizeof(filepath), "receipt_%04d%02d%02d_%d.txt",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             receipt->receipt_no);

    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;

    char buf[256];
    struct tm *print_tm = localtime(&receipt->print_time);

    fprintf(fp, "========== 购物小票 ==========\n");
    fprintf(fp, "%s\n", receipt->shop_name);
    fprintf(fp, "%s\n", receipt->shop_address);
    fprintf(fp, "%s\n", receipt->shop_phone);
    fprintf(fp, "----------------------------\n");

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", print_tm);
    fprintf(fp, "时间: %s\n", buf);
    fprintf(fp, "小票号: %08d\n", receipt->receipt_no);
    fprintf(fp, "收银员: %s\n", receipt->cashier);

    if (receipt->member_phone[0]) {
        fprintf(fp, "会员: %s (积分: %d)\n", receipt->member_phone, receipt->member_points);
    }

    fprintf(fp, "----------------------------\n");
    fprintf(fp, "%-20s %10s\n", "商品", "金额");
    fprintf(fp, "----------------------------\n");

    for (int i = 0; i < receipt->item_count && i < 100; i++) {
        const ReceiptItem *item = &receipt->items[i];
        fprintf(fp, "%s\n", item->name);
        fprintf(fp, "  %.2f x %.2f = %.2f\n", item->price, item->quantity, item->subtotal);
        if (item->discount > 0.001f) {
            fprintf(fp, "  -折扣: %.2f\n", item->discount);
        }
    }

    fprintf(fp, "----------------------------\n");
    fprintf(fp, "%-20s %10.2f\n", "合计:", receipt->subtotal);
    if (receipt->discount > 0.001f) {
        fprintf(fp, "%-20s %10.2f\n", "优惠:", -receipt->discount);
    }
    fprintf(fp, "%-20s %10.2f\n", "实收:", receipt->total);

    if (receipt->points_earned > 0) {
        fprintf(fp, "获得积分: %d\n", receipt->points_earned);
    }
    if (receipt->points_used > 0) {
        fprintf(fp, "使用积分: %d\n", receipt->points_used);
    }

    fprintf(fp, "----------------------------\n");
    fprintf(fp, "%-20s %10.2f\n", "现金:", receipt->cash);
    fprintf(fp, "%-20s %10.2f\n", "找零:", receipt->change);
    fprintf(fp, "============================\n");
    fprintf(fp, "谢谢惠顾，欢迎下次光临!\n");

    fclose(fp);
    printf("小票已保存到文件: %s\n", filepath);
    return 0;
}

// Test page
int printer_test_page(void) {
    Receipt test_receipt;
    memset(&test_receipt, 0, sizeof(test_receipt));

    strncpy(test_receipt.shop_name, "测试超市", sizeof(test_receipt.shop_name) - 1);
    strncpy(test_receipt.shop_address, "测试地址123号", sizeof(test_receipt.shop_address) - 1);
    strncpy(test_receipt.shop_phone, "400-123-4567", sizeof(test_receipt.shop_phone) - 1);
    strncpy(test_receipt.cashier, "测试收银员", sizeof(test_receipt.cashier) - 1);
    test_receipt.receipt_no = 9999;
    test_receipt.print_time = time(NULL);

    strncpy(test_receipt.member_phone, "13800138000", sizeof(test_receipt.member_phone) - 1);
    test_receipt.member_points = 1000;

    test_receipt.item_count = 2;
    strncpy(test_receipt.items[0].name, "测试商品A", sizeof(test_receipt.items[0].name) - 1);
    test_receipt.items[0].price = 9.99f;
    test_receipt.items[0].quantity = 2.0f;
    test_receipt.items[0].subtotal = 19.98f;
    test_receipt.items[0].discount = 1.00f;

    strncpy(test_receipt.items[1].name, "测试商品B", sizeof(test_receipt.items[1].name) - 1);
    test_receipt.items[1].price = 5.50f;
    test_receipt.items[1].quantity = 1.0f;
    test_receipt.items[1].subtotal = 5.50f;
    test_receipt.items[1].discount = 0.0f;

    test_receipt.subtotal = 25.48f;
    test_receipt.discount = 1.00f;
    test_receipt.total = 24.48f;
    test_receipt.points_earned = 24;
    test_receipt.points_used = 0;
    test_receipt.cash = 30.00f;
    test_receipt.change = 5.52f;

    return printer_print_receipt(&test_receipt);
}

// Get current device path
const char* printer_get_device_path(void) {
    return g_printer_config.device_path;
}

// Check if printer is available
int printer_is_available(void) {
#ifdef _WIN32
    HANDLE test = CreateFileA(
        g_printer_config.device_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    if (test == INVALID_HANDLE_VALUE) {
        // Try COM path
        char com_path[256];
        snprintf(com_path, sizeof(com_path), "\\\\.\\%s", g_printer_config.device_path);
        test = CreateFileA(com_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (test != INVALID_HANDLE_VALUE) {
        CloseHandle(test);
        return 1;
    }
    return 0;
#else
    int test_fd = open(g_printer_config.device_path, O_WRONLY | O_NOCTTY);
    if (test_fd >= 0) {
        close(test_fd);
        return 1;
    }
    return 0;
#endif
}

// ==================== 数据看板模块 ====================

// 声明外部全局变量
extern HashNode **g_product_hash;
extern Sale *g_pending_sales;
extern Member *g_members;

/**
 * 生成完整看板数据
 */
DashboardMetrics generate_dashboard_metrics(void) {
    DashboardMetrics m;
    memset(&m, 0, sizeof(m));
    m.generated_at = time(NULL);
    
    // 获取今日时间范围
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    t->tm_hour = 0;
    t->tm_min = 0;
    t->tm_sec = 0;
    time_t today_start = mktime(t);
    
    // 昨日
    time_t yesterday_start = today_start - 24 * 3600;
    
    // 读取销售数据计算今日销售额和订单数
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *fp = fopen(sales_path, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            char *token = strtok(line, "|");
            int sale_id = atoi(token);
            token = strtok(NULL, "|"); int cashier_id = atoi(token);
            token = strtok(NULL, "|"); float total = atof(token);
            token = strtok(NULL, "|"); float discount = atof(token);
            token = strtok(NULL, "|"); float final_amount = atof(token);
            token = strtok(NULL, "|"); char *payment = strtok(NULL, "|");
            token = strtok(NULL, "|"); int status = atoi(token);
            token = strtok(NULL, "|"); time_t created_at = (time_t)atoi(token);
            
            (void)sale_id; (void)cashier_id; (void)total; (void)discount; (void)payment;
            
            if (status == SALE_COMPLETED && created_at >= today_start) {
                m.today_sales += final_amount;
                m.today_orders++;
            }
            
            if (status == SALE_COMPLETED && created_at >= yesterday_start && created_at < today_start) {
                m.yesterday_sales += final_amount;
            }
        }
        fclose(fp);
    }
    
    // 计算客单价
    if (m.today_orders > 0) {
        m.today_avg_order = m.today_sales / m.today_orders;
    }
    
    // 计算库存预警
    int low_idx = 0;
    int zero_count = 0;
    float lowest_ratio = 1.0f;
    int lowest_idx = 0;
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_product_hash[i];
        while (node) {
            Product *prod = (Product*)node->data;
            if (prod->status == 1) {
                if (prod->stock == 0) {
                    zero_count++;
                }
                
                if (prod->stock <= prod->min_stock && low_idx < 5) {
                    strncpy(m.low_stock_products[low_idx].product_id, prod->id, MAX_ID_LEN - 1);
                    strncpy(m.low_stock_products[low_idx].product_name, prod->name, MAX_NAME_LEN - 1);
                    m.low_stock_products[low_idx].stock = prod->stock;
                    m.low_stock_products[low_idx].min_stock = prod->min_stock;
                    low_idx++;
                }
                
                // 追踪库存比例最低的5个商品
                if (prod->min_stock > 0) {
                    float ratio = (float)prod->stock / prod->min_stock;
                    if (ratio < lowest_ratio) {
                        int insert_idx = -1;
                        for (int j = 0; j < 5; j++) {
                            if (m.low_stock_products[j].stock == 0) {
                                insert_idx = j;
                                break;
                            }
                        }
                        if (insert_idx < 0) {
                            insert_idx = lowest_idx;
                        }
                        if (insert_idx >= 0 && insert_idx < 5) {
                            strncpy(m.low_stock_products[insert_idx].product_id, prod->id, MAX_ID_LEN - 1);
                            strncpy(m.low_stock_products[insert_idx].product_name, prod->name, MAX_NAME_LEN - 1);
                            m.low_stock_products[insert_idx].stock = prod->stock;
                            m.low_stock_products[insert_idx].min_stock = prod->min_stock;
                        }
                        
                        // 重新计算最低比例
                        lowest_ratio = 1.0f;
                        for (int j = 0; j < 5; j++) {
                            if (m.low_stock_products[j].min_stock > 0) {
                                float r = (float)m.low_stock_products[j].stock / m.low_stock_products[j].min_stock;
                                if (r < lowest_ratio) {
                                    lowest_ratio = r;
                                }
                            }
                        }
                    }
                }
            }
            node = node->next;
        }
    }
    
    m.low_stock_count = low_idx + zero_count;
    m.zero_stock_count = zero_count;
    
    // 计算热销商品
    char sales_item_path[256];
    snprintf(sales_item_path, sizeof(sales_item_path), "%s/sale_item.txt", DATA_DIR);
    
    typedef struct { char product_id[MAX_ID_LEN]; char name[MAX_NAME_LEN]; int qty; } ProdStat;
    ProdStat stats[100] = {0};
    int stat_count = 0;
    
    fp = fopen(sales_item_path, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            char *token = strtok(line, "|");
            int item_id = atoi(token);
            token = strtok(NULL, "|"); int sale_id = atoi(token);
            char product_id[MAX_ID_LEN];
            strncpy(product_id, strtok(NULL, "|"), MAX_ID_LEN - 1);
            char product_name[MAX_NAME_LEN];
            strncpy(product_name, strtok(NULL, "|"), MAX_NAME_LEN - 1);
            token = strtok(NULL, "|"); float qty = atof(token);
            
            (void)item_id;
            
            // 检查销售单日期
            FILE *fps = fopen(sales_path, "r");
            int valid = 0;
            if (fps) {
                char sline[MAX_LINE_LEN];
                while (fgets(sline, sizeof(sline), fps)) {
                    trim(sline);
                    char *stok = strtok(sline, "|");
                    if (atoi(stok) == sale_id) {
                        for (int i = 0; i < 6; i++) stok = strtok(NULL, "|");
                        int status = atoi(stok);
                        stok = strtok(NULL, "|"); 
                        time_t created = (time_t)atoi(stok);
                        if (status == SALE_COMPLETED && created >= today_start) {
                            valid = 1;
                        }
                        break;
                    }
                }
                fclose(fps);
            }
            
            if (valid) {
                int found = -1;
                for (int i = 0; i < stat_count; i++) {
                    if (strcmp(stats[i].product_id, product_id) == 0) {
                        found = i;
                        break;
                    }
                }
                
                if (found >= 0) {
                    stats[found].qty += (int)qty;
                } else if (stat_count < 100) {
                    strncpy(stats[stat_count].product_id, product_id, MAX_ID_LEN - 1);
                    strncpy(stats[stat_count].name, product_name, MAX_NAME_LEN - 1);
                    stats[stat_count].qty = (int)qty;
                    stat_count++;
                }
            }
        }
        fclose(fp);
    }
    
    // 排序取TOP5
    for (int i = 0; i < stat_count - 1; i++) {
        for (int j = i + 1; j < stat_count; j++) {
            if (stats[j].qty > stats[i].qty) {
                ProdStat tmp = stats[i];
                stats[i] = stats[j];
                stats[j] = tmp;
            }
        }
    }
    
    for (int i = 0; i < 5 && i < stat_count; i++) {
        strncpy(m.top_products[i].product_id, stats[i].product_id, MAX_ID_LEN - 1);
        strncpy(m.top_products[i].product_name, stats[i].name, MAX_NAME_LEN - 1);
        m.top_products[i].quantity = stats[i].qty;
    }
    
    // 会员统计
    int member_count = 0;
    Member *member = g_members;
    int new_today = 0;
    while (member) {
        member_count++;
        if (member->created_at >= today_start) {
            new_today++;
        }
        member = member->next;
    }
    m.total_members = member_count;
    m.new_members_today = new_today;
    
    return m;
}

/**
 * 打印ASCII风格看板
 */
void print_dashboard(void) {
    DashboardMetrics m = generate_dashboard_metrics();
    
    char time_str[32];
    format_time(m.generated_at, time_str);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                     超市数据看板                              %s ║\n", time_str);
    printf("╠══════════════════════════════════════════════════════════════════════╣\n");
    
    // 第一行：销售指标
    printf("║  【今日销售】                                    【库存预警】          ║\n");
    printf("║  销售额: ¥%10.2f                      库存预警: %d 个商品            ║\n", 
           m.today_sales, m.low_stock_count);
    printf("║  订单数: %10d                          零库存:   %d 个商品            ║\n", 
           m.today_orders, m.zero_stock_count);
    printf("║  客单价: ¥%10.2f                                                       ║\n", 
           m.today_avg_order);
    
    // 昨日对比
    float change = 0;
    if (m.yesterday_sales > 0) {
        change = (m.today_sales - m.yesterday_sales) / m.yesterday_sales * 100;
    }
    printf("║  昨日:    ¥%9.2f  (同比: %+.1f%%)                                  ║\n", 
           m.yesterday_sales, change);
    
    printf("╠══════════════════════════════════════════════════════════════════════╣\n");
    
    // 热销商品TOP5
    printf("║  【热销商品TOP5】                                                       ║\n");
    for (int i = 0; i < 5; i++) {
        if (m.top_products[i].quantity > 0) {
            printf("║  %d. %-20s  %4d 件                                ║\n",
                   i + 1, m.top_products[i].product_name, m.top_products[i].quantity);
        } else {
            printf("║  %d. %-20s  %4s                                   ║\n",
                   i + 1, "-", "-");
        }
    }
    
    printf("╠══════════════════════════════════════════════════════════════════════╣\n");
    
    // 库存预警TOP5
    printf("║  【库存预警TOP5】                                                       ║\n");
    for (int i = 0; i < 5; i++) {
        if (m.low_stock_products[i].stock > 0 || m.low_stock_products[i].min_stock > 0) {
            printf("║  %d. %-15s  库存:%3d / %3d                              ║\n",
                   i + 1, m.low_stock_products[i].product_name, 
                   m.low_stock_products[i].stock, m.low_stock_products[i].min_stock);
        } else {
            printf("║  %d. %-15s  %4s                                       ║\n",
                   i + 1, "-", "-");
        }
    }
    
    printf("╠══════════════════════════════════════════════════════════════════════╣\n");
    
    // 会员统计
    printf("║  【会员统计】                    ║\n");
    printf("║  总会员数: %d                    ║\n", m.total_members);
    printf("║  今日新注册: %d                  ║\n", m.new_members_today);
    
    printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/**
 * 打印ASCII柱状图
 */
void print_bar_chart(const char *label, float value, float max_value, int width) {
    int bar_width = (int)((value / max_value) * width);
    if (bar_width < 0) bar_width = 0;
    if (bar_width > width) bar_width = width;
    
    printf("%-20s [", label);
    for (int i = 0; i < bar_width; i++) {
        printf("█");
    }
    for (int i = bar_width; i < width; i++) {
        printf("░");
    }
    printf("] %10.2f\n", value);
}

/**
 * 获取今日销售额
 */
float get_today_sales(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    t->tm_hour = 0;
    t->tm_min = 0;
    t->tm_sec = 0;
    time_t today_start = mktime(t);
    
    float total = 0;
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *fp = fopen(sales_path, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            char *token = strtok(line, "|");
            for (int i = 0; i < 5; i++) token = strtok(NULL, "|");
            float final_amount = atof(token ? token : "0");
            token = strtok(NULL, "|"); int status = atoi(token ? token : "0");
            token = strtok(NULL, "|"); time_t created_at = (time_t)atoi(token ? token : "0");
            
            if (status == SALE_COMPLETED && created_at >= today_start) {
                total += final_amount;
            }
        }
        fclose(fp);
    }
    
    return total;
}

/**
 * 获取今日订单数
 */
int get_today_orders(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    t->tm_hour = 0;
    t->tm_min = 0;
    t->tm_sec = 0;
    time_t today_start = mktime(t);
    
    int count = 0;
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *fp = fopen(sales_path, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            char *token = strtok(line, "|");
            for (int i = 0; i < 6; i++) token = strtok(NULL, "|");
            int status = atoi(token ? token : "0");
            token = strtok(NULL, "|"); time_t created_at = (time_t)atoi(token ? token : "0");
            
            if (status == SALE_COMPLETED && created_at >= today_start) {
                count++;
            }
        }
        fclose(fp);
    }
    
    return count;
}

/**
 * 获取库存预警
 */
void get_low_stock_alerts(int top_n) {
    printf("\n========== 库存预警 ==========\n");
    
    typedef struct { char name[50]; int stock; int min; } StockItem;
    StockItem items[100];
    int count = 0;
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_product_hash[i];
        while (node) {
            Product *prod = (Product*)node->data;
            if (prod->status == 1 && prod->stock <= prod->min_stock) {
                if (count < 100) {
                    strncpy(items[count].name, prod->name, 49);
                    items[count].stock = prod->stock;
                    items[count].min = prod->min_stock;
                    count++;
                }
            }
            node = node->next;
        }
    }
    
    // 按比例排序
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            float ratio_i = (float)items[i].stock / (items[i].min > 0 ? items[i].min : 1);
            float ratio_j = (float)items[j].stock / (items[j].min > 0 ? items[j].min : 1);
            if (ratio_j < ratio_i) {
                StockItem tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }
    
    printf("%-20s %-10s %-10s\n", "商品名称", "当前库存", "最低库存");
    printf("------------------------------------------------\n");
    
    for (int i = 0; i < top_n && i < count; i++) {
        printf("%-20s %-10d %-10d\n", items[i].name, items[i].stock, items[i].min);
    }
    
    if (count == 0) {
        printf("所有商品库存充足\n");
    }
    printf("\n");
}

/**
 * 获取热销商品
 */
void get_top_selling_products(int top_n) {
    printf("\n========== 热销商品TOP%d ==========\n", top_n);
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    t->tm_hour = 0;
    t->tm_min = 0;
    t->tm_sec = 0;
    time_t today_start = mktime(t);
    
    typedef struct { char id[20]; char name[50]; int qty; } SellItem;
    SellItem items[100] = {0};
    int count = 0;
    
    char sales_path[256], item_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    snprintf(item_path, sizeof(item_path), "%s/sale_item.txt", DATA_DIR);
    
    FILE *fpi = fopen(item_path, "r");
    if (fpi) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fpi)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            char *token = strtok(line, "|");
            int item_id = atoi(token);
            token = strtok(NULL, "|"); int sale_id = atoi(token);
            char pid[20], pname[50];
            strncpy(pid, strtok(NULL, "|"), 19);
            strncpy(pname, strtok(NULL, "|"), 49);
            token = strtok(NULL, "|"); float qty = atof(token);
            
            (void)item_id;
            
            // 检查销售单
            FILE *fps = fopen(sales_path, "r");
            if (fps) {
                char sline[MAX_LINE_LEN];
                while (fgets(sline, sizeof(sline), fps)) {
                    trim(sline);
                    char *stok = strtok(sline, "|");
                    if (atoi(stok) == sale_id) {
                        for (int i = 0; i < 6; i++) stok = strtok(NULL, "|");
                        int status = atoi(stok);
                        stok = strtok(NULL, "|");
                        time_t created = (time_t)atoi(stok);
                        if (status == SALE_COMPLETED && created >= today_start) {
                            int found = -1;
                            for (int j = 0; j < count; j++) {
                                if (strcmp(items[j].id, pid) == 0) {
                                    found = j;
                                    break;
                                }
                            }
                            if (found >= 0) {
                                items[found].qty += (int)qty;
                            } else if (count < 100) {
                                strcpy(items[count].id, pid);
                                strcpy(items[count].name, pname);
                                items[count].qty = (int)qty;
                                count++;
                            }
                        }
                        break;
                    }
                }
                fclose(fps);
            }
        }
        fclose(fpi);
    }
    
    // 排序
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (items[j].qty > items[i].qty) {
                SellItem tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }
    
    printf("%-4s %-20s %-10s\n", "排名", "商品名称", "销量");
    printf("------------------------------------------------\n");
    
    for (int i = 0; i < top_n && i < count; i++) {
        printf("%-4d %-20s %-10d\n", i + 1, items[i].name, items[i].qty);
    }
    
    if (count == 0) {
        printf("今日暂无销售数据\n");
    }
    printf("\n");
}
