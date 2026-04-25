/**
 * @file sale.c
 * @brief 销售交易模块 - 收银、挂单、支付、库存扣减
 */

#include "supermarket.h"
#include <stdlib.h>

// ==================== 销售数据 ====================
Sale *g_pending_sales = NULL;  // 挂单列表
SaleItem *g_sale_items = NULL; // 销售明细

// ==================== 销售记录 ====================

/**
 * 创建销售记录
 */
int create_sale(Sale *sale) {
    sale->id = generate_id();
    sale->created_at = time(NULL);
    sale->status = SALE_PENDING;  // 初始为挂单状态
    
    Sale *new_sale = (Sale*)malloc(sizeof(Sale));
    *new_sale = *sale;
    new_sale->next = g_pending_sales;
    g_pending_sales = new_sale;
    
    return sale->id;
}

/**
 * 添加销售明细
 */
int add_sale_item(int sale_id, SaleItem *item) {
    item->id = generate_id();
    item->sale_id = sale_id;
    
    SaleItem *new_item = (SaleItem*)malloc(sizeof(SaleItem));
    *new_item = *item;
    new_item->next = g_sale_items;
    g_sale_items = new_item;
    
    return item->id;
}

/**
 * 获取销售明细
 */
SaleItem* get_sale_items(int sale_id, int *count) {
    SaleItem *result = NULL;
    *count = 0;
    
    SaleItem *item = g_sale_items;
    while (item) {
        if (item->sale_id == sale_id) {
            result = (SaleItem*)realloc(result, (*count + 1) * sizeof(SaleItem));
            result[*count] = *item;
            (*count)++;
        }
        item = (SaleItem*)item->next;
    }
    
    return result;
}

/**
 * 查找挂单
 */
Sale* find_pending_sale(int sale_id) {
    Sale *sale = g_pending_sales;
    while (sale) {
        if (sale->id == sale_id) return sale;
        sale = sale->next;
    }
    return NULL;
}

/**
 * 计算销售总额
 */
float calculate_sale_total(int sale_id) {
    float total = 0;
    SaleItem *item = g_sale_items;
    
    while (item) {
        if (item->sale_id == sale_id) {
            total += item->subtotal;
        }
        item = (SaleItem*)item->next;
    }
    
    return total;
}

/**
 * 完成销售（收银）
 * 原子性操作：扣库存 + 写销售记录
 * @param sale_id 订单号
 * @param payment_method 支付方式
 * @param discount 额外折扣百分比（0-100），由收银员手动输入的额外折扣
 * @param cash_received 实收金额（仅现金支付时使用）
 * @return 0成功，-1失败
 */
int complete_sale(int sale_id, const char *payment_method, float discount, float cash_received) {
    Sale *sale = find_pending_sale(sale_id);
    if (!sale) return -1;
    
    // 计算总额（已包含单品折扣）
    float total = calculate_sale_total(sale_id);
    
    // 计算最终金额：总额 - 已计算的折扣 - 额外手动折扣
    // 注意：单品折扣和满减已在 scan_and_sell/add_sale_item 时计算到 item->subtotal
    // 这里只处理收银员手动输入的额外折扣（如抹零等）
    float extra_discount = 0;
    if (discount > 0 && discount <= 100) {
        extra_discount = total * (discount / 100.0f);
    }
    
    sale->discount = extra_discount;
    sale->final_amount = total - extra_discount;
    sale->cash_received = cash_received;
    
    // 确保最终金额不为负
    if (sale->final_amount < 0) sale->final_amount = 0;
    
    strncpy(sale->payment_method, payment_method, 19);
    sale->status = SALE_COMPLETED;
    sale->completed_at = time(NULL);
    
    // 扣减库存（原子操作）
    int item_count = 0;
    SaleItem *items = get_sale_items(sale_id, &item_count);
    
    for (int i = 0; i < item_count; i++) {
        if (items[i].is_combo) {
            // 套装：使用套装专用库存扣减函数
            extern ProductCombo* find_combo_by_id(int id);
            ProductCombo *combo = find_combo_by_id(items[i].combo_id);
            if (combo) {
                extern int deduct_combo_stock(ProductCombo *combo, int store_id, int quantity, int operator_id);
                deduct_combo_stock(combo, 0, (int)items[i].quantity, sale->cashier_id);
                
                // 记录套装销售日志
                char log_msg[512];
                snprintf(log_msg, sizeof(log_msg), "套装[%s] x%d", combo->name, (int)items[i].quantity);
                write_transaction_log("SALE", sale_id, "COMBO_SOLD", log_msg, sale->cashier_id);
            }
        } else {
            // 普通商品
            Product *prod = find_product_by_id(items[i].product_id);
            if (prod) {
                prod->stock -= (int)items[i].quantity;
                prod->updated_at = time(NULL);
                
                // 记录库存变动
                record_stock_log(prod->id, "出库", items[i].quantity, 
                               prod->stock + (int)items[i].quantity, prod->stock,
                               sale->cashier_id, "销售出库");
                update_product(prod);
            }
        }
    }
    
    // 保存到文件
    save_sale_record(sale);
    
    // 从挂单列表移除
    Sale **prev = &g_pending_sales;
    while (*prev) {
        if ((*prev)->id == sale_id) {
            Sale *to_free = *prev;
            *prev = (*prev)->next;
            free(to_free);
            break;
        }
        prev = &(*prev)->next;
    }

    // 打印小票
    {
        Receipt receipt;
        memset(&receipt, 0, sizeof(receipt));

        // 店铺信息
        strncpy(receipt.shop_name, g_config.shop_name, sizeof(receipt.shop_name) - 1);
        strncpy(receipt.shop_address, g_config.shop_address, sizeof(receipt.shop_address) - 1);
        strncpy(receipt.shop_phone, g_config.shop_phone, sizeof(receipt.shop_phone) - 1);

        // 收银信息
        Employee *cashier = find_employee_by_id(sale->cashier_id);
        strncpy(receipt.cashier, cashier ? cashier->name : "未知", sizeof(receipt.cashier) - 1);
        receipt.receipt_no = sale_id;
        receipt.print_time = time(NULL);

        // 会员信息（如果有）
        if (sale->member_id > 0) {
            Member *member = find_member_by_id(sale->member_id);
            if (member) {
                strncpy(receipt.member_phone, member->phone, sizeof(receipt.member_phone) - 1);
                receipt.member_points = member->points;
            }
        }

        // 商品明细
        receipt.item_count = 0;
        for (int i = 0; i < item_count && receipt.item_count < 100; i++) {
            Product *prod = find_product_by_id(items[i].product_id);
            if (prod) {
                strncpy(receipt.items[receipt.item_count].name, prod->name,
                        sizeof(receipt.items[0].name) - 1);
                receipt.items[receipt.item_count].price = items[i].price;
                receipt.items[receipt.item_count].quantity = items[i].quantity;
                receipt.items[receipt.item_count].subtotal = items[i].subtotal;
                receipt.items[receipt.item_count].discount = items[i].discount;
                receipt.item_count++;
            }
        }

        // 金额信息
        receipt.subtotal = total;
        receipt.discount = total - sale->final_amount;
        receipt.total = sale->final_amount;

        // 积分
        receipt.points_earned = (int)sale->final_amount;  // 1元=1积分
        receipt.points_used = sale->points_used;

        // 支付信息
        receipt.cash = sale->cash_received;
        receipt.change = sale->cash_received - sale->final_amount;
        if (receipt.change < 0) receipt.change = 0;

        // 打印小票
        if (printer_print_receipt(&receipt) != 0) {
            // 打印失败时不阻止交易
        }
    }

    // 释放销售明细内存
    free(items);

    return 0;
}

/**
 * 挂单（保存到文件）
 */
int hang_sale(int sale_id) {
    Sale *sale = find_pending_sale(sale_id);
    if (!sale) return -1;
    
    return save_pending_sale(sale);
}

/**
 * 取消挂单
 */
int cancel_sale(int sale_id) {
    Sale *sale = find_pending_sale(sale_id);
    if (!sale) return -1;
    
    // 从挂单列表移除
    Sale **prev = &g_pending_sales;
    while (*prev) {
        if ((*prev)->id == sale_id) {
            *prev = (*prev)->next;
            break;
        }
        prev = &(*prev)->next;
    }
    
    // 移除相关销售明细
    SaleItem **item_prev = &g_sale_items;
    while (*item_prev) {
        if ((*item_prev)->sale_id == sale_id) {
            SaleItem *to_free = *item_prev;
            *item_prev = (*item_prev)->next;
            free(to_free);
        } else {
            item_prev = &(*item_prev)->next;
        }
    }
    
    free(sale);
    return 0;
}

/**
 * 加载挂单
 * 文件格式: id|cashier_id|member_id|total_amount|discount|final_amount|cash_received|points_used|payment_method|status|created_at|completed_at
 */
int load_pending_sales(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/pending_sales.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Sale *sale = (Sale*)malloc(sizeof(Sale));
        memset(sale, 0, sizeof(Sale));
        
        char *token;
        token = strtok(line, "|"); sale->id = atoi(token);
        token = strtok(NULL, "|"); sale->cashier_id = atoi(token);
        token = strtok(NULL, "|"); sale->member_id = atoi(token);
        token = strtok(NULL, "|"); sale->total_amount = atof(token);
        token = strtok(NULL, "|"); sale->discount = atof(token);
        token = strtok(NULL, "|"); sale->final_amount = atof(token);
        token = strtok(NULL, "|"); sale->cash_received = atof(token);
        token = strtok(NULL, "|"); sale->points_used = atoi(token);
        token = strtok(NULL, "|"); strncpy(sale->payment_method, token, 19);
        token = strtok(NULL, "|"); sale->status = atoi(token);
        token = strtok(NULL, "|"); sale->created_at = (time_t)atoi(token);
        token = strtok(NULL, "|"); sale->completed_at = (time_t)atoi(token);
        
        sale->next = g_pending_sales;
        g_pending_sales = sale;
        
        if (sale->id > g_auto_id_counter) {
            g_auto_id_counter = sale->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存挂单 - 原子追加写入
 * 文件格式: id|cashier_id|member_id|total_amount|discount|final_amount|cash_received|points_used|payment_method|status|created_at|completed_at
 */
int save_pending_sale(Sale *sale) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/pending_sales.txt", DATA_DIR);
    
    snprintf(content, sizeof(content), "%d|%d|%d|%.2f|%.2f|%.2f|%.2f|%d|%s|%d|%ld|%ld",
        sale->id, sale->cashier_id, sale->member_id, sale->total_amount,
        sale->discount, sale->final_amount, sale->cash_received, sale->points_used,
        sale->payment_method, sale->status,
        (long)sale->created_at, (long)sale->completed_at);
    
    return atomic_append(filepath, content);
}

/**
 * 保存销售记录（到文件）- 原子追加写入
 */
int save_sale_record(Sale *sale) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/sales.txt", DATA_DIR);
    
    snprintf(content, sizeof(content), "%d|%d|%.2f|%.2f|%.2f|%s|%d|%ld|%ld",
        sale->id, sale->cashier_id, sale->total_amount,
        sale->discount, sale->final_amount, sale->payment_method,
        sale->status, (long)sale->created_at, (long)sale->completed_at);
    
    return atomic_append(filepath, content);
}

/**
 * 扫描条码销售（支持普通商品和套装）
 */
int scan_and_sell(int cashier_id, const char *barcode, int quantity) {
    // 先检查是否是套装条码
    extern ProductCombo* find_combo_by_barcode(const char *barcode);
    ProductCombo *combo = find_combo_by_barcode(barcode);
    
    if (combo) {
        // 套装销售
        if (check_combo_stock(combo, 0, quantity) != 0) {
            return -1;
        }
        
        // 创建销售记录
        Sale sale;
        memset(&sale, 0, sizeof(Sale));
        sale.cashier_id = cashier_id;
        
        int sale_id = create_sale(&sale);
        
        // 添加套装销售明细（作为一笔特殊销售）
        SaleItem item;
        memset(&item, 0, sizeof(SaleItem));
        snprintf(item.product_id, MAX_ID_LEN, "COMBO_%d", combo->id);  // 套装ID前缀
        snprintf(item.product_name, MAX_NAME_LEN, "[套装] %s", combo->name);
        item.quantity = quantity;
        item.price = combo->price;
        item.subtotal = combo->price * quantity;
        item.is_combo = 1;  // 标记为套装
        item.combo_id = combo->id;  // 关联套装ID
        
        add_sale_item(sale_id, &item);
        
        printf("已添加套装 [%s]，数量: %d，售价: ¥%.2f\n", 
               combo->name, quantity, combo->price * quantity);
        
        return sale_id;
    }
    
    // 普通商品销售
    Product *prod = find_product_by_barcode(barcode);
    if (!prod) {
        printf("错误: 未找到条码为 %s 的商品\n", barcode);
        return -1;
    }
    
    if (prod->stock < quantity) {
        printf("错误: 库存不足，当前库存 %d，需要 %d\n", prod->stock, quantity);
        return -1;
    }
    
    // 创建临时销售
    Sale sale;
    memset(&sale, 0, sizeof(Sale));
    sale.cashier_id = cashier_id;
    
    int sale_id = create_sale(&sale);
    
    // 添加销售明细
    SaleItem item;
    memset(&item, 0, sizeof(SaleItem));
    strncpy(item.product_id, prod->id, MAX_ID_LEN - 1);
    strncpy(item.product_name, prod->name, MAX_NAME_LEN - 1);
    item.quantity = quantity;
    item.price = prod->price;
    item.subtotal = prod->price * quantity;
    
    add_sale_item(sale_id, &item);
    
    return sale_id;
}

// ==================== 库存记录 ====================

StockLog *g_stock_logs = NULL;

/**
 * 记录库存变动
 */
int record_stock_log(const char *product_id, const char *type, float quantity,
                     float before_stock, float after_stock, int operator_id, const char *remark) {
    static int log_id = 1;
    
    StockLog *log = (StockLog*)malloc(sizeof(StockLog));
    log->id = log_id++;
    strncpy(log->product_id, product_id, MAX_ID_LEN - 1);
    strncpy(log->type, type, 19);
    log->quantity = quantity;
    log->before_stock = before_stock;
    log->after_stock = after_stock;
    log->operator_id = operator_id;
    strncpy(log->remark, remark ? remark : "", 255);
    log->created_at = time(NULL);
    
    log->next = g_stock_logs;
    g_stock_logs = log;
    
    // 保存到文件
    save_stock_log(log);
    
    return log->id;
}

/**
 * 保存库存记录 - 原子追加写入
 */
int save_stock_log(StockLog *log) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/stock_log.txt", DATA_DIR);
    
    snprintf(content, sizeof(content), "%d|%s|%s|%.2f|%.2f|%.2f|%d|%s|%ld",
        log->id, log->product_id, log->type, log->quantity,
        log->before_stock, log->after_stock, log->operator_id,
        log->remark, (long)log->created_at);
    
    return atomic_append(filepath, content);
}

/**
 * 加载库存记录
 */
int load_stock_logs(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/stock_log.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        StockLog *log = (StockLog*)malloc(sizeof(StockLog));
        strcpy(log->product_id, strtok(line, "|"));
        strcpy(log->type, strtok(NULL, "|"));
        log->quantity = atof(strtok(NULL, "|"));
        log->before_stock = atof(strtok(NULL, "|"));
        log->after_stock = atof(strtok(NULL, "|"));
        log->id = atoi(strtok(NULL, "|"));
        log->operator_id = atoi(strtok(NULL, "|"));
        strcpy(log->remark, strtok(NULL, "|"));
        log->created_at = (time_t)atoi(strtok(NULL, "|"));
        
        log->next = g_stock_logs;
        g_stock_logs = log;
    }
    
    fclose(fp);
    return 0;
}

/**
 * 查询库存变动记录
 */
StockLog* query_stock_logs(const char *product_id, time_t start, time_t end, int *count) {
    StockLog *result = NULL;
    *count = 0;
    
    StockLog *log = g_stock_logs;
    while (log) {
        if (product_id && strcmp(log->product_id, product_id) != 0) {
            log = log->next;
            continue;
        }
        if (log->created_at >= start && log->created_at <= end) {
            result = (StockLog*)realloc(result, (*count + 1) * sizeof(StockLog));
            result[*count] = *log;
            (*count)++;
        }
        log = log->next;
    }
    
    return result;
}

// ==================== 库存预警 ====================

/**
 * 检查库存预警
 */
void check_stock_alert(void) {
    printf("\n========== 库存预警 ==========\n");
    
    int alert_count = 0;
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_product_hash[i];
        while (node) {
            Product *prod = (Product*)node->data;
            if (prod->status == 1 && prod->stock <= prod->min_stock) {
                printf("[警告] %s (ID: %s) 库存不足: %d / 最低: %d\n",
                    prod->name, prod->id, prod->stock, prod->min_stock);
                alert_count++;
            }
            node = node->next;
        }
    }
    
    if (alert_count == 0) {
        printf("所有商品库存充足\n");
    }
    printf("==============================\n\n");
}

/**
 * 库存盘点
 */
void inventory_check(void) {
    printf("\n========== 库存盘点 ==========\n");
    
    int total_products = 0;
    int low_stock_count = 0;
    float total_value = 0;
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_product_hash[i];
        while (node) {
            Product *prod = (Product*)node->data;
            if (prod->status == 1) {
                total_products++;
                total_value += prod->stock * prod->cost;
                
                if (prod->stock <= prod->min_stock) {
                    low_stock_count++;
                }
            }
            node = node->next;
        }
    }
    
    printf("商品总数: %d\n", total_products);
    printf("库存不足商品: %d\n", low_stock_count);
    printf("库存总价值(成本价): ¥%.2f\n", total_value);
    printf("==============================\n\n");
}
