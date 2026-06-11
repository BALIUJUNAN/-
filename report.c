/**
 * @file report.c
 * @brief 报表模块 - 销售/库存/采购报表，支持CSV/HTML导出
 */

#include "supermarket.h"
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#define mkdir_recursive(dir) _mkdir(dir)
#else
#include <sys/stat.h>
#define mkdir_recursive(dir) mkdir(dir, 0755)
#endif

// 确保目录存在
static int ensure_dir(const char *dir) {
    return mkdir_recursive(dir);
}

// ==================== 报表类型 ====================
typedef enum {
    REPORT_SALES,        // 销售报表
    REPORT_INVENTORY,    // 库存报表
    REPORT_PURCHASE,     // 采购报表
    REPORT_STOCK_LOG     // 库存变动报表
} ReportType;

// ==================== 时间筛选 ====================

/**
 * 获取时间范围内的日期字符串
 */
void get_date_range(TimeRange *range, int days_ago) {
    time_t now = time(NULL);
    range->end = now;
    range->start = now - (days_ago * 24 * 3600);
}

/**
 * 格式化时间
 */
void format_time(time_t t, char *buffer) {
    struct tm *tm = localtime(&t);
    strftime(buffer, 32, "%Y-%m-%d %H:%M", tm);
}

// ==================== 销售报表 ====================

/**
 * 生成销售报表
 */
void generate_sales_report(time_t start, time_t end, const char *format) {
    printf("\n========== 销售报表 (%s) ==========\n", 
           format == NULL ? "控制台" : format);
    
    char start_str[32], end_str[32];
    format_time(start, start_str);
    format_time(end, end_str);
    printf("统计周期: %s 至 %s\n\n", start_str, end_str);
    
    // 统计变量
    int total_orders = 0;
    float total_amount = 0;
    float total_discount = 0;
    float cash_total = 0, wechat_total = 0, alipay_total = 0;
    int cash_count = 0, wechat_count = 0, alipay_count = 0;
    
    // 读取销售文件
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/sales.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        printf("无销售记录\n");
        return;
    }
    
    char line[MAX_LINE_LEN];
    printf("%-8s %-8s %-12s %-10s %-8s %-10s %-10s\n",
           "订单号", "收银员", "时间", "原价", "优惠", "实收", "支付方式");
    printf("--------------------------------------------------------------------\n");
    
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        // 复制一份用于分割
        char copy[MAX_LINE_LEN];
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        
        Sale sale;
        memset(&sale, 0, sizeof(Sale));
        
        char *token;
        char *saveptr;
        
        // 解析: id|cashier_id|member_id|total_amount|discount|final_amount|payment_method|status|created_at|completed_at
        token = strtok_r(copy, "|", &saveptr);
        sale.id = token ? atoi(token) : 0;
        
        token = strtok_r(NULL, "|", &saveptr);
        sale.cashier_id = token ? atoi(token) : 0;

        token = strtok_r(NULL, "|", &saveptr);  // member_id（跳过）

        token = strtok_r(NULL, "|", &saveptr);
        sale.total_amount = token ? atof(token) : 0.0f;
        
        token = strtok_r(NULL, "|", &saveptr);
        sale.discount = token ? atof(token) : 0.0f;
        
        token = strtok_r(NULL, "|", &saveptr);
        sale.final_amount = token ? atof(token) : 0.0f;
        
        token = strtok_r(NULL, "|", &saveptr);
        if (token) strncpy(sale.payment_method, token, sizeof(sale.payment_method) - 1);
        
        token = strtok_r(NULL, "|", &saveptr);
        sale.status = token ? atoi(token) : 0;
        
        token = strtok_r(NULL, "|", &saveptr);
        sale.created_at = token ? (time_t)atoll(token) : 0;
        
        token = strtok_r(NULL, "|", &saveptr);
        sale.completed_at = token ? (time_t)atoll(token) : 0;
        
        if (sale.created_at < start || sale.created_at > end) continue;
        if (sale.status != SALE_COMPLETED) continue;
        
        total_orders++;
        total_amount += sale.total_amount;
        total_discount += sale.discount;  // 使用存储的优惠金额
        
        char time_str[32];
        format_time(sale.created_at, time_str);
        
        printf("%-8d %-8d %-12s %-10.2f %-8.2f %-10.2f %-10s\n",
               sale.id, sale.cashier_id, time_str,
               sale.total_amount, sale.discount, sale.final_amount,
               sale.payment_method);
        
        // 按支付方式统计
        if (strcmp(sale.payment_method, "现金") == 0) {
            cash_total += sale.final_amount;
            cash_count++;
        } else if (strcmp(sale.payment_method, "微信") == 0) {
            wechat_total += sale.final_amount;
            wechat_count++;
        } else if (strcmp(sale.payment_method, "支付宝") == 0) {
            alipay_total += sale.final_amount;
            alipay_count++;
        }
    }
    
    fclose(fp);
    
    // 打印汇总
    printf("--------------------------------------------------------------------\n");
    printf("总订单数: %d\n", total_orders);
    printf("总销售额: ¥%.2f\n", total_amount);
    printf("总优惠: ¥%.2f\n", total_discount);
    printf("实收总额: ¥%.2f\n", total_amount - total_discount);
    printf("\n支付方式统计:\n");
    printf("  现金: %d笔, ¥%.2f\n", cash_count, cash_total);
    printf("  微信: %d笔, ¥%.2f\n", wechat_count, wechat_total);
    printf("  支付宝: %d笔, ¥%.2f\n", alipay_count, alipay_total);
    printf("================================\n\n");
}

/**
 * 导出销售报表到CSV
 */
int export_sales_csv(time_t start, time_t end, const char *filename) {
    char filepath[256];
    
    // 确保输出目录存在
    ensure_dir(OUTPUT_DIR);
    
    // 导出到output目录
    snprintf(filepath, sizeof(filepath), "%s/%s", OUTPUT_DIR, filename);
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        printf("[错误] 无法创建文件: %s\n", filepath);
        printf("[提示] 请检查目录写入权限\n");
        return -1;
    }
    
    // 写入UTF-8 BOM
    fprintf(fp, "\xEF\xBB\xBF");
    fprintf(fp, "订单号,收银员ID,时间,原价,优惠,实收,支付方式,状态\n");
    
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *src = fopen(sales_path, "r");
    if (src) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), src)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            // 复制一份用于分割，避免修改原字符串
            char copy[MAX_LINE_LEN];
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            
            Sale sale;
            memset(&sale, 0, sizeof(Sale));
            
            char *token;
            char *saveptr;
            
            // 解析字段: id|cashier_id|member_id|total_amount|discount|final_amount|payment_method|status|created_at|completed_at
            token = strtok_r(copy, "|", &saveptr);
            sale.id = token ? atoi(token) : 0;

            token = strtok_r(NULL, "|", &saveptr);
            sale.cashier_id = token ? atoi(token) : 0;

            token = strtok_r(NULL, "|", &saveptr);  // member_id（跳过）

            token = strtok_r(NULL, "|", &saveptr);
            sale.total_amount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            sale.discount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            sale.final_amount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            if (token) strncpy(sale.payment_method, token, sizeof(sale.payment_method) - 1);

            token = strtok_r(NULL, "|", &saveptr);
            sale.status = token ? atoi(token) : 0;

            token = strtok_r(NULL, "|", &saveptr);
            sale.created_at = token ? (time_t)atoll(token) : 0;

            if (sale.created_at < start || sale.created_at > end) continue;

            char time_str[32];
            format_time(sale.created_at, time_str);

            fprintf(fp, "%d,%d,%s,%.2f,%.2f,%.2f,%s,%s\n",
                sale.id, sale.cashier_id, time_str,
                sale.total_amount, sale.discount, sale.final_amount,
                sale.payment_method,
                sale.status == SALE_COMPLETED ? "已完成" : "已退款");
        }
        fclose(src);
    }

    fclose(fp);
    printf("销售报表已导出: %s\n", filepath);
    return 0;
}

/**
 * 导出销售报表到HTML
 */
int export_sales_html(time_t start, time_t end, const char *filename) {
    char filepath[256];
    
    // 确保输出目录存在
    ensure_dir(OUTPUT_DIR);
    
    // 导出到output目录
    snprintf(filepath, sizeof(filepath), "%s/%s", OUTPUT_DIR, filename);
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        printf("[错误] 无法创建文件: %s\n", filepath);
        printf("[提示] 请检查目录写入权限\n");
        return -1;
    }
    
    fprintf(fp, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(fp, "<meta charset=\"UTF-8\">\n");
    fprintf(fp, "<title>销售报表</title>\n");
    fprintf(fp, "<style>\n");
    fprintf(fp, "body { font-family: Arial, sans-serif; margin: 20px; }\n");
    fprintf(fp, "table { border-collapse: collapse; width: 100%%; }\n");
    fprintf(fp, "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n");
    fprintf(fp, "th { background-color: #4CAF50; color: white; }\n");
    fprintf(fp, "tr:nth-child(even) { background-color: #f2f2f2; }\n");
    fprintf(fp, ".summary { margin: 20px 0; font-size: 18px; }\n");
    fprintf(fp, "</style>\n</head>\n<body>\n");
    
    char start_str[32], end_str[32];
    format_time(start, start_str);
    format_time(end, end_str);
    
    fprintf(fp, "<h1>销售报表</h1>\n");
    fprintf(fp, "<p>统计周期: %s 至 %s</p>\n", start_str, end_str);
    
    fprintf(fp, "<table>\n");
    fprintf(fp, "<tr><th>订单号</th><th>收银员ID</th><th>时间</th><th>原价</th>"
               "<th>优惠</th><th>实收</th><th>支付方式</th><th>状态</th></tr>\n");
    
    float total = 0;
    int count = 0;
    
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *src = fopen(sales_path, "r");
    if (src) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), src)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            // 复制一份用于分割
            char copy[MAX_LINE_LEN];
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            
            Sale sale;
            memset(&sale, 0, sizeof(Sale));
            
            char *token;
            char *saveptr;
            
            token = strtok_r(copy, "|", &saveptr);
            sale.id = token ? atoi(token) : 0;

            token = strtok_r(NULL, "|", &saveptr);
            sale.cashier_id = token ? atoi(token) : 0;

            token = strtok_r(NULL, "|", &saveptr);  // member_id（跳过）

            token = strtok_r(NULL, "|", &saveptr);
            sale.total_amount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            sale.discount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            sale.final_amount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            if (token) strncpy(sale.payment_method, token, sizeof(sale.payment_method) - 1);

            token = strtok_r(NULL, "|", &saveptr);
            sale.status = token ? atoi(token) : 0;

            token = strtok_r(NULL, "|", &saveptr);
            sale.created_at = token ? (time_t)atoll(token) : 0;

            if (sale.created_at < start || sale.created_at > end) continue;
            
            char time_str[32];
            format_time(sale.created_at, time_str);
            
            fprintf(fp, "<tr><td>%d</td><td>%d</td><td>%s</td>"
                       "<td>¥%.2f</td><td>¥%.2f</td><td>¥%.2f</td>"
                       "<td>%s</td><td>%s</td></tr>\n",
                sale.id, sale.cashier_id, time_str,
                sale.total_amount, sale.discount, sale.final_amount,
                sale.payment_method,
                sale.status == SALE_COMPLETED ? "已完成" : "已退款");
            
            total += sale.final_amount;
            count++;
        }
        fclose(src);
    }
    
    fprintf(fp, "</table>\n");
    fprintf(fp, "<div class=\"summary\">\n");
    fprintf(fp, "<p>总订单数: <strong>%d</strong></p>\n", count);
    fprintf(fp, "<p>销售总额: <strong>¥%.2f</strong></p>\n", total);
    fprintf(fp, "</div>\n");
    fprintf(fp, "<p>生成时间: %s</p>\n", ctime(&(time_t){time(NULL)}));
    fprintf(fp, "</body>\n</html>\n");
    
    fclose(fp);
    printf("HTML报表已导出: %s\n", filepath);
    return 0;
}

// ==================== 库存报表 ====================

/**
 * 生成库存报表
 */
void generate_inventory_report(const char *format) {
    printf("\n========== 库存报表 (%s) ==========\n",
           format == NULL ? "控制台" : format);
    
    int total_products = 0;
    float total_cost_value = 0;
    float total_sale_value = 0;
    int low_stock_count = 0;
    
    printf("%-12s %-20s %-8s %-8s %-10s %-10s\n",
           "商品ID", "商品名称", "库存", "最低库存", "成本价", "零售价");
    printf("------------------------------------------------------------\n");
    
    for (int i = 0; i < g_product_hash->size; i++) {
        HashNode *node = g_product_hash->buckets[i];
        while (node) {
            Product *prod = (Product*)node->data;
            if (prod->status == 1) {
                total_products++;
                total_cost_value += prod->stock * prod->cost;
                total_sale_value += prod->stock * prod->price;
                
                if (prod->stock <= prod->min_stock) {
                    low_stock_count++;
                    printf("[预警] ");
                }
                
                printf("%-12s %-20s %-8d %-8d %-10.2f %-10.2f\n",
                       prod->id, prod->name, prod->stock, prod->min_stock,
                       prod->cost, prod->price);
            }
            node = node->next;
        }
    }
    
    printf("------------------------------------------------------------\n");
    printf("商品总数: %d\n", total_products);
    printf("库存不足商品: %d\n", low_stock_count);
    printf("成本总额: ¥%.2f\n", total_cost_value);
    printf("零售总额: ¥%.2f\n", total_sale_value);
    printf("================================\n\n");
}

// ==================== 采购报表 ====================

/**
 * 生成采购报表
 */
void generate_purchase_report(time_t start, time_t end, const char *format) {
    printf("\n========== 采购报表 (%s) ==========\n",
           format == NULL ? "控制台" : format);
    
    char start_str[32], end_str[32];
    format_time(start, start_str);
    format_time(end, end_str);
    printf("统计周期: %s 至 %s\n\n", start_str, end_str);
    
    float total_pending = 0, total_approved = 0, total_completed = 0;
    int count_pending = 0, count_approved = 0, count_completed = 0;
    
    printf("%-8s %-15s %-12s %-10s %-10s\n",
           "订单号", "供应商", "创建时间", "金额", "状态");
    printf("------------------------------------------------------------\n");
    
    Purchase *pur = g_purchases;
    while (pur) {
        if (pur->created_at >= start && pur->created_at <= end) {
            char time_str[32];
            format_time(pur->created_at, time_str);
            
            printf("%-8d %-15s %-12s %-10.2f %-10s\n",
                   pur->id, pur->supplier_name, time_str,
                   pur->total_amount, get_purchase_status_str(pur->status));
            
            switch (pur->status) {
                case PURCHASE_PENDING:
                    total_pending += pur->total_amount;
                    count_pending++;
                    break;
                case PURCHASE_APPROVED:
                    total_approved += pur->total_amount;
                    count_approved++;
                    break;
                case PURCHASE_COMPLETED:
                    total_completed += pur->total_amount;
                    count_completed++;
                    break;
            }
        }
        pur = pur->next;
    }
    
    printf("------------------------------------------------------------\n");
    printf("待审核: %d单, ¥%.2f\n", count_pending, total_pending);
    printf("已审核: %d单, ¥%.2f\n", count_approved, total_approved);
    printf("已完成: %d单, ¥%.2f\n", count_completed, total_completed);
    printf("================================\n\n");
}

// ==================== 盈亏报表 ====================

/**
 * 获取商品的真实进价（从采购单或商品表）
 * 优先使用采购单中该商品的最新进价，如果不存在则使用商品表的进价
 */
static float get_product_cost(const char *product_id, time_t before_date) {
    // 先查找采购单中的历史进价
    PurchaseItem *item = g_purchase_items;
    time_t latest_date = 0;
    float latest_cost = 0;
    
    while (item) {
        if (strcmp(item->product_id, product_id) == 0) {
            // 查找对应的采购单日期
            Purchase *pur = g_purchases;
            while (pur) {
                if (pur->id == item->purchase_id && 
                    pur->completed_at <= before_date &&
                    pur->status == PURCHASE_COMPLETED &&
                    pur->completed_at > latest_date) {
                    latest_date = pur->completed_at;
                    latest_cost = item->price;
                    break;
                }
                pur = pur->next;
            }
        }
        item = item->next;
    }
    
    // 如果找到了历史进价，返回它
    if (latest_cost > 0) {
        return latest_cost;
    }
    
    // 否则使用商品表的进价
    Product *prod = find_product_by_id(product_id);
    if (prod) {
        return prod->cost;
    }
    
    return 0;
}

/**
 * 生成盈亏报告 - 使用真实进价
 */
void generate_profit_loss_report(time_t start, time_t end) {
    printf("\n========== 盈亏报告 ==========\n");

    char start_str[32], end_str[32];
    format_time(start, start_str);
    format_time(end, end_str);
    printf("统计周期: %s 至 %s\n\n", start_str, end_str);

    // 1. 销售收入与优惠统计
    float sales_original = 0;   // 原价总额
    float sales_revenue = 0;    // 实收总额
    float total_discount = 0;   // 总优惠
    int completed_orders = 0;

    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);

    FILE *fp = fopen(sales_path, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;

            char copy[MAX_LINE_LEN];
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';

            char *token;
            char *saveptr;

            // 解析: id|cashier_id|member_id|total_amount|discount|final_amount|payment_method|status|created_at|completed_at
            token = strtok_r(copy, "|", &saveptr);  // id
            token = strtok_r(NULL, "|", &saveptr);   // cashier_id
            token = strtok_r(NULL, "|", &saveptr);   // member_id

            float total_amount = 0, discount = 0, final_amount = 0;
            int status = 0;
            time_t created_at = 0;

            token = strtok_r(NULL, "|", &saveptr);
            total_amount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            discount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);
            final_amount = token ? atof(token) : 0.0f;

            token = strtok_r(NULL, "|", &saveptr);  // payment_method

            token = strtok_r(NULL, "|", &saveptr);
            status = token ? atoi(token) : 0;

            token = strtok_r(NULL, "|", &saveptr);
            created_at = token ? (time_t)atoll(token) : 0;

            if (created_at >= start && created_at <= end && status == SALE_COMPLETED) {
                sales_original += total_amount;      // 原价合计
                total_discount += discount;           // 总优惠
                sales_revenue += final_amount;        // 实收
                completed_orders++;
            }
        }
        fclose(fp);
    }

    // 2. 计算销售商品的真实成本（从库存变动日志中获取出库记录）
    float cogs = 0;  // Cost of Goods Sold 销售成本
    {
        int log_count = 0;
        StockLog *logs = query_stock_logs(NULL, start, end, &log_count);
        for (int i = 0; i < log_count; i++) {
            if (strcmp(logs[i].type, "出库") == 0) {
                // 优先从采购记录中获取进价，否则用商品表进价
                float cost = get_product_cost(logs[i].product_id, logs[i].created_at);
                cogs += cost * logs[i].quantity;
            }
        }
        if (logs) free(logs);
    }

    // 3. 采购成本统计（仅用于参考展示）
    float purchase_cost = 0;
    Purchase *pur = g_purchases;
    while (pur) {
        if (pur->completed_at >= start && pur->completed_at <= end &&
            pur->status == PURCHASE_COMPLETED) {
            purchase_cost += pur->total_amount;
        }
        pur = pur->next;
    }

    // 4. 计算毛利 = 实收 - 销售成本
    float gross_profit = sales_revenue - cogs;

    // 5. 月固定成本摊销
    float days = (end - start) / (24.0f * 3600.0f);
    float monthly_fixed = g_config.monthly_fixed_cost;
    float period_fixed_cost = monthly_fixed * (days / 30.0f);

    // 6. 净利润
    float net_profit = gross_profit - period_fixed_cost;
    float gross_margin = sales_revenue > 0 ? (gross_profit / sales_revenue * 100) : 0;
    float net_margin = sales_revenue > 0 ? (net_profit / sales_revenue * 100) : 0;

    printf("【销售概况】\n");
    printf("完成订单数:   %d 笔\n", completed_orders);
    printf("原价总额:     ¥%.2f\n", sales_original);
    printf("优惠总额:     ¥%.2f\n", total_discount);
    printf("实收总额:     ¥%.2f\n", sales_revenue);

    printf("\n【成本分析】\n");
    printf("销售成本:     ¥%.2f (基于库存出库记录)\n", cogs);
    printf("同期采购总额: ¥%.2f (参考)\n", purchase_cost);
    printf("固定成本摊销: ¥%.2f (月固定: ¥%.2f, 天数: %.1f)\n", period_fixed_cost, monthly_fixed, days);

    printf("\n【利润分析】\n");
    printf("毛利:         ¥%.2f (毛利率: %.1f%%)\n", gross_profit, gross_margin);
    printf("净利:         ¥%.2f (净利率: %.1f%%)\n", net_profit, net_margin);
    printf("==============================\n\n");

    // 打印说明
    printf("[注] 销售成本 = 期间内出库商品的进价合计\n");
    printf("[注] 毛利 = 实收总额 - 销售成本\n");
    printf("[注] 净利 = 毛利 - 固定成本摊销\n\n");
}

// ==================== 导出功能 ====================

/**
 * 导出报表菜单
 */
void show_export_menu(void) {
    printf("\n========== 导出报表 ==========\n");
    printf("1. 销售报表 (CSV)\n");
    printf("2. 销售报表 (HTML)\n");
    printf("3. 库存报表 (CSV)\n");
    printf("4. 库存报表 (HTML)\n");
    printf("5. 返回\n");
    printf("请选择: ");
}

/**
 * 导出报表处理
 */
void handle_export(int choice) {
    TimeRange range;
    char filename[128];
    
    switch (choice) {
        case 1:
            get_date_range(&range, 30);
            snprintf(filename, sizeof(filename), "sales_report_%lld.csv", (long long)time(NULL));
            export_sales_csv(range.start, range.end, filename);
            break;
        case 2:
            get_date_range(&range, 30);
            snprintf(filename, sizeof(filename), "sales_report_%lld.html", (long long)time(NULL));
            export_sales_html(range.start, range.end, filename);
            break;
        case 3:
            // 库存报表CSV
            printf("库存报表CSV导出功能\n");
            break;
        case 4:
            // 库存报表HTML
            printf("库存报表HTML导出功能\n");
            break;
        default:
            printf("无效选择\n");
    }
}
