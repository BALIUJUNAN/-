#include "sm_sales_service.h"

#include "app/sm_app_context.h"
#include "app/sm_finance_service.h"
#include "domain/sm_base_entities.h"
#include "domain/sm_inventory_entities.h"
#include "domain/sm_purchase_entities.h"
#include "domain/sm_sales_entities.h"
#include "repo/sm_base_repository.h"
#include "repo/sm_inventory_repository.h"
#include "repo/sm_purchase_repository.h"
#include "repo/sm_sales_repository.h"
#include "repo/sm_operations_repository.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int sm_money_to_cents(float value, int64_t *out) {
    double scaled;
    if (!out || !isfinite(value) || value < 0.0f) return 0;
    scaled = (double)value * 100.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_quantity_to_milli(float value, int64_t *out) {
    double scaled;
    if (!out || !isfinite(value) || value <= 0.0f) return 0;
    scaled = (double)value * 1000.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_sale_to_entity(const Sale *source, sm_sale *out) {
    if (!source || !out || source->id < 0 || source->cashier_id <= 0 ||
        source->member_id < 0 || source->points_used < 0 ||
        source->status < SALE_PENDING || source->status > SALE_REFUNDED)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)source->id;
    out->cashier_id = (uint64_t)source->cashier_id;
    out->member_id = (uint64_t)source->member_id;
    if (!sm_money_to_cents(source->total_amount, &out->total_cents) ||
        !sm_money_to_cents(source->discount, &out->discount_cents) ||
        !sm_money_to_cents(source->final_amount, &out->final_cents) ||
        !sm_money_to_cents(source->cash_received,
                           &out->cash_received_cents))
        return 0;
    out->points_used = source->points_used;
    out->status = (uint8_t)source->status;
    memcpy(out->payment_method, source->payment_method,
           sizeof(out->payment_method));
    out->created_at = (int64_t)source->created_at;
    out->completed_at = (int64_t)source->completed_at;
    return 1;
}

static int sm_sale_from_entity(const sm_sale *source, Sale *out) {
    if (!source || !out || source->id > INT_MAX ||
        source->cashier_id > INT_MAX || source->member_id > INT_MAX ||
        source->points_used > INT_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)source->id;
    out->cashier_id = (int)source->cashier_id;
    out->member_id = (int)source->member_id;
    out->total_amount = (float)((double)source->total_cents / 100.0);
    out->discount = (float)((double)source->discount_cents / 100.0);
    out->final_amount = (float)((double)source->final_cents / 100.0);
    out->cash_received =
        (float)((double)source->cash_received_cents / 100.0);
    out->points_used = (int)source->points_used;
    out->status = source->status;
    memcpy(out->payment_method, source->payment_method,
           sizeof(out->payment_method));
    out->created_at = (time_t)source->created_at;
    out->completed_at = (time_t)source->completed_at;
    return 1;
}

static int sm_item_to_entity(const SaleItem *source, sm_sale_item *out) {
    if (!source || !out || source->id < 0 || source->sale_id <= 0 ||
        source->combo_id < 0 || (source->is_combo != 0 && source->is_combo != 1))
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)source->id;
    out->sale_id = (uint64_t)source->sale_id;
    memcpy(out->product_id, source->product_id, sizeof(out->product_id));
    memcpy(out->product_name, source->product_name,
           sizeof(out->product_name));
    if (!sm_quantity_to_milli(source->quantity, &out->quantity_milli) ||
        !sm_money_to_cents(source->price, &out->price_cents) ||
        !sm_money_to_cents(source->original_price,
                           &out->original_price_cents) ||
        !sm_money_to_cents(source->subtotal, &out->subtotal_cents) ||
        !sm_money_to_cents(source->discount, &out->discount_cents))
        return 0;
    out->is_combo = (uint8_t)source->is_combo;
    out->combo_id = (uint64_t)source->combo_id;
    return 1;
}

static int sm_item_from_entity(const sm_sale_item *source, SaleItem *out) {
    if (!source || !out || source->id > INT_MAX || source->sale_id > INT_MAX ||
        source->combo_id > INT_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)source->id;
    out->sale_id = (int)source->sale_id;
    memcpy(out->product_id, source->product_id, sizeof(out->product_id));
    memcpy(out->product_name, source->product_name,
           sizeof(out->product_name));
    out->quantity = (float)((double)source->quantity_milli / 1000.0);
    out->price = (float)((double)source->price_cents / 100.0);
    out->original_price =
        (float)((double)source->original_price_cents / 100.0);
    out->subtotal = (float)((double)source->subtotal_cents / 100.0);
    out->discount = (float)((double)source->discount_cents / 100.0);
    out->is_combo = source->is_combo;
    out->combo_id = (int)source->combo_id;
    return 1;
}

static int sm_member_from_entity(const sm_member *source, Member *out) {
    if (!source || !out || source->id > INT_MAX || source->points > INT_MAX ||
        source->level > INT_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)source->id;
    memcpy(out->phone, source->phone, sizeof(out->phone));
    memcpy(out->name, source->name, sizeof(out->name));
    out->level = (int)source->level;
    out->points = (int)source->points;
    out->total_consume =
        (float)((double)source->total_consume_cents / 100.0);
    out->created_at = (time_t)source->created_at;
    out->updated_at = (time_t)source->updated_at;
    out->last_consume_at = (time_t)source->last_consume_at;
    return 1;
}

static sm_repo_status sm_finish(sm_repo_uow **uow, sm_repo_status status) {
    if (status == SM_REPO_OK) return sm_repo_uow_commit(uow);
    (void)sm_repo_uow_rollback(uow);
    return status;
}

static sm_repo_status sm_sale_audit(sm_repo_uow *uow, uint64_t ref,
                                    const char *operation,
                                    uint64_t operator_id, int64_t at) {
    sm_audit_record record;
    memset(&record, 0, sizeof(record));
    snprintf(record.type, sizeof(record.type), "%s", "SALE");
    snprintf(record.operation, sizeof(record.operation), "%s", operation);
    record.ref_id = ref; record.operator_id = operator_id; record.created_at = at;
    return sm_audit_record_create(uow, &record);
}

sm_repo_status sm_service_sale_create(Sale *sale) {
    sm_repository *repo = sm_app_repository();
    sm_repo_uow *uow = NULL;
    sm_sale entity;
    sm_repo_status status;
    if (!repo || !sale) return SM_REPO_ERR_INVALID;
    sale->id = 0;
    sale->status = SALE_PENDING;
    if (!sm_sale_to_entity(sale, &entity)) return SM_REPO_ERR_INVALID;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_sale_create(uow, &entity);
    if (status == SM_REPO_OK)
        status = sm_sale_audit(uow, entity.id, "CREATE", entity.cashier_id,
                               entity.created_at);
    status = sm_finish(&uow, status);
    if (status == SM_REPO_OK && !sm_sale_from_entity(&entity, sale))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_sale_update_pending(const Sale *sale) {
    sm_repository *repo = sm_app_repository();
    sm_repo_uow *uow = NULL;
    sm_sale current, updated;
    sm_repo_status status;
    if (!repo || !sale || !sm_sale_to_entity(sale, &updated) ||
        updated.id == 0 || updated.status != SALE_PENDING)
        return SM_REPO_ERR_INVALID;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_sale_get(uow, updated.id, &current);
    if (status == SM_REPO_OK && current.status != SALE_PENDING)
        status = SM_REPO_CONFLICT;
    if (status == SM_REPO_OK) {
        updated.cashier_id = current.cashier_id;
        updated.created_at = current.created_at;
        status = sm_sale_update(uow, &updated);
    }
    return sm_finish(&uow, status);
}

sm_repo_status sm_service_sale_get(int id, Sale *out) {
    sm_repository *repo = sm_app_repository();
    sm_repo_uow *uow = NULL;
    sm_sale entity;
    sm_repo_status status;
    if (!repo || id <= 0 || !out) return SM_REPO_ERR_INVALID;
    status = sm_repo_uow_begin(repo, 0, &uow);
    if (status == SM_REPO_OK) status = sm_sale_get(uow, (uint64_t)id, &entity);
    status = sm_finish(&uow, status);
    if (status == SM_REPO_OK && !sm_sale_from_entity(&entity, out))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_convert_sales(sm_sale *entities, size_t count,
                                       Sale **out, size_t *out_count) {
    Sale *items = NULL;
    size_t i;
    sm_repo_status status = SM_REPO_OK;
    if (count != 0) {
        if (count > SIZE_MAX / sizeof(*items)) status = SM_REPO_ERR_FULL;
        else if (!(items = (Sale *)calloc(count, sizeof(*items))))
            status = SM_REPO_ERR_NOMEM;
    }
    for (i = 0; status == SM_REPO_OK && i < count; ++i)
        if (!sm_sale_from_entity(&entities[i], &items[i]))
            status = SM_REPO_ERR_CORRUPT;
    sm_sales_array_free(entities);
    if (status != SM_REPO_OK) { free(items); return status; }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

static sm_repo_status sm_list_sales(int completed, Sale **out,
                                    size_t *out_count) {
    sm_repository *repo = sm_app_repository();
    sm_sale *entities = NULL;
    size_t count = 0;
    sm_repo_status status;
    if (!repo || !out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = completed ? sm_sale_list_completed(repo, &entities, &count)
                       : sm_sale_list_by_status(repo, SALE_PENDING,
                                                &entities, &count);
    return status == SM_REPO_OK
               ? sm_convert_sales(entities, count, out, out_count)
               : status;
}

sm_repo_status sm_service_sale_list_pending(Sale **out, size_t *out_count) {
    return sm_list_sales(0, out, out_count);
}

sm_repo_status sm_service_sale_list_completed(Sale **out, size_t *out_count) {
    return sm_list_sales(1, out, out_count);
}

sm_repo_status sm_service_sale_item_create(int sale_id, SaleItem *item) {
    sm_repository *repo = sm_app_repository();
    sm_repo_uow *uow = NULL;
    sm_sale_item entity;
    sm_repo_status status;
    if (!repo || sale_id <= 0 || !item) return SM_REPO_ERR_INVALID;
    item->id = 0;
    item->sale_id = sale_id;
    if (!sm_item_to_entity(item, &entity)) return SM_REPO_ERR_INVALID;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_sale_item_create(uow, &entity);
    status = sm_finish(&uow, status);
    if (status == SM_REPO_OK && !sm_item_from_entity(&entity, item))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_sale_item_list(int sale_id, SaleItem **out,
                                         size_t *out_count) {
    sm_repository *repo = sm_app_repository();
    sm_sale_item *entities = NULL;
    SaleItem *items = NULL;
    size_t count = 0, i;
    sm_repo_status status;
    if (!repo || sale_id <= 0 || !out || !out_count)
        return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_sale_item_list(repo, (uint64_t)sale_id, &entities, &count);
    if (status == SM_REPO_OK && count != 0) {
        if (count > SIZE_MAX / sizeof(*items)) status = SM_REPO_ERR_FULL;
        else if (!(items = (SaleItem *)calloc(count, sizeof(*items))))
            status = SM_REPO_ERR_NOMEM;
    }
    for (i = 0; status == SM_REPO_OK && i < count; ++i)
        if (!sm_item_from_entity(&entities[i], &items[i]))
            status = SM_REPO_ERR_CORRUPT;
    sm_sales_array_free(entities);
    if (status != SM_REPO_OK) { free(items); return status; }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

sm_repo_status sm_service_sale_cancel(int sale_id) {
    sm_repository *repo = sm_app_repository();
    sm_sale_item *items = NULL;
    sm_repo_uow *uow = NULL;
    sm_sale sale;
    size_t count = 0, i;
    sm_repo_status status;
    if (!repo || sale_id <= 0) return SM_REPO_ERR_INVALID;
    status = sm_sale_item_list(repo, (uint64_t)sale_id, &items, &count);
    if (status == SM_REPO_OK) status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_sale_get(uow, (uint64_t)sale_id, &sale);
    if (status == SM_REPO_OK && sale.status != SALE_PENDING)
        status = SM_REPO_CONFLICT;
    for (i = 0; status == SM_REPO_OK && i < count; ++i)
        status = sm_sale_item_delete(uow, sale.id, items[i].id);
    if (status == SM_REPO_OK) status = sm_sale_delete(uow, &sale);
    if (status == SM_REPO_OK)
        status = sm_sale_audit(uow, sale.id, "CANCEL", sale.cashier_id,
                               sale.created_at);
    sm_sales_array_free(items);
    return sm_finish(&uow, status);
}

static sm_repo_status sm_service_sale_complete_internal(
    const Sale *completed_sale, const sm_stock_adjustment *adjustments,
    size_t adjustment_count, int earned_points, const char *vip_card_no,
    Member *out_member) {
    sm_repository *repo = sm_app_repository();
    sm_repo_uow *uow = NULL;
    sm_sale current, updated;
    sm_member member;
    size_t i;
    int has_member = 0;
    sm_repo_status status;
    if (!repo || !completed_sale || completed_sale->id <= 0 ||
        completed_sale->status != SALE_COMPLETED || earned_points < 0 ||
        (adjustment_count != 0 && !adjustments))
        return SM_REPO_ERR_INVALID;
    if (!sm_sale_to_entity(completed_sale, &updated))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK)
        status = sm_sale_get(uow, updated.id, &current);
    if (status == SM_REPO_OK && current.status != SALE_PENDING)
        status = SM_REPO_CONFLICT;
    for (i = 0; status == SM_REPO_OK && i < adjustment_count; ++i) {
        sm_product product;
        sm_stock_log stock_log;
        int64_t before_stock;
        if (adjustments[i].product_id[0] == '\0' ||
            adjustments[i].quantity <= 0) {
            status = SM_REPO_ERR_INVALID;
            break;
        }
        status = sm_product_get(uow, adjustments[i].product_id, &product);
        if (status == SM_REPO_OK &&
            product.stock < adjustments[i].quantity)
            status = SM_REPO_CONFLICT;
        if (status == SM_REPO_OK) {
            before_stock = product.stock;
            product.stock -= adjustments[i].quantity;
            product.updated_at = updated.completed_at;
            status = sm_product_update(uow, &product);
        }
        if (status == SM_REPO_OK) {
            sm_batch *batches = NULL;
            size_t batch_count = 0, batch_index;
            int64_t batch_available = 0;
            int64_t batch_remaining =
                (int64_t)adjustments[i].quantity * 1000;
            status = sm_batch_list(repo, adjustments[i].product_id, 1,
                                   &batches, &batch_count);
            for (batch_index = 0;
                 status == SM_REPO_OK && batch_index < batch_count;
                 ++batch_index) {
                if (batches[batch_index].quantity_milli >
                    INT64_MAX - batch_available)
                    status = SM_REPO_ERR_FULL;
                else
                    batch_available += batches[batch_index].quantity_milli;
            }
            if (status == SM_REPO_OK && batch_count != 0 &&
                batch_available < batch_remaining)
                status = SM_REPO_CONFLICT;
            for (batch_index = 0;
                 status == SM_REPO_OK && batch_count != 0 &&
                 batch_index < batch_count && batch_remaining > 0;
                 ++batch_index) {
                int64_t take = batches[batch_index].quantity_milli <
                                       batch_remaining
                                   ? batches[batch_index].quantity_milli
                                   : batch_remaining;
                if (take == 0) continue;
                batches[batch_index].quantity_milli -= take;
                batch_remaining -= take;
                status = sm_batch_update(uow, &batches[batch_index]);
            }
            sm_purchase_array_free(batches);
        }
        if (status == SM_REPO_OK) {
            if (before_stock > INT64_MAX / 1000 ||
                product.stock > INT64_MAX / 1000) {
                status = SM_REPO_ERR_FULL;
                break;
            }
            memset(&stock_log, 0, sizeof(stock_log));
            snprintf(stock_log.product_id, sizeof(stock_log.product_id),
                     "%s", product.id);
            snprintf(stock_log.type, sizeof(stock_log.type), "出库");
            snprintf(stock_log.remark, sizeof(stock_log.remark),
                     "销售出库");
            stock_log.quantity_milli =
                (int64_t)adjustments[i].quantity * 1000;
            stock_log.before_stock_milli = before_stock * 1000;
            stock_log.after_stock_milli = product.stock * 1000;
            stock_log.operator_id = current.cashier_id;
            stock_log.created_at = updated.completed_at;
            status = sm_stock_log_create(uow, &stock_log);
        }
    }
    if (status == SM_REPO_OK && updated.member_id != 0) {
        status = sm_member_get(uow, updated.member_id, &member);
        has_member = status == SM_REPO_OK;
    }
    if (status == SM_REPO_OK && has_member) {
        if (member.points > INT64_MAX - earned_points ||
            member.total_consume_cents > INT64_MAX - updated.final_cents)
            status = SM_REPO_ERR_FULL;
        else {
            member.points += earned_points;
            member.total_consume_cents += updated.final_cents;
            member.last_consume_at = updated.completed_at;
            member.updated_at = updated.completed_at;
            if (member.total_consume_cents >= 2000000) member.level = 3;
            else if (member.total_consume_cents >= 500000) member.level = 2;
            else if (member.total_consume_cents >= 100000) member.level = 1;
            status = sm_member_update(uow, &member);
        }
    }
    if (status == SM_REPO_OK && vip_card_no && *vip_card_no) {
        sm_vip_transaction transaction;
        status = sm_vip_apply_uow(uow, vip_card_no, 1u,
                                  updated.final_cents, updated.id,
                                  current.cashier_id, "", "sale payment",
                                  updated.completed_at, &transaction);
    }
    if (status == SM_REPO_OK) {
        updated.cashier_id = current.cashier_id;
        updated.created_at = current.created_at;
        status = sm_sale_update(uow, &updated);
    }
    if (status == SM_REPO_OK)
        status = sm_sale_audit(uow, updated.id, "COMPLETE",
                               current.cashier_id, updated.completed_at);
    status = sm_finish(&uow, status);
    if (status == SM_REPO_OK && has_member && out_member &&
        !sm_member_from_entity(&member, out_member))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_sale_complete(
    const Sale *completed_sale, const sm_stock_adjustment *adjustments,
    size_t adjustment_count, int earned_points, Member *out_member) {
    return sm_service_sale_complete_internal(
        completed_sale, adjustments, adjustment_count, earned_points, NULL,
        out_member);
}

sm_repo_status sm_service_sale_complete_with_vip(
    const Sale *completed_sale, const sm_stock_adjustment *adjustments,
    size_t adjustment_count, int earned_points, const char *vip_card_no,
    Member *out_member) {
    if (!vip_card_no || !*vip_card_no) return SM_REPO_ERR_INVALID;
    return sm_service_sale_complete_internal(
        completed_sale, adjustments, adjustment_count, earned_points,
        vip_card_no, out_member);
}

void sm_service_sales_array_free(void *array) { free(array); }
