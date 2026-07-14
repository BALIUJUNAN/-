#include "sm_inventory_repository.h"

#include "repo/sm_inventory_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdlib.h>
#include <string.h>

static int sm_stock_log_valid(const sm_stock_log *log, int require_id) {
    return log && (!require_id || log->id != 0) &&
           memchr(log->product_id, '\0', sizeof(log->product_id)) &&
           log->product_id[0] != '\0' &&
           memchr(log->type, '\0', sizeof(log->type)) &&
           log->type[0] != '\0' &&
           memchr(log->remark, '\0', sizeof(log->remark)) &&
           log->quantity_milli > 0 && log->before_stock_milli >= 0 &&
           log->after_stock_milli >= 0 && log->created_at >= 0;
}

static sm_repo_status sm_stock_primary_key(uint64_t id,
                                           sm_key_builder *key) {
    sm_key_begin(key, SM_NS_STOCK_LOG_BY_ID);
    return sm_key_add_u64(key, id) == SM_KEY_OK ? SM_REPO_OK
                                                : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_stock_product_key(const sm_stock_log *log,
                                           sm_key_builder *key) {
    sm_key_begin(key, SM_NS_STOCK_LOG_BY_PRODUCT_TIME);
    return sm_key_add_string(key, log->product_id) == SM_KEY_OK &&
                   sm_key_add_i64(key, log->created_at) == SM_KEY_OK &&
                   sm_key_add_u64(key, log->id) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_stock_type_key(const sm_stock_log *log,
                                        sm_key_builder *key) {
    sm_key_begin(key, SM_NS_STOCK_LOG_BY_TYPE_TIME);
    return sm_key_add_string(key, log->type) == SM_KEY_OK &&
                   sm_key_add_i64(key, log->created_at) == SM_KEY_OK &&
                   sm_key_add_u64(key, log->id) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_stock_expect_missing(sm_repo_uow *uow,
                                              const sm_key_builder *key) {
    void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status = sm_repo_get(uow, sm_key_data(key),
                                        sm_key_size(key), &value, &value_len);
    if (status == SM_REPO_NOT_FOUND) return SM_REPO_OK;
    if (status == SM_REPO_OK) {
        sm_repo_value_free(value);
        return SM_REPO_CONFLICT;
    }
    return status;
}

static sm_repo_status sm_stock_insert(sm_repo_uow *uow,
                                      const sm_stock_log *log,
                                      int importing) {
    sm_key_builder primary, index;
    uint8_t *value = NULL;
    size_t value_len = 0;
    sm_codec_status codec;
    sm_repo_status status;
    if (!uow || !sm_stock_log_valid(log, 1)) return SM_REPO_ERR_INVALID;
    status = sm_stock_primary_key(log->id, &primary);
    if (status == SM_REPO_OK) status = sm_stock_expect_missing(uow, &primary);
    codec = status == SM_REPO_OK
                ? sm_stock_log_encode(log, &value, &value_len)
                : SM_CODEC_ERR_INVALID;
    if (status == SM_REPO_OK && codec != SM_CODEC_OK)
        status = codec == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM
                                             : SM_REPO_ERR_INVALID;
    if (status == SM_REPO_OK)
        status = sm_repo_put(uow, sm_key_data(&primary),
                             sm_key_size(&primary), value, value_len);
    free(value);
    if (status == SM_REPO_OK) status = sm_stock_product_key(log, &index);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&index),
                                   sm_key_size(&index));
    if (status == SM_REPO_OK) status = sm_stock_type_key(log, &index);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&index),
                                   sm_key_size(&index));
    if (status == SM_REPO_OK && importing)
        status = sm_repo_counter_ensure_at_least(
            uow, SM_COUNTER_STOCK_LOG, log->id + 1u);
    return status;
}

sm_repo_status sm_stock_log_create(sm_repo_uow *uow, sm_stock_log *log) {
    sm_repo_status status;
    if (!uow || !log || log->id != 0 || !sm_stock_log_valid(log, 0))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_counter_next(uow, SM_COUNTER_STOCK_LOG, &log->id);
    return status == SM_REPO_OK ? sm_stock_insert(uow, log, 0) : status;
}

sm_repo_status sm_stock_log_import(sm_repo_uow *uow,
                                   const sm_stock_log *log) {
    if (!uow || !log || log->id == UINT64_MAX)
        return SM_REPO_ERR_INVALID;
    return sm_stock_insert(uow, log, 1);
}

sm_repo_status sm_stock_log_get(sm_repo_uow *uow, uint64_t id,
                                sm_stock_log *out) {
    sm_key_builder key;
    void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status;
    if (!uow || !out || id == 0) return SM_REPO_ERR_INVALID;
    status = sm_stock_primary_key(id, &key);
    if (status == SM_REPO_OK)
        status = sm_repo_get(uow, sm_key_data(&key), sm_key_size(&key),
                             &value, &value_len);
    if (status == SM_REPO_OK &&
        sm_stock_log_decode(value, value_len, out) != SM_CODEC_OK)
        status = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    return status;
}

static sm_repo_status sm_stock_append(sm_stock_log **items, size_t *count,
                                      size_t *capacity,
                                      const sm_stock_log *value) {
    sm_stock_log *next;
    size_t next_capacity;
    if (*count == *capacity) {
        next_capacity = *capacity ? *capacity * 2u : 16u;
        if (next_capacity < *capacity ||
            next_capacity > SIZE_MAX / sizeof(**items))
            return SM_REPO_ERR_FULL;
        next = (sm_stock_log *)realloc(
            *items, next_capacity * sizeof(**items));
        if (!next) return SM_REPO_ERR_NOMEM;
        *items = next;
        *capacity = next_capacity;
    }
    (*items)[(*count)++] = *value;
    return SM_REPO_OK;
}

static sm_repo_status sm_stock_scan_id(sm_repo_scan *scan, uint8_t ns,
                                       uint64_t *out_id) {
    const void *key;
    size_t key_len;
    sm_key_reader reader;
    sm_repo_status status = sm_repo_scan_key(scan, &key, &key_len);
    if (status != SM_REPO_OK) return status;
    if (sm_key_reader_init(&reader, key, key_len, ns) != SM_KEY_OK)
        return SM_REPO_ERR_CORRUPT;
    if (ns != SM_NS_STOCK_LOG_BY_ID) {
        const char *ignored;
        size_t ignored_len;
        int64_t ignored_time;
        if (sm_key_read_string(&reader, &ignored, &ignored_len) != SM_KEY_OK ||
            sm_key_read_i64(&reader, &ignored_time) != SM_KEY_OK)
            return SM_REPO_ERR_CORRUPT;
    }
    return sm_key_read_u64(&reader, out_id) == SM_KEY_OK &&
                   sm_key_reader_done(&reader) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_CORRUPT;
}

sm_repo_status sm_stock_log_list(sm_repository *repo,
                                 const char *product_id,
                                 const char *type,
                                 int64_t start, int64_t end,
                                 sm_stock_log **out, size_t *out_count) {
    sm_key_builder prefix, primary;
    sm_repo_scan *scan = NULL;
    sm_stock_log *items = NULL;
    size_t count = 0, capacity = 0;
    uint8_t ns;
    sm_repo_status status, close_status;
    if (!repo || !out || !out_count || start < 0 || end < start)
        return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    ns = product_id ? SM_NS_STOCK_LOG_BY_PRODUCT_TIME
                    : type ? SM_NS_STOCK_LOG_BY_TYPE_TIME
                           : SM_NS_STOCK_LOG_BY_ID;
    sm_key_begin(&prefix, ns);
    if ((product_id && sm_key_add_string(&prefix, product_id) != SM_KEY_OK) ||
        (!product_id && type &&
         sm_key_add_string(&prefix, type) != SM_KEY_OK))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_scan_open_prefix(repo, ns, sm_key_data(&prefix),
                                      sm_key_size(&prefix), &scan);
    while (status == SM_REPO_OK &&
           (status = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        uint64_t id;
        void *value = NULL;
        size_t value_len = 0;
        sm_stock_log log;
        status = sm_stock_scan_id(scan, ns, &id);
        if (status == SM_REPO_OK) status = sm_stock_primary_key(id, &primary);
        if (status == SM_REPO_OK)
            status = ns == SM_NS_STOCK_LOG_BY_ID
                         ? sm_repo_scan_value(scan, (const void **)&value,
                                              &value_len)
                         : sm_repo_scan_get(scan, sm_key_data(&primary),
                                            sm_key_size(&primary), &value,
                                            &value_len);
        if (status == SM_REPO_OK &&
            sm_stock_log_decode(value, value_len, &log) != SM_CODEC_OK)
            status = SM_REPO_ERR_CORRUPT;
        if (ns != SM_NS_STOCK_LOG_BY_ID) sm_repo_value_free(value);
        if (status == SM_REPO_OK &&
            (!sm_stock_log_valid(&log, 1) || log.id != id))
            status = SM_REPO_ERR_CORRUPT;
        if (status == SM_REPO_OK && log.created_at >= start &&
            log.created_at <= end &&
            (!type || strcmp(log.type, type) == 0))
            status = sm_stock_append(&items, &count, &capacity, &log);
    }
    if (status == SM_REPO_ITER_END) status = SM_REPO_OK;
    close_status = sm_repo_scan_close(&scan);
    if (status == SM_REPO_OK) status = close_status;
    if (status != SM_REPO_OK) { free(items); return status; }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

void sm_inventory_array_free(void *array) { free(array); }
