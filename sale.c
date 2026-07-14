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
 *   → complete_sale → 原子提交订单、库存和会员状态
 *   → 从可丢弃的挂单与明细缓存移除
 *
 * 折扣计算层次（由外层 main.c 的 show_sale_menu 完成）：
 *   第1层：单品促销折扣（scan_and_sell 时写入 item.price）
 *   第2层：会员百分比折扣（discount_pct 参数）
 *   第3层：满减固定金额（discount_amount 参数）
 */

#include "supermarket.h"
#include "app/sm_sales_service.h"
#include "app/sm_inventory_service.h"
#include <limits.h>
#include <stdlib.h>

// ==================== 销售数据 ====================
Sale *g_pending_sales = NULL;   // 挂单链表头（状态=SALE_PENDING 的订单）
SaleItem *g_sale_items = NULL;  // 全局销售明细链表（所有挂单的明细，按 sale_id 关联）

/**
 * 清理指定收银员的挂单及其销售明细（供logout调用）
 */
void cleanup_cashier_sales(int cashier_id) {
    Sale *current = g_pending_sales;
    while (current) {
        Sale *next = current->next;
        if (current->cashier_id == cashier_id)
            (void)cancel_sale(current->id);
        current = next;
    }
}

// ==================== 销售记录 ====================

/**
 * 创建销售记录
 */
int create_sale(Sale *sale) {
    if (!sale) return -1;
    sale->created_at = time(NULL);
    sale->status = SALE_PENDING;  // 初始为挂单状态
    
    sale->id = 0;
    if (sm_service_sale_create(sale) != SM_REPO_OK) return -1;
    Sale *new_sale = (Sale*)malloc(sizeof(Sale));
    if (!new_sale) {
        (void)sm_service_sale_cancel(sale->id);
        return -1;
    }
    *new_sale = *sale;
    new_sale->next = g_pending_sales;
    g_pending_sales = new_sale;
    
    return sale->id;
}

/**
 * 添加销售明细
 */
int add_sale_item(int sale_id, SaleItem *item) {
    if (!item || sm_service_sale_item_create(sale_id, item) != SM_REPO_OK)
        return -1;
    SaleItem *new_item = (SaleItem*)malloc(sizeof(SaleItem));
    if (!new_item) return item->id;
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
    size_t result_count = 0;
    if (!count) return NULL;
    *count = 0;
    if (sm_service_sale_item_list(sale_id, &result, &result_count) !=
            SM_REPO_OK || result_count > INT_MAX) {
        sm_service_sales_array_free(result);
        return NULL;
    }
    *count = (int)result_count;
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
    int count = 0;
    int i;
    SaleItem *items = get_sale_items(sale_id, &count);
    for (i = 0; i < count; ++i) total += items[i].subtotal;
    sm_service_sales_array_free(items);
    return total;
}

static int append_stock_adjustment(sm_stock_adjustment **items,
                                   size_t *count, size_t *capacity,
                                   const char *product_id, int quantity) {
    size_t i;
    sm_stock_adjustment *next;
    if (!items || !count || !capacity || !product_id || !*product_id ||
        quantity <= 0)
        return 0;
    for (i = 0; i < *count; ++i) {
        if (strcmp((*items)[i].product_id, product_id) == 0) {
            if ((*items)[i].quantity > INT_MAX - quantity) return 0;
            (*items)[i].quantity += quantity;
            return 1;
        }
    }
    if (*count == *capacity) {
        size_t next_capacity = *capacity ? *capacity * 2u : 8u;
        if (next_capacity < *capacity ||
            next_capacity > SIZE_MAX / sizeof(**items))
            return 0;
        next = (sm_stock_adjustment *)realloc(
            *items, next_capacity * sizeof(**items));
        if (!next) return 0;
        *items = next;
        *capacity = next_capacity;
    }
    memset(&(*items)[*count], 0, sizeof(**items));
    snprintf((*items)[*count].product_id,
             sizeof((*items)[*count].product_id), "%s", product_id);
    (*items)[*count].quantity = quantity;
    ++*count;
    return 1;
}

static int build_stock_adjustments(const SaleItem *items, int item_count,
                                   sm_stock_adjustment **out,
                                   size_t *out_count) {
    sm_stock_adjustment *result = NULL;
    size_t count = 0, capacity = 0;
    int i;
    if (!out || !out_count || item_count < 0) return 0;
    *out = NULL;
    *out_count = 0;
    for (i = 0; i < item_count; ++i) {
        int quantity = (int)items[i].quantity;
        if (items[i].quantity <= 0.0f ||
            fabsf(items[i].quantity - (float)quantity) > 0.0001f)
            goto fail;
        if (items[i].is_combo) {
            ProductCombo *combo = find_combo_by_id(items[i].combo_id);
            ComboItem *part;
            if (!combo) goto fail;
            for (part = combo->items; part; part = part->next) {
                if (part->quantity <= 0 ||
                    quantity > INT_MAX / part->quantity ||
                    !append_stock_adjustment(&result, &count, &capacity,
                                             part->product_id,
                                             quantity * part->quantity))
                    goto fail;
            }
        } else if (!append_stock_adjustment(&result, &count, &capacity,
                                            items[i].product_id, quantity)) {
            goto fail;
        }
    }
    *out = result;
    *out_count = count;
    return 1;
fail:
    free(result);
    return 0;
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
int complete_sale_with_vip(int sale_id, const char *payment_method,
                           float discount_pct, float discount_amount,
                           float cash_received, const char *vip_card_no) {
    Sale *cached_sale = find_pending_sale(sale_id);
    Sale completed_sale;
    Sale *sale;
    if (!cached_sale || !payment_method) return -1;
    completed_sale = *cached_sale;
    sale = &completed_sale;

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
    sale->payment_method[19] = '\0';
    sale->status = SALE_COMPLETED;
    sale->completed_at = time(NULL);
    
    // 扣减库存（原子操作）
    int item_count = 0;
    SaleItem *items = get_sale_items(sale_id, &item_count);
    sm_stock_adjustment *adjustments = NULL;
    size_t adjustment_count = 0;
    int earned_points;
    if (item_count <= 0 ||
        !build_stock_adjustments(items, item_count, &adjustments,
                                 &adjustment_count)) {
        sm_service_sales_array_free(items);
        return -1;
    }
    earned_points = calculate_ladder_points(sale->final_amount);
    if ((vip_card_no && *vip_card_no
             ? sm_service_sale_complete_with_vip(
                   sale, adjustments, adjustment_count, earned_points,
                   vip_card_no, NULL)
             : sm_service_sale_complete(sale, adjustments, adjustment_count,
                                        earned_points, NULL)) !=
        SM_REPO_OK) {
        free(adjustments);
        sm_service_sales_array_free(items);
        return -1;
    }
    for (size_t i = 0; i < adjustment_count; ++i) {
        (void)refresh_product_by_id(adjustments[i].product_id);
    }
    if (sale->member_id > 0)
        (void)refresh_member_by_id(sale->member_id);
    free(adjustments);
    
    for (int i = 0; i < item_count; i++) {
        if (items[i].is_combo) {
            // 套装：使用套装专用库存扣减函数
            ProductCombo *combo = find_combo_by_id(items[i].combo_id);
            if (combo) {
                // 记录套装销售日志
                char log_msg[512];
                snprintf(log_msg, sizeof(log_msg), "套装[%s] x%d", combo->name, (int)items[i].quantity);
                write_transaction_log("SALE", sale_id, "COMBO_SOLD", log_msg, sale->cashier_id);
            }
        }
    }

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
            int earned = earned_points;
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
    sm_service_sales_array_free(items);

    return 0;
}

int complete_sale(int sale_id, const char *payment_method,
                  float discount_pct, float discount_amount,
                  float cash_received) {
    return complete_sale_with_vip(sale_id, payment_method, discount_pct,
                                  discount_amount, cash_received, NULL);
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
    if (sm_service_sale_cancel(sale_id) != SM_REPO_OK) return -1;

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

/* Rebuild the discardable pending-sale cache from database indexes. */
int load_pending_sales(void) {
    Sale *values = NULL;
    size_t value_count = 0;
    size_t i;
    if (sm_service_sale_list_pending(&values, &value_count) != SM_REPO_OK)
        return -1;
    for (i = 0; i < value_count; ++i) {
        Sale *copy = (Sale *)malloc(sizeof(*copy));
        SaleItem *items = NULL;
        size_t item_count = 0;
        size_t j;
        if (!copy || sm_service_sale_item_list(
                         values[i].id, &items, &item_count) != SM_REPO_OK) {
            free(copy);
            sm_service_sales_array_free(items);
            sm_service_sales_array_free(values);
            return -1;
        }
        *copy = values[i];
        copy->next = g_pending_sales;
        g_pending_sales = copy;
        for (j = 0; j < item_count; ++j) {
            SaleItem *item_copy = (SaleItem *)malloc(sizeof(*item_copy));
            if (!item_copy) {
                sm_service_sales_array_free(items);
                sm_service_sales_array_free(values);
                return -1;
            }
            *item_copy = items[j];
            item_copy->next = g_sale_items;
            g_sale_items = item_copy;
        }
        sm_service_sales_array_free(items);
    }
    sm_service_sales_array_free(values);
    return 0;
}

/* Persist pending header changes; items are persisted when they are added. */
int save_pending_sale(Sale *sale) {
    if (!sale) return -1;
    return sm_service_sale_update_pending(sale) == SM_REPO_OK ? 0 : -1;
}

/* Flush pending header cache changes without rewriting a text projection. */
int save_all_pending_sales(void) {
    Sale *current = g_pending_sales;
    while (current) {
        if (sm_service_sale_update_pending(current) != SM_REPO_OK)
            return -1;
        current = current->next;
    }
    return 0;
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

/**
 * 记录库存变动
 */
int record_stock_log(const char *product_id, const char *type, float quantity,
                     float before_stock, float after_stock, int operator_id, const char *remark) {
    StockLog log;
    if (!product_id || !type || quantity <= 0.0f || before_stock < 0.0f ||
        after_stock < 0.0f || operator_id < 0)
        return -1;
    memset(&log, 0, sizeof(log));
    snprintf(log.product_id, sizeof(log.product_id), "%s", product_id);
    snprintf(log.type, sizeof(log.type), "%s", type);
    log.quantity = quantity;
    log.before_stock = before_stock;
    log.after_stock = after_stock;
    log.operator_id = operator_id;
    snprintf(log.remark, sizeof(log.remark), "%s", remark ? remark : "");
    log.created_at = time(NULL);
    return sm_service_stock_log_create(&log) == SM_REPO_OK ? log.id : -1;
}

/**
 * 查询库存变动记录
 */
StockLog* query_stock_logs(const char *product_id, time_t start, time_t end, int *count) {
    StockLog *result = NULL;
    size_t result_count = 0;
    if (!count) return NULL;
    *count = 0;
    if (sm_service_stock_log_list(product_id, NULL, start, end, &result,
                                  &result_count) != SM_REPO_OK ||
        result_count > INT_MAX) {
        sm_service_inventory_array_free(result);
        return NULL;
    }
    *count = (int)result_count;
    return result;
}

// ==================== 库存预警 ====================

/**
 * 检查库存预警
 */
void check_stock_alert(void) {
    int product_count = 0;
    Product **products = list_low_stock_products(&product_count);
    printf("\n========== 库存预警 ==========\n");
    
    int alert_count = 0;
    for (int i = 0; i < product_count; ++i) {
        Product *prod = products[i];
        if (prod->status == STATUS_ACTIVE) {
            printf("[警告] %s (ID: %s) 库存不足: %d / 最低: %d\n",
                prod->name, prod->id, prod->stock, prod->min_stock);
            alert_count++;
        }
    }
    free(products);
    
    if (alert_count == 0) {
        printf("所有商品库存充足\n");
    }
    printf("==============================\n\n");
}

/**
 * 库存盘点
 */
void inventory_check(void) {
    int product_count = 0;
    Product **products = list_products(&product_count);
    printf("\n========== 库存盘点 ==========\n");
    
    int total_products = 0;
    int low_stock_count = 0;
    float total_value = 0;
    
    for (int i = 0; i < product_count; ++i) {
        Product *prod = products[i];
        if (prod->status == STATUS_ACTIVE) {
            total_products++;
            total_value += prod->stock * prod->cost;
            if (prod->stock <= prod->min_stock) {
                low_stock_count++;
            }
        }
    }
    free(products);
    
    printf("商品总数: %d\n", total_products);
    printf("库存不足商品: %d\n", low_stock_count);
    printf("库存总价值(成本价): ¥%.2f\n", total_value);
    printf("==============================\n\n");
}
