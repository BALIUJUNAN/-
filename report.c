/**
 * @file report.c
 * @brief 报表模块 - 销售/库存/采购/盈亏报表，支持 CSV/HTML 导出
 *
 * 本模块提供四类报表：
 *   1. 销售报表：按时间区间统计订单数、销售额、优惠、支付方式分布
 *   2. 库存报表：商品库存清单、成本/零售价值、预警统计
 *   3. 采购报表：按状态分类统计采购订单
 *   4. 盈亏报告：收入 - 销售成本(COGS) - 固定成本 = 净利润
 *
 * 导出格式：
 *   - 控制台输出（默认）
 *   - CSV 文件（UTF-8 BOM，Excel 兼容）
 *   - HTML 文件（带 CSS 样式的表格）
 *
 * 盈亏报告的 COGS 计算逻辑：
 *   从库存账本 type/time 索引中筛选"出库"记录，
 *   每条出库记录的成本 = 该商品的进价（优先从采购单历史取，否则用商品表进价）。
 */

#include "supermarket.h"
#include "app/sm_sales_service.h"
#include "app/sm_inventory_service.h"
#include "app/sm_purchase_service.h"
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
    
    Sale *sales = NULL;
    size_t sale_count = 0;
    size_t sale_index;
    if (sm_service_sale_list_completed(&sales, &sale_count) != SM_REPO_OK) {
        printf("无销售记录\n");
        return;
    }

    printf("%-8s %-8s %-12s %-10s %-8s %-10s %-10s\n",
           "订单号", "收银员", "时间", "原价", "优惠", "实收", "支付方式");
    printf("--------------------------------------------------------------------\n");
    
    for (sale_index = 0; sale_index < sale_count; ++sale_index) {
        Sale *sale = &sales[sale_index];
        char time_str[32];
        if (sale->completed_at < start || sale->completed_at > end) continue;
        total_orders++;
        total_amount += sale->total_amount;
        total_discount += sale->discount;
        format_time(sale->completed_at, time_str);
        printf("%-8d %-8d %-12s %-10.2f %-8.2f %-10.2f %-10s\n",
               sale->id, sale->cashier_id, time_str,
               sale->total_amount, sale->discount, sale->final_amount,
               sale->payment_method);
        if (strcmp(sale->payment_method, "现金") == 0) {
            cash_total += sale->final_amount;
            cash_count++;
        } else if (strcmp(sale->payment_method, "微信") == 0) {
            wechat_total += sale->final_amount;
            wechat_count++;
        } else if (strcmp(sale->payment_method, "支付宝") == 0) {
            alipay_total += sale->final_amount;
            alipay_count++;
        }
    }
    sm_service_sales_array_free(sales);
    
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
    
    {
        Sale *sales = NULL;
        size_t count = 0;
        size_t i;
        if (sm_service_sale_list_completed(&sales, &count) == SM_REPO_OK) {
            for (i = 0; i < count; ++i) {
                char time_str[32];
                if (sales[i].completed_at < start ||
                    sales[i].completed_at > end)
                    continue;
                format_time(sales[i].completed_at, time_str);
                fprintf(fp, "%d,%d,%s,%.2f,%.2f,%.2f,%s,已完成\n",
                        sales[i].id, sales[i].cashier_id, time_str,
                        sales[i].total_amount, sales[i].discount,
                        sales[i].final_amount, sales[i].payment_method);
            }
        }
        sm_service_sales_array_free(sales);
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
    
    {
        Sale *sales = NULL;
        size_t sale_count = 0;
        size_t i;
        if (sm_service_sale_list_completed(&sales, &sale_count) == SM_REPO_OK) {
            for (i = 0; i < sale_count; ++i) {
                char time_str[32];
                if (sales[i].completed_at < start ||
                    sales[i].completed_at > end)
                    continue;
                format_time(sales[i].completed_at, time_str);
                fprintf(fp, "<tr><td>%d</td><td>%d</td><td>%s</td>"
                            "<td>¥%.2f</td><td>¥%.2f</td><td>¥%.2f</td>"
                            "<td>%s</td><td>已完成</td></tr>\n",
                        sales[i].id, sales[i].cashier_id, time_str,
                        sales[i].total_amount, sales[i].discount,
                        sales[i].final_amount, sales[i].payment_method);
                total += sales[i].final_amount;
                ++count;
            }
        }
        sm_service_sales_array_free(sales);
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
    int product_count = 0;
    Product **products = list_products(&product_count);
    printf("\n========== 库存报表 (%s) ==========\n",
           format == NULL ? "控制台" : format);
    
    int total_products = 0;
    float total_cost_value = 0;
    float total_sale_value = 0;
    int low_stock_count = 0;
    
    printf("%-12s %-20s %-8s %-8s %-10s %-10s\n",
           "商品ID", "商品名称", "库存", "最低库存", "成本价", "零售价");
    printf("------------------------------------------------------------\n");
    
    for (int i = 0; i < product_count; ++i) {
        Product *prod = products[i];
        if (prod->status == STATUS_ACTIVE) {
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
    }
    free(products);
    
    printf("------------------------------------------------------------\n");
    printf("商品总数: %d\n", total_products);
    printf("库存不足商品: %d\n", low_stock_count);
    printf("成本总额: ¥%.2f\n", total_cost_value);
    printf("零售总额: ¥%.2f\n", total_sale_value);
    printf("================================\n\n");
}

// ==================== 采购报表 ====================

/**
 * 导出库存报表到CSV
 */
int export_inventory_csv(const char *filename) {
    char filepath[256];
    int product_count = 0;
    Product **products = list_products(&product_count);

    ensure_dir(OUTPUT_DIR);
    snprintf(filepath, sizeof(filepath), "%s/%s", OUTPUT_DIR, filename);

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        free(products);
        printf("[错误] 无法创建文件: %s\n", filepath);
        return -1;
    }

    /* UTF-8 BOM（让 Excel 正确识别中文） */
    fprintf(fp, "\xEF\xBB\xBF");
    fprintf(fp, "商品ID,商品名称,条码,库存,最低库存,成本价,零售价,库存成本,库存售价,状态\n");

    int total_products = 0;
    float total_cost_value = 0;
    float total_sale_value = 0;
    int low_stock_count = 0;

    for (int i = 0; i < product_count; ++i) {
        Product *prod = products[i];
        if (prod->status == STATUS_ACTIVE) {
            total_products++;
            float item_cost = prod->stock * prod->cost;
            float item_sale = prod->stock * prod->price;
            total_cost_value += item_cost;
            total_sale_value += item_sale;
            const char *status_str = "正常";
            if (prod->stock <= prod->min_stock) {
                status_str = "库存预警";
                low_stock_count++;
            }
            fprintf(fp, "%s,%s,%s,%d,%d,%.2f,%.2f,%.2f,%.2f,%s\n",
                    prod->id, prod->name, prod->barcode,
                    prod->stock, prod->min_stock,
                    prod->cost, prod->price,
                    item_cost, item_sale, status_str);
        }
    }
    free(products);

    /* 汇总行 */
    fprintf(fp, "\n汇总,,,,,,,\n");
    fprintf(fp, "商品总数,%d\n", total_products);
    fprintf(fp, "库存预警商品,%d\n", low_stock_count);
    fprintf(fp, "成本总额,,%.2f\n", total_cost_value);
    fprintf(fp, "零售总额,,%.2f\n", total_sale_value);

    fclose(fp);
    printf("库存报表已导出: %s\n", filepath);
    return 0;
}

/**
 * 导出库存报表到HTML
 */
int export_inventory_html(const char *filename) {
    char filepath[256];
    int product_count = 0;
    Product **products = list_products(&product_count);

    ensure_dir(OUTPUT_DIR);
    snprintf(filepath, sizeof(filepath), "%s/%s", OUTPUT_DIR, filename);

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        free(products);
        printf("[错误] 无法创建文件: %s\n", filepath);
        return -1;
    }

    fprintf(fp, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(fp, "<meta charset=\"UTF-8\">\n");
    fprintf(fp, "<title>库存报表</title>\n");
    fprintf(fp, "<style>\n");
    fprintf(fp, "body { font-family: Arial, sans-serif; margin: 20px; }\n");
    fprintf(fp, "table { border-collapse: collapse; width: 100%%; }\n");
    fprintf(fp, "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n");
    fprintf(fp, "th { background-color: #2196F3; color: white; }\n");
    fprintf(fp, "tr:nth-child(even) { background-color: #f2f2f2; }\n");
    fprintf(fp, ".warning { background-color: #fff3cd; color: #856404; font-weight: bold; }\n");
    fprintf(fp, ".summary { margin: 20px 0; font-size: 18px; }\n");
    fprintf(fp, "</style>\n</head>\n<body>\n");

    fprintf(fp, "<h1>库存报表</h1>\n");

    time_t now = time(NULL);
    char time_str[32];
    format_time(now, time_str);
    fprintf(fp, "<p>生成时间: %s</p>\n", time_str);

    fprintf(fp, "<table>\n");
    fprintf(fp, "<tr><th>商品ID</th><th>商品名称</th><th>条码</th>"
               "<th>库存</th><th>最低库存</th><th>成本价</th>"
               "<th>零售价</th><th>库存成本</th><th>库存售价</th><th>状态</th></tr>\n");

    int total_products = 0;
    float total_cost_value = 0;
    float total_sale_value = 0;
    int low_stock_count = 0;

    for (int i = 0; i < product_count; ++i) {
        Product *prod = products[i];
        if (prod->status == STATUS_ACTIVE) {
            total_products++;
            float item_cost = prod->stock * prod->cost;
            float item_sale = prod->stock * prod->price;
            int is_low = (prod->stock <= prod->min_stock);
            total_cost_value += item_cost;
            total_sale_value += item_sale;
            if (is_low) low_stock_count++;
            fprintf(fp, "<tr%s><td>%s</td><td>%s</td><td>%s</td>"
                       "<td>%d</td><td>%d</td><td>¥%.2f</td>"
                       "<td>¥%.2f</td><td>¥%.2f</td><td>¥%.2f</td>"
                       "<td>%s</td></tr>\n",
                    is_low ? " class=\"warning\"" : "",
                    prod->id, prod->name, prod->barcode,
                    prod->stock, prod->min_stock,
                    prod->cost, prod->price,
                    item_cost, item_sale,
                    is_low ? "⚠ 库存预警" : "正常");
        }
    }
    free(products);

    fprintf(fp, "</table>\n");

    fprintf(fp, "<div class=\"summary\">\n");
    fprintf(fp, "<p>商品总数: <strong>%d</strong></p>\n", total_products);
    fprintf(fp, "<p>库存预警商品: <strong>%d</strong></p>\n", low_stock_count);
    fprintf(fp, "<p>成本总额: <strong>¥%.2f</strong></p>\n", total_cost_value);
    fprintf(fp, "<p>零售总额: <strong>¥%.2f</strong></p>\n", total_sale_value);
    fprintf(fp, "</div>\n");
    fprintf(fp, "</body>\n</html>\n");

    fclose(fp);
    printf("库存报表已导出: %s\n", filepath);
    return 0;
}

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
    
    Purchase *purchases = NULL;
    size_t purchase_count = 0, purchase_index;
    if (sm_service_purchase_list(-1, &purchases,
                                 &purchase_count) != SM_REPO_OK)
        purchase_count = 0;
    for (purchase_index = 0; purchase_index < purchase_count;
         ++purchase_index) {
        Purchase *pur = &purchases[purchase_index];
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
    }
    sm_service_purchase_array_free(purchases);
    
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
    Purchase *purchases = NULL;
    size_t purchase_count = 0, purchase_index;
    time_t latest_date = 0;
    float latest_cost = 0;
    if (sm_service_purchase_list(PURCHASE_COMPLETED, &purchases,
                                 &purchase_count) == SM_REPO_OK) {
        for (purchase_index = 0; purchase_index < purchase_count;
             ++purchase_index) {
            PurchaseItem *items = NULL;
            size_t item_count = 0, item_index;
            if (purchases[purchase_index].completed_at > before_date ||
                purchases[purchase_index].completed_at <= latest_date)
                continue;
            if (sm_service_purchase_item_list(
                    (uint64_t)purchases[purchase_index].id,
                    &items, &item_count) != SM_REPO_OK)
                continue;
            for (item_index = 0; item_index < item_count; ++item_index) {
                if (strcmp(items[item_index].product_id, product_id) == 0) {
                    latest_date = purchases[purchase_index].completed_at;
                    latest_cost = items[item_index].price;
                    break;
                }
            }
            sm_service_purchase_array_free(items);
        }
    }
    sm_service_purchase_array_free(purchases);
    
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

    {
        Sale *sales = NULL;
        size_t count = 0;
        size_t i;
        if (sm_service_sale_list_completed(&sales, &count) == SM_REPO_OK) {
            for (i = 0; i < count; ++i) {
                if (sales[i].completed_at < start ||
                    sales[i].completed_at > end)
                    continue;
                sales_original += sales[i].total_amount;
                total_discount += sales[i].discount;
                sales_revenue += sales[i].final_amount;
                ++completed_orders;
            }
        }
        sm_service_sales_array_free(sales);
    }

    // 2. 计算销售商品的真实成本（从库存变动日志中获取出库记录）
    float cogs = 0;  // Cost of Goods Sold 销售成本
    {
        StockLog *logs = NULL;
        size_t log_count = 0;
        size_t i;
        if (sm_service_stock_log_list(NULL, "出库", start, end,
                                      &logs, &log_count) == SM_REPO_OK) {
            for (i = 0; i < log_count; ++i) {
                float cost = get_product_cost(logs[i].product_id,
                                              logs[i].created_at);
                cogs += cost * logs[i].quantity;
            }
        }
        sm_service_inventory_array_free(logs);
    }

    // 3. 采购成本统计（仅用于参考展示）
    float purchase_cost = 0;
    {
        Purchase *purchases = NULL;
        size_t purchase_count = 0, i;
        if (sm_service_purchase_list(PURCHASE_COMPLETED, &purchases,
                                     &purchase_count) == SM_REPO_OK) {
            for (i = 0; i < purchase_count; ++i) {
                if (purchases[i].completed_at >= start &&
                    purchases[i].completed_at <= end)
                    purchase_cost += purchases[i].total_amount;
            }
        }
        sm_service_purchase_array_free(purchases);
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
            snprintf(filename, sizeof(filename), "inventory_report_%lld.csv", (long long)time(NULL));
            export_inventory_csv(filename);
            break;
        case 4:
            snprintf(filename, sizeof(filename), "inventory_report_%lld.html", (long long)time(NULL));
            export_inventory_html(filename);
            break;
        default:
            printf("无效选择\n");
    }
}
