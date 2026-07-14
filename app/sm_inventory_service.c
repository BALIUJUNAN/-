#include "sm_inventory_service.h"

#include "app/sm_app_context.h"
#include "domain/sm_inventory_entities.h"
#include "repo/sm_inventory_repository.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int sm_stock_scaled(float value, int allow_zero, int64_t *out) {
    double scaled;
    if (!out || !isfinite(value) || value < 0.0f ||
        (!allow_zero && value == 0.0f))
        return 0;
    scaled = (double)value * 1000.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_stock_to_entity(const StockLog *source, sm_stock_log *out) {
    if (!source || !out || source->id < 0 || source->operator_id < 0)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)source->id;
    memcpy(out->product_id, source->product_id, sizeof(out->product_id));
    memcpy(out->type, source->type, sizeof(out->type));
    if (!sm_stock_scaled(source->quantity, 0, &out->quantity_milli) ||
        !sm_stock_scaled(source->before_stock, 1,
                         &out->before_stock_milli) ||
        !sm_stock_scaled(source->after_stock, 1,
                         &out->after_stock_milli))
        return 0;
    out->operator_id = (uint64_t)source->operator_id;
    memcpy(out->remark, source->remark, sizeof(out->remark));
    out->created_at = (int64_t)source->created_at;
    return 1;
}

static int sm_stock_from_entity(const sm_stock_log *source, StockLog *out) {
    if (!source || !out || source->id > INT_MAX ||
        source->operator_id > INT_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)source->id;
    memcpy(out->product_id, source->product_id, sizeof(out->product_id));
    memcpy(out->type, source->type, sizeof(out->type));
    out->quantity = (float)((double)source->quantity_milli / 1000.0);
    out->before_stock =
        (float)((double)source->before_stock_milli / 1000.0);
    out->after_stock =
        (float)((double)source->after_stock_milli / 1000.0);
    out->operator_id = (int)source->operator_id;
    memcpy(out->remark, source->remark, sizeof(out->remark));
    out->created_at = (time_t)source->created_at;
    return 1;
}

sm_repo_status sm_service_stock_log_create(StockLog *log) {
    sm_repository *repo = sm_app_repository();
    sm_repo_uow *uow = NULL;
    sm_stock_log entity;
    sm_repo_status status;
    if (!repo || !log || !sm_stock_to_entity(log, &entity))
        return SM_REPO_ERR_INVALID;
    log->id = 0;
    entity.id = 0;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_stock_log_create(uow, &entity);
    if (status == SM_REPO_OK) status = sm_repo_uow_commit(&uow);
    else (void)sm_repo_uow_rollback(&uow);
    if (status == SM_REPO_OK && !sm_stock_from_entity(&entity, log))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_stock_log_list(const char *product_id,
                                         const char *type,
                                         time_t start, time_t end,
                                         StockLog **out,
                                         size_t *out_count) {
    sm_repository *repo = sm_app_repository();
    sm_stock_log *entities = NULL;
    StockLog *items = NULL;
    size_t count = 0, i;
    sm_repo_status status;
    if (!repo || !out || !out_count || start < 0 || end < start)
        return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_stock_log_list(repo, product_id, type,
                               (int64_t)start, (int64_t)end,
                               &entities, &count);
    if (status == SM_REPO_OK && count != 0) {
        if (count > SIZE_MAX / sizeof(*items)) status = SM_REPO_ERR_FULL;
        else if (!(items = (StockLog *)calloc(count, sizeof(*items))))
            status = SM_REPO_ERR_NOMEM;
    }
    for (i = 0; status == SM_REPO_OK && i < count; ++i)
        if (!sm_stock_from_entity(&entities[i], &items[i]))
            status = SM_REPO_ERR_CORRUPT;
    sm_inventory_array_free(entities);
    if (status != SM_REPO_OK) { free(items); return status; }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

void sm_service_inventory_array_free(void *array) { free(array); }
