#include "supermarket.h"

#include "app/sm_app_context.h"
#include "app/sm_operations_service.h"
#include "repo/sm_operations_repository.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static Store *stores;
static StoreStock *stocks;
static TransferOrder *orders;
static TransferItem *items;

static void free_stores(void) { while (stores) { Store *n = stores->next; free(stores); stores = n; } }
static void free_stocks(void) { while (stocks) { StoreStock *n = stocks->next; free(stocks); stocks = n; } }
static void free_orders(void) { while (orders) { TransferOrder *n = orders->next; free(orders); orders = n; } }
static void free_items(void) { while (items) { TransferItem *n = items->next; free(items); items = n; } }

static void to_store(const sm_store_record *src, Store *dst) {
    memset(dst, 0, sizeof(*dst)); dst->id = (int)src->id;
    snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    snprintf(dst->address, sizeof(dst->address), "%s", src->address);
    snprintf(dst->phone, sizeof(dst->phone), "%s", src->phone);
    snprintf(dst->manager_name, sizeof(dst->manager_name), "%s", src->manager_name);
    dst->manager_id = (int)src->manager_id; dst->status = src->status;
    dst->created_at = (time_t)src->created_at; dst->updated_at = (time_t)src->updated_at;
}

static int from_store(const Store *src, sm_store_record *dst) {
    if (!src || src->id < 0 || src->manager_id < 0) return 0;
    memset(dst, 0, sizeof(*dst)); dst->id = (uint64_t)src->id;
    snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    snprintf(dst->address, sizeof(dst->address), "%s", src->address);
    snprintf(dst->phone, sizeof(dst->phone), "%s", src->phone);
    snprintf(dst->manager_name, sizeof(dst->manager_name), "%s", src->manager_name);
    dst->manager_id = (uint64_t)src->manager_id; dst->status = (uint8_t)src->status;
    dst->created_at = (int64_t)src->created_at; dst->updated_at = (int64_t)src->updated_at;
    return 1;
}

static int refresh_stores(void) {
    sm_store_record *values = NULL; size_t count = 0, i;
    if (sm_service_store_list(-1, &values, &count) != SM_REPO_OK) return -1;
    free_stores();
    for (i = count; i > 0; --i) {
        Store *node = malloc(sizeof(*node)); if (!node) { sm_operations_array_free(values); return -1; }
        to_store(&values[i - 1], node); node->next = stores; stores = node;
    }
    sm_operations_array_free(values); return 0;
}

static int refresh_stocks(void) {
    sm_store_stock_record *values = NULL; size_t count = 0, i;
    if (sm_service_store_stock_list(0, NULL, &values, &count) != SM_REPO_OK) return -1;
    free_stocks();
    for (i = count; i > 0; --i) {
        StoreStock *node = calloc(1, sizeof(*node)); if (!node) { sm_operations_array_free(values); return -1; }
        node->id = (int)(i); node->store_id = (int)values[i - 1].store_id;
        snprintf(node->product_id, sizeof(node->product_id), "%s", values[i - 1].product_id);
        node->quantity = (int)values[i - 1].quantity; node->min_stock = (int)values[i - 1].min_stock;
        node->updated_at = (time_t)values[i - 1].updated_at; node->next = stocks; stocks = node;
    }
    sm_operations_array_free(values); return 0;
}

static void to_order(const sm_transfer_order_record *src, TransferOrder *dst) {
    memset(dst, 0, sizeof(*dst)); dst->id = (int)src->id;
    dst->from_store_id = (int)src->from_store_id; dst->to_store_id = (int)src->to_store_id;
    snprintf(dst->from_store_name, sizeof(dst->from_store_name), "%s", src->from_store_name);
    snprintf(dst->to_store_name, sizeof(dst->to_store_name), "%s", src->to_store_name);
    dst->status = src->status; dst->creator_id = (int)src->creator_id;
    snprintf(dst->creator_name, sizeof(dst->creator_name), "%s", src->creator_name);
    dst->approver_id = (int)src->approver_id; snprintf(dst->approver_name, sizeof(dst->approver_name), "%s", src->approver_name);
    dst->out_operator_id = (int)src->out_operator_id; snprintf(dst->out_operator_name, sizeof(dst->out_operator_name), "%s", src->out_operator_name);
    dst->in_operator_id = (int)src->in_operator_id; snprintf(dst->in_operator_name, sizeof(dst->in_operator_name), "%s", src->in_operator_name);
    dst->created_at = (time_t)src->created_at; dst->approved_at = (time_t)src->approved_at;
    dst->out_at = (time_t)src->out_at; dst->in_at = (time_t)src->in_at;
    snprintf(dst->remark, sizeof(dst->remark), "%s", src->remark);
}

static int refresh_transfers(void) {
    sm_transfer_order_record *ov = NULL; sm_transfer_item_record *iv = NULL;
    size_t oc = 0, ic = 0, i, j;
    if (sm_service_transfer_list(-1, 0, &ov, &oc) != SM_REPO_OK) return -1;
    free_orders(); free_items();
    for (i = oc; i > 0; --i) {
        TransferOrder *node = malloc(sizeof(*node));
        if (!node) { sm_operations_array_free(ov); return -1; }
        to_order(&ov[i - 1], node); node->next = orders; orders = node;
    }
    for (i = 0; i < oc; ++i) {
        if (sm_service_transfer_item_list(ov[i].id, &iv, &ic) != SM_REPO_OK) { sm_operations_array_free(ov); return -1; }
        for (j = 0; j < ic; ++j) {
            TransferItem *node = calloc(1, sizeof(*node));
            if (!node) { sm_operations_array_free(iv); sm_operations_array_free(ov); return -1; }
            node->id = (int)iv[j].id; node->transfer_id = (int)iv[j].transfer_id;
            snprintf(node->product_id, sizeof(node->product_id), "%s", iv[j].product_id);
            snprintf(node->product_name, sizeof(node->product_name), "%s", iv[j].product_name);
            node->quantity = (int)iv[j].quantity; node->next = items; items = node;
        }
        sm_operations_array_free(iv); iv = NULL; ic = 0;
    }
    for (TransferOrder *o = orders; o; o = o->next)
        for (TransferItem *it = items; it; it = it->next)
            if (it->transfer_id == o->id) { o->items = it; break; }
    sm_operations_array_free(ov); return 0;
}

int create_store(Store *store) {
    sm_store_record value;
    if (!store || !from_store(store, &value)) return -1;
    value.id = 0; value.status = STORE_ACTIVE; value.created_at = value.updated_at = (int64_t)time(NULL);
    if (sm_service_store_create(&value) != SM_REPO_OK || value.id > INT_MAX) return -1;
    store->id = (int)value.id; store->status = value.status;
    store->created_at = (time_t)value.created_at; store->updated_at = (time_t)value.updated_at;
    return refresh_stores() == 0 ? store->id : -1;
}

Store *find_store_by_id(int id) { Store *p; for (p = stores; p; p = p->next) if (p->id == id) return p; return NULL; }
Store *find_store_by_name(const char *name) { Store *p; if (!name) return NULL; for (p = stores; p; p = p->next) if (!strcmp(p->name, name)) return p; return NULL; }

int update_store(Store *store) {
    sm_store_record value; if (!from_store(store, &value)) return -1;
    value.updated_at = (int64_t)time(NULL);
    return sm_service_store_update(&value) == SM_REPO_OK && refresh_stores() == 0 ? 0 : -1;
}

int delete_store(int id) { Store *store = find_store_by_id(id); if (!store) return -1; store->status = STORE_INACTIVE; return update_store(store); }

static Store **store_list(int active, int *count) {
    Store **out = NULL; Store *p; int n = 0, i = 0;
    if (!count) return NULL;
    for (p = stores; p; p = p->next) if (!active || p->status == STORE_ACTIVE) ++n;
    if (n) { out = malloc((size_t)n * sizeof(*out)); if (!out) { *count = 0; return NULL; } }
    for (p = stores; p; p = p->next) if (!active || p->status == STORE_ACTIVE) out[i++] = p;
    *count = n; return out;
}

Store **list_stores(int *count) { return store_list(0, count); }
Store **list_active_stores(int *count) { return store_list(1, count); }

int get_store_stock(int store_id, const char *product_id) {
    sm_store_stock_record value; sm_repo_status s = sm_service_store_stock_get((uint64_t)store_id, product_id, &value);
    return s == SM_REPO_OK && value.quantity <= INT_MAX ? (int)value.quantity : 0;
}

int set_store_stock(int store_id, const char *product_id, int quantity, int min_stock) {
    sm_store_stock_record value; if (store_id <= 0 || !product_id || quantity < 0 || min_stock < 0) return -1;
    memset(&value, 0, sizeof(value)); value.store_id = (uint64_t)store_id;
    snprintf(value.product_id, sizeof(value.product_id), "%s", product_id);
    value.quantity = quantity; value.min_stock = min_stock; value.updated_at = (int64_t)time(NULL);
    return sm_service_store_stock_set(&value) == SM_REPO_OK && refresh_stocks() == 0 ? 0 : -1;
}

int add_store_stock(int store_id, const char *product_id, int quantity, int operator_id, const char *remark) {
    (void)operator_id; (void)remark; if (quantity <= 0) return -1;
    return sm_service_store_stock_adjust((uint64_t)store_id, product_id, quantity, -1, (int64_t)time(NULL)) == SM_REPO_OK && refresh_stocks() == 0 ? 0 : -1;
}

int reduce_store_stock(int store_id, const char *product_id, int quantity, int operator_id, const char *remark) {
    (void)operator_id; (void)remark; if (quantity <= 0) return -1;
    return sm_service_store_stock_adjust((uint64_t)store_id, product_id, -quantity, -1, (int64_t)time(NULL)) == SM_REPO_OK && refresh_stocks() == 0 ? 0 : -1;
}

StoreStock **get_product_all_stores(const char *product_id, int *count) {
    StoreStock **out = NULL; StoreStock *p; int n = 0, i = 0;
    if (!count || !product_id) return NULL;
    for (p = stocks; p; p = p->next) if (!strcmp(p->product_id, product_id)) ++n;
    if (n) { out = malloc((size_t)n * sizeof(*out)); if (!out) { *count = 0; return NULL; } }
    for (p = stocks; p; p = p->next) if (!strcmp(p->product_id, product_id)) out[i++] = p;
    *count = n; return out;
}

int load_stores(void) {
    if (!sm_app_repository() || refresh_stores() != 0) return -1;
    if (!stores) { Store value; memset(&value, 0, sizeof(value)); snprintf(value.name, sizeof(value.name), "Main Store"); if (create_store(&value) < 0) return -1; }
    return 0;
}
int save_store(Store *store) { return update_store(store); }
int save_all_stores(void) { return sm_app_repository() ? 0 : -1; }
int load_store_stocks(void) { return sm_app_repository() ? refresh_stocks() : -1; }
int save_store_stock(StoreStock *stock) { return stock ? set_store_stock(stock->store_id, stock->product_id, stock->quantity, stock->min_stock) : -1; }
int save_all_store_stocks(void) { return sm_app_repository() ? 0 : -1; }

int create_transfer_order(int from_store_id, int to_store_id, int creator_id, const char *remark) {
    sm_transfer_order_record value; Employee *employee;
    if (from_store_id <= 0 || to_store_id <= 0 || creator_id < 0) return -1;
    memset(&value, 0, sizeof(value)); value.from_store_id = (uint64_t)from_store_id;
    value.to_store_id = (uint64_t)to_store_id; value.creator_id = (uint64_t)creator_id;
    value.created_at = (int64_t)time(NULL); snprintf(value.remark, sizeof(value.remark), "%s", remark ? remark : "");
    employee = find_employee_by_id(creator_id); if (employee) snprintf(value.creator_name, sizeof(value.creator_name), "%s", employee->name);
    if (sm_service_transfer_create(&value) != SM_REPO_OK || value.id > INT_MAX) return -1;
    return refresh_transfers() == 0 ? (int)value.id : -1;
}

int add_transfer_item(int transfer_id, const char *product_id, int quantity) {
    sm_transfer_item_record value; Product *product;
    if (transfer_id <= 0 || !product_id || quantity <= 0) return -1;
    product = find_product_by_id(product_id); if (!product) return -1;
    memset(&value, 0, sizeof(value)); snprintf(value.product_id, sizeof(value.product_id), "%s", product_id);
    snprintf(value.product_name, sizeof(value.product_name), "%s", product->name); value.quantity = quantity;
    return sm_service_transfer_add_item((uint64_t)transfer_id, &value) == SM_REPO_OK && refresh_transfers() == 0 ? 0 : -1;
}

TransferOrder *find_transfer_order(int id) { TransferOrder *p; for (p = orders; p; p = p->next) if (p->id == id) return p; return NULL; }

static const char *employee_name(int id) { Employee *e = find_employee_by_id(id); return e ? e->name : ""; }
int approve_transfer(int id, int operator_id) { return sm_service_transfer_approve((uint64_t)id, (uint64_t)operator_id, employee_name(operator_id), (int64_t)time(NULL)) == SM_REPO_OK && refresh_transfers() == 0 ? 0 : -1; }
int reject_transfer(int id, int operator_id, const char *reason) { return sm_service_transfer_reject((uint64_t)id, (uint64_t)operator_id, employee_name(operator_id), reason, (int64_t)time(NULL)) == SM_REPO_OK && refresh_transfers() == 0 ? 0 : -1; }
int confirm_out_transfer(int id, int operator_id) { return sm_service_transfer_out((uint64_t)id, (uint64_t)operator_id, employee_name(operator_id), (int64_t)time(NULL)) == SM_REPO_OK && refresh_transfers() == 0 ? 0 : -1; }
int confirm_in_transfer(int id, int operator_id) { int ok = sm_service_transfer_in((uint64_t)id, (uint64_t)operator_id, employee_name(operator_id), (int64_t)time(NULL)) == SM_REPO_OK; if (ok) ok = refresh_transfers() == 0 && refresh_stocks() == 0; return ok ? 0 : -1; }
int cancel_transfer(int id, int operator_id) { return sm_service_transfer_cancel((uint64_t)id, (uint64_t)operator_id, (int64_t)time(NULL)) == SM_REPO_OK && refresh_transfers() == 0 ? 0 : -1; }

static TransferOrder **transfer_list(int status, int store, int *count) {
    TransferOrder **out = NULL; TransferOrder *p; int n = 0, i = 0;
    if (!count) return NULL;
    for (p = orders; p; p = p->next) if ((status < 0 || p->status == status) && (!store || p->from_store_id == store || p->to_store_id == store)) ++n;
    if (n) { out = malloc((size_t)n * sizeof(*out)); if (!out) { *count = 0; return NULL; } }
    for (p = orders; p; p = p->next) if ((status < 0 || p->status == status) && (!store || p->from_store_id == store || p->to_store_id == store)) out[i++] = p;
    *count = n; return out;
}
TransferOrder **list_transfers_by_status(int status, int *count) { return transfer_list(status, 0, count); }
TransferOrder **list_transfers_by_store(int store, int *count) { return transfer_list(-1, store, count); }
int load_transfers(void) { return sm_app_repository() ? refresh_transfers() : -1; }
int load_transfer_items(void) { return sm_app_repository() ? refresh_transfers() : -1; }
int save_transfer_order(TransferOrder *order) { (void)order; return sm_app_repository() ? 0 : -1; }
int save_all_transfers(void) { return sm_app_repository() ? 0 : -1; }
int save_transfer_item(TransferItem *item) { (void)item; return sm_app_repository() ? 0 : -1; }
