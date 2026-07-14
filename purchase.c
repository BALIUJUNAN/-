/**
 * @file purchase.c
 * @brief 采购管理模块 - 订单创建、审批、收货入库
 *
 * 采购业务流程（状态机）：
 *
 *   创建订单(PURCHASE_PENDING)
 *        ↓
 *   店长审批 → 通过(PURCHASE_APPROVED) 或 拒绝(PURCHASE_REJECTED)
 *        ↓
 *   库管收货(PURCHASE_COMPLETED) → 商品入库 + 库存日志
 *
 * 事务日志：每个操作都通过 write_transaction_log 记录到 transaction.log
 *
 * 数据存储：Purchase/PurchaseItem/Batch/StockLog 使用 AbyssDB；
 * transaction.log 暂保留为提交后的审计投影。
 */

#include "supermarket.h"

int sm_legacy_write_transaction_log(const char *, int, const char *,
                                    const char *, int);
#include "app/sm_app_context.h"
#include "app/sm_purchase_service.h"
#include <stdlib.h>

// ==================== 采购数据 ====================
Purchase *g_purchases = NULL;         // 采购订单链表
PurchaseItem *g_purchase_items = NULL; // 采购明细链表（通过 purchase_id 关联订单）

// ==================== 采购订单 ====================

/**
 * 创建采购订单
 */
int create_purchase(Purchase *purchase) {
    Purchase *new_pur;
    if (!purchase) return -1;
    purchase->created_at = time(NULL);
    purchase->status = PURCHASE_PENDING;  // 待审核
    if (sm_service_purchase_create(purchase) != SM_REPO_OK) return -1;
    new_pur = (Purchase*)malloc(sizeof(Purchase));
    if (!new_pur) return -1;
    *new_pur = *purchase;
    new_pur->next = g_purchases;
    g_purchases = new_pur;
    
    // 记录事务日志
    char log_data[256];
    snprintf(log_data, sizeof(log_data), "创建采购订单 #%d, 总额: ¥%.2f", 
             purchase->id, purchase->total_amount);
    (void)log_data; /* Audit is committed atomically by the purchase service. */
    
    return purchase->id;
}

/**
 * 添加采购明细
 */
int add_purchase_item(int purchase_id, PurchaseItem *item) {
    PurchaseItem *new_item;
    if (!item || sm_service_purchase_item_create(
                     (uint64_t)purchase_id, item) != SM_REPO_OK)
        return -1;
    new_item = (PurchaseItem*)malloc(sizeof(PurchaseItem));
    if (!new_item) return -1;
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
    Purchase persisted;
    Purchase *next;
    if (!pur) return -1;
    
    if (pur->status != PURCHASE_PENDING) {
        printf("错误: 订单状态不是待审核\n");
        return -1;
    }
    
    if (sm_service_purchase_approve((uint64_t)purchase_id,
                                     (uint64_t)approver_id,
                                     time(NULL)) != SM_REPO_OK ||
        sm_service_purchase_get((uint64_t)purchase_id,
                                &persisted) != SM_REPO_OK)
        return -1;
    next = pur->next; *pur = persisted; pur->next = next;
    
    // 记录事务日志
    char log_data[256];
    snprintf(log_data, sizeof(log_data), "审批采购订单 #%d", purchase_id);
    (void)log_data;
    
    return 0;
}

/**
 * 拒绝采购订单
 */
int reject_purchase(int purchase_id, int approver_id) {
    Purchase *pur = find_purchase(purchase_id);
    Purchase persisted;
    Purchase *next;
    if (!pur) return -1;
    
    if (pur->status != PURCHASE_PENDING) {
        printf("错误: 订单状态不是待审核\n");
        return -1;
    }
    
    if (sm_service_purchase_reject((uint64_t)purchase_id,
                                    (uint64_t)approver_id,
                                    time(NULL)) != SM_REPO_OK ||
        sm_service_purchase_get((uint64_t)purchase_id,
                                &persisted) != SM_REPO_OK)
        return -1;
    next = pur->next; *pur = persisted; pur->next = next;
    
    char log_data[256];
    snprintf(log_data, sizeof(log_data), "拒绝采购订单 #%d", purchase_id);
    (void)log_data;
    
    return 0;
}

/**
 * 库管收货
 */
int receive_purchase(int purchase_id, int operator_id) {
    Purchase *pur = find_purchase(purchase_id);
    Purchase persisted;
    Purchase *next;
    if (!pur) return -1;
    
    if (pur->status != PURCHASE_APPROVED) {
        printf("错误: 订单未通过审批\n");
        return -1;
    }
    
    if (sm_service_purchase_receive((uint64_t)purchase_id,
                                     (uint64_t)operator_id,
                                     time(NULL)) != SM_REPO_OK ||
        sm_service_purchase_get((uint64_t)purchase_id,
                                &persisted) != SM_REPO_OK)
        return -1;
    next = pur->next; *pur = persisted; pur->next = next;
    {
        PurchaseItem *items = NULL;
        size_t item_count = 0, i;
        if (sm_service_purchase_item_list((uint64_t)purchase_id,
                                          &items, &item_count) == SM_REPO_OK)
            for (i = 0; i < item_count; ++i)
                (void)refresh_product_by_id(items[i].product_id);
        sm_service_purchase_array_free(items);
    }
    (void)load_purchase_items();
    (void)load_batches();

    char log_data[256];
    snprintf(log_data, sizeof(log_data), "采购订单 #%d 收货完成", purchase_id);
    (void)log_data;

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
    return pur && sm_app_repository() ? 0 : -1;
}

/**
 * 保存采购明细 - 原子追加写入
 */
int save_purchase_item(PurchaseItem *item) {
    return item && sm_app_repository() ? 0 : -1;
}

/**
 * 加载采购订单
 */
int load_purchases(void) {
    Purchase *values = NULL, *old;
    size_t count = 0, i;
    while (g_purchases) { old = g_purchases; g_purchases = old->next; free(old); }
    if (sm_service_purchase_list(-1, &values, &count) != SM_REPO_OK) return -1;
    for (i = count; i > 0; --i) {
        Purchase *node = malloc(sizeof(*node));
        if (!node) { sm_service_purchase_array_free(values); return -1; }
        *node = values[i - 1]; node->next = g_purchases; g_purchases = node;
    }
    sm_service_purchase_array_free(values);
    return 0;
}

/**
 * 加载采购明细
 */
int load_purchase_items(void) {
    PurchaseItem *old;
    Purchase *order;
    while (g_purchase_items) { old = g_purchase_items; g_purchase_items = old->next; free(old); }
    for (order = g_purchases; order; order = order->next) {
        PurchaseItem *values = NULL;
        size_t count = 0, i;
        if (sm_service_purchase_item_list((uint64_t)order->id,
                                          &values, &count) != SM_REPO_OK)
            return -1;
        for (i = count; i > 0; --i) {
            PurchaseItem *node = malloc(sizeof(*node));
            if (!node) { sm_service_purchase_array_free(values); return -1; }
            *node = values[i - 1]; node->next = g_purchase_items; g_purchase_items = node;
        }
        sm_service_purchase_array_free(values);
    }
    return 0;
}

// ==================== 事务日志 ====================

static TransactionLog *g_transaction_logs = NULL;

/**
 * 写入事务日志 - 原子追加写入
 */
int sm_legacy_write_transaction_log(const char *type, int ref_id, const char *operation,
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
int sm_legacy_load_transaction_logs(void) {
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
        char *saveptr;
        token = strtok_r(line, "|", &saveptr); log->id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); if (token) strncpy(log->type, token, 19);
        token = strtok_r(NULL, "|", &saveptr); log->ref_id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); if (token) strncpy(log->operation, token, 19);
        token = strtok_r(NULL, "|", &saveptr); if (token) strncpy(log->data, token, 1023);
        token = strtok_r(NULL, "|", &saveptr); log->operator_id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); log->created_at = token ? (time_t)atoll(token) : 0;
        
        log->next = g_transaction_logs;
        g_transaction_logs = log;
    }
    
    fclose(fp);
    return 0;
}

/**
 * 查询事务日志
 */
TransactionLog* sm_legacy_query_transaction_logs(const char *type, time_t start, time_t end, int *count) {
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
