/**
 * @file purchase.c
 * @brief 采购管理模块 - 订单创建、审批、收货
 */

#include "supermarket.h"
#include <stdlib.h>

// ==================== 采购数据 ====================
Purchase *g_purchases = NULL;
PurchaseItem *g_purchase_items = NULL;

// ==================== 采购订单 ====================

/**
 * 创建采购订单
 */
int create_purchase(Purchase *purchase) {
    purchase->id = generate_id();
    purchase->created_at = time(NULL);
    purchase->status = PURCHASE_PENDING;  // 待审核
    
    Purchase *new_pur = (Purchase*)malloc(sizeof(Purchase));
    *new_pur = *purchase;
    new_pur->next = g_purchases;
    g_purchases = new_pur;
    
    // 记录事务日志
    char log_data[256];
    snprintf(log_data, sizeof(log_data), "创建采购订单 #%d, 总额: ¥%.2f", 
             purchase->id, purchase->total_amount);
    write_transaction_log("PURCHASE", purchase->id, "CREATE", log_data, purchase->creator_id);
    
    return purchase->id;
}

/**
 * 添加采购明细
 */
int add_purchase_item(int purchase_id, PurchaseItem *item) {
    item->id = generate_id();
    item->purchase_id = purchase_id;
    item->received_qty = 0;
    
    PurchaseItem *new_item = (PurchaseItem*)malloc(sizeof(PurchaseItem));
    *new_item = *item;
    new_item->next = g_purchase_items;
    g_purchase_items = new_item;
    
    return item->id;
}

/**
 * 查找采购订单
 */
Purchase* find_purchase(int id) {
    Purchase *pur = g_purchases;
    while (pur) {
        if (pur->id == id) return pur;
        pur = pur->next;
    }
    return NULL;
}

/**
 * 获取采购明细
 */
PurchaseItem* get_purchase_items(int purchase_id, int *count) {
    int capacity = 16;
    PurchaseItem *result = (PurchaseItem*)malloc(capacity * sizeof(PurchaseItem));
    *count = 0;

    PurchaseItem *item = g_purchase_items;
    while (item) {
        if (item->purchase_id == purchase_id) {
            if (*count >= capacity) {
                capacity *= 2;
                result = (PurchaseItem*)realloc(result, capacity * sizeof(PurchaseItem));
            }
            result[*count] = *item;
            (*count)++;
        }
        item = item->next;
    }

    return result;
}

/**
 * 计算采购总额
 */
float calculate_purchase_total(int purchase_id) {
    float total = 0;
    PurchaseItem *item = g_purchase_items;
    
    while (item) {
        if (item->purchase_id == purchase_id) {
            total += item->quantity * item->price;
        }
        item = item->next;
    }
    
    return total;
}

/**
 * 审批采购订单（店长审批）
 */
int approve_purchase(int purchase_id, int approver_id) {
    Purchase *pur = find_purchase(purchase_id);
    if (!pur) return -1;
    
    if (pur->status != PURCHASE_PENDING) {
        printf("错误: 订单状态不是待审核\n");
        return -1;
    }
    
    pur->approver_id = approver_id;
    pur->approved_at = time(NULL);
    pur->status = PURCHASE_APPROVED;
    
    // 更新总额
    pur->total_amount = calculate_purchase_total(purchase_id);
    
    // 记录事务日志
    char log_data[256];
    snprintf(log_data, sizeof(log_data), "审批采购订单 #%d", purchase_id);
    write_transaction_log("PURCHASE", purchase_id, "APPROVE", log_data, approver_id);
    
    // 保存到文件
    save_purchase(pur);
    
    return 0;
}

/**
 * 拒绝采购订单
 */
int reject_purchase(int purchase_id, int approver_id) {
    Purchase *pur = find_purchase(purchase_id);
    if (!pur) return -1;
    
    if (pur->status != PURCHASE_PENDING) {
        printf("错误: 订单状态不是待审核\n");
        return -1;
    }
    
    pur->approver_id = approver_id;
    pur->approved_at = time(NULL);
    pur->status = PURCHASE_REJECTED;
    
    char log_data[256];
    snprintf(log_data, sizeof(log_data), "拒绝采购订单 #%d", purchase_id);
    write_transaction_log("PURCHASE", purchase_id, "REJECT", log_data, approver_id);
    
    save_purchase(pur);
    
    return 0;
}

/**
 * 库管收货
 */
int receive_purchase(int purchase_id, int operator_id) {
    Purchase *pur = find_purchase(purchase_id);
    if (!pur) return -1;
    
    if (pur->status != PURCHASE_APPROVED) {
        printf("错误: 订单未通过审批\n");
        return -1;
    }
    
    // 获取采购明细并入库
    int item_count = 0;
    PurchaseItem *items = get_purchase_items(purchase_id, &item_count);
    
    for (int i = 0; i < item_count; i++) {
        Product *prod = find_product_by_id(items[i].product_id);
        if (prod) {
            int before_stock = prod->stock;
            prod->stock += (int)items[i].quantity;
            prod->updated_at = time(NULL);
            
            // 记录入库
            record_stock_log(prod->id, "入库", items[i].quantity,
                           before_stock, prod->stock, operator_id, "采购入库");
            
            // 更新收货数量
            items[i].received_qty = items[i].quantity;
        } else {
            printf("警告: 商品 %s 不存在，自动创建\n", items[i].product_id);
            // 可以选择自动创建商品或跳过
        }
    }
    
    free(items);
    
    pur->status = PURCHASE_COMPLETED;
    pur->completed_at = time(NULL);
    
    char log_data[256];
    snprintf(log_data, sizeof(log_data), "采购订单 #%d 收货完成", purchase_id);
    write_transaction_log("PURCHASE", purchase_id, "COMPLETE", log_data, operator_id);
    
    save_purchase(pur);
    
    return 0;
}

/**
 * 列出采购订单
 */
Purchase** list_purchases(int status, int *count) {
    int capacity = 16;
    Purchase **result = (Purchase**)malloc(capacity * sizeof(Purchase*));
    *count = 0;

    Purchase *pur = g_purchases;
    while (pur) {
        if (status == -1 || pur->status == status) {
            if (*count >= capacity) {
                capacity *= 2;
                result = (Purchase**)realloc(result, capacity * sizeof(Purchase*));
            }
            result[*count] = pur;
            (*count)++;
        }
        pur = pur->next;
    }

    return result;
}

/**
 * 显示订单状态字符串
 */
const char* get_purchase_status_str(int status) {
    switch (status) {
        case PURCHASE_PENDING:   return "待审核";
        case PURCHASE_APPROVED:  return "已审核";
        case PURCHASE_COMPLETED: return "已完成";
        case PURCHASE_REJECTED:  return "已拒绝";
        default:                 return "未知";
    }
}

/**
 * 打印采购订单详情
 */
void print_purchase_detail(int purchase_id) {
    Purchase *pur = find_purchase(purchase_id);
    if (!pur) {
        printf("未找到采购订单 #%d\n", purchase_id);
        return;
    }
    
    printf("\n========== 采购订单 #%d ==========\n", purchase_id);
    printf("供应商: %s (ID: %s)\n", pur->supplier_name, pur->supplier_id);
    printf("状态: %s\n", get_purchase_status_str(pur->status));
    printf("总金额: ¥%.2f\n", pur->total_amount);
    printf("创建人: ID %d\n", pur->creator_id);
    if (pur->approver_id > 0) {
        printf("审批人: ID %d\n", pur->approver_id);
    }
    
    printf("\n采购明细:\n");
    printf("%-12s %-20s %-10s %-10s %-10s\n", "商品ID", "商品名称", "数量", "单价", "小计");
    printf("----------------------------------------------------\n");
    
    int item_count = 0;
    PurchaseItem *items = get_purchase_items(purchase_id, &item_count);
    for (int i = 0; i < item_count; i++) {
        printf("%-12s %-20s %-10.2f %-10.2f %-10.2f\n",
            items[i].product_id, items[i].product_name,
            items[i].quantity, items[i].price,
            items[i].quantity * items[i].price);
    }
    free(items);
    
    printf("================================\n\n");
}

// ==================== 文件操作 ====================

/**
 * 保存采购订单 - 原子追加写入
 */
int save_purchase(Purchase *pur) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/purchase.txt", DATA_DIR);
    
    snprintf(content, sizeof(content), "%d|%s|%s|%d|%d|%d|%.2f|%lld|%lld|%lld",
        pur->id, pur->supplier_id, pur->supplier_name,
        pur->creator_id, pur->approver_id, pur->status,
        pur->total_amount, (long long)pur->created_at,
        (long long)pur->approved_at, (long long)pur->completed_at);
    
    return atomic_append(filepath, content);
}

/**
 * 保存采购明细 - 原子追加写入
 */
int save_purchase_item(PurchaseItem *item) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/purchase_item.txt", DATA_DIR);
    
    snprintf(content, sizeof(content), "%d|%d|%s|%s|%.2f|%.2f|%.2f",
        item->id, item->purchase_id, item->product_id,
        item->product_name, item->quantity, item->price, item->received_qty);
    
    return atomic_append(filepath, content);
}

/**
 * 加载采购订单
 */
int load_purchases(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/purchase.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Purchase *pur = (Purchase*)malloc(sizeof(Purchase));
        memset(pur, 0, sizeof(Purchase));
        char *token;
        token = strtok(line, "|"); pur->id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); if (token) strncpy(pur->supplier_id, token, 19);
        token = strtok(NULL, "|"); if (token) strncpy(pur->supplier_name, token, MAX_NAME_LEN-1);
        token = strtok(NULL, "|"); pur->creator_id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); pur->approver_id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); pur->status = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); pur->total_amount = token ? atof(token) : 0.0f;
        token = strtok(NULL, "|"); pur->created_at = token ? (time_t)atoll(token) : 0;
        token = strtok(NULL, "|"); pur->approved_at = token ? (time_t)atoll(token) : 0;
        token = strtok(NULL, "|"); pur->completed_at = token ? (time_t)atoll(token) : 0;
        
        pur->next = g_purchases;
        g_purchases = pur;
        
        if (pur->id > g_auto_id_counter) {
            g_auto_id_counter = pur->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 加载采购明细
 */
int load_purchase_items(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/purchase_item.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        PurchaseItem *item = (PurchaseItem*)malloc(sizeof(PurchaseItem));
        memset(item, 0, sizeof(PurchaseItem));
        char *token;
        token = strtok(line, "|"); item->id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); item->purchase_id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); if (token) strncpy(item->product_id, token, MAX_ID_LEN-1);
        token = strtok(NULL, "|"); if (token) strncpy(item->product_name, token, MAX_NAME_LEN-1);
        token = strtok(NULL, "|"); item->quantity = token ? atof(token) : 0.0f;
        token = strtok(NULL, "|"); item->price = token ? atof(token) : 0.0f;
        token = strtok(NULL, "|"); item->received_qty = token ? atof(token) : 0.0f;
        
        item->next = g_purchase_items;
        g_purchase_items = item;
    }
    
    fclose(fp);
    return 0;
}

// ==================== 事务日志 ====================

static TransactionLog *g_transaction_logs = NULL;

/**
 * 写入事务日志 - 原子追加写入
 */
int write_transaction_log(const char *type, int ref_id, const char *operation,
                         const char *data, int operator_id) {
    static int log_id = 1;
    
    TransactionLog *log = (TransactionLog*)malloc(sizeof(TransactionLog));
    log->id = log_id++;
    strncpy(log->type, type, 19);
    log->ref_id = ref_id;
    strncpy(log->operation, operation, 19);
    strncpy(log->data, data, 1023);
    log->operator_id = operator_id;
    log->created_at = time(NULL);
    
    log->next = g_transaction_logs;
    g_transaction_logs = log;
    
    // 原子追加到文件
    char filepath[256];
    char content[2048];
    
    snprintf(filepath, sizeof(filepath), "%s/transaction.log", DATA_DIR);
    snprintf(content, sizeof(content), "%d|%s|%d|%s|%s|%d|%lld",
        log->id, log->type, log->ref_id, log->operation,
        log->data, log->operator_id, (long long)log->created_at);
    
    atomic_append(filepath, content);
    
    return log->id;
}

/**
 * 加载事务日志
 */
int load_transaction_logs(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/transaction.log", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        TransactionLog *log = (TransactionLog*)malloc(sizeof(TransactionLog));
        memset(log, 0, sizeof(TransactionLog));
        char *token;
        token = strtok(line, "|"); log->id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); if (token) strncpy(log->type, token, 19);
        token = strtok(NULL, "|"); log->ref_id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); if (token) strncpy(log->operation, token, 19);
        token = strtok(NULL, "|"); if (token) strncpy(log->data, token, 1023);
        token = strtok(NULL, "|"); log->operator_id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); log->created_at = token ? (time_t)atoll(token) : 0;
        
        log->next = g_transaction_logs;
        g_transaction_logs = log;
    }
    
    fclose(fp);
    return 0;
}

/**
 * 查询事务日志
 */
TransactionLog* query_transaction_logs(const char *type, time_t start, time_t end, int *count) {
    int capacity = 16;
    TransactionLog *result = (TransactionLog*)malloc(capacity * sizeof(TransactionLog));
    *count = 0;

    TransactionLog *log = g_transaction_logs;
    while (log) {
        if (type && strcmp(log->type, type) != 0) {
            log = log->next;
            continue;
        }
        if (log->created_at >= start && log->created_at <= end) {
            if (*count >= capacity) {
                capacity *= 2;
                result = (TransactionLog*)realloc(result, capacity * sizeof(TransactionLog));
            }
            result[*count] = *log;
            (*count)++;
        }
        log = log->next;
    }

    return result;
}
