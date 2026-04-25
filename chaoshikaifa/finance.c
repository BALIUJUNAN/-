/**
 * @file finance.c
 * @brief 财务管理模块 - 日结对账实现
 */

#include "supermarket.h"
#include <math.h>

// ==================== 全局变量 ====================
DailySettlement *g_settlements = NULL;

// ==================== 日结操作 ====================

/**
 * 计算指定收银员在指定时间段内的销售统计
 */
void calculate_cashier_sales(int cashier_id, time_t start, time_t end,
                            int *order_count, float *cash_total, 
                            float *online_total, float *grand_total) {
    *order_count = 0;
    *cash_total = 0;
    *online_total = 0;
    *grand_total = 0;
    
    char sales_path[256];
    snprintf(sales_path, sizeof(sales_path), "%s/sales.txt", DATA_DIR);
    
    FILE *fp = fopen(sales_path, "r");
    if (!fp) return;
    
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
        strcpy(sale.payment_method, strtok(NULL, "|"));
        sale.status = atoi(strtok(NULL, "|"));
        sale.created_at = (time_t)atoi(strtok(NULL, "|"));
        
        // 筛选该收银员在时间段内的已完成订单
        if (sale.cashier_id == cashier_id &&
            sale.created_at >= start && sale.created_at <= end &&
            sale.status == SALE_COMPLETED) {
            
            (*order_count)++;
            *grand_total += sale.final_amount;
            
            // 按支付方式分类
            if (strcmp(sale.payment_method, "现金") == 0) {
                *cash_total += sale.final_amount;
            } else {
                *online_total += sale.final_amount;
            }
        }
    }
    
    fclose(fp);
}

/**
 * 创建日结单
 */
int create_settlement(int cashier_id, float actual_cash, float actual_online,
                     const char *remark) {
    Employee *cashier = find_employee_by_id(cashier_id);
    if (!cashier) {
        printf("错误: 未找到收银员 ID %d\n", cashier_id);
        return -1;
    }
    
    DailySettlement *sett = (DailySettlement*)malloc(sizeof(DailySettlement));
    if (!sett) return -1;
    
    // 获取当前班次（简化：假设是最近8小时）
    time_t now = time(NULL);
    sett->shift_end = now;
    sett->shift_start = now - 8 * 3600;  // 假设8小时班
    
    // 计算系统统计
    calculate_cashier_sales(cashier_id, sett->shift_start, sett->shift_end,
                           &sett->total_orders,
                           &sett->system_cash,
                           &sett->system_online,
                           &sett->system_total);
    
    // 收银员录入
    sett->actual_cash = actual_cash;
    sett->actual_online = actual_online;
    sett->actual_total = actual_cash + actual_online;
    
    // 计算差异
    sett->cash_diff = actual_cash - sett->system_cash;
    sett->online_diff = actual_online - sett->system_online;
    sett->total_diff = sett->actual_total - sett->system_total;
    
    // 基本信息
    sett->id = generate_id();
    sett->cashier_id = cashier_id;
    strncpy(sett->cashier_name, cashier->name, 49);
    sett->settlement_date = now;
    
    // 设置状态
    if (fabs(sett->total_diff) < 0.01f) {
        sett->status = SETTLEMENT_CONFIRMED;
    } else {
        sett->status = SETTLEMENT_PENDING;
    }
    
    sett->created_at = now;
    sett->confirmed_at = 0;
    strncpy(sett->remark, remark ? remark : "", 511);
    
    // 添加到链表
    sett->next = g_settlements;
    g_settlements = sett;
    
    // 保存到文件
    save_settlement(sett);
    
    return sett->id;
}

/**
 * 查找日结单
 */
DailySettlement* find_settlement(int id) {
    DailySettlement *sett = g_settlements;
    while (sett) {
        if (sett->id == id) return sett;
        sett = sett->next;
    }
    return NULL;
}

/**
 * 按收银员和日期查询
 */
DailySettlement* find_settlement_by_cashier_date(int cashier_id, time_t date) {
    struct tm *tm_date = localtime(&date);
    int target_day = tm_date->tm_yday;
    int target_year = tm_date->tm_year;
    
    DailySettlement *sett = g_settlements;
    while (sett) {
        struct tm *tm_sett = localtime(&sett->settlement_date);
        if (sett->cashier_id == cashier_id &&
            tm_sett->tm_yday == target_day &&
            tm_sett->tm_year == target_year) {
            return sett;
        }
        sett = sett->next;
    }
    return NULL;
}

/**
 * 按日期列出所有日结单
 */
DailySettlement** list_settlements_by_date(time_t date, int *count) {
    DailySettlement **list = NULL;
    *count = 0;
    
    struct tm *tm_date = localtime(&date);
    int target_day = tm_date->tm_yday;
    int target_year = tm_date->tm_year;
    
    DailySettlement *sett = g_settlements;
    while (sett) {
        struct tm *tm_sett = localtime(&sett->settlement_date);
        if (tm_sett->tm_yday == target_day && tm_sett->tm_year == target_year) {
            list = (DailySettlement**)realloc(list, (*count + 1) * sizeof(DailySettlement*));
            list[*count] = sett;
            (*count)++;
        }
        sett = sett->next;
    }
    
    return list;
}

/**
 * 列出某收银员的日结单
 */
DailySettlement** list_cashier_settlements(int cashier_id, int *count) {
    DailySettlement **list = NULL;
    *count = 0;
    
    DailySettlement *sett = g_settlements;
    while (sett) {
        if (sett->cashier_id == cashier_id) {
            list = (DailySettlement**)realloc(list, (*count + 1) * sizeof(DailySettlement*));
            list[*count] = sett;
            (*count)++;
        }
        sett = sett->next;
    }
    
    return list;
}

/**
 * 确认日结单
 */
int confirm_settlement(int settlement_id, const char *remark) {
    DailySettlement *sett = find_settlement(settlement_id);
    if (!sett) return -1;
    
    sett->status = SETTLEMENT_CONFIRMED;
    sett->confirmed_at = time(NULL);
    if (remark && strlen(remark) > 0) {
        strncpy(sett->remark, remark, 511);
    }
    
    return save_all_settlements();
}

/**
 * 获取状态名称
 */
const char* get_settlement_status_name(int status) {
    switch (status) {
        case SETTLEMENT_PENDING:   return "待确认";
        case SETTLEMENT_CONFIRMED: return "已确认";
        case SETTLEMENT_DISPUTED: return "有差异";
        default: return "未知";
    }
}

/**
 * 打印日结单详情
 */
void print_settlement_detail(int settlement_id) {
    DailySettlement *sett = find_settlement(settlement_id);
    if (!sett) {
        printf("未找到日结单 #%d\n", settlement_id);
        return;
    }
    
    char date_str[32];
    format_time(sett->settlement_date, date_str);
    
    printf("\n========== 日结单 #%d ==========\n", sett->id);
    printf("收银员: %s (ID: %d)\n", sett->cashier_name, sett->cashier_id);
    printf("日期: %s\n", date_str);
    printf("班次: %ld - %ld\n", (long)sett->shift_start, (long)sett->shift_end);
    
    printf("\n【系统统计】\n");
    printf("订单数: %d\n", sett->total_orders);
    printf("系统现金: ¥%.2f\n", sett->system_cash);
    printf("系统线上: ¥%.2f\n", sett->system_online);
    printf("系统总额: ¥%.2f\n", sett->system_total);
    
    printf("\n【实际录入】\n");
    printf("实际现金: ¥%.2f\n", sett->actual_cash);
    printf("实际线上: ¥%.2f\n", sett->actual_online);
    printf("实际总额: ¥%.2f\n", sett->actual_total);
    
    printf("\n【差异分析】\n");
    if (fabs(sett->cash_diff) > 0.01f) {
        printf("现金差异: ¥%.2f %s\n", sett->cash_diff, 
               sett->cash_diff > 0 ? "(多)" : "(少)");
    } else {
        printf("现金差异: ¥%.2f (平)\n", sett->cash_diff);
    }
    
    if (fabs(sett->online_diff) > 0.01f) {
        printf("线上差异: ¥%.2f %s\n", sett->online_diff,
               sett->online_diff > 0 ? "(多)" : "(少)");
    } else {
        printf("线上差异: ¥%.2f (平)\n", sett->online_diff);
    }
    
    printf("总差异: ¥%.2f\n", sett->total_diff);
    
    printf("\n状态: %s\n", get_settlement_status_name(sett->status));
    if (strlen(sett->remark) > 0) {
        printf("备注: %s\n", sett->remark);
    }
    printf("==============================\n\n");
}

// ==================== 文件操作 ====================

/**
 * 加载日结单
 */
int load_settlements(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/daily_settlement.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        DailySettlement *sett = (DailySettlement*)malloc(sizeof(DailySettlement));
        
        sett->id = atoi(strtok(line, "|"));
        sett->cashier_id = atoi(strtok(NULL, "|"));
        strcpy(sett->cashier_name, strtok(NULL, "|"));
        sett->settlement_date = (time_t)atoi(strtok(NULL, "|"));
        sett->shift_start = (time_t)atoi(strtok(NULL, "|"));
        sett->shift_end = (time_t)atoi(strtok(NULL, "|"));
        sett->total_orders = atoi(strtok(NULL, "|"));
        sett->system_cash = atof(strtok(NULL, "|"));
        sett->system_online = atof(strtok(NULL, "|"));
        sett->system_total = atof(strtok(NULL, "|"));
        sett->actual_cash = atof(strtok(NULL, "|"));
        sett->actual_online = atof(strtok(NULL, "|"));
        sett->actual_total = atof(strtok(NULL, "|"));
        sett->cash_diff = atof(strtok(NULL, "|"));
        sett->online_diff = atof(strtok(NULL, "|"));
        sett->total_diff = atof(strtok(NULL, "|"));
        sett->status = atoi(strtok(NULL, "|"));
        sett->created_at = (time_t)atoi(strtok(NULL, "|"));
        sett->confirmed_at = (time_t)atoi(strtok(NULL, "|"));
        strncpy(sett->remark, strtok(NULL, "|"), 511);
        
        sett->next = g_settlements;
        g_settlements = sett;
        
        if (sett->id > g_auto_id_counter) {
            g_auto_id_counter = sett->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存日结单 - 原子追加
 */
int save_settlement(DailySettlement *sett) {
    char filepath[256];
    char content[2048];
    
    snprintf(filepath, sizeof(filepath), "%s/daily_settlement.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%d|%s|%ld|%ld|%ld|%d|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%d|%ld|%ld|%s",
        sett->id, sett->cashier_id, sett->cashier_name,
        (long)sett->settlement_date, (long)sett->shift_start, (long)sett->shift_end,
        sett->total_orders, sett->system_cash, sett->system_online, sett->system_total,
        sett->actual_cash, sett->actual_online, sett->actual_total,
        sett->cash_diff, sett->online_diff, sett->total_diff,
        sett->status, (long)sett->created_at, (long)sett->confirmed_at,
        sett->remark);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有日结单 - 原子写入（全量覆盖）
 */
int save_all_settlements(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/daily_settlement.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    DailySettlement *sett = g_settlements;
    while (sett) {
        char line[2048];
        snprintf(line, sizeof(line),
            "%d|%d|%s|%ld|%ld|%ld|%d|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%d|%ld|%ld|%s\n",
            sett->id, sett->cashier_id, sett->cashier_name,
            (long)sett->settlement_date, (long)sett->shift_start, (long)sett->shift_end,
            sett->total_orders, sett->system_cash, sett->system_online, sett->system_total,
            sett->actual_cash, sett->actual_online, sett->actual_total,
            sett->cash_diff, sett->online_diff, sett->total_diff,
            sett->status, (long)sett->created_at, (long)sett->confirmed_at,
            sett->remark);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        sett = sett->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}
