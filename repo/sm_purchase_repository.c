#include "sm_purchase_repository.h"

#include "repo/sm_purchase_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef sm_codec_status (*sm_encode_fn)(const void *, uint8_t **, size_t *);
typedef sm_codec_status (*sm_decode_fn)(const void *, size_t, void *);

static int sm_text_ok(const char *value, size_t capacity, int required) {
    const char *end = value ? memchr(value, '\0', capacity) : NULL;
    return end && (!required || end != value);
}

static int sm_purchase_valid(const sm_purchase *v, int require_id) {
    return v && (!require_id || v->id) &&
           sm_text_ok(v->supplier_id, sizeof(v->supplier_id), 1) &&
           sm_text_ok(v->supplier_name, sizeof(v->supplier_name), 1) &&
           v->creator_id && v->status <= 3u && v->total_cents >= 0 &&
           v->created_at >= 0 && v->approved_at >= 0 && v->completed_at >= 0 &&
           (v->status == 0u || (v->approver_id && v->approved_at >= v->created_at)) &&
           (v->status != 2u || v->completed_at >= v->approved_at);
}

static int sm_item_valid(const sm_purchase_item *v, int require_id) {
    return v && (!require_id || v->id) && v->purchase_id &&
           sm_text_ok(v->product_id, sizeof(v->product_id), 1) &&
           sm_text_ok(v->product_name, sizeof(v->product_name), 1) &&
           v->quantity_milli > 0 && v->price_cents >= 0 &&
           v->received_milli >= 0 && v->received_milli <= v->quantity_milli;
}

static int sm_batch_valid(const sm_batch *v) {
    return v && sm_text_ok(v->batch_no, sizeof(v->batch_no), 1) &&
           sm_text_ok(v->product_id, sizeof(v->product_id), 1) &&
           sm_text_ok(v->product_name, sizeof(v->product_name), 1) &&
           sm_text_ok(v->supplier_id, sizeof(v->supplier_id), 0) &&
           v->quantity_milli >= 0 &&
           v->initial_quantity_milli >= v->quantity_milli &&
           v->price_cents >= 0 && v->production_date >= 0 &&
           v->expiry_date >= 0 && v->received_date >= 0 &&
           v->created_at >= 0 &&
           (!v->expiry_date || v->expiry_date >= v->production_date);
}

static sm_repo_status sm_codec(sm_codec_status s) {
    if (s == SM_CODEC_OK) return SM_REPO_OK;
    if (s == SM_CODEC_ERR_NOMEM) return SM_REPO_ERR_NOMEM;
    if (s == SM_CODEC_ERR_TOO_LARGE) return SM_REPO_ERR_FULL;
    return SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_key_u64(uint8_t ns, uint64_t id,
                                 sm_key_builder *key) {
    sm_key_begin(key, ns);
    return sm_key_add_u64(key, id) == SM_KEY_OK ? SM_REPO_OK
                                                : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_key_text(uint8_t ns, const char *text,
                                  sm_key_builder *key) {
    sm_key_begin(key, ns);
    return sm_key_add_string(key, text) == SM_KEY_OK ? SM_REPO_OK
                                                     : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_expect_missing(sm_repo_uow *uow,
                                        const sm_key_builder *key) {
    void *value = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &value, &len);
    if (s == SM_REPO_NOT_FOUND) return SM_REPO_OK;
    if (s == SM_REPO_OK) { sm_repo_value_free(value); return SM_REPO_CONFLICT; }
    return s;
}

static sm_repo_status sm_put(sm_repo_uow *uow, const sm_key_builder *key,
                             const void *entity, sm_encode_fn encode) {
    uint8_t *value = NULL; size_t len = 0;
    sm_repo_status s = sm_codec(encode(entity, &value, &len));
    if (s == SM_REPO_OK)
        s = sm_repo_put(uow, sm_key_data(key), sm_key_size(key), value, len);
    free(value);
    return s;
}

static sm_repo_status sm_get(sm_repo_uow *uow, const sm_key_builder *key,
                             void *out, sm_decode_fn decode) {
    void *value = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &value, &len);
    if (s == SM_REPO_OK && decode(value, len, out) != SM_CODEC_OK)
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    return s;
}

static sm_codec_status sm_order_encode(const void *v, uint8_t **out, size_t *len) {
    return sm_purchase_encode(v, out, len);
}
static sm_codec_status sm_order_decode(const void *v, size_t len, void *out) {
    return sm_purchase_decode(v, len, out);
}
static sm_codec_status sm_item_encode(const void *v, uint8_t **out, size_t *len) {
    return sm_purchase_item_encode(v, out, len);
}
static sm_codec_status sm_batch_encode_any(const void *v, uint8_t **out, size_t *len) {
    return sm_batch_encode(v, out, len);
}
static sm_codec_status sm_batch_decode_any(const void *v, size_t len, void *out) {
    return sm_batch_decode(v, len, out);
}

static sm_repo_status sm_order_status_key(const sm_purchase *v,
                                          sm_key_builder *key) {
    sm_key_begin(key, SM_NS_PURCHASE_BY_STATUS);
    return sm_key_add_u8(key, v->status) == SM_KEY_OK &&
                   sm_key_add_u64(key, v->id) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_order_supplier_key(const sm_purchase *v,
                                            sm_key_builder *key) {
    sm_key_begin(key, SM_NS_PURCHASE_BY_SUPPLIER_TIME);
    return sm_key_add_string(key, v->supplier_id) == SM_KEY_OK &&
                   sm_key_add_i64(key, v->created_at) == SM_KEY_OK &&
                   sm_key_add_u64(key, v->id) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_order_indexes(sm_repo_uow *uow,
                                       const sm_purchase *v, int add) {
    sm_key_builder key; sm_repo_status s = sm_order_status_key(v, &key);
    if (s == SM_REPO_OK)
        s = add ? sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key))
                : sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    if (s == SM_REPO_OK) s = sm_order_supplier_key(v, &key);
    if (s == SM_REPO_OK)
        s = add ? sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key))
                : sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    return s;
}

static sm_repo_status sm_order_insert(sm_repo_uow *uow,
                                      const sm_purchase *v, int importing) {
    sm_key_builder key; sm_repo_status s;
    if (!sm_purchase_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_key_u64(SM_NS_PURCHASE_BY_ID, v->id, &key);
    if (s == SM_REPO_OK) s = sm_expect_missing(uow, &key);
    if (s == SM_REPO_OK) s = sm_put(uow, &key, v, sm_order_encode);
    if (s == SM_REPO_OK) s = sm_order_indexes(uow, v, 1);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_PURCHASE, v->id + 1u);
    return s;
}

sm_repo_status sm_purchase_create(sm_repo_uow *uow, sm_purchase *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !sm_purchase_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_PURCHASE, &v->id);
    return s == SM_REPO_OK ? sm_order_insert(uow, v, 0) : s;
}

sm_repo_status sm_purchase_import(sm_repo_uow *uow, const sm_purchase *v) {
    if (!uow || !v || !v->id || v->id == UINT64_MAX) return SM_REPO_ERR_INVALID;
    return sm_order_insert(uow, v, 1);
}

sm_repo_status sm_purchase_get(sm_repo_uow *uow, uint64_t id, sm_purchase *out) {
    sm_key_builder key;
    if (!uow || !id || !out) return SM_REPO_ERR_INVALID;
    if (sm_key_u64(SM_NS_PURCHASE_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return sm_get(uow, &key, out, sm_order_decode);
}

sm_repo_status sm_purchase_update(sm_repo_uow *uow, const sm_purchase *v) {
    sm_purchase old; sm_key_builder key; sm_repo_status s;
    if (!uow || !sm_purchase_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_purchase_get(uow, v->id, &old);
    if (s == SM_REPO_OK) s = sm_order_indexes(uow, &old, 0);
    if (s == SM_REPO_OK) s = sm_key_u64(SM_NS_PURCHASE_BY_ID, v->id, &key);
    if (s == SM_REPO_OK) s = sm_put(uow, &key, v, sm_order_encode);
    if (s == SM_REPO_OK) s = sm_order_indexes(uow, v, 1);
    return s;
}

static sm_repo_status sm_append(void **items, size_t *count, size_t *capacity,
                                size_t item_size, const void *value) {
    void *next; size_t cap;
    if (*count == *capacity) {
        cap = *capacity ? *capacity * 2u : 16u;
        if (cap < *capacity || cap > SIZE_MAX / item_size) return SM_REPO_ERR_FULL;
        next = realloc(*items, cap * item_size);
        if (!next) return SM_REPO_ERR_NOMEM;
        *items = next; *capacity = cap;
    }
    memcpy((uint8_t *)(*items) + (*count) * item_size, value, item_size);
    ++*count;
    return SM_REPO_OK;
}

static sm_repo_status sm_scan_entity(sm_repo_scan *scan, uint8_t primary_ns,
                                     uint64_t id, void *out,
                                     sm_decode_fn decode) {
    const void *value = NULL; void *owned = NULL; size_t len = 0;
    sm_key_builder key; sm_repo_status s;
    if (id == 0) s = sm_repo_scan_value(scan, &value, &len);
    else {
        s = sm_key_u64(primary_ns, id, &key);
        if (s == SM_REPO_OK)
            s = sm_repo_scan_get(scan, sm_key_data(&key), sm_key_size(&key),
                                 &owned, &len);
        value = owned;
    }
    if (s == SM_REPO_OK && decode(value, len, out) != SM_CODEC_OK)
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(owned);
    return s;
}

sm_repo_status sm_purchase_list(sm_repository *repo, int wanted,
                                sm_purchase **out, size_t *out_count) {
    sm_key_builder prefix; sm_repo_scan *scan = NULL; sm_purchase *items = NULL;
    size_t count = 0, capacity = 0; sm_repo_status s, close_s; uint8_t ns;
    if (!repo || !out || !out_count || wanted < -1 || wanted > 3)
        return SM_REPO_ERR_INVALID;
    *out = NULL; *out_count = 0;
    ns = wanted < 0 ? SM_NS_PURCHASE_BY_ID : SM_NS_PURCHASE_BY_STATUS;
    sm_key_begin(&prefix, ns);
    if (wanted >= 0 && sm_key_add_u8(&prefix, (uint8_t)wanted) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, ns, sm_key_data(&prefix), sm_key_size(&prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        sm_purchase v; uint64_t id = 0;
        if (wanted >= 0) {
            const void *key; size_t len; sm_key_reader reader; uint8_t ignored;
            s = sm_repo_scan_key(scan, &key, &len);
            if (s == SM_REPO_OK && (sm_key_reader_init(&reader, key, len, ns) != SM_KEY_OK ||
                sm_key_read_u8(&reader, &ignored) != SM_KEY_OK ||
                sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
                sm_key_reader_done(&reader) != SM_KEY_OK)) s = SM_REPO_ERR_CORRUPT;
        }
        if (s == SM_REPO_OK) s = sm_scan_entity(scan, SM_NS_PURCHASE_BY_ID, id, &v, sm_order_decode);
        if (s == SM_REPO_OK && !sm_purchase_valid(&v, 1)) s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = sm_append((void **)&items, &count, &capacity, sizeof(v), &v);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *out_count = count; return SM_REPO_OK;
}

static sm_repo_status sm_item_key(uint64_t purchase_id, uint64_t id,
                                  sm_key_builder *key) {
    sm_key_begin(key, SM_NS_PURCHASE_ITEM_BY_PURCHASE);
    return sm_key_add_u64(key, purchase_id) == SM_KEY_OK &&
                   sm_key_add_u64(key, id) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_item_insert(sm_repo_uow *uow,
                                     const sm_purchase_item *v, int importing) {
    sm_key_builder key; sm_purchase order; sm_repo_status s;
    if (!sm_item_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_purchase_get(uow, v->purchase_id, &order);
    if (s == SM_REPO_OK) s = sm_item_key(v->purchase_id, v->id, &key);
    if (s == SM_REPO_OK) s = sm_expect_missing(uow, &key);
    if (s == SM_REPO_OK) s = sm_put(uow, &key, v, sm_item_encode);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_PURCHASE_ITEM, v->id + 1u);
    return s;
}

sm_repo_status sm_purchase_item_create(sm_repo_uow *uow, sm_purchase_item *v) {
    sm_purchase order; sm_repo_status s;
    if (!uow || !v || v->id || !sm_item_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_purchase_get(uow, v->purchase_id, &order);
    if (s == SM_REPO_OK && order.status != 0u) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_repo_counter_next(uow, SM_COUNTER_PURCHASE_ITEM, &v->id);
    return s == SM_REPO_OK ? sm_item_insert(uow, v, 0) : s;
}

sm_repo_status sm_purchase_item_import(sm_repo_uow *uow, const sm_purchase_item *v) {
    if (!uow || !v || !v->id || v->id == UINT64_MAX) return SM_REPO_ERR_INVALID;
    return sm_item_insert(uow, v, 1);
}

sm_repo_status sm_purchase_item_update(sm_repo_uow *uow, const sm_purchase_item *v) {
    sm_key_builder key; void *old = NULL; size_t len = 0; sm_repo_status s;
    if (!uow || !sm_item_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_item_key(v->purchase_id, v->id, &key);
    if (s == SM_REPO_OK) s = sm_repo_get(uow, sm_key_data(&key), sm_key_size(&key), &old, &len);
    sm_repo_value_free(old);
    if (s == SM_REPO_OK) s = sm_put(uow, &key, v, sm_item_encode);
    return s;
}

sm_repo_status sm_purchase_item_list(sm_repository *repo, uint64_t purchase_id,
                                     sm_purchase_item **out, size_t *out_count) {
    sm_key_builder prefix; sm_repo_scan *scan = NULL; sm_purchase_item *items = NULL;
    size_t count = 0, capacity = 0; sm_repo_status s, close_s;
    if (!repo || !purchase_id || !out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL; *out_count = 0; sm_key_begin(&prefix, SM_NS_PURCHASE_ITEM_BY_PURCHASE);
    if (sm_key_add_u64(&prefix, purchase_id) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, SM_NS_PURCHASE_ITEM_BY_PURCHASE,
                                 sm_key_data(&prefix), sm_key_size(&prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *value; size_t len; sm_purchase_item v;
        s = sm_repo_scan_value(scan, &value, &len);
        if (s == SM_REPO_OK && sm_purchase_item_decode(value, len, &v) != SM_CODEC_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK && (!sm_item_valid(&v, 1) || v.purchase_id != purchase_id))
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = sm_append((void **)&items, &count, &capacity, sizeof(v), &v);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *out_count = count; return SM_REPO_OK;
}

static sm_repo_status sm_batch_expiry_key(const sm_batch *v, sm_key_builder *key) {
    sm_key_begin(key, SM_NS_BATCH_BY_PRODUCT_EXPIRY);
    return sm_key_add_string(key, v->product_id) == SM_KEY_OK &&
                   sm_key_add_i64(key, v->expiry_date) == SM_KEY_OK &&
                   sm_key_add_string(key, v->batch_no) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_batch_received_key(const sm_batch *v, sm_key_builder *key) {
    sm_key_begin(key, SM_NS_BATCH_BY_PRODUCT_RECEIVED);
    return sm_key_add_string(key, v->product_id) == SM_KEY_OK &&
                   sm_key_add_i64(key, v->received_date) == SM_KEY_OK &&
                   sm_key_add_string(key, v->batch_no) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_batch_indexes(sm_repo_uow *uow, const sm_batch *v, int add) {
    sm_key_builder key; sm_repo_status s = sm_batch_expiry_key(v, &key);
    if (s == SM_REPO_OK) s = add ? sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key))
                                 : sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    if (s == SM_REPO_OK) s = sm_batch_received_key(v, &key);
    if (s == SM_REPO_OK) s = add ? sm_repo_index_add(uow, sm_key_data(&key), sm_key_size(&key))
                                 : sm_repo_index_remove(uow, sm_key_data(&key), sm_key_size(&key));
    return s;
}

static sm_repo_status sm_batch_insert(sm_repo_uow *uow, const sm_batch *v) {
    sm_key_builder key; sm_repo_status s;
    if (!sm_batch_valid(v)) return SM_REPO_ERR_INVALID;
    s = sm_key_text(SM_NS_BATCH_BY_NO, v->batch_no, &key);
    if (s == SM_REPO_OK) s = sm_expect_missing(uow, &key);
    if (s == SM_REPO_OK) s = sm_put(uow, &key, v, sm_batch_encode_any);
    if (s == SM_REPO_OK) s = sm_batch_indexes(uow, v, 1);
    return s;
}

sm_repo_status sm_batch_create(sm_repo_uow *uow, sm_batch *v) {
    uint64_t id; sm_repo_status s;
    if (!uow || !v || v->batch_no[0]) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_BATCH, &id);
    if (s != SM_REPO_OK) return s;
    if (snprintf(v->batch_no, sizeof(v->batch_no), "B%018llu",
                 (unsigned long long)id) != 19) return SM_REPO_ERR_FULL;
    return sm_batch_insert(uow, v);
}

sm_repo_status sm_batch_import(sm_repo_uow *uow, const sm_batch *v) {
    return !uow || !v ? SM_REPO_ERR_INVALID : sm_batch_insert(uow, v);
}

sm_repo_status sm_batch_get(sm_repo_uow *uow, const char *batch_no, sm_batch *out) {
    sm_key_builder key;
    if (!uow || !batch_no || !*batch_no || !out) return SM_REPO_ERR_INVALID;
    if (sm_key_text(SM_NS_BATCH_BY_NO, batch_no, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return sm_get(uow, &key, out, sm_batch_decode_any);
}

sm_repo_status sm_batch_update(sm_repo_uow *uow, const sm_batch *v) {
    sm_batch old; sm_key_builder key; sm_repo_status s;
    if (!uow || !sm_batch_valid(v)) return SM_REPO_ERR_INVALID;
    s = sm_batch_get(uow, v->batch_no, &old);
    if (s == SM_REPO_OK && (strcmp(old.product_id, v->product_id) ||
        old.expiry_date != v->expiry_date || old.received_date != v->received_date))
        s = sm_batch_indexes(uow, &old, 0);
    if (s == SM_REPO_OK) s = sm_key_text(SM_NS_BATCH_BY_NO, v->batch_no, &key);
    if (s == SM_REPO_OK) s = sm_put(uow, &key, v, sm_batch_encode_any);
    if (s == SM_REPO_OK && (strcmp(old.product_id, v->product_id) ||
        old.expiry_date != v->expiry_date || old.received_date != v->received_date))
        s = sm_batch_indexes(uow, v, 1);
    return s;
}

sm_repo_status sm_batch_list(sm_repository *repo, const char *product_id,
                             int fifo, sm_batch **out, size_t *out_count) {
    sm_key_builder prefix, primary; sm_repo_scan *scan = NULL; sm_batch *items = NULL;
    size_t count = 0, capacity = 0; sm_repo_status s, close_s; uint8_t ns;
    if (!repo || !out || !out_count || (fifo != 0 && fifo != 1)) return SM_REPO_ERR_INVALID;
    *out = NULL; *out_count = 0;
    ns = product_id ? (fifo ? SM_NS_BATCH_BY_PRODUCT_RECEIVED : SM_NS_BATCH_BY_PRODUCT_EXPIRY)
                    : SM_NS_BATCH_BY_NO;
    sm_key_begin(&prefix, ns);
    if (product_id && sm_key_add_string(&prefix, product_id) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, ns, sm_key_data(&prefix), sm_key_size(&prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *value = NULL; void *owned = NULL; size_t len; sm_batch v;
        if (!product_id) s = sm_repo_scan_value(scan, &value, &len);
        else {
            const void *key; size_t key_len; sm_key_reader reader;
            const char *ignored, *batch_no; size_t ignored_len, batch_len; int64_t time;
            s = sm_repo_scan_key(scan, &key, &key_len);
            if (s == SM_REPO_OK && (sm_key_reader_init(&reader, key, key_len, ns) != SM_KEY_OK ||
                sm_key_read_string(&reader, &ignored, &ignored_len) != SM_KEY_OK ||
                sm_key_read_i64(&reader, &time) != SM_KEY_OK ||
                sm_key_read_string(&reader, &batch_no, &batch_len) != SM_KEY_OK ||
                sm_key_reader_done(&reader) != SM_KEY_OK || batch_len >= SM_BATCH_NO_CAPACITY))
                s = SM_REPO_ERR_CORRUPT;
            if (s == SM_REPO_OK) {
                char no[SM_BATCH_NO_CAPACITY]; memcpy(no, batch_no, batch_len); no[batch_len] = '\0';
                s = sm_key_text(SM_NS_BATCH_BY_NO, no, &primary);
                if (s == SM_REPO_OK) s = sm_repo_scan_get(scan, sm_key_data(&primary),
                                                           sm_key_size(&primary), &owned, &len);
                value = owned;
            }
        }
        if (s == SM_REPO_OK && sm_batch_decode(value, len, &v) != SM_CODEC_OK)
            s = SM_REPO_ERR_CORRUPT;
        sm_repo_value_free(owned);
        if (s == SM_REPO_OK && !sm_batch_valid(&v)) s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = sm_append((void **)&items, &count, &capacity, sizeof(v), &v);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *out_count = count; return SM_REPO_OK;
}

void sm_purchase_array_free(void *array) { free(array); }
