#include "sm_purchase_service.h"

#include "app/sm_app_context.h"
#include "app/sm_finance_service.h"
#include "domain/sm_inventory_entities.h"
#include "domain/sm_purchase_entities.h"
#include "repo/sm_base_repository.h"
#include "repo/sm_inventory_repository.h"
#include "repo/sm_purchase_repository.h"
#include "repo/sm_operations_repository.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sm_bounded(const char *value, size_t capacity, int required) {
    const char *end = value ? memchr(value, '\0', capacity) : NULL;
    return end && (!required || end != value);
}

static int sm_scaled(float value, int allow_zero, int64_t *out) {
    double scaled;
    if (!out || !isfinite(value) || value < 0.0f ||
        (!allow_zero && value == 0.0f)) return 0;
    scaled = (double)value * 1000.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_cents(float value, int64_t *out) {
    double scaled;
    if (!out || !isfinite(value) || value < 0.0f) return 0;
    scaled = (double)value * 100.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_purchase_to_entity(const Purchase *v, sm_purchase *out) {
    if (!v || !out || v->id < 0 || v->creator_id < 0 || v->approver_id < 0 ||
        v->status < 0 || v->status > 3 ||
        !sm_bounded(v->supplier_id, sizeof(v->supplier_id), 1) ||
        !sm_bounded(v->supplier_name, sizeof(v->supplier_name), 1)) return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)v->id;
    memcpy(out->supplier_id, v->supplier_id, sizeof(out->supplier_id));
    memcpy(out->supplier_name, v->supplier_name, sizeof(out->supplier_name));
    out->creator_id = (uint64_t)v->creator_id;
    out->approver_id = (uint64_t)v->approver_id;
    out->status = (uint8_t)v->status;
    if (!sm_cents(v->total_amount, &out->total_cents)) return 0;
    out->created_at = (int64_t)v->created_at;
    out->approved_at = (int64_t)v->approved_at;
    out->completed_at = (int64_t)v->completed_at;
    return out->created_at >= 0 && out->approved_at >= 0 && out->completed_at >= 0;
}

static int sm_purchase_from_entity(const sm_purchase *v, Purchase *out) {
    if (!v || !out || v->id > INT_MAX || v->creator_id > INT_MAX ||
        v->approver_id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)v->id;
    memcpy(out->supplier_id, v->supplier_id, sizeof(out->supplier_id));
    memcpy(out->supplier_name, v->supplier_name, sizeof(out->supplier_name));
    out->creator_id = (int)v->creator_id;
    out->approver_id = (int)v->approver_id;
    out->status = v->status;
    out->total_amount = (float)((double)v->total_cents / 100.0);
    out->created_at = (time_t)v->created_at;
    out->approved_at = (time_t)v->approved_at;
    out->completed_at = (time_t)v->completed_at;
    return 1;
}

static int sm_item_to_entity(const PurchaseItem *v, sm_purchase_item *out) {
    if (!v || !out || v->id < 0 || v->purchase_id <= 0 ||
        !sm_bounded(v->product_id, sizeof(v->product_id), 1) ||
        !sm_bounded(v->product_name, sizeof(v->product_name), 1)) return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)v->id; out->purchase_id = (uint64_t)v->purchase_id;
    memcpy(out->product_id, v->product_id, sizeof(out->product_id));
    memcpy(out->product_name, v->product_name, sizeof(out->product_name));
    return sm_scaled(v->quantity, 0, &out->quantity_milli) &&
           sm_cents(v->price, &out->price_cents) &&
           sm_scaled(v->received_qty, 1, &out->received_milli);
}

static int sm_item_from_entity(const sm_purchase_item *v, PurchaseItem *out) {
    if (!v || !out || v->id > INT_MAX || v->purchase_id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out)); out->id = (int)v->id;
    out->purchase_id = (int)v->purchase_id;
    memcpy(out->product_id, v->product_id, sizeof(out->product_id));
    memcpy(out->product_name, v->product_name, sizeof(out->product_name));
    out->quantity = (float)((double)v->quantity_milli / 1000.0);
    out->price = (float)((double)v->price_cents / 100.0);
    out->received_qty = (float)((double)v->received_milli / 1000.0);
    return 1;
}

static int sm_batch_to_entity(const Batch *v, sm_batch *out) {
    if (!v || !out || v->supplier_id < 0 ||
        !sm_bounded(v->batch_no, sizeof(v->batch_no), 0) ||
        !sm_bounded(v->product_id, sizeof(v->product_id), 1) ||
        !sm_bounded(v->product_name, sizeof(v->product_name), 1)) return 0;
    memset(out, 0, sizeof(*out));
    memcpy(out->batch_no, v->batch_no, sizeof(out->batch_no));
    memcpy(out->product_id, v->product_id, sizeof(out->product_id));
    memcpy(out->product_name, v->product_name, sizeof(out->product_name));
    snprintf(out->supplier_id, sizeof(out->supplier_id), "%d", v->supplier_id);
    if (!sm_scaled(v->quantity, 1, &out->quantity_milli) ||
        !sm_scaled(v->initial_quantity, 1, &out->initial_quantity_milli) ||
        !sm_cents(v->price, &out->price_cents)) return 0;
    out->production_date = (int64_t)v->production_date;
    out->expiry_date = (int64_t)v->expiry_date;
    out->received_date = (int64_t)v->received_date;
    out->created_at = (int64_t)v->created_at;
    return out->production_date >= 0 && out->expiry_date >= 0 &&
           out->received_date >= 0 && out->created_at >= 0;
}

static int sm_batch_from_entity(const sm_batch *v, Batch *out) {
    char *end; unsigned long supplier;
    if (!v || !out) return 0;
    memset(out, 0, sizeof(*out));
    memcpy(out->batch_no, v->batch_no, sizeof(out->batch_no));
    memcpy(out->product_id, v->product_id, sizeof(out->product_id));
    memcpy(out->product_name, v->product_name, sizeof(out->product_name));
    out->quantity = (float)((double)v->quantity_milli / 1000.0);
    out->initial_quantity = (float)((double)v->initial_quantity_milli / 1000.0);
    out->price = (float)((double)v->price_cents / 100.0);
    out->production_date = (time_t)v->production_date;
    out->expiry_date = (time_t)v->expiry_date;
    out->received_date = (time_t)v->received_date;
    out->created_at = (time_t)v->created_at;
    supplier = strtoul(v->supplier_id, &end, 10);
    out->supplier_id = *v->supplier_id && !*end && supplier <= INT_MAX
                           ? (int)supplier : 0;
    return 1;
}

static sm_repo_status sm_finish(sm_repo_uow **uow, sm_repo_status status) {
    if (status == SM_REPO_OK) return sm_repo_uow_commit(uow);
    (void)sm_repo_uow_rollback(uow); return status;
}

static sm_repo_status sm_purchase_audit(sm_repo_uow *uow, uint64_t ref,
                                        const char *operation,
                                        uint64_t operator_id, int64_t at) {
    sm_audit_record record;
    memset(&record, 0, sizeof(record));
    snprintf(record.type, sizeof(record.type), "%s", "PURCHASE");
    snprintf(record.operation, sizeof(record.operation), "%s", operation);
    record.ref_id = ref; record.operator_id = operator_id; record.created_at = at;
    return sm_audit_record_create(uow, &record);
}

sm_repo_status sm_service_purchase_create(Purchase *v) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_purchase entity; sm_repo_status s;
    if (!repo || !v || !sm_purchase_to_entity(v, &entity)) return SM_REPO_ERR_INVALID;
    entity.id = 0; entity.status = 0; entity.approver_id = 0;
    entity.approved_at = 0; entity.completed_at = 0; v->id = 0;
    s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_purchase_create(uow, &entity);
    if (s == SM_REPO_OK)
        s = sm_purchase_audit(uow, entity.id, "CREATE", entity.creator_id,
                              entity.created_at);
    s = sm_finish(&uow, s);
    if (s == SM_REPO_OK && !sm_purchase_from_entity(&entity, v)) return SM_REPO_ERR_CORRUPT;
    return s;
}

sm_repo_status sm_service_purchase_item_create(uint64_t purchase_id, PurchaseItem *v) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_purchase_item entity; sm_repo_status s;
    if (!repo || !v) return SM_REPO_ERR_INVALID;
    v->purchase_id = purchase_id <= INT_MAX ? (int)purchase_id : -1;
    v->id = 0; v->received_qty = 0;
    if (!sm_item_to_entity(v, &entity)) return SM_REPO_ERR_INVALID;
    entity.id = 0;
    s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_purchase_item_create(uow, &entity);
    s = sm_finish(&uow, s);
    if (s == SM_REPO_OK && !sm_item_from_entity(&entity, v)) return SM_REPO_ERR_CORRUPT;
    return s;
}

sm_repo_status sm_service_purchase_get(uint64_t id, Purchase *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_purchase entity; sm_repo_status s;
    if (!repo || !id || !out) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 0, &uow);
    if (s == SM_REPO_OK) s = sm_purchase_get(uow, id, &entity);
    s = sm_finish(&uow, s);
    return s == SM_REPO_OK && !sm_purchase_from_entity(&entity, out)
               ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_purchase_list(int status, Purchase **out, size_t *out_count) {
    sm_repository *repo = sm_app_repository(); sm_purchase *entities = NULL;
    Purchase *items = NULL; size_t count = 0, i; sm_repo_status s;
    if (!repo || !out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL; *out_count = 0; s = sm_purchase_list(repo, status, &entities, &count);
    if (s == SM_REPO_OK && count && !(items = calloc(count, sizeof(*items)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < count; ++i)
        if (!sm_purchase_from_entity(&entities[i], &items[i])) s = SM_REPO_ERR_CORRUPT;
    sm_purchase_array_free(entities);
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *out_count = count; return SM_REPO_OK;
}

sm_repo_status sm_service_purchase_item_list(uint64_t purchase_id,
                                             PurchaseItem **out, size_t *out_count) {
    sm_repository *repo = sm_app_repository(); sm_purchase_item *entities = NULL;
    PurchaseItem *items = NULL; size_t count = 0, i; sm_repo_status s;
    if (!repo || !out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL; *out_count = 0;
    s = sm_purchase_item_list(repo, purchase_id, &entities, &count);
    if (s == SM_REPO_OK && count && !(items = calloc(count, sizeof(*items)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < count; ++i)
        if (!sm_item_from_entity(&entities[i], &items[i])) s = SM_REPO_ERR_CORRUPT;
    sm_purchase_array_free(entities);
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *out_count = count; return SM_REPO_OK;
}

static sm_repo_status sm_total(const sm_purchase_item *items, size_t count,
                               int64_t *out) {
    size_t i; int64_t total = 0;
    for (i = 0; i < count; ++i) {
        int64_t whole = items[i].quantity_milli / 1000;
        int64_t rem = items[i].quantity_milli % 1000;
        int64_t part, tail;
        if (items[i].price_cents && whole > INT64_MAX / items[i].price_cents)
            return SM_REPO_ERR_FULL;
        part = whole * items[i].price_cents;
        if (items[i].price_cents && rem > (INT64_MAX - 500) / items[i].price_cents)
            return SM_REPO_ERR_FULL;
        tail = (rem * items[i].price_cents + 500) / 1000;
        if (total > INT64_MAX - part || total + part > INT64_MAX - tail)
            return SM_REPO_ERR_FULL;
        total += part + tail;
    }
    *out = total; return SM_REPO_OK;
}

static sm_repo_status sm_purchase_decide(uint64_t id, uint64_t approver,
                                         int64_t at, uint8_t status) {
    sm_repository *repo = sm_app_repository(); sm_purchase_item *items = NULL;
    sm_repo_uow *uow = NULL; sm_purchase order; size_t count = 0;
    sm_repo_status s;
    if (!repo || !id || !approver || at < 0 || (status != 1u && status != 3u))
        return SM_REPO_ERR_INVALID;
    s = sm_purchase_item_list(repo, id, &items, &count);
    if (s == SM_REPO_OK && status == 1u && count == 0) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_purchase_get(uow, id, &order);
    if (s == SM_REPO_OK && order.status != 0u) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK && at < order.created_at) s = SM_REPO_ERR_INVALID;
    if (s == SM_REPO_OK && status == 1u) s = sm_total(items, count, &order.total_cents);
    if (s == SM_REPO_OK) {
        order.status = status; order.approver_id = approver; order.approved_at = at;
        s = sm_purchase_update(uow, &order);
    }
    if (s == SM_REPO_OK)
        s = sm_purchase_audit(uow, order.id,
                              status == 1u ? "APPROVE" : "REJECT",
                              approver, at);
    sm_purchase_array_free(items); return sm_finish(&uow, s);
}

sm_repo_status sm_service_purchase_approve(uint64_t id, uint64_t approver, time_t at) {
    return sm_purchase_decide(id, approver, (int64_t)at, 1u);
}
sm_repo_status sm_service_purchase_reject(uint64_t id, uint64_t approver, time_t at) {
    return sm_purchase_decide(id, approver, (int64_t)at, 3u);
}

sm_repo_status sm_service_purchase_receive(uint64_t id, uint64_t operator_id,
                                           time_t completed_at) {
    sm_repository *repo = sm_app_repository(); sm_purchase_item *items = NULL;
    sm_repo_uow *uow = NULL; sm_purchase order; size_t count = 0, i;
    sm_repo_status s;
    if (!repo || !id || !operator_id || completed_at < 0) return SM_REPO_ERR_INVALID;
    s = sm_purchase_item_list(repo, id, &items, &count);
    if (s == SM_REPO_OK && count == 0) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_purchase_get(uow, id, &order);
    if (s == SM_REPO_OK && order.status != 1u) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK && (int64_t)completed_at < order.approved_at) s = SM_REPO_ERR_INVALID;
    for (i = 0; s == SM_REPO_OK && i < count; ++i) {
        sm_product product; sm_batch batch; sm_stock_log log; int64_t before;
        if (items[i].received_milli != 0 || items[i].quantity_milli % 1000 != 0) {
            s = SM_REPO_CONFLICT; break;
        }
        s = sm_product_get(uow, items[i].product_id, &product);
        if (s != SM_REPO_OK) break;
        before = product.stock;
        if (items[i].quantity_milli / 1000 > INT64_MAX - product.stock ||
            product.stock > INT64_MAX / 1000) { s = SM_REPO_ERR_FULL; break; }
        product.stock += items[i].quantity_milli / 1000;
        product.updated_at = (int64_t)completed_at;
        if (product.stock > INT64_MAX / 1000) { s = SM_REPO_ERR_FULL; break; }
        s = sm_product_update(uow, &product);
        if (s != SM_REPO_OK) break;
        items[i].received_milli = items[i].quantity_milli;
        s = sm_purchase_item_update(uow, &items[i]);
        if (s != SM_REPO_OK) break;
        memset(&batch, 0, sizeof(batch));
        snprintf(batch.product_id, sizeof(batch.product_id), "%s", items[i].product_id);
        snprintf(batch.product_name, sizeof(batch.product_name), "%s", items[i].product_name);
        snprintf(batch.supplier_id, sizeof(batch.supplier_id), "%s", order.supplier_id);
        batch.quantity_milli = batch.initial_quantity_milli = items[i].quantity_milli;
        batch.price_cents = items[i].price_cents;
        batch.production_date = batch.received_date = batch.created_at = (int64_t)completed_at;
        batch.source_purchase_id = order.id; batch.source_item_id = items[i].id;
        s = sm_batch_create(uow, &batch);
        if (s != SM_REPO_OK) break;
        memset(&log, 0, sizeof(log));
        snprintf(log.product_id, sizeof(log.product_id), "%s", items[i].product_id);
        snprintf(log.type, sizeof(log.type), "入库");
        snprintf(log.remark, sizeof(log.remark), "采购入库 #%llu / %s",
                 (unsigned long long)order.id, batch.batch_no);
        log.quantity_milli = items[i].quantity_milli;
        log.before_stock_milli = before * 1000;
        log.after_stock_milli = product.stock * 1000;
        log.operator_id = operator_id; log.created_at = (int64_t)completed_at;
        s = sm_stock_log_create(uow, &log);
    }
    if (s == SM_REPO_OK) {
        char *end = NULL;
        const char *supplier_text = order.supplier_id;
        unsigned long long supplier_id;
        if (*supplier_text == 'S' || *supplier_text == 's') ++supplier_text;
        supplier_id = strtoull(supplier_text, &end, 10);
        if (!*supplier_text || !end || *end || supplier_id == 0)
            s = SM_REPO_ERR_INVALID;
        else
            s = sm_finance_payable_for_purchase_uow(
                uow, (uint64_t)supplier_id, order.supplier_name, order.id,
                order.total_cents, (int64_t)completed_at);
    }
    if (s == SM_REPO_OK) {
        order.status = 2u; order.completed_at = (int64_t)completed_at;
        s = sm_purchase_update(uow, &order);
    }
    if (s == SM_REPO_OK)
        s = sm_purchase_audit(uow, order.id, "COMPLETE", operator_id,
                              (int64_t)completed_at);
    sm_purchase_array_free(items); return sm_finish(&uow, s);
}

sm_repo_status sm_service_batch_create(Batch *v) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_batch entity; sm_repo_status s;
    if (!repo || !v || !sm_batch_to_entity(v, &entity)) return SM_REPO_ERR_INVALID;
    entity.batch_no[0] = '\0'; v->batch_no[0] = '\0';
    s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_batch_create(uow, &entity);
    s = sm_finish(&uow, s);
    if (s == SM_REPO_OK && !sm_batch_from_entity(&entity, v)) return SM_REPO_ERR_CORRUPT;
    return s;
}

sm_repo_status sm_service_batch_update(const Batch *v) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_batch entity, existing; sm_repo_status s;
    if (!repo || !sm_batch_to_entity(v, &entity) || !entity.batch_no[0]) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_batch_get(uow, entity.batch_no, &existing);
    if (s == SM_REPO_OK) {
        if (strcmp(entity.product_id, existing.product_id) != 0)
            s = SM_REPO_CONFLICT;
        else {
            entity.source_purchase_id = existing.source_purchase_id;
            entity.source_item_id = existing.source_item_id;
            snprintf(entity.supplier_id, sizeof(entity.supplier_id), "%s",
                     existing.supplier_id);
            s = sm_batch_update(uow, &entity);
        }
    }
    return sm_finish(&uow, s);
}

sm_repo_status sm_service_batch_get(const char *no, Batch *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_batch entity; sm_repo_status s;
    if (!repo || !no || !out) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 0, &uow);
    if (s == SM_REPO_OK) s = sm_batch_get(uow, no, &entity);
    s = sm_finish(&uow, s);
    return s == SM_REPO_OK && !sm_batch_from_entity(&entity, out) ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_batch_list(const char *product_id, int fifo,
                                     Batch **out, size_t *out_count) {
    sm_repository *repo = sm_app_repository(); sm_batch *entities = NULL;
    Batch *items = NULL; size_t count = 0, i; sm_repo_status s;
    if (!repo || !out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL; *out_count = 0; s = sm_batch_list(repo, product_id, fifo, &entities, &count);
    if (s == SM_REPO_OK && count && !(items = calloc(count, sizeof(*items)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < count; ++i)
        if (!sm_batch_from_entity(&entities[i], &items[i])) s = SM_REPO_ERR_CORRUPT;
    sm_purchase_array_free(entities);
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *out_count = count; return SM_REPO_OK;
}

sm_repo_status sm_service_batch_deduct_fifo(const char *product_id, float quantity,
                                            uint64_t operator_id, time_t at,
                                            float *out_deducted) {
    sm_repository *repo = sm_app_repository(); sm_batch *batches = NULL;
    sm_repo_uow *uow = NULL; sm_product product; size_t count = 0, i;
    int64_t wanted, available = 0, remaining, before; sm_repo_status s;
    if (out_deducted) *out_deducted = 0;
    if (!repo || !product_id || !*product_id || !operator_id || at < 0 ||
        !sm_scaled(quantity, 0, &wanted) || wanted % 1000 != 0)
        return SM_REPO_ERR_INVALID;
    s = sm_batch_list(repo, product_id, 1, &batches, &count);
    for (i = 0; s == SM_REPO_OK && i < count; ++i) {
        if (batches[i].quantity_milli > INT64_MAX - available) s = SM_REPO_ERR_FULL;
        else available += batches[i].quantity_milli;
    }
    if (s == SM_REPO_OK && available < wanted) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_product_get(uow, product_id, &product);
    if (s == SM_REPO_OK && product.stock < wanted / 1000) s = SM_REPO_CONFLICT;
    remaining = wanted; before = s == SM_REPO_OK ? product.stock : 0;
    for (i = 0; s == SM_REPO_OK && i < count && remaining; ++i) {
        int64_t take = batches[i].quantity_milli < remaining ? batches[i].quantity_milli : remaining;
        sm_stock_log log;
        if (!take) continue;
        batches[i].quantity_milli -= take; remaining -= take;
        s = sm_batch_update(uow, &batches[i]);
        if (s != SM_REPO_OK) break;
        memset(&log, 0, sizeof(log));
        snprintf(log.product_id, sizeof(log.product_id), "%s", product_id);
        snprintf(log.type, sizeof(log.type), "批次出库");
        snprintf(log.remark, sizeof(log.remark), "%s", batches[i].batch_no);
        log.quantity_milli = take;
        log.before_stock_milli = before * 1000 - (wanted - remaining - take);
        log.after_stock_milli = before * 1000 - (wanted - remaining);
        log.operator_id = operator_id; log.created_at = (int64_t)at;
        s = sm_stock_log_create(uow, &log);
    }
    if (s == SM_REPO_OK) {
        product.stock -= wanted / 1000; product.updated_at = (int64_t)at;
        s = sm_product_update(uow, &product);
    }
    sm_purchase_array_free(batches); s = sm_finish(&uow, s);
    if (s == SM_REPO_OK && out_deducted) *out_deducted = quantity;
    return s;
}

void sm_service_purchase_array_free(void *array) { free(array); }
