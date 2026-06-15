/**
 * @file sale.c
 * @brief 销售交易模块 - 收银、挂单、支付、库存扣减
 *
 * 本模块实现完整的销售业务流程：
 *   1. 扫描条码创建挂单（scan_and_sell）
 *   2. 收银结算（complete_sale）：计算折扣 → 扣库存 → 打印小票 → 发放积分
 *   3. 挂单管理：挂起、取消
 *   4. 库存变动日志记录
 *   5. 库存预警和盘点
 *
 * 数据流：
 *   用户扫码 → create_sale（挂单链表）→ add_sale_item（明细链表）
 *   → complete_sale → 扣库存 + save_sale_record（sales.txt）
 *   → 从挂单链表移除 + 从明细链表移除
 *
 * 折扣计算层次（由外层 main.c 的 show_sale_menu 完成）：
 *   第1层：单品促销折扣（scan_and_sell 时写入 item.price）
 *   第2层：会员百分比折扣（discount_pct 参数）
 *   第3层：满减固定金额（discount_amount 参数）
 */

#include "supermarket.h"
#include <stdlib.h>

// ==================== 销售数据 ====================
Sale *g_pending_sales = NULL;   // 挂单链表头（状态=SALE_PENDING 的订单）
SaleItem *g_sale_items = NULL;  // 全局销售明细链表（所有挂单的明细，按 sale_id 关联）

/**
 * 清理指定收银员的挂单及其销售明细（供logout调用）
 */
void cleanup_cashier_sales(int cashier_id) {
    // 先清理关联的SaleItem
    SaleItem **si_prev = &g_sale_items;
    while (*si_prev) {
        // 检查该sale_id是否属于该收银员的挂单
        Sale *s = g_pending_sales;
        int belongs = 0;
        while (s) {
            if (s->id == (*si_prev)->sale_id && s->cashier_id == cashier_id) {
                belongs = 1;
                break;
            }
            s = s->next;
        }
        if (belongs) {
            SaleItem *to_free = *si_prev;
            *si_prev = (*si_prev)->next;
            free(to_free);
        } else {
            si_prev = &(*si_prev)->next;
        }
    }

    // 再清理挂单
    Sale **prev = &g_pending_sales;
    while (*prev) {
        if ((*prev)->cashier_id == cashier_id) {
            Sale *to_free = *prev;
            *prev = (*prev)->next;
            free(to_free);
        } else {
            prev = &(*prev)->next;
        }
    }
}

// ==================== 销售记录 ====================

/**
 * 创建销售记录
 */
int create_sale(Sale *sale) {
    sale->id = generate_sale_order_id();  // 使用销售订单专用计数器
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
    int capacity = 0;
    *count = 0;

    SaleItem *item = g_sale_items;
    while (item) {
        if (item->sale_id == sale_id) {
            if (*count >= capacity) {
                capacity = capacity == 0 ? 16 : capacity * 2;
                result = (SaleItem*)realloc(result, capacity * sizeof(SaleItem));
            }
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
 *
 * 折扣说明：
 *   - 单品促销折扣：已在 scan_and_sell 时写入 item.price（折后价）
 *   - 会员/VIP折扣：通过 discount_pct 百分比参数传入
 *   - 满减/抹零：通过 discount_amount 固定金额参数传入
 *
 * @param sale_id          订单号
 * @param payment_method   支付方式
 * @param discount_pct     会员/VIP折扣百分比（0-100）
 * @param discount_amount  满减/固定金额优惠
 * @param cash_received    实收金额（仅现金支付时使用）
 * @return 0成功，-1失败
 */
int complete_sale(int sale_id, const char *payment_method,
                  float discount_pct, float discount_amount, float cash_received) {
    Sale *sale = find_pending_sale(sale_id);
    if (!sale) return -1;

    // subtotal = 各商品折后价之和（已含单品促销折扣）
    float subtotal = calculate_sale_total(sale_id);

    // 计算原价合计（所有商品原始零售价 × 数量）
    float original_total = 0;
    float item_promo_discount = 0;  // 单品促销折扣总额
    int cnt = 0;
    SaleItem *si = get_sale_items(sale_id, &cnt);
    for (int i = 0; i < cnt; i++) {
        float op = si[i].original_price > 0 ? si[i].original_price : si[i].price;
        original_total += op * si[i].quantity;
        // 单品促销折扣 = (原价 - 折后价) × 数量
        item_promo_discount += (op - si[i].price) * si[i].quantity;
    }
    free(si);

    // 应用会员/VIP 百分比折扣（基于已含单品促销折扣后的价格）
    float pct_off = 0;
    if (discount_pct > 0 && discount_pct <= 100) {
        pct_off = subtotal * (discount_pct / 100.0f);
    }

    // 应用满减等固定金额优惠
    float fixed_off = discount_amount > 0 ? discount_amount : 0;

    // total_amount = 原价合计（报表中显示的"原价"）
    sale->total_amount = original_total;
    // discount = 单品促销折扣 + 会员折扣 + 满减（总优惠金额）
    sale->discount = item_promo_discount + pct_off + fixed_off;
    // final_amount = 原价 - 总优惠 = 实收
    sale->final_amount = original_total - sale->discount;
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
            ProductCombo *combo = find_combo_by_id(items[i].combo_id);
            if (combo) {
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

    // 打印小票（必须在释放sale之前完成）
    {
        Receipt receipt;
        memset(&receipt, 0, sizeof(receipt));

        // 店铺信息
        strncpy(receipt.shop_name, g_config.shop_name, sizeof(receipt.shop_name) - 1);
        strncpy(receipt.shop_address, g_config.shop_address, sizeof(receipt.shop_address) - 1);
        strncpy(receipt.shop_phone, g_config.shop_phone, sizeof(receipt.shop_phone) - 1);

        // 收银信息（显示收银员ID）
        snprintf(receipt.cashier, sizeof(receipt.cashier), "%d", sale->cashier_id);
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

        // 商品明细（普通商品和套装统一处理）
        receipt.item_count = 0;
        for (int i = 0; i < item_count && receipt.item_count < 100; i++) {
            /* 优先使用 SaleItem 中已保存的商品名称（支持套装） */
            const char *item_name = items[i].product_name;
            if (strlen(item_name) == 0) {
                /* 兜底：从商品表查找（仅普通商品有效） */
                Product *prod = find_product_by_id(items[i].product_id);
                item_name = prod ? prod->name : items[i].product_id;
            }
            strncpy(receipt.items[receipt.item_count].name, item_name,
                    sizeof(receipt.items[0].name) - 1);
            float orig = items[i].original_price > 0 ? items[i].original_price : items[i].price;
            receipt.items[receipt.item_count].price = orig;
            receipt.items[receipt.item_count].quantity = items[i].quantity;
            receipt.items[receipt.item_count].subtotal = orig * items[i].quantity;
            receipt.items[receipt.item_count].discount = (orig - items[i].price) * items[i].quantity;
            receipt.item_count++;
        }

        // 金额信息
        receipt.subtotal = original_total;
        receipt.discount = original_total - sale->final_amount;
        receipt.total = sale->final_amount;

        // 积分
        receipt.points_earned = calculate_ladder_points(sale->final_amount);
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

    /* 发放会员积分（必须在小票打印之后、sale释放之前） */
    if (sale->member_id > 0) {
        Member *member = find_member_by_id(sale->member_id);
        if (member) {
            int earned = add_points(member, sale->final_amount);
            save_members();
            printf("[积分] 会员 %s 获得 %d 积分，当前余额: %d\n",
                   member->name, earned, member->points);
        }
    }

    // 从挂单列表移除并释放sale（小票打印完成后再释放，避免use-after-free）
    {
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
    }

    /* 从全局销售明细链表中移除该订单的所有明细（防止内存泄漏） */
    {
        SaleItem **si_prev = &g_sale_items;
        while (*si_prev) {
            if ((*si_prev)->sale_id == sale_id) {
                SaleItem *to_free = *si_prev;
                *si_prev = (*si_prev)->next;
                free(to_free);
            } else {
                si_prev = &(*si_prev)->next;
            }
        }
    }

    // 重写挂单文件，清理已完成的记录
    save_all_pending_sales();

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

    // 重写挂单文件，清理已取消的记录
    save_all_pending_sales();

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
        char *saveptr;
        token = strtok_r(line, "|", &saveptr); sale->id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sale->cashier_id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sale->member_id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sale->total_amount = token ? atof(token) : 0.0f;
        token = strtok_r(NULL, "|", &saveptr); sale->discount = token ? atof(token) : 0.0f;
        token = strtok_r(NULL, "|", &saveptr); sale->final_amount = token ? atof(token) : 0.0f;
        token = strtok_r(NULL, "|", &saveptr); sale->cash_received = token ? atof(token) : 0.0f;
        token = strtok_r(NULL, "|", &saveptr); sale->points_used = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); if (token) strncpy(sale->payment_method, token, 19);
        token = strtok_r(NULL, "|", &saveptr); sale->status = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sale->created_at = token ? (time_t)atoll(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); sale->completed_at = token ? (time_t)atoll(token) : 0;
        
        sale->next = g_pending_sales;
        g_pending_sales = sale;
        
        if (sale->id > g_sale_order_counter) {
            g_sale_order_counter = sale->id;
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

    snprintf(content, sizeof(content), "%d|%d|%d|%.2f|%.2f|%.2f|%.2f|%d|%s|%d|%lld|%lld",
        sale->id, sale->cashier_id, sale->member_id, sale->total_amount,
        sale->discount, sale->final_amount, sale->cash_received, sale->points_used,
        sale->payment_method, sale->status,
        (long long)sale->created_at, (long long)sale->completed_at);

    return atomic_append(filepath, content);
}

/**
 * 重写挂单文件（仅保留当前内存中的挂单）
 * 用于完成销售或取消挂单后清理文件中的过期数据
 */
int save_all_pending_sales(void) {
    char filepath[256];
    size_t bufsize = 65536;
    char *buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    char *pos = buffer;
    size_t remaining = bufsize;

    snprintf(filepath, sizeof(filepath), "%s/pending_sales.txt", DATA_DIR);

    Sale *sale = g_pending_sales;
    while (sale) {
        int written = snprintf(pos, remaining, "%d|%d|%d|%.2f|%.2f|%.2f|%.2f|%d|%s|%d|%lld|%lld\n",
            sale->id, sale->cashier_id, sale->member_id, sale->total_amount,
            sale->discount, sale->final_amount, sale->cash_received, sale->points_used,
            sale->payment_method, sale->status,
            (long long)sale->created_at, (long long)sale->completed_at);

        if (written >= (int)remaining) {
            size_t offset = pos - buffer;
            bufsize *= 2;
            char *tmp_buf = (char*)realloc(buffer, bufsize);
            if (!tmp_buf) { free(buffer); return -1; }
            buffer = tmp_buf;
            pos = buffer + offset;
            remaining = bufsize - offset;
            written = snprintf(pos, remaining, "%d|%d|%d|%.2f|%.2f|%.2f|%.2f|%d|%s|%d|%lld|%lld\n",
                sale->id, sale->cashier_id, sale->member_id, sale->total_amount,
                sale->discount, sale->final_amount, sale->cash_received, sale->points_used,
                sale->payment_method, sale->status,
                (long long)sale->created_at, (long long)sale->completed_at);
        }
        pos += written;
        remaining -= written;
        sale = sale->next;
    }

    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

/**
 * 保存销售记录（到文件）- 原子追加写入
 */
int save_sale_record(Sale *sale) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/sales.txt", DATA_DIR);
    
    snprintf(content, sizeof(content), "%d|%d|%d|%.2f|%.2f|%.2f|%s|%d|%lld|%lld",
        sale->id, sale->cashier_id, sale->member_id, sale->total_amount,
        sale->discount, sale->final_amount, sale->payment_method,
        sale->status, (long long)sale->created_at, (long long)sale->completed_at);
    
    return atomic_append(filepath, content);
}

/**
 * 扫描条码销售（支持普通商品和套装）
 */
int scan_and_sell(int cashier_id, const char *barcode, int quantity) {
    // 先检查是否是套装条码
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
        item.original_price = combo->price;
        item.subtotal = combo->price * quantity;
        item.discount = 0;
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
    item.original_price = prod->price;

    // 应用单品促销折扣
    Promotion *promo = get_product_promotion(prod->id);
    float discounted_price = prod->price;
    float discount_amount = 0;

    if (promo && is_promotion_valid(promo) && promo->type == PROMOTION_TYPE_DISCOUNT) {
        discounted_price = prod->price * promo->discount_rate;
        discount_amount = prod->price - discounted_price;
        item.price = discounted_price;  // 折后价（含单品折扣）
    }

    item.discount = discount_amount;   // 每件优惠金额
    item.subtotal = item.price * quantity;
    
    add_sale_item(sale_id, &item);
    
    // 如果有折扣，显示折扣信息
    if (discount_amount > 0) {
        printf("  [促销折扣: %.0f折 -¥%.2f]\n", promo->discount_rate * 10, discount_amount * quantity);
    }
    
    return sale_id;
}

// ==================== 库存记录 ====================

static StockLog *g_stock_logs = NULL;

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
    
    snprintf(content, sizeof(content), "%d|%s|%s|%.2f|%.2f|%.2f|%d|%s|%lld",
        log->id, log->product_id, log->type, log->quantity,
        log->before_stock, log->after_stock, log->operator_id,
        log->remark, (long long)log->created_at);
    
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
        memset(log, 0, sizeof(StockLog));
        char *token;
        char *saveptr;
        token = strtok_r(line, "|", &saveptr); log->id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); if (token) strncpy(log->product_id, token, MAX_ID_LEN-1);
        token = strtok_r(NULL, "|", &saveptr); if (token) strncpy(log->type, token, 19);
        token = strtok_r(NULL, "|", &saveptr); log->quantity = token ? atof(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); log->before_stock = token ? atof(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); log->after_stock = token ? atof(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); log->operator_id = token ? atoi(token) : 0;
        token = strtok_r(NULL, "|", &saveptr); if (token) strncpy(log->remark, token, 255);
        token = strtok_r(NULL, "|", &saveptr); log->created_at = token ? (time_t)atoll(token) : 0;
        
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
    int capacity = 0;
    *count = 0;

    StockLog *log = g_stock_logs;
    while (log) {
        if (product_id && strcmp(log->product_id, product_id) != 0) {
            log = log->next;
            continue;
        }
        if (log->created_at >= start && log->created_at <= end) {
            if (*count >= capacity) {
                capacity = capacity == 0 ? 16 : capacity * 2;
                result = (StockLog*)realloc(result, capacity * sizeof(StockLog));
            }
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
    for (int i = 0; i < g_product_hash->size; i++) {
        HashNode *node = g_product_hash->buckets[i];
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
    
    for (int i = 0; i < g_product_hash->size; i++) {
        HashNode *node = g_product_hash->buckets[i];
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
