/**
 * @file store_ops.c
 * @brief 门店运营模块合并文件
 * 
 * 合并了以下模块：
 * - 门店管理 (store)
 * - 库存调拨 (transfer)
 * - 供应商结算 (supplier_settlement)
 */

#include "supermarket.h"

// ==================== 门店模块 ====================

// ==================== 全局变量 ====================
Store *g_stores = NULL;
StoreStock *g_store_stocks = NULL;
int g_default_store_id = 1;  // 默认门店ID为1

// ==================== 门店操作 ====================

/**
 * 创建门店
 */
int create_store(Store *store) {
    store->id = generate_id();
    store->created_at = time(NULL);
    store->updated_at = store->created_at;
    store->status = STORE_ACTIVE;
    
    Store *new_store = (Store*)malloc(sizeof(Store));
    *new_store = *store;
    new_store->next = g_stores;
    g_stores = new_store;
    
    save_store(new_store);
    return store->id;
}

/**
 * 查找门店（按ID）
 */
Store* find_store_by_id(int id) {
    Store *store = g_stores;
    while (store) {
        if (store->id == id) return store;
        store = store->next;
    }
    return NULL;
}

/**
 * 查找门店（按名称）
 */
Store* find_store_by_name(const char *name) {
    Store *store = g_stores;
    while (store) {
        if (strcmp(store->name, name) == 0) return store;
        store = store->next;
    }
    return NULL;
}

/**
 * 更新门店
 */
int update_store(Store *store) {
    store->updated_at = time(NULL);
    
    Store *existing = find_store_by_id(store->id);
    if (existing) {
        *existing = *store;
        return save_all_stores();
    }
    return -1;
}

/**
 * 删除门店（下架）
 */
int delete_store(int id) {
    Store **prev = &g_stores;
    while (*prev) {
        if ((*prev)->id == id) {
            (*prev)->status = STORE_INACTIVE;
            (*prev)->updated_at = time(NULL);
            return save_all_stores();
        }
        prev = &(*prev)->next;
    }
    return -1;
}

/**
 * 列出所有门店
 */
Store** list_stores(int *count) {
    Store **list = NULL;
    *count = 0;
    
    Store *store = g_stores;
    while (store) {
        list = (Store**)realloc(list, (*count + 1) * sizeof(Store*));
        list[*count] = store;
        (*count)++;
        store = store->next;
    }
    
    return list;
}

/**
 * 列出活跃门店
 */
Store** list_active_stores(int *count) {
    Store **list = NULL;
    *count = 0;
    
    Store *store = g_stores;
    while (store) {
        if (store->status == STORE_ACTIVE) {
            list = (Store**)realloc(list, (*count + 1) * sizeof(Store*));
            list[*count] = store;
            (*count)++;
        }
        store = store->next;
    }
    
    return list;
}

// ==================== 门店库存操作 ====================

/**
 * 获取商品在指定门店的库存
 */
int get_store_stock(int store_id, const char *product_id) {
    StoreStock *stock = g_store_stocks;
    while (stock) {
        if (stock->store_id == store_id && strcmp(stock->product_id, product_id) == 0) {
            return stock->quantity;
        }
        stock = stock->next;
    }
    return 0;
}

/**
 * 设置/更新门店库存
 */
int set_store_stock(int store_id, const char *product_id, int quantity, int min_stock) {
    StoreStock *stock = g_store_stocks;
    while (stock) {
        if (stock->store_id == store_id && strcmp(stock->product_id, product_id) == 0) {
            stock->quantity = quantity;
            stock->min_stock = min_stock;
            stock->updated_at = time(NULL);
            return save_all_store_stocks();
        }
        stock = stock->next;
    }
    
    // 新增
    StoreStock *new_stock = (StoreStock*)malloc(sizeof(StoreStock));
    new_stock->id = generate_id();
    new_stock->store_id = store_id;
    strncpy(new_stock->product_id, product_id, MAX_ID_LEN - 1);
    new_stock->quantity = quantity;
    new_stock->min_stock = min_stock;
    new_stock->updated_at = time(NULL);
    new_stock->next = g_store_stocks;
    g_store_stocks = new_stock;
    
    return save_store_stock(new_stock);
}

/**
 * 增加门店库存
 */
int add_store_stock(int store_id, const char *product_id, int quantity, int operator_id, const char *remark) {
    StoreStock *stock = g_store_stocks;
    while (stock) {
        if (stock->store_id == store_id && strcmp(stock->product_id, product_id) == 0) {
            int before = stock->quantity;
            stock->quantity += quantity;
            stock->updated_at = time(NULL);
            
            record_stock_log(product_id, "入库", quantity, before, stock->quantity,
                           operator_id, remark ? remark : "门店入库");
            
            save_all_store_stocks();
            return 0;
        }
        stock = stock->next;
    }
    
    return -1;
}

/**
 * 扣减门店库存
 */
int reduce_store_stock(int store_id, const char *product_id, int quantity, int operator_id, const char *remark) {
    StoreStock *stock = g_store_stocks;
    while (stock) {
        if (stock->store_id == store_id && strcmp(stock->product_id, product_id) == 0) {
            if (stock->quantity < quantity) return -1;
            
            int before = stock->quantity;
            stock->quantity -= quantity;
            stock->updated_at = time(NULL);
            
            record_stock_log(product_id, "出库", quantity, before, stock->quantity,
                           operator_id, remark ? remark : "门店出库");
            
            save_all_store_stocks();
            return 0;
        }
        stock = stock->next;
    }
    
    return -1;
}

/**
 * 查询商品所有门店库存
 */
StoreStock** get_product_all_stores(const char *product_id, int *count) {
    StoreStock **list = NULL;
    *count = 0;
    
    StoreStock *stock = g_store_stocks;
    while (stock) {
        if (strcmp(stock->product_id, product_id) == 0) {
            list = (StoreStock**)realloc(list, (*count + 1) * sizeof(StoreStock*));
            list[*count] = stock;
            (*count)++;
        }
        stock = stock->next;
    }
    
    return list;
}

// ==================== 调拨模块 ====================

// ==================== 全局变量 ====================
TransferOrder *g_transfer_orders = NULL;
TransferItem *g_transfer_items = NULL;

// ==================== 调拨单操作 ====================

/**
 * 创建调拨单
 */
int create_transfer_order(int from_store_id, int to_store_id, int creator_id, const char *remark) {
    if (from_store_id == to_store_id) {
        printf("错误: 源门店和目标门店不能相同\n");
        return -1;
    }
    
    Store *from_store = find_store_by_id(from_store_id);
    Store *to_store = find_store_by_id(to_store_id);
    
    if (!from_store || !to_store) {
        printf("错误: 门店不存在\n");
        return -1;
    }
    
    TransferOrder *order = (TransferOrder*)malloc(sizeof(TransferOrder));
    memset(order, 0, sizeof(TransferOrder));
    
    order->id = generate_id();
    order->from_store_id = from_store_id;
    order->to_store_id = to_store_id;
    strncpy(order->from_store_name, from_store->name, 99);
    strncpy(order->to_store_name, to_store->name, 99);
    order->status = TRANSFER_PENDING;
    order->creator_id = creator_id;
    strncpy(order->remark, remark ? remark : "", 255);
    
    Employee *emp = find_employee_by_id(creator_id);
    if (emp) {
        strncpy(order->creator_name, emp->name, 49);
    }
    
    order->created_at = time(NULL);
    order->items = NULL;
    
    order->next = g_transfer_orders;
    g_transfer_orders = order;
    
    // 记录操作日志
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "创建调拨单: %s->%s", 
             from_store->name, to_store->name);
    write_transaction_log("TRANSFER", order->id, "CREATE", log_msg, creator_id);
    
    save_transfer_order(order);
    
    return order->id;
}

/**
 * 添加调拨明细
 */
int add_transfer_item(int transfer_id, const char *product_id, int quantity) {
    TransferOrder *order = find_transfer_order(transfer_id);
    if (!order) return -1;
    
    if (order->status != TRANSFER_PENDING) {
        printf("错误: 只有待审批的调拨单才能添加商品\n");
        return -1;
    }
    
    Product *prod = find_product_by_id(product_id);
    if (!prod) {
        printf("错误: 商品不存在\n");
        return -1;
    }
    
    TransferItem *item = (TransferItem*)malloc(sizeof(TransferItem));
    item->id = generate_id();
    item->transfer_id = transfer_id;
    strncpy(item->product_id, product_id, MAX_ID_LEN - 1);
    strncpy(item->product_name, prod->name, MAX_NAME_LEN - 1);
    item->quantity = quantity;
    
    item->next = order->items;
    order->items = item;
    
    // 同时添加到全局列表
    item->next = g_transfer_items;
    g_transfer_items = item;
    
    save_transfer_item(item);
    
    return 0;
}

/**
 * 查找调拨单
 */
TransferOrder* find_transfer_order(int id) {
    TransferOrder *order = g_transfer_orders;
    while (order) {
        if (order->id == id) return order;
        order = order->next;
    }
    return NULL;
}

/**
 * 审批调拨单
 */
int approve_transfer(int transfer_id, int approver_id) {
    TransferOrder *order = find_transfer_order(transfer_id);
    if (!order) return -1;
    
    if (order->status != TRANSFER_PENDING) {
        printf("错误: 调拨单状态不是待审批\n");
        return -1;
    }
    
    Employee *emp = find_employee_by_id(approver_id);
    if (emp) {
        strncpy(order->approver_name, emp->name, 49);
    }
    order->approver_id = approver_id;
    order->approved_at = time(NULL);
    
    // 记录日志
    write_transaction_log("TRANSFER", transfer_id, "APPROVE", 
                         "调拨单审批通过", approver_id);
    
    return save_all_transfers();
}

/**
 * 拒绝调拨单
 */
int reject_transfer(int transfer_id, int approver_id, const char *reason) {
    TransferOrder *order = find_transfer_order(transfer_id);
    if (!order) return -1;
    
    order->status = TRANSFER_REJECTED;
    order->approver_id = approver_id;
    order->approved_at = time(NULL);
    
    Employee *emp = find_employee_by_id(approver_id);
    if (emp) {
        strncpy(order->approver_name, emp->name, 49);
    }
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "调拨单被拒绝: %s", reason ? reason : "");
    write_transaction_log("TRANSFER", transfer_id, "REJECT", log_msg, approver_id);
    
    return save_all_transfers();
}

/**
 * 出库确认
 */
int confirm_out_transfer(int transfer_id, int operator_id) {
    TransferOrder *order = find_transfer_order(transfer_id);
    if (!order) return -1;
    
    if (order->status != TRANSFER_PENDING) {
        printf("错误: 调拨单状态不是待出库\n");
        return -1;
    }
    
    // 扣减源门店库存
    TransferItem *item = order->items;
    while (item) {
        if (reduce_store_stock(order->from_store_id, item->product_id, 
                              item->quantity, operator_id, "调拨出库") != 0) {
            // 如果没有使用多门店库存，使用全局库存
            Product *prod = find_product_by_id(item->product_id);
            if (prod) {
                int before = prod->stock;
                prod->stock -= item->quantity;
                record_stock_log(item->product_id, "出库", item->quantity,
                               before, prod->stock, operator_id, "调拨出库");
                update_product(prod);
            }
        }
        item = item->next;
    }
    
    order->status = TRANSFER_OUT;
    order->out_operator_id = operator_id;
    order->out_at = time(NULL);
    
    Employee *emp = find_employee_by_id(operator_id);
    if (emp) {
        strncpy(order->out_operator_name, emp->name, 49);
    }
    
    write_transaction_log("TRANSFER", transfer_id, "OUT", 
                         "调拨出库确认", operator_id);
    
    return save_all_transfers();
}

/**
 * 入库确认
 */
int confirm_in_transfer(int transfer_id, int operator_id) {
    TransferOrder *order = find_transfer_order(transfer_id);
    if (!order) return -1;
    
    if (order->status != TRANSFER_OUT) {
        printf("错误: 调拨单状态不是待入库\n");
        return -1;
    }
    
    // 增加目标门店库存
    TransferItem *item = order->items;
    while (item) {
        if (add_store_stock(order->to_store_id, item->product_id,
                           item->quantity, operator_id, "调拨入库") != 0) {
            // 如果没有使用多门店库存，增加全局库存
            Product *prod = find_product_by_id(item->product_id);
            if (prod) {
                int before = prod->stock;
                prod->stock += item->quantity;
                record_stock_log(item->product_id, "入库", item->quantity,
                               before, prod->stock, operator_id, "调拨入库");
                update_product(prod);
            }
        }
        item = item->next;
    }
    
    order->status = TRANSFER_IN;
    order->in_operator_id = operator_id;
    order->in_at = time(NULL);
    
    Employee *emp = find_employee_by_id(operator_id);
    if (emp) {
        strncpy(order->in_operator_name, emp->name, 49);
    }
    
    write_transaction_log("TRANSFER", transfer_id, "IN", 
                         "调拨入库确认", operator_id);
    
    return save_all_transfers();
}

/**
 * 取消调拨单
 */
int cancel_transfer(int transfer_id, int operator_id) {
    TransferOrder *order = find_transfer_order(transfer_id);
    if (!order) return -1;
    
    if (order->status == TRANSFER_IN) {
        printf("错误: 已完成的调拨单不能取消\n");
        return -1;
    }
    
    order->status = TRANSFER_CANCELLED;
    
    write_transaction_log("TRANSFER", transfer_id, "CANCEL", 
                         "调拨单已取消", operator_id);
    
    return save_all_transfers();
}

/**
 * 按状态列出调拨单
 */
TransferOrder** list_transfers_by_status(int status, int *count) {
    TransferOrder **list = NULL;
    *count = 0;
    
    TransferOrder *order = g_transfer_orders;
    while (order) {
        if (status == -1 || order->status == status) {  // -1表示全部
            list = (TransferOrder**)realloc(list, (*count + 1) * sizeof(TransferOrder*));
            list[*count] = order;
            (*count)++;
        }
        order = order->next;
    }
    
    return list;
}

/**
 * 列出某门店的调拨单
 */
TransferOrder** list_transfers_by_store(int store_id, int *count) {
    TransferOrder **list = NULL;
    *count = 0;
    
    TransferOrder *order = g_transfer_orders;
    while (order) {
        if (order->from_store_id == store_id || order->to_store_id == store_id) {
            list = (TransferOrder**)realloc(list, (*count + 1) * sizeof(TransferOrder*));
            list[*count] = order;
            (*count)++;
        }
        order = order->next;
    }
    
    return list;
}

// ==================== 供应商结算模块 ====================

// ==================== 全局变量 ====================
SupplierFinance *g_supplier_finances = NULL;
Payable *g_payables = NULL;
PaymentRecord *g_payment_records = NULL;

// ==================== 供应商财务操作 ====================

/**
 * 获取供应商财务信息
 */
SupplierFinance* get_supplier_finance(int supplier_id) {
    SupplierFinance *fin = g_supplier_finances;
    while (fin) {
        if (fin->supplier_id == supplier_id) return fin;
        fin = fin->next;
    }
    return NULL;
}

/**
 * 创建/更新供应商财务信息
 */
int update_supplier_finance(int supplier_id, float payment_days, char rating) {
    SupplierFinance *fin = get_supplier_finance(supplier_id);
    
    if (fin) {
        fin->payment_days = payment_days;
        fin->rating = rating;
        fin->updated_at = time(NULL);
    } else {
        fin = (SupplierFinance*)malloc(sizeof(SupplierFinance));
        memset(fin, 0, sizeof(SupplierFinance));
        fin->supplier_id = supplier_id;
        fin->payment_days = payment_days;
        fin->rating = rating;
        fin->updated_at = time(NULL);
        
        fin->next = g_supplier_finances;
        g_supplier_finances = fin;
    }
    
    save_supplier_finance(fin);
    return 0;
}

/**
 * 列出所有供应商财务信息
 */
SupplierFinance** list_supplier_finances(int *count) {
    SupplierFinance **list = NULL;
    *count = 0;
    
    SupplierFinance *fin = g_supplier_finances;
    while (fin) {
        list = (SupplierFinance**)realloc(list, (*count + 1) * sizeof(SupplierFinance*));
        list[*count] = fin;
        (*count)++;
        fin = fin->next;
    }
    
    return list;
}

// ==================== 应付账款操作 ====================

/**
 * 生成应付账款（基于已完成的采购单）
 */
int generate_payable(int purchase_id) {
    extern Purchase* find_purchase(int id);
    Purchase *pur = find_purchase(purchase_id);
    if (!pur) return -1;
    
    if (pur->status != PURCHASE_COMPLETED) {
        printf("错误: 只有已完成的采购单才能生成应付款\n");
        return -1;
    }
    
    // 检查是否已生成过
    Payable *exist = g_payables;
    while (exist) {
        if (exist->purchase_id == purchase_id) {
            printf("警告: 该采购单已生成过应付款\n");
            return -1;
        }
        exist = exist->next;
    }
    
    Payable *payable = (Payable*)malloc(sizeof(Payable));
    memset(payable, 0, sizeof(Payable));
    
    payable->id = generate_id();
    payable->supplier_id = atoi(pur->supplier_id);
    strncpy(payable->supplier_name, pur->supplier_name, 99);
    payable->purchase_id = purchase_id;
    payable->amount = pur->total_amount;
    payable->paid_amount = 0;
    payable->pending_amount = pur->total_amount;
    payable->status = PAYMENT_PENDING;
    payable->created_at = time(NULL);
    
    // 计算到期日期（基于供应商账期）
    SupplierFinance *fin = get_supplier_finance(payable->supplier_id);
    if (fin && fin->payment_days > 0) {
        payable->due_date = payable->created_at + (time_t)(fin->payment_days * 24 * 3600);
    }
    
    payable->next = g_payables;
    g_payables = payable;
    
    // 更新供应商财务统计
    if (fin) {
        fin->total_amount += pur->total_amount;
        fin->pending_amount += pur->total_amount;
        fin->updated_at = time(NULL);
        save_supplier_finance(fin);
    }
    
    // 记录日志
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "生成应付款: ¥%.2f (采购单#%d)",
            pur->total_amount, purchase_id);
    write_transaction_log("PAYABLE", payable->id, "CREATE", log_msg, 0);
    
    save_payable(payable);
    
    return payable->id;
}

/**
 * 手动创建应付账款
 */
int create_payable(int supplier_id, float amount, const char *remark) {
    Supplier *sup = find_supplier_by_id(supplier_id);
    if (!sup) return -1;
    
    Payable *payable = (Payable*)malloc(sizeof(Payable));
    memset(payable, 0, sizeof(Payable));
    
    payable->id = generate_id();
    payable->supplier_id = supplier_id;
    strncpy(payable->supplier_name, sup->name, 99);
    payable->purchase_id = 0;  // 手动创建，无关联采购单
    payable->amount = amount;
    payable->paid_amount = 0;
    payable->pending_amount = amount;
    payable->status = PAYMENT_PENDING;
    payable->created_at = time(NULL);
    
    SupplierFinance *fin = get_supplier_finance(supplier_id);
    if (fin && fin->payment_days > 0) {
        payable->due_date = payable->created_at + (time_t)(fin->payment_days * 24 * 3600);
    }
    
    payable->next = g_payables;
    g_payables = payable;
    
    if (fin) {
        fin->total_amount += amount;
        fin->pending_amount += amount;
        fin->updated_at = time(NULL);
        save_supplier_finance(fin);
    }
    
    save_payable(payable);
    
    return payable->id;
}

/**
 * 查找应付账款
 */
Payable* find_payable(int id) {
    Payable *payable = g_payables;
    while (payable) {
        if (payable->id == id) return payable;
        payable = payable->next;
    }
    return NULL;
}

/**
 * 按供应商列出应付账款
 */
Payable** list_payables_by_supplier(int supplier_id, int *count) {
    Payable **list = NULL;
    *count = 0;
    
    Payable *payable = g_payables;
    while (payable) {
        if (payable->supplier_id == supplier_id) {
            list = (Payable**)realloc(list, (*count + 1) * sizeof(Payable*));
            list[*count] = payable;
            (*count)++;
        }
        payable = payable->next;
    }
    
    return list;
}

/**
 * 列出所有待付款
 */
Payable** list_pending_payables(int *count) {
    Payable **list = NULL;
    *count = 0;
    
    Payable *payable = g_payables;
    while (payable) {
        if (payable->status != PAYMENT_COMPLETED) {
            list = (Payable**)realloc(list, (*count + 1) * sizeof(Payable*));
            list[*count] = payable;
            (*count)++;
        }
        payable = payable->next;
    }
    
    return list;
}

// ==================== 付款操作 ====================

/**
 * 付款
 */
int record_payment(int payable_id, float amount, const char *method, 
                  const char *reference, int operator_id, const char *remark) {
    Payable *payable = find_payable(payable_id);
    if (!payable) return -1;
    
    if (payable->status == PAYMENT_COMPLETED) {
        printf("错误: 该应付款已结清\n");
        return -1;
    }
    
    if (amount > payable->pending_amount) {
        printf("警告: 付款金额超过待付金额\n");
        amount = payable->pending_amount;
    }
    
    // 创建付款记录
    PaymentRecord *record = (PaymentRecord*)malloc(sizeof(PaymentRecord));
    memset(record, 0, sizeof(PaymentRecord));
    
    record->id = generate_id();
    record->payable_id = payable_id;
    record->supplier_id = payable->supplier_id;
    record->amount = amount;
    strncpy(record->method, method ? method : "银行转账", 19);
    strncpy(record->reference, reference ? reference : "", 49);
    record->operator_id = operator_id;
    strncpy(record->remark, remark ? remark : "", 255);
    record->paid_at = time(NULL);
    
    Employee *emp = find_employee_by_id(operator_id);
    if (emp) {
        strncpy(record->operator_name, emp->name, 49);
    }
    
    record->next = g_payment_records;
    g_payment_records = record;
    
    // 更新应付账款
    payable->paid_amount += amount;
    payable->pending_amount -= amount;
    
    if (payable->pending_amount <= 0.01f) {
        payable->status = PAYMENT_COMPLETED;
        payable->paid_at = time(NULL);
    } else {
        payable->status = PAYMENT_PARTIAL;
    }
    
    // 更新供应商财务统计
    SupplierFinance *fin = get_supplier_finance(payable->supplier_id);
    if (fin) {
        fin->paid_amount += amount;
        fin->pending_amount -= amount;
        fin->last_payment_date = time(NULL);
        fin->updated_at = time(NULL);
        save_supplier_finance(fin);
    }
    
    // 记录日志
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "供应商付款: ¥%.2f (应付单#%d, 方式:%s)",
            amount, payable_id, method);
    write_transaction_log("PAYMENT", record->id, "PAY", log_msg, operator_id);
    
    save_payment_record(record);
    
    return 0;
}

/**
 * 按供应商结算所有应付款
 */
int settle_supplier(int supplier_id, float amount, const char *method,
                    const char *reference, int operator_id, const char *remark) {
    // 查找该供应商所有待付款
    int count = 0;
    Payable **list = list_payables_by_supplier(supplier_id, &count);
    
    float remaining = amount;
    for (int i = 0; i < count && remaining > 0; i++) {
        if (list[i]->pending_amount > 0) {
            float pay_amt = remaining < list[i]->pending_amount ? remaining : list[i]->pending_amount;
            record_payment(list[i]->id, pay_amt, method, reference, operator_id, remark);
            remaining -= pay_amt;
        }
    }
    
    free(list);
    return 0;
}

/**
 * 列出付款记录
 */
PaymentRecord** list_payment_records(int *count) {
    PaymentRecord **list = NULL;
    *count = 0;
    
    PaymentRecord *rec = g_payment_records;
    while (rec) {
        list = (PaymentRecord**)realloc(list, (*count + 1) * sizeof(PaymentRecord*));
        list[*count] = rec;
        (*count)++;
        rec = rec->next;
    }
    
    return list;
}

/**
 * 列出供应商付款记录
 */
PaymentRecord** list_supplier_payment_records(int supplier_id, int *count) {
    PaymentRecord **list = NULL;
    *count = 0;
    
    PaymentRecord *rec = g_payment_records;
    while (rec) {
        if (rec->supplier_id == supplier_id) {
            list = (PaymentRecord**)realloc(list, (*count + 1) * sizeof(PaymentRecord*));
            list[*count] = rec;
            (*count)++;
        }
        rec = rec->next;
    }
    
    return list;
}

// ==================== 报表 ====================

/**
 * 生成供应商对账单
 */
void generate_supplier_statement(int supplier_id, time_t start, time_t end) {
    Supplier *sup = find_supplier_by_id(supplier_id);
    if (!sup) {
        printf("供应商不存在\n");
        return;
    }
    
    printf("\n========== 供应商对账单 ==========\n");
    printf("供应商: %s (ID: %d)\n", sup->name, supplier_id);
    
    char date_str[32];
    format_time(start, date_str);
    printf("起始日期: %s\n", date_str);
    format_time(end, date_str);
    printf("结束日期: %s\n", date_str);
    
    printf("\n【应付账款】\n");
    printf("%-10s %-12s %-10s %-10s %-10s\n", "应付单ID", "采购单ID", "应付金额", "已付金额", "待付金额");
    printf("------------------------------------------------\n");
    
    float total_amount = 0, total_paid = 0, total_pending = 0;
    
    Payable *payable = g_payables;
    while (payable) {
        if (payable->supplier_id == supplier_id &&
            payable->created_at >= start && payable->created_at <= end) {
            printf("%-10d %-12d %-10.2f %-10.2f %-10.2f\n",
                   payable->id, payable->purchase_id, payable->amount,
                   payable->paid_amount, payable->pending_amount);
            total_amount += payable->amount;
            total_paid += payable->paid_amount;
            total_pending += payable->pending_amount;
        }
        payable = payable->next;
    }
    
    printf("------------------------------------------------\n");
    printf("合计:     %10.2f %10.2f %10.2f\n", total_amount, total_paid, total_pending);
    
    printf("\n【付款记录】\n");
    printf("%-10s %-12s %-10s %-20s\n", "付款ID", "时间", "金额", "方式");
    printf("------------------------------------------------\n");
    
    PaymentRecord *rec = g_payment_records;
    while (rec) {
        if (rec->supplier_id == supplier_id &&
            rec->paid_at >= start && rec->paid_at <= end) {
            char time_str[32];
            format_time(rec->paid_at, time_str);
            printf("%-10d %-12s %-10.2f %-20s\n",
                   rec->id, time_str, rec->amount, rec->method);
        }
        rec = rec->next;
    }
    
    printf("================================\n\n");
}

/**
 * 打印应付款汇总
 */
void print_payables_summary(void) {
    printf("\n========== 应付款汇总 ==========\n");
    printf("%-10s %-20s %-10s %-10s %-10s %-8s\n", 
           "供应商ID", "供应商名称", "应付总额", "已付总额", "待付总额", "评级");
    printf("---------------------------------------------------------------------\n");
    
    // 按供应商汇总
    typedef struct { int id; char name[100]; float total; float paid; float pending; char rating; } SupplierSummary;
    SupplierSummary *sums = NULL;
    int sum_count = 0;
    
    Payable *payable = g_payables;
    while (payable) {
        // 查找或创建汇总项
        int found = -1;
        for (int i = 0; i < sum_count; i++) {
            if (sums[i].id == payable->supplier_id) {
                found = i;
                break;
            }
        }
        
        if (found < 0) {
            sum_count++;
            sums = (SupplierSummary*)realloc(sums, sum_count * sizeof(SupplierSummary));
            memset(&sums[sum_count - 1], 0, sizeof(SupplierSummary));
            sums[sum_count - 1].id = payable->supplier_id;
            strncpy(sums[sum_count - 1].name, payable->supplier_name, 99);
            SupplierFinance *fin = get_supplier_finance(payable->supplier_id);
            if (fin) sums[sum_count - 1].rating = fin->rating;
            found = sum_count - 1;
        }
        
        sums[found].total += payable->amount;
        sums[found].paid += payable->paid_amount;
        sums[found].pending += payable->pending_amount;
        
        payable = payable->next;
    }
    
    float grand_total = 0, grand_paid = 0, grand_pending = 0;
    for (int i = 0; i < sum_count; i++) {
        printf("%-10d %-20s %-10.2f %-10.2f %-10.2f %-8c\n",
               sums[i].id, sums[i].name, sums[i].total, sums[i].paid, 
               sums[i].pending, sums[i].rating ? sums[i].rating : '-');
        grand_total += sums[i].total;
        grand_paid += sums[i].paid;
        grand_pending += sums[i].pending;
    }
    
    printf("---------------------------------------------------------------------\n");
    printf("总计:     %33.2f %10.2f %10.2f\n", grand_total, grand_paid, grand_pending);
    printf("================================\n\n");
    
    free(sums);
}

// ==================== 门店文件操作 ====================

/**
 * 加载门店数据
 */
int load_stores(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/store.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        // 如果文件不存在，创建默认门店
        Store default_store;
        memset(&default_store, 0, sizeof(default_store));
        strcpy(default_store.name, "总店");
        strcpy(default_store.address, "");
        strcpy(default_store.phone, "");
        strcpy(default_store.manager_name, "");
        default_store.manager_id = 0;
        create_store(&default_store);
        return 0;
    }
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Store *store = (Store*)malloc(sizeof(Store));
        memset(store, 0, sizeof(Store));
        
        strncpy(store->name, strtok(line, "|"), 99);
        strncpy(store->address, strtok(NULL, "|"), 255);
        strncpy(store->phone, strtok(NULL, "|"), 19);
        strncpy(store->manager_name, strtok(NULL, "|"), 49);
        store->id = atoi(strtok(NULL, "|"));
        store->manager_id = atoi(strtok(NULL, "|"));
        store->status = atoi(strtok(NULL, "|"));
        store->created_at = (time_t)atoi(strtok(NULL, "|"));
        store->updated_at = (time_t)atoi(strtok(NULL, "|"));
        
        store->next = g_stores;
        g_stores = store;
        
        if (store->id > g_auto_id_counter) {
            g_auto_id_counter = store->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存门店
 */
int save_store(Store *store) {
    char filepath[256];
    char content[1024];
    
    snprintf(filepath, sizeof(filepath), "%s/store.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%s|%s|%s|%s|%d|%d|%d|%ld|%ld",
        store->name, store->address, store->phone, store->manager_name,
        store->id, store->manager_id, store->status,
        (long)store->created_at, (long)store->updated_at);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有门店
 */
int save_all_stores(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/store.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    Store *store = g_stores;
    while (store) {
        char line[1024];
        snprintf(line, sizeof(line),
            "%s|%s|%s|%s|%d|%d|%d|%ld|%ld\n",
            store->name, store->address, store->phone, store->manager_name,
            store->id, store->manager_id, store->status,
            (long)store->created_at, (long)store->updated_at);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        store = store->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

/**
 * 加载门店库存数据
 */
int load_store_stocks(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/store_stock.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        StoreStock *stock = (StoreStock*)malloc(sizeof(StoreStock));
        memset(stock, 0, sizeof(StoreStock));
        
        stock->store_id = atoi(strtok(line, "|"));
        strncpy(stock->product_id, strtok(NULL, "|"), MAX_ID_LEN - 1);
        stock->quantity = atoi(strtok(NULL, "|"));
        stock->min_stock = atoi(strtok(NULL, "|"));
        stock->id = atoi(strtok(NULL, "|"));
        stock->updated_at = (time_t)atoi(strtok(NULL, "|"));
        
        stock->next = g_store_stocks;
        g_store_stocks = stock;
        
        if (stock->id > g_auto_id_counter) {
            g_auto_id_counter = stock->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存门店库存
 */
int save_store_stock(StoreStock *stock) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/store_stock.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%s|%d|%d|%d|%ld",
        stock->store_id, stock->product_id, stock->quantity, 
        stock->min_stock, stock->id, (long)stock->updated_at);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有门店库存
 */
int save_all_store_stocks(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/store_stock.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    StoreStock *stock = g_store_stocks;
    while (stock) {
        char line[512];
        snprintf(line, sizeof(line),
            "%d|%s|%d|%d|%d|%ld\n",
            stock->store_id, stock->product_id, stock->quantity,
            stock->min_stock, stock->id, (long)stock->updated_at);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        stock = stock->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

// ==================== 调拨文件操作 ====================

/**
 * 加载调拨单
 */
int load_transfers(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/transfer.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        TransferOrder *order = (TransferOrder*)malloc(sizeof(TransferOrder));
        memset(order, 0, sizeof(TransferOrder));
        
        order->id = atoi(strtok(line, "|"));
        order->from_store_id = atoi(strtok(NULL, "|"));
        order->to_store_id = atoi(strtok(NULL, "|"));
        strncpy(order->from_store_name, strtok(NULL, "|"), 99);
        strncpy(order->to_store_name, strtok(NULL, "|"), 99);
        order->status = atoi(strtok(NULL, "|"));
        order->creator_id = atoi(strtok(NULL, "|"));
        strncpy(order->creator_name, strtok(NULL, "|"), 49);
        order->approver_id = atoi(strtok(NULL, "|"));
        strncpy(order->approver_name, strtok(NULL, "|"), 49);
        order->out_operator_id = atoi(strtok(NULL, "|"));
        strncpy(order->out_operator_name, strtok(NULL, "|"), 49);
        order->in_operator_id = atoi(strtok(NULL, "|"));
        strncpy(order->in_operator_name, strtok(NULL, "|"), 49);
        order->created_at = (time_t)atoi(strtok(NULL, "|"));
        order->approved_at = (time_t)atoi(strtok(NULL, "|"));
        order->out_at = (time_t)atoi(strtok(NULL, "|"));
        order->in_at = (time_t)atoi(strtok(NULL, "|"));
        strncpy(order->remark, strtok(NULL, "|"), 255);
        
        order->items = NULL;
        order->next = g_transfer_orders;
        g_transfer_orders = order;
        
        if (order->id > g_auto_id_counter) {
            g_auto_id_counter = order->id;
        }
    }
    
    fclose(fp);
    
    // 加载调拨明细
    load_transfer_items();
    
    return 0;
}

/**
 * 加载调拨明细
 */
int load_transfer_items(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/transfer_item.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        TransferItem *item = (TransferItem*)malloc(sizeof(TransferItem));
        memset(item, 0, sizeof(TransferItem));
        
        item->transfer_id = atoi(strtok(line, "|"));
        strncpy(item->product_id, strtok(NULL, "|"), MAX_ID_LEN - 1);
        strncpy(item->product_name, strtok(NULL, "|"), MAX_NAME_LEN - 1);
        item->quantity = atoi(strtok(NULL, "|"));
        item->id = atoi(strtok(NULL, "|"));
        
        // 添加到对应的调拨单
        TransferOrder *order = find_transfer_order(item->transfer_id);
        if (order) {
            item->next = order->items;
            order->items = item;
        }
        
        item->next = g_transfer_items;
        g_transfer_items = item;
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存调拨单
 */
int save_transfer_order(TransferOrder *order) {
    char filepath[256];
    char content[2048];
    
    snprintf(filepath, sizeof(filepath), "%s/transfer.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%d|%d|%s|%s|%d|%d|%s|%d|%s|%d|%s|%d|%s|%ld|%ld|%ld|%ld|%s",
        order->id, order->from_store_id, order->to_store_id,
        order->from_store_name, order->to_store_name,
        order->status, order->creator_id, order->creator_name,
        order->approver_id, order->approver_name,
        order->out_operator_id, order->out_operator_name,
        order->in_operator_id, order->in_operator_name,
        (long)order->created_at, (long)order->approved_at,
        (long)order->out_at, (long)order->in_at,
        order->remark);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有调拨单
 */
int save_all_transfers(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/transfer.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    TransferOrder *order = g_transfer_orders;
    while (order) {
        char line[2048];
        snprintf(line, sizeof(line),
            "%d|%d|%d|%s|%s|%d|%d|%s|%d|%s|%d|%s|%d|%s|%ld|%ld|%ld|%ld|%s\n",
            order->id, order->from_store_id, order->to_store_id,
            order->from_store_name, order->to_store_name,
            order->status, order->creator_id, order->creator_name,
            order->approver_id, order->approver_name,
            order->out_operator_id, order->out_operator_name,
            order->in_operator_id, order->in_operator_name,
            (long)order->created_at, (long)order->approved_at,
            (long)order->out_at, (long)order->in_at,
            order->remark);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        order = order->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

/**
 * 保存调拨明细
 */
int save_transfer_item(TransferItem *item) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/transfer_item.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%s|%s|%d|%d",
        item->transfer_id, item->product_id, item->product_name,
        item->quantity, item->id);
    
    return atomic_append(filepath, content);
}

// ==================== 供应商结算文件操作 ====================

/**
 * 加载供应商财务信息
 */
int load_supplier_finances(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/supplier_finance.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        SupplierFinance *fin = (SupplierFinance*)malloc(sizeof(SupplierFinance));
        memset(fin, 0, sizeof(SupplierFinance));
        
        fin->supplier_id = atoi(strtok(line, "|"));
        fin->payment_days = atof(strtok(NULL, "|"));
        fin->rating = strtok(NULL, "|")[0];
        fin->total_amount = atof(strtok(NULL, "|"));
        fin->paid_amount = atof(strtok(NULL, "|"));
        fin->pending_amount = atof(strtok(NULL, "|"));
        fin->last_payment_date = (time_t)atoi(strtok(NULL, "|"));
        fin->updated_at = (time_t)atoi(strtok(NULL, "|"));
        
        fin->next = g_supplier_finances;
        g_supplier_finances = fin;
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存供应商财务信息
 */
int save_supplier_finance(SupplierFinance *fin) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/supplier_finance.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%.2f|%c|%.2f|%.2f|%.2f|%ld|%ld",
        fin->supplier_id, fin->payment_days, fin->rating,
        fin->total_amount, fin->paid_amount, fin->pending_amount,
        (long)fin->last_payment_date, (long)fin->updated_at);
    
    return atomic_append(filepath, content);
}

/**
 * 加载应付账款
 */
int load_payables(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/payable.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Payable *payable = (Payable*)malloc(sizeof(Payable));
        memset(payable, 0, sizeof(Payable));
        
        payable->id = atoi(strtok(line, "|"));
        payable->supplier_id = atoi(strtok(NULL, "|"));
        strncpy(payable->supplier_name, strtok(NULL, "|"), 99);
        payable->purchase_id = atoi(strtok(NULL, "|"));
        payable->amount = atof(strtok(NULL, "|"));
        payable->paid_amount = atof(strtok(NULL, "|"));
        payable->pending_amount = atof(strtok(NULL, "|"));
        payable->status = atoi(strtok(NULL, "|"));
        payable->due_date = (time_t)atoi(strtok(NULL, "|"));
        payable->created_at = (time_t)atoi(strtok(NULL, "|"));
        payable->paid_at = (time_t)atoi(strtok(NULL, "|"));
        
        payable->next = g_payables;
        g_payables = payable;
        
        if (payable->id > g_auto_id_counter) {
            g_auto_id_counter = payable->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存应付账款
 */
int save_payable(Payable *payable) {
    char filepath[256];
    char content[1024];
    
    snprintf(filepath, sizeof(filepath), "%s/payable.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%d|%s|%d|%.2f|%.2f|%.2f|%d|%ld|%ld|%ld",
        payable->id, payable->supplier_id, payable->supplier_name,
        payable->purchase_id, payable->amount, payable->paid_amount,
        payable->pending_amount, payable->status,
        (long)payable->due_date, (long)payable->created_at, (long)payable->paid_at);
    
    return atomic_append(filepath, content);
}

/**
 * 加载付款记录
 */
int load_payment_records(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/payment_record.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        PaymentRecord *rec = (PaymentRecord*)malloc(sizeof(PaymentRecord));
        memset(rec, 0, sizeof(PaymentRecord));
        
        rec->id = atoi(strtok(line, "|"));
        rec->payable_id = atoi(strtok(NULL, "|"));
        rec->supplier_id = atoi(strtok(NULL, "|"));
        rec->amount = atof(strtok(NULL, "|"));
        strncpy(rec->method, strtok(NULL, "|"), 19);
        strncpy(rec->reference, strtok(NULL, "|"), 49);
        rec->operator_id = atoi(strtok(NULL, "|"));
        strncpy(rec->operator_name, strtok(NULL, "|"), 49);
        strncpy(rec->remark, strtok(NULL, "|"), 255);
        rec->paid_at = (time_t)atoi(strtok(NULL, "|"));
        
        rec->next = g_payment_records;
        g_payment_records = rec;
        
        if (rec->id > g_auto_id_counter) {
            g_auto_id_counter = rec->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存付款记录
 */
int save_payment_record(PaymentRecord *rec) {
    char filepath[256];
    char content[1024];
    
    snprintf(filepath, sizeof(filepath), "%s/payment_record.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%d|%d|%.2f|%s|%s|%d|%s|%s|%ld",
        rec->id, rec->payable_id, rec->supplier_id, rec->amount,
        rec->method, rec->reference, rec->operator_id,
        rec->operator_name, rec->remark, (long)rec->paid_at);
    
    return atomic_append(filepath, content);
}
