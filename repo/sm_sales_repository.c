#include "sm_sales_repository.h"

#include "repo/sm_sales_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdlib.h>
#include <string.h>

typedef sm_codec_status (*sm_sales_encode_fn)(const void *, uint8_t **,
                                              size_t *);
typedef sm_codec_status (*sm_sales_decode_fn)(const void *, size_t, void *);

static int sm_sales_text(const char *value, size_t capacity, int required) {
    const char *end;
    if (!value) return 0;
    end = (const char *)memchr(value, '\0', capacity);
    return end && (!required || end != value);
}

static int sm_sale_valid(const sm_sale *sale, int require_id) {
    return sale && (!require_id || sale->id != 0) && sale->cashier_id != 0 &&
           sale->member_id <= INT64_MAX && sale->total_cents >= 0 &&
           sale->discount_cents >= 0 && sale->final_cents >= 0 &&
           sale->cash_received_cents >= 0 && sale->points_used >= 0 &&
           sale->status <= 2u &&
           sm_sales_text(sale->payment_method,
                         sizeof(sale->payment_method), 0) &&
           sale->created_at >= 0 && sale->completed_at >= 0 &&
           (sale->status == 0u || sale->completed_at >= sale->created_at);
}

static int sm_sale_item_valid(const sm_sale_item *item, int require_id) {
    return item && (!require_id || item->id != 0) && item->sale_id != 0 &&
           sm_sales_text(item->product_id, sizeof(item->product_id), 1) &&
           sm_sales_text(item->product_name, sizeof(item->product_name), 1) &&
           item->quantity_milli > 0 && item->price_cents >= 0 &&
           item->original_price_cents >= 0 && item->subtotal_cents >= 0 &&
           item->discount_cents >= 0 && item->is_combo <= 1u &&
           (!item->is_combo || item->combo_id != 0);
}

static sm_repo_status sm_sales_codec_encode(sm_codec_status status) {
    if (status == SM_CODEC_OK) return SM_REPO_OK;
    if (status == SM_CODEC_ERR_NOMEM) return SM_REPO_ERR_NOMEM;
    if (status == SM_CODEC_ERR_TOO_LARGE) return SM_REPO_ERR_FULL;
    return SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_sales_key_u64(uint8_t ns, uint64_t id,
                                       sm_key_builder *key) {
    sm_key_begin(key, ns);
    return sm_key_add_u64(key, id) == SM_KEY_OK ? SM_REPO_OK
                                                : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_sales_status_key(uint8_t status, uint64_t id,
                                          sm_key_builder *key) {
    sm_key_begin(key, SM_NS_SALE_BY_STATUS);
    return sm_key_add_u8(key, status) == SM_KEY_OK &&
                   sm_key_add_u64(key, id) == SM_KEY_OK
               ? SM_REPO_OK
               : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_sales_item_key(uint64_t sale_id, uint64_t item_id,
                                        sm_key_builder *key) {
    sm_key_begin(key, SM_NS_SALE_ITEM_BY_SALE);
    return sm_key_add_u64(key, sale_id) == SM_KEY_OK &&
                   sm_key_add_u64(key, item_id) == SM_KEY_OK
               ? SM_REPO_OK
               : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_sales_time_key(const sm_sale *sale,
                                        sm_key_builder *key) {
    sm_key_begin(key, SM_NS_SALE_BY_TIME);
    return sm_key_add_i64(key, sale->completed_at) == SM_KEY_OK &&
                   sm_key_add_u64(key, sale->id) == SM_KEY_OK
               ? SM_REPO_OK
               : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_sales_cashier_key(const sm_sale *sale,
                                           sm_key_builder *key) {
    sm_key_begin(key, SM_NS_SALE_BY_CASHIER_TIME);
    return sm_key_add_u64(key, sale->cashier_id) == SM_KEY_OK &&
                   sm_key_add_i64(key, sale->completed_at) == SM_KEY_OK &&
                   sm_key_add_u64(key, sale->id) == SM_KEY_OK
               ? SM_REPO_OK
               : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_sales_member_key(const sm_sale *sale,
                                          sm_key_builder *key) {
    sm_key_begin(key, SM_NS_SALE_BY_MEMBER_TIME);
    return sm_key_add_u64(key, sale->member_id) == SM_KEY_OK &&
                   sm_key_add_i64(key, sale->completed_at) == SM_KEY_OK &&
                   sm_key_add_u64(key, sale->id) == SM_KEY_OK
               ? SM_REPO_OK
               : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_sales_expect_missing(sm_repo_uow *uow,
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

static sm_repo_status sm_sales_put(sm_repo_uow *uow,
                                   const sm_key_builder *key,
                                   const void *entity,
                                   sm_sales_encode_fn encode) {
    uint8_t *value = NULL;
    size_t value_len = 0;
    sm_repo_status status = sm_sales_codec_encode(
        encode(entity, &value, &value_len));
    if (status == SM_REPO_OK)
        status = sm_repo_put(uow, sm_key_data(key), sm_key_size(key),
                             value, value_len);
    free(value);
    return status;
}

static sm_repo_status sm_sales_get(sm_repo_uow *uow,
                                   const sm_key_builder *key, void *out,
                                   sm_sales_decode_fn decode) {
    void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status = sm_repo_get(uow, sm_key_data(key),
                                        sm_key_size(key), &value, &value_len);
    if (status != SM_REPO_OK) return status;
    status = decode(value, value_len, out) == SM_CODEC_OK
                 ? SM_REPO_OK
                 : SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    return status;
}

static sm_codec_status sm_sale_encode_any(const void *entity, uint8_t **out,
                                          size_t *out_len) {
    return sm_sale_encode((const sm_sale *)entity, out, out_len);
}

static sm_codec_status sm_item_encode_any(const void *entity, uint8_t **out,
                                          size_t *out_len) {
    return sm_sale_item_encode((const sm_sale_item *)entity, out, out_len);
}

static sm_codec_status sm_sale_decode_any(const void *data, size_t len,
                                          void *out) {
    return sm_sale_decode(data, len, (sm_sale *)out);
}

static sm_repo_status sm_sales_index_add(sm_repo_uow *uow,
                                         const sm_sale *sale) {
    sm_key_builder key;
    sm_repo_status status = sm_sales_status_key(sale->status, sale->id, &key);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key));
    if (status != SM_REPO_OK || sale->status == 0u) return status;
    status = sm_sales_time_key(sale, &key);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key));
    if (status == SM_REPO_OK) status = sm_sales_cashier_key(sale, &key);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key));
    if (status == SM_REPO_OK && sale->member_id != 0)
        status = sm_sales_member_key(sale, &key);
    if (status == SM_REPO_OK && sale->member_id != 0)
        status = sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key));
    return status;
}

static sm_repo_status sm_sales_index_remove(sm_repo_uow *uow,
                                            const sm_sale *sale) {
    sm_key_builder key;
    sm_repo_status status = sm_sales_status_key(sale->status, sale->id, &key);
    if (status == SM_REPO_OK)
        status = sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    if (status != SM_REPO_OK || sale->status == 0u) return status;
    status = sm_sales_time_key(sale, &key);
    if (status == SM_REPO_OK)
        status = sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    if (status == SM_REPO_OK) status = sm_sales_cashier_key(sale, &key);
    if (status == SM_REPO_OK)
        status = sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    if (status == SM_REPO_OK && sale->member_id != 0)
        status = sm_sales_member_key(sale, &key);
    if (status == SM_REPO_OK && sale->member_id != 0)
        status = sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    return status;
}

static sm_repo_status sm_sale_insert(sm_repo_uow *uow, const sm_sale *sale,
                                     int import) {
    sm_key_builder primary;
    sm_repo_status status;
    if (!sm_sale_valid(sale, 1)) return SM_REPO_ERR_INVALID;
    status = sm_sales_key_u64(SM_NS_SALE_BY_ID, sale->id, &primary);
    if (status == SM_REPO_OK) status = sm_sales_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_sales_put(uow, &primary, sale, sm_sale_encode_any);
    if (status == SM_REPO_OK) status = sm_sales_index_add(uow, sale);
    if (status == SM_REPO_OK && import)
        status = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_SALE,
                                                 sale->id + 1u);
    return status;
}

sm_repo_status sm_sale_create(sm_repo_uow *uow, sm_sale *sale) {
    sm_repo_status status;
    if (!uow || !sale || sale->id != 0 || !sm_sale_valid(sale, 0))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_counter_next(uow, SM_COUNTER_SALE, &sale->id);
    return status == SM_REPO_OK ? sm_sale_insert(uow, sale, 0) : status;
}

sm_repo_status sm_sale_import(sm_repo_uow *uow, const sm_sale *sale) {
    if (!uow || !sale || sale->id == UINT64_MAX) return SM_REPO_ERR_INVALID;
    return sm_sale_insert(uow, sale, 1);
}

sm_repo_status sm_sale_get(sm_repo_uow *uow, uint64_t id, sm_sale *out) {
    sm_key_builder key;
    if (!uow || !out || id == 0) return SM_REPO_ERR_INVALID;
    if (sm_sales_key_u64(SM_NS_SALE_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return sm_sales_get(uow, &key, out, sm_sale_decode_any);
}

sm_repo_status sm_sale_update(sm_repo_uow *uow, const sm_sale *sale) {
    sm_key_builder primary;
    sm_sale old;
    sm_repo_status status;
    if (!uow || !sm_sale_valid(sale, 1)) return SM_REPO_ERR_INVALID;
    status = sm_sales_key_u64(SM_NS_SALE_BY_ID, sale->id, &primary);
    if (status == SM_REPO_OK)
        status = sm_sales_get(uow, &primary, &old, sm_sale_decode_any);
    if (status == SM_REPO_OK) status = sm_sales_index_remove(uow, &old);
    if (status == SM_REPO_OK)
        status = sm_sales_put(uow, &primary, sale, sm_sale_encode_any);
    if (status == SM_REPO_OK) status = sm_sales_index_add(uow, sale);
    return status;
}

sm_repo_status sm_sale_delete(sm_repo_uow *uow, const sm_sale *sale) {
    sm_key_builder primary;
    sm_repo_status status;
    if (!uow || !sm_sale_valid(sale, 1)) return SM_REPO_ERR_INVALID;
    status = sm_sales_index_remove(uow, sale);
    if (status == SM_REPO_OK)
        status = sm_sales_key_u64(SM_NS_SALE_BY_ID, sale->id, &primary);
    if (status == SM_REPO_OK)
        status = sm_repo_delete(uow, sm_key_data(&primary),
                                sm_key_size(&primary));
    return status;
}

static sm_repo_status sm_item_insert(sm_repo_uow *uow,
                                     const sm_sale_item *item, int import) {
    sm_key_builder key;
    sm_repo_status status;
    if (!sm_sale_item_valid(item, 1)) return SM_REPO_ERR_INVALID;
    status = sm_sales_item_key(item->sale_id, item->id, &key);
    if (status == SM_REPO_OK) status = sm_sales_expect_missing(uow, &key);
    if (status == SM_REPO_OK)
        status = sm_sales_put(uow, &key, item, sm_item_encode_any);
    if (status == SM_REPO_OK && import)
        status = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_SALE_ITEM,
                                                 item->id + 1u);
    return status;
}

sm_repo_status sm_sale_item_create(sm_repo_uow *uow, sm_sale_item *item) {
    sm_sale sale;
    sm_repo_status status;
    if (!uow || !item || item->id != 0 || !sm_sale_item_valid(item, 0))
        return SM_REPO_ERR_INVALID;
    status = sm_sale_get(uow, item->sale_id, &sale);
    if (status == SM_REPO_OK && sale.status != 0u) status = SM_REPO_CONFLICT;
    if (status == SM_REPO_OK)
        status = sm_repo_counter_next(uow, SM_COUNTER_SALE_ITEM, &item->id);
    return status == SM_REPO_OK ? sm_item_insert(uow, item, 0) : status;
}

sm_repo_status sm_sale_item_import(sm_repo_uow *uow,
                                   const sm_sale_item *item) {
    sm_sale sale;
    sm_repo_status status;
    if (!uow || !item || item->id == UINT64_MAX) return SM_REPO_ERR_INVALID;
    status = sm_sale_get(uow, item->sale_id, &sale);
    return status == SM_REPO_OK ? sm_item_insert(uow, item, 1) : status;
}

sm_repo_status sm_sale_item_get(sm_repo_uow *uow, uint64_t sale_id,
                                uint64_t item_id, sm_sale_item *out) {
    sm_key_builder key;
    if (!uow || !out || sale_id == 0 || item_id == 0)
        return SM_REPO_ERR_INVALID;
    if (sm_sales_item_key(sale_id, item_id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return sm_sales_get(uow, &key, out,
                        (sm_sales_decode_fn)sm_sale_item_decode);
}

sm_repo_status sm_sale_item_delete(sm_repo_uow *uow, uint64_t sale_id,
                                   uint64_t item_id) {
    sm_key_builder key;
    if (!uow || sale_id == 0 || item_id == 0) return SM_REPO_ERR_INVALID;
    if (sm_sales_item_key(sale_id, item_id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return sm_repo_delete(uow, sm_key_data(&key), sm_key_size(&key));
}

static sm_repo_status sm_sales_append(void **items, size_t *count,
                                      size_t *capacity, size_t element_size,
                                      const void *value) {
    void *next;
    size_t next_capacity;
    if (*count == *capacity) {
        next_capacity = *capacity == 0 ? 8u : *capacity * 2u;
        if (next_capacity < *capacity ||
            next_capacity > SIZE_MAX / element_size)
            return SM_REPO_ERR_FULL;
        next = realloc(*items, next_capacity * element_size);
        if (!next) return SM_REPO_ERR_NOMEM;
        *items = next;
        *capacity = next_capacity;
    }
    memcpy((uint8_t *)*items + *count * element_size, value, element_size);
    ++*count;
    return SM_REPO_OK;
}

static sm_repo_status sm_sales_decode_scan_value(sm_repo_scan *scan,
                                                 void *out,
                                                 sm_sales_decode_fn decode) {
    const void *value;
    size_t value_len;
    sm_repo_status status = sm_repo_scan_value(scan, &value, &value_len);
    if (status != SM_REPO_OK) return status;
    return decode(value, value_len, out) == SM_CODEC_OK
               ? SM_REPO_OK
               : SM_REPO_ERR_CORRUPT;
}

sm_repo_status sm_sale_item_list(sm_repository *repo, uint64_t sale_id,
                                 sm_sale_item **out, size_t *out_count) {
    sm_key_builder prefix;
    sm_repo_scan *scan = NULL;
    sm_sale_item *items = NULL;
    size_t count = 0, capacity = 0;
    sm_repo_status status, close_status;
    if (!repo || !out || !out_count || sale_id == 0)
        return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    sm_key_begin(&prefix, SM_NS_SALE_ITEM_BY_SALE);
    if (sm_key_add_u64(&prefix, sale_id) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    status = sm_repo_scan_open_prefix(repo, SM_NS_SALE_ITEM_BY_SALE,
                                      sm_key_data(&prefix),
                                      sm_key_size(&prefix), &scan);
    while (status == SM_REPO_OK &&
           (status = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        sm_sale_item item;
        status = sm_sales_decode_scan_value(scan, &item,
                                            (sm_sales_decode_fn)sm_sale_item_decode);
        if (status == SM_REPO_OK &&
            (!sm_sale_item_valid(&item, 1) || item.sale_id != sale_id))
            status = SM_REPO_ERR_CORRUPT;
        if (status == SM_REPO_OK)
            status = sm_sales_append((void **)&items, &count, &capacity,
                                     sizeof(item), &item);
    }
    if (status == SM_REPO_ITER_END) status = SM_REPO_OK;
    close_status = sm_repo_scan_close(&scan);
    if (status == SM_REPO_OK) status = close_status;
    if (status != SM_REPO_OK) {
        free(items);
        return status;
    }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

static sm_repo_status sm_sale_list_index(sm_repository *repo,
                                         uint8_t namespace_kind,
                                         const sm_key_builder *prefix,
                                         sm_sale **out, size_t *out_count) {
    sm_repo_scan *scan = NULL;
    sm_sale *items = NULL;
    size_t count = 0, capacity = 0;
    sm_repo_status status, close_status;
    if (!repo || !prefix || !out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_repo_scan_open_prefix(repo, namespace_kind,
                                      sm_key_data(prefix),
                                      sm_key_size(prefix), &scan);
    while (status == SM_REPO_OK &&
           (status = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *key;
        size_t key_len;
        sm_key_reader reader;
        uint64_t id;
        sm_key_builder primary;
        void *value = NULL;
        size_t value_len = 0;
        sm_sale sale;
        status = sm_repo_scan_key(scan, &key, &key_len);
        if (status != SM_REPO_OK) break;
        if (sm_key_reader_init(&reader, key, key_len, namespace_kind) != SM_KEY_OK)
            status = SM_REPO_ERR_CORRUPT;
        if (status == SM_REPO_OK && namespace_kind == SM_NS_SALE_BY_STATUS) {
            uint8_t ignored;
            if (sm_key_read_u8(&reader, &ignored) != SM_KEY_OK)
                status = SM_REPO_ERR_CORRUPT;
        } else if (status == SM_REPO_OK) {
            int64_t ignored_time;
            if (sm_key_read_i64(&reader, &ignored_time) != SM_KEY_OK)
                status = SM_REPO_ERR_CORRUPT;
        }
        if (status == SM_REPO_OK &&
            (sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
             sm_key_reader_done(&reader) != SM_KEY_OK))
            status = SM_REPO_ERR_CORRUPT;
        if (status == SM_REPO_OK)
            status = sm_sales_key_u64(SM_NS_SALE_BY_ID, id, &primary);
        if (status == SM_REPO_OK)
            status = sm_repo_scan_get(scan, sm_key_data(&primary),
                                      sm_key_size(&primary), &value,
                                      &value_len);
        if (status == SM_REPO_OK &&
            sm_sale_decode(value, value_len, &sale) != SM_CODEC_OK)
            status = SM_REPO_ERR_CORRUPT;
        sm_repo_value_free(value);
        if (status == SM_REPO_OK &&
            (!sm_sale_valid(&sale, 1) || sale.id != id))
            status = SM_REPO_ERR_CORRUPT;
        if (status == SM_REPO_OK)
            status = sm_sales_append((void **)&items, &count, &capacity,
                                     sizeof(sale), &sale);
    }
    if (status == SM_REPO_ITER_END) status = SM_REPO_OK;
    close_status = sm_repo_scan_close(&scan);
    if (status == SM_REPO_OK) status = close_status;
    if (status != SM_REPO_OK) {
        free(items);
        return status;
    }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

sm_repo_status sm_sale_list_by_status(sm_repository *repo, uint8_t status,
                                      sm_sale **out, size_t *out_count) {
    sm_key_builder prefix;
    if (status > 2u) return SM_REPO_ERR_INVALID;
    sm_key_begin(&prefix, SM_NS_SALE_BY_STATUS);
    if (sm_key_add_u8(&prefix, status) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return sm_sale_list_index(repo, SM_NS_SALE_BY_STATUS, &prefix,
                              out, out_count);
}

sm_repo_status sm_sale_list_completed(sm_repository *repo,
                                      sm_sale **out, size_t *out_count) {
    return sm_sale_list_by_status(repo, 1u, out, out_count);
}

void sm_sales_array_free(void *array) { free(array); }
