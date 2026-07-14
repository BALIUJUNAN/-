#include "app/sm_operations_service.h"

#include "app/sm_app_context.h"
#include "repo/sm_operations_repository.h"

#include <stdio.h>
#include <string.h>

static sm_repo_status finish(sm_repo_uow **uow, sm_repo_status status) {
    return status == SM_REPO_OK ? sm_repo_uow_commit(uow)
                                : ((void)sm_repo_uow_rollback(uow), status);
}

static sm_repo_status begin(int write, sm_repo_uow **uow) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_repo_uow_begin(repo, write, uow) : SM_REPO_ERR_INVALID;
}

static sm_repo_status audit(sm_repo_uow *uow, const char *operation,
                            uint64_t ref, uint64_t operator_id, int64_t at,
                            const char *data) {
    sm_audit_record record;
    memset(&record, 0, sizeof(record));
    snprintf(record.type, sizeof(record.type), "%s", "TRANSFER");
    snprintf(record.operation, sizeof(record.operation), "%s", operation);
    snprintf(record.data, sizeof(record.data), "%s", data ? data : "");
    record.ref_id = ref; record.operator_id = operator_id; record.created_at = at;
    return sm_audit_record_create(uow, &record);
}

sm_repo_status sm_service_store_create(sm_store_record *value) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow);
    if (s == SM_REPO_OK) s = sm_store_record_create(uow, value);
    return finish(&uow, s);
}

sm_repo_status sm_service_store_get(uint64_t id, sm_store_record *out) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(0, &uow);
    if (s == SM_REPO_OK) s = sm_store_record_get(uow, id, out);
    return finish(&uow, s);
}

sm_repo_status sm_service_store_update(const sm_store_record *value) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow);
    if (s == SM_REPO_OK) s = sm_store_record_update(uow, value);
    return finish(&uow, s);
}

sm_repo_status sm_service_store_list(int status, sm_store_record **out,
                                     size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_store_record_list(repo, status, out, count) : SM_REPO_ERR_INVALID;
}

sm_repo_status sm_service_store_stock_get(uint64_t store, const char *product,
                                          sm_store_stock_record *out) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(0, &uow);
    if (s == SM_REPO_OK) s = sm_store_stock_get(uow, store, product, out);
    return finish(&uow, s);
}

sm_repo_status sm_service_store_stock_set(const sm_store_stock_record *value) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); sm_store_record store;
    if (s == SM_REPO_OK) s = sm_store_record_get(uow, value->store_id, &store);
    if (s == SM_REPO_OK) s = sm_store_stock_put(uow, value);
    return finish(&uow, s);
}

static sm_repo_status adjust(sm_repo_uow *uow, uint64_t store,
                             const char *product, int64_t delta,
                             int64_t min_stock, int64_t at) {
    sm_store_stock_record stock; sm_repo_status s;
    s = sm_store_stock_get(uow, store, product, &stock);
    if (s == SM_REPO_NOT_FOUND) {
        if (delta < 0) return SM_REPO_CONFLICT;
        memset(&stock, 0, sizeof(stock)); stock.store_id = store;
        snprintf(stock.product_id, sizeof(stock.product_id), "%s", product);
        stock.min_stock = min_stock >= 0 ? min_stock : 0; s = SM_REPO_OK;
    }
    if (s == SM_REPO_OK && (delta < 0 && stock.quantity < -delta))
        s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) {
        stock.quantity += delta;
        if (min_stock >= 0) stock.min_stock = min_stock;
        stock.updated_at = at; s = sm_store_stock_put(uow, &stock);
    }
    return s;
}

sm_repo_status sm_service_store_stock_adjust(uint64_t store, const char *product,
                                             int64_t delta, int64_t min_stock,
                                             int64_t at) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); sm_store_record record;
    if (s == SM_REPO_OK) s = sm_store_record_get(uow, store, &record);
    if (s == SM_REPO_OK && record.status != 1u) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = adjust(uow, store, product, delta, min_stock, at);
    return finish(&uow, s);
}

sm_repo_status sm_service_store_stock_list(uint64_t store, const char *product,
                                           sm_store_stock_record **out,
                                           size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_store_stock_list(repo, store, product, out, count) : SM_REPO_ERR_INVALID;
}

sm_repo_status sm_service_transfer_create(sm_transfer_order_record *value) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); sm_store_record from, to;
    if (s == SM_REPO_OK) s = sm_store_record_get(uow, value->from_store_id, &from);
    if (s == SM_REPO_OK) s = sm_store_record_get(uow, value->to_store_id, &to);
    if (s == SM_REPO_OK && (from.status != 1u || to.status != 1u)) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) {
        snprintf(value->from_store_name, sizeof(value->from_store_name), "%s", from.name);
        snprintf(value->to_store_name, sizeof(value->to_store_name), "%s", to.name);
        s = sm_transfer_order_create(uow, value);
    }
    if (s == SM_REPO_OK) s = audit(uow, "CREATE", value->id, value->creator_id,
                                   value->created_at, value->remark);
    return finish(&uow, s);
}

sm_repo_status sm_service_transfer_add_item(uint64_t transfer,
                                            sm_transfer_item_record *value) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); sm_transfer_order_record order;
    if (s == SM_REPO_OK) s = sm_transfer_order_get(uow, transfer, &order);
    if (s == SM_REPO_OK && (order.status != 0u || order.approver_id)) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) { value->transfer_id = transfer; s = sm_transfer_item_create(uow, value); }
    return finish(&uow, s);
}

sm_repo_status sm_service_transfer_get(uint64_t id,
                                       sm_transfer_order_record *out) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(0, &uow);
    if (s == SM_REPO_OK) s = sm_transfer_order_get(uow, id, out);
    return finish(&uow, s);
}

sm_repo_status sm_service_transfer_list(int status, uint64_t store,
                                        sm_transfer_order_record **out,
                                        size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_transfer_order_list(repo, status, store, out, count) : SM_REPO_ERR_INVALID;
}

sm_repo_status sm_service_transfer_item_list(uint64_t transfer,
                                             sm_transfer_item_record **out,
                                             size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_transfer_item_list(repo, transfer, out, count) : SM_REPO_ERR_INVALID;
}

static sm_repo_status order_transition(uint64_t id, uint8_t expected,
                                       uint8_t next, uint64_t operator_id,
                                       const char *name, const char *operation,
                                       const char *data, int64_t at) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow);
    sm_transfer_order_record order;
    if (s == SM_REPO_OK) s = sm_transfer_order_get(uow, id, &order);
    if (s == SM_REPO_OK && order.status != expected) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) {
        order.status = next;
        if (next == 3u) { order.approver_id = operator_id; order.approved_at = at; snprintf(order.approver_name, sizeof(order.approver_name), "%s", name ? name : ""); }
        else if (next == 4u) { }
        s = sm_transfer_order_update(uow, &order);
    }
    if (s == SM_REPO_OK) s = audit(uow, operation, id, operator_id, at, data);
    return finish(&uow, s);
}

sm_repo_status sm_service_transfer_approve(uint64_t id, uint64_t approver,
                                           const char *name, int64_t at) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow);
    sm_transfer_order_record order; sm_transfer_item_record *items = NULL; size_t count = 0;
    if (s == SM_REPO_OK) s = sm_transfer_order_get(uow, id, &order);
    if (s == SM_REPO_OK && (order.status != 0u || order.approver_id)) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_transfer_item_list(sm_app_repository(), id, &items, &count);
    if (s == SM_REPO_OK && count == 0) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) {
        order.approver_id = approver; order.approved_at = at;
        snprintf(order.approver_name, sizeof(order.approver_name), "%s", name ? name : "");
        s = sm_transfer_order_update(uow, &order);
    }
    if (s == SM_REPO_OK) s = audit(uow, "APPROVE", id, approver, at, "approved");
    sm_operations_array_free(items); return finish(&uow, s);
}

sm_repo_status sm_service_transfer_reject(uint64_t id, uint64_t approver,
                                          const char *name, const char *reason,
                                          int64_t at) {
    (void)name;
    return order_transition(id, 0u, 3u, approver, name, "REJECT", reason, at);
}

sm_repo_status sm_service_transfer_out(uint64_t id, uint64_t operator_id,
                                       const char *name, int64_t at) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); size_t i, count = 0;
    sm_transfer_order_record order; sm_transfer_item_record *items = NULL;
    if (s == SM_REPO_OK) s = sm_transfer_order_get(uow, id, &order);
    if (s == SM_REPO_OK && (order.status != 0u || !order.approver_id)) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_transfer_item_list(sm_app_repository(), id, &items, &count);
    if (s == SM_REPO_OK && count == 0) s = SM_REPO_CONFLICT;
    for (i = 0; s == SM_REPO_OK && i < count; ++i) {
        sm_store_stock_record stock;
        s = sm_store_stock_get(uow, order.from_store_id, items[i].product_id, &stock);
        if (s == SM_REPO_OK && stock.quantity < items[i].quantity) s = SM_REPO_CONFLICT;
    }
    if (s == SM_REPO_OK) {
        order.status = 1u; order.out_operator_id = operator_id; order.out_at = at;
        snprintf(order.out_operator_name, sizeof(order.out_operator_name), "%s", name ? name : "");
        s = sm_transfer_order_update(uow, &order);
    }
    if (s == SM_REPO_OK) s = audit(uow, "OUT", id, operator_id, at, "ready in transit");
    sm_operations_array_free(items); return finish(&uow, s);
}

sm_repo_status sm_service_transfer_in(uint64_t id, uint64_t operator_id,
                                      const char *name, int64_t at) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); size_t i, count = 0;
    sm_transfer_order_record order; sm_transfer_item_record *items = NULL;
    if (s == SM_REPO_OK) s = sm_transfer_order_get(uow, id, &order);
    if (s == SM_REPO_OK && order.status != 1u) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_transfer_item_list(sm_app_repository(), id, &items, &count);
    if (s == SM_REPO_OK && count == 0) s = SM_REPO_CONFLICT;
    for (i = 0; s == SM_REPO_OK && i < count; ++i)
        s = adjust(uow, order.from_store_id, items[i].product_id, -items[i].quantity, -1, at);
    for (i = 0; s == SM_REPO_OK && i < count; ++i)
        s = adjust(uow, order.to_store_id, items[i].product_id, items[i].quantity, -1, at);
    if (s == SM_REPO_OK) {
        order.status = 2u; order.in_operator_id = operator_id; order.in_at = at;
        snprintf(order.in_operator_name, sizeof(order.in_operator_name), "%s", name ? name : "");
        s = sm_transfer_order_update(uow, &order);
    }
    if (s == SM_REPO_OK) s = audit(uow, "IN", id, operator_id, at, "inventory moved atomically");
    sm_operations_array_free(items); return finish(&uow, s);
}

sm_repo_status sm_service_transfer_cancel(uint64_t id, uint64_t operator_id,
                                          int64_t at) {
    sm_transfer_order_record order; sm_repo_status s = sm_service_transfer_get(id, &order);
    if (s != SM_REPO_OK) return s;
    if (order.status != 0u) return SM_REPO_CONFLICT;
    return order_transition(id, 0u, 4u, operator_id, NULL, "CANCEL", "cancelled", at);
}
