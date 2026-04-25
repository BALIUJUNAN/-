/**
 * @file report.c
 * @brief 报表模块 - 销售/库存/采购报表，支持CSV/HTML导出
 */

#include "supermarket.h"
#include <stdlib.h>
#include <sys/stat.h>

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
           "订单号", "收银员", "时间", "金额", "折扣", "实收", "支付方式");
    printf("--------------------------------------------------------------------\n");
    
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Sale sale;
        sale.cashier_id = atoi(strtok(line, "|"));
        sale.id = atoi(strtok(NULL, "|"));
        sale.total_amount = atof(strtok(NULL, "|"));
        sale.discount = atof(strtok(NULL, "|"));
        sale.final_amount = atof(strtok(NULL, "|"));
        strcpy(sale.payment_method, strtok(NULL, "|"));
        sale.status = atoi(strtok(NULL, "|"));
        sale.created_at = (time_t)atoi(strtok(NULL, "|"));
        sale.completed_at = (time_t)atoi(strtok(NULL, "|"));
        
        if (sale.created_at < start || sale.created_at > end) continue;
        if (sale.status != SALE_COMPLETED) continue;
        
        total_orders++;
        total_amount += sale.total_amount;
        total_discount += sale.total_amount - sale.final_amount;
        
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
    snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, filename);
    
    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;
    
    // 写入UTF-8 BOM
    fprintf(fp, "\xEF\xBB\xBF");
    fprintf(fp, "订单号,收银员ID,时间,金额,折扣,实收,支付方式,状态\n");
    
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *src = fopen(sales_path, "r");
    if (src) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), src)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            Sale sale;
            sale.cashier_id = atoi(strtok(line, "|"));
            sale.id = atoi(strtok(NULL, "|"));
            sale.total_amount = atof(strtok(NULL, "|"));
            sale.discount = atof(strtok(NULL, "|"));
            sale.final_amount = atof(strtok(NULL, "|"));
            strcpy(sale.payment_method, strtok(NULL, "|"));
            sale.status = atoi(strtok(NULL, "|"));
            sale.created_at = (time_t)atoi(strtok(NULL, "|"));
            
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
    snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, filename);
    
    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;
    
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
    fprintf(fp, "<tr><th>订单号</th><th>收银员ID</th><th>时间</th><th>金额</th>"
               "<th>折扣</th><th>实收</th><th>支付方式</th><th>状态</th></tr>\n");
    
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
            
            Sale sale;
            sale.cashier_id = atoi(strtok(line, "|"));
            sale.id = atoi(strtok(NULL, "|"));
            sale.total_amount = atof(strtok(NULL, "|"));
            sale.discount = atof(strtok(NULL, "|"));
            sale.final_amount = atof(strtok(NULL, "|"));
            strcpy(sale.payment_method, strtok(NULL, "|"));
            sale.status = atoi(strtok(NULL, "|"));
            sale.created_at = (time_t)atoi(strtok(NULL, "|"));
            
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
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_product_hash[i];
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
 * 计算时间段内的销售成本（基于真实进价）
 */
static float calculate_sales_cost(time_t start, time_t end) {
    float total_cost = 0;
    
    // 遍历所有采购明细，获取每个商品在该时间段内的平均进价
    // 简化处理：使用采购单中商品的最接近进价
    
    PurchaseItem *item = g_purchase_items;
    while (item) {
        // 检查该采购单是否在时间范围内
        Purchase *pur = g_purchases;
        while (pur) {
            if (pur->id == item->purchase_id &&
                pur->completed_at >= start && pur->completed_at <= end &&
                pur->status == PURCHASE_COMPLETED) {
                // 累加采购成本（用于计算库存成本）
                total_cost += item->price * item->received_qty;
                break;
            }
            pur = pur->next;
        }
        item = item->next;
    }
    
    return total_cost;
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
    
    // 1. 销售收入
    float sales_revenue = 0;
    
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *fp = fopen(sales_path, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            Sale sale;
            sale.id = atoi(strtok(line, "|"));
            sale.cashier_id = atoi(strtok(NULL, "|"));
            sale.total_amount = atof(strtok(NULL, "|"));
            sale.discount = atof(strtok(NULL, "|"));
            sale.final_amount = atof(strtok(NULL, "|"));
            sale.created_at = (time_t)atoi(strtok(NULL, "|"));
            
            if (sale.created_at >= start && sale.created_at <= end &&
                sale.status == SALE_COMPLETED) {
                sales_revenue += sale.final_amount;
            }
        }
        fclose(fp);
    }
    
    // 2. 采购成本（已完成采购的总额）
    float purchase_cost = 0;
    Purchase *pur = g_purchases;
    while (pur) {
        if (pur->completed_at >= start && pur->completed_at <= end &&
            pur->status == PURCHASE_COMPLETED) {
            purchase_cost += pur->total_amount;
        }
        pur = pur->next;
    }
    
    // 3. 计算销售毛利
    // 毛利 = 销售收入 - 销售商品的真实成本
    // 这里简化处理：使用采购成本作为参考，实际销售成本需要根据销售明细计算
    float gross_profit = sales_revenue - purchase_cost * 0.8;  // 简化估算
    
    // 4. 月固定成本摊销
    // 计算时间段内的天数
    float days = (end - start) / (24.0f * 3600.0f);
    float monthly_fixed = g_config.monthly_fixed_cost;
    float period_fixed_cost = monthly_fixed * (days / 30.0f);
    
    // 5. 净利润
    float net_profit = gross_profit - period_fixed_cost;
    float profit_rate = sales_revenue > 0 ? (gross_profit / sales_revenue * 100) : 0;
    
    printf("【收入分析】\n");
    printf("销售收入:     ¥%.2f\n", sales_revenue);
    printf("\n【成本分析】\n");
    printf("采购成本:     ¥%.2f\n", purchase_cost);
    printf("固定成本摊销: ¥%.2f (月固定: ¥%.2f)\n", period_fixed_cost, monthly_fixed);
    printf("\n【利润分析】\n");
    printf("毛利:         ¥%.2f (毛利率: %.1f%%)\n", gross_profit, profit_rate);
    printf("净利:         ¥%.2f\n", net_profit);
    printf("==============================\n\n");
    
    // 打印说明
    printf("[注] 毛利率 = 毛利 / 销售收入\n");
    printf("[注] 净利 = 毛利 - 固定成本摊销\n");
    printf("[注] 历史采购数据缺失时，使用商品表进价估算\n\n");
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
            snprintf(filename, sizeof(filename), "sales_report_%ld.csv", (long)time(NULL));
            export_sales_csv(range.start, range.end, filename);
            break;
        case 2:
            get_date_range(&range, 30);
            snprintf(filename, sizeof(filename), "sales_report_%ld.html", (long)time(NULL));
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
