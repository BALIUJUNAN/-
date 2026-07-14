#include "repo/sm_operations_repository.h"

#include "repo/sm_operations_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdlib.h>
#include <string.h>

typedef sm_codec_status (*decode_fn)(const void *, size_t, void *);
typedef sm_codec_status (*encode_fn)(const void *, uint8_t **, size_t *);

static sm_repo_status key_id(uint8_t ns, uint64_t id, sm_key_builder *key) {
    sm_key_begin(key, ns);
    return sm_key_add_u64(key, id) == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status get_decoded(sm_repo_uow *uow, const sm_key_builder *key,
                                  decode_fn decode, void *out) {
    void *data = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &data, &len);
    if (s == SM_REPO_OK && decode(data, len, out) != SM_CODEC_OK)
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(data); return s;
}

static sm_repo_status put_encoded(sm_repo_uow *uow, const sm_key_builder *key,
                                  encode_fn encode, const void *value) {
    uint8_t *data = NULL; size_t len = 0; sm_repo_status s;
    sm_codec_status c = encode(value, &data, &len);
    if (c != SM_CODEC_OK) { free(data); return c == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM : SM_REPO_ERR_INVALID; }
    s = sm_repo_put(uow, sm_key_data(key), sm_key_size(key), data, len);
    free(data); return s;
}

static sm_repo_status missing(sm_repo_uow *uow, const sm_key_builder *key) {
    void *data = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &data, &len);
    sm_repo_value_free(data);
    return s == SM_REPO_NOT_FOUND ? SM_REPO_OK : s == SM_REPO_OK ? SM_REPO_CONFLICT : s;
}

static sm_repo_status append(void **values, size_t *count, size_t *capacity,
                             size_t width, const void *value) {
    void *next; size_t cap;
    if (*count == *capacity) {
        cap = *capacity ? *capacity * 2u : 16u;
        if (cap < *capacity || cap > SIZE_MAX / width) return SM_REPO_ERR_FULL;
        next = realloc(*values, cap * width); if (!next) return SM_REPO_ERR_NOMEM;
        *values = next; *capacity = cap;
    }
    memcpy((uint8_t *)*values + *count * width, value, width); ++*count;
    return SM_REPO_OK;
}

static sm_repo_status scan_values(sm_repository *repo, uint8_t ns,
                                  const sm_key_builder *prefix,
                                  decode_fn decode, size_t width,
                                  void **out, size_t *count) {
    sm_repo_scan *scan = NULL; void *values = NULL; size_t used = 0, cap = 0;
    sm_repo_status s, close_s;
    *out = NULL; *count = 0;
    s = sm_repo_scan_open_prefix(repo, ns, sm_key_data(prefix),
                                 sm_key_size(prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *data; size_t len; uint8_t record[2048];
        if (width > sizeof(record)) { s = SM_REPO_ERR_INVALID; break; }
        s = sm_repo_scan_value(scan, &data, &len);
        if (s == SM_REPO_OK && decode(data, len, record) != SM_CODEC_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = append(&values, &used, &cap, width, record);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(values); return s; }
    *out = values; *count = used; return SM_REPO_OK;
}

static int text_ok(const char *s, size_t cap, int required) {
    return s && memchr(s, '\0', cap) && (!required || s[0]);
}

static int store_valid(const sm_store_record *v, int require_id) {
    return v && (!require_id || v->id) && text_ok(v->name, sizeof(v->name), 1) &&
           text_ok(v->address, sizeof(v->address), 0) &&
           text_ok(v->phone, sizeof(v->phone), 0) &&
           text_ok(v->manager_name, sizeof(v->manager_name), 0) &&
           v->status <= 1u && v->created_at >= 0 && v->updated_at >= 0;
}

static sm_repo_status store_name_key(const sm_store_record *v, sm_key_builder *key) {
    sm_key_begin(key, SM_NS_STORE_BY_NAME);
    return sm_key_add_string(key, v->name) == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status store_insert(sm_repo_uow *uow, const sm_store_record *v,
                                   int importing) {
    sm_key_builder primary, name; sm_repo_status s;
    if (!uow || !store_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = key_id(SM_NS_STORE_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = missing(uow, &primary);
    if (s == SM_REPO_OK) s = store_name_key(v, &name);
    if (s == SM_REPO_OK)
        s = sm_repo_unique_claim(uow, sm_key_data(&name), sm_key_size(&name),
                                 sm_key_data(&primary), sm_key_size(&primary));
    if (s == SM_REPO_OK)
        s = put_encoded(uow, &primary, (encode_fn)sm_store_record_encode, v);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_STORE, v->id + 1u);
    return s;
}

sm_repo_status sm_store_record_create(sm_repo_uow *uow, sm_store_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !store_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_STORE, &v->id);
    return s == SM_REPO_OK ? store_insert(uow, v, 0) : s;
}

sm_repo_status sm_store_record_import(sm_repo_uow *uow, const sm_store_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID : store_insert(uow, v, 1);
}

sm_repo_status sm_store_record_get(sm_repo_uow *uow, uint64_t id,
                                   sm_store_record *out) {
    sm_key_builder key;
    if (!uow || !id || !out || key_id(SM_NS_STORE_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return get_decoded(uow, &key, (decode_fn)sm_store_record_decode, out);
}

sm_repo_status sm_store_record_update(sm_repo_uow *uow,
                                      const sm_store_record *v) {
    sm_store_record old; sm_key_builder primary, old_name, new_name; sm_repo_status s;
    if (!uow || !store_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_store_record_get(uow, v->id, &old);
    if (s == SM_REPO_OK) s = key_id(SM_NS_STORE_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = store_name_key(&old, &old_name);
    if (s == SM_REPO_OK) s = store_name_key(v, &new_name);
    if (s == SM_REPO_OK && strcmp(old.name, v->name) != 0) {
        s = sm_repo_unique_claim(uow, sm_key_data(&new_name), sm_key_size(&new_name),
                                 sm_key_data(&primary), sm_key_size(&primary));
        if (s == SM_REPO_OK)
            s = sm_repo_unique_release(uow, sm_key_data(&old_name), sm_key_size(&old_name),
                                       sm_key_data(&primary), sm_key_size(&primary));
    }
    return s == SM_REPO_OK ? put_encoded(uow, &primary,
        (encode_fn)sm_store_record_encode, v) : s;
}

sm_repo_status sm_store_record_list(sm_repository *repo, int status,
                                    sm_store_record **out, size_t *count) {
    sm_key_builder prefix; sm_store_record *all = NULL, *filtered = NULL;
    size_t n = 0, used = 0, cap = 0, i; sm_repo_status s;
    if (!repo || !out || !count || status < -1 || status > 1)
        return SM_REPO_ERR_INVALID;
    sm_key_begin(&prefix, SM_NS_STORE_BY_ID);
    s = scan_values(repo, SM_NS_STORE_BY_ID, &prefix,
                    (decode_fn)sm_store_record_decode, sizeof(*all),
                    (void **)&all, &n);
    if (status < 0 || s != SM_REPO_OK) { *out = all; *count = n; return s; }
    for (i = 0; i < n && s == SM_REPO_OK; ++i)
        if (all[i].status == (uint8_t)status)
            s = append((void **)&filtered, &used, &cap, sizeof(*filtered), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(filtered); return s; }
    *out = filtered; *count = used; return SM_REPO_OK;
}

static int stock_valid(const sm_store_stock_record *v) {
    return v && v->store_id && text_ok(v->product_id, sizeof(v->product_id), 1) &&
           v->quantity >= 0 && v->min_stock >= 0 && v->updated_at >= 0;
}

static sm_repo_status stock_key(uint64_t store, const char *product,
                                sm_key_builder *key) {
    sm_key_begin(key, SM_NS_STORE_STOCK);
    return sm_key_add_u64(key, store) == SM_KEY_OK &&
           sm_key_add_string(key, product) == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

sm_repo_status sm_store_stock_get(sm_repo_uow *uow, uint64_t store,
                                  const char *product,
                                  sm_store_stock_record *out) {
    sm_key_builder key;
    if (!uow || !store || !product || !*product || !out ||
        stock_key(store, product, &key) != SM_REPO_OK) return SM_REPO_ERR_INVALID;
    return get_decoded(uow, &key, (decode_fn)sm_store_stock_record_decode, out);
}

sm_repo_status sm_store_stock_put(sm_repo_uow *uow,
                                  const sm_store_stock_record *v) {
    sm_key_builder key;
    if (!uow || !stock_valid(v) || stock_key(v->store_id, v->product_id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return put_encoded(uow, &key, (encode_fn)sm_store_stock_record_encode, v);
}

sm_repo_status sm_store_stock_list(sm_repository *repo, uint64_t store,
                                   const char *product,
                                   sm_store_stock_record **out, size_t *count) {
    sm_key_builder prefix; sm_store_stock_record *all = NULL, *filtered = NULL;
    size_t n = 0, used = 0, cap = 0, i; sm_repo_status s;
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    sm_key_begin(&prefix, SM_NS_STORE_STOCK);
    if (store && sm_key_add_u64(&prefix, store) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
    s = scan_values(repo, SM_NS_STORE_STOCK, &prefix,
                    (decode_fn)sm_store_stock_record_decode, sizeof(*all),
                    (void **)&all, &n);
    if (!product || s != SM_REPO_OK) { *out = all; *count = n; return s; }
    for (i = 0; i < n && s == SM_REPO_OK; ++i)
        if (strcmp(all[i].product_id, product) == 0)
            s = append((void **)&filtered, &used, &cap, sizeof(*filtered), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(filtered); return s; }
    *out = filtered; *count = used; return SM_REPO_OK;
}

static int transfer_valid(const sm_transfer_order_record *v, int require_id) {
    return v && (!require_id || v->id) && v->from_store_id && v->to_store_id &&
           v->from_store_id != v->to_store_id && v->status <= 4u &&
           text_ok(v->from_store_name, sizeof(v->from_store_name), 1) &&
           text_ok(v->to_store_name, sizeof(v->to_store_name), 1) &&
           text_ok(v->creator_name, sizeof(v->creator_name), 0) &&
           text_ok(v->approver_name, sizeof(v->approver_name), 0) &&
           text_ok(v->out_operator_name, sizeof(v->out_operator_name), 0) &&
           text_ok(v->in_operator_name, sizeof(v->in_operator_name), 0) &&
           text_ok(v->remark, sizeof(v->remark), 0) && v->created_at >= 0 &&
           v->approved_at >= 0 && v->out_at >= 0 && v->in_at >= 0;
}

static sm_repo_status transfer_indexes(const sm_transfer_order_record *v,
                                       sm_key_builder *status,
                                       sm_key_builder *from,
                                       sm_key_builder *to) {
    sm_key_begin(status, SM_NS_TRANSFER_BY_STATUS);
    sm_key_begin(from, SM_NS_TRANSFER_BY_STORE);
    sm_key_begin(to, SM_NS_TRANSFER_BY_STORE);
    return sm_key_add_u8(status, v->status) == SM_KEY_OK &&
           sm_key_add_u64(status, v->id) == SM_KEY_OK &&
           sm_key_add_u64(from, v->from_store_id) == SM_KEY_OK &&
           sm_key_add_u8(from, 0u) == SM_KEY_OK && sm_key_add_u64(from, v->id) == SM_KEY_OK &&
           sm_key_add_u64(to, v->to_store_id) == SM_KEY_OK &&
           sm_key_add_u8(to, 1u) == SM_KEY_OK && sm_key_add_u64(to, v->id) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status transfer_insert(sm_repo_uow *uow,
                                      const sm_transfer_order_record *v,
                                      int importing) {
    sm_key_builder primary, status, from, to; sm_repo_status s;
    if (!uow || !transfer_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = key_id(SM_NS_TRANSFER_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = missing(uow, &primary);
    if (s == SM_REPO_OK) s = transfer_indexes(v, &status, &from, &to);
    if (s == SM_REPO_OK) s = put_encoded(uow, &primary, (encode_fn)sm_transfer_order_record_encode, v);
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&status), sm_key_size(&status));
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&from), sm_key_size(&from));
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&to), sm_key_size(&to));
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_TRANSFER, v->id + 1u);
    return s;
}

sm_repo_status sm_transfer_order_create(sm_repo_uow *uow,
                                        sm_transfer_order_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !transfer_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_TRANSFER, &v->id);
    return s == SM_REPO_OK ? transfer_insert(uow, v, 0) : s;
}

sm_repo_status sm_transfer_order_import(sm_repo_uow *uow,
                                        const sm_transfer_order_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID : transfer_insert(uow, v, 1);
}

sm_repo_status sm_transfer_order_get(sm_repo_uow *uow, uint64_t id,
                                     sm_transfer_order_record *out) {
    sm_key_builder key;
    if (!uow || !id || !out || key_id(SM_NS_TRANSFER_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return get_decoded(uow, &key, (decode_fn)sm_transfer_order_record_decode, out);
}

sm_repo_status sm_transfer_order_update(sm_repo_uow *uow,
                                        const sm_transfer_order_record *v) {
    sm_transfer_order_record old; sm_key_builder primary;
    sm_key_builder os, of, ot, ns, nf, nt; sm_repo_status s;
    if (!uow || !transfer_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_transfer_order_get(uow, v->id, &old);
    if (s == SM_REPO_OK && (old.from_store_id != v->from_store_id ||
                            old.to_store_id != v->to_store_id)) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = transfer_indexes(&old, &os, &of, &ot);
    if (s == SM_REPO_OK) s = transfer_indexes(v, &ns, &nf, &nt);
    if (s == SM_REPO_OK && old.status != v->status) {
        s = sm_repo_index_remove(uow, sm_key_data(&os), sm_key_size(&os));
        if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&ns), sm_key_size(&ns));
    }
    if (s == SM_REPO_OK) s = key_id(SM_NS_TRANSFER_BY_ID, v->id, &primary);
    return s == SM_REPO_OK ? put_encoded(uow, &primary,
        (encode_fn)sm_transfer_order_record_encode, v) : s;
}

static uint64_t tail_id(const void *key, size_t len) {
    const uint8_t *p = key; uint64_t id = 0; size_t i;
    if (len < 8u) return 0;
    p += len - 8u; for (i = 0; i < 8u; ++i) id = (id << 8) | p[i];
    return id;
}

sm_repo_status sm_transfer_order_list(sm_repository *repo, int status,
                                      uint64_t store,
                                      sm_transfer_order_record **out,
                                      size_t *count) {
    sm_key_builder prefix, primary; sm_repo_scan *scan = NULL;
    sm_transfer_order_record *values = NULL; size_t used = 0, cap = 0;
    sm_repo_status s, close_s; uint8_t ns;
    if (!repo || !out || !count || status < -1 || status > 4) return SM_REPO_ERR_INVALID;
    if (!store && status < 0) {
        sm_key_begin(&prefix, SM_NS_TRANSFER_BY_ID);
        return scan_values(repo, SM_NS_TRANSFER_BY_ID, &prefix,
            (decode_fn)sm_transfer_order_record_decode, sizeof(**out),
            (void **)out, count);
    }
    *out = NULL; *count = 0; ns = store ? SM_NS_TRANSFER_BY_STORE : SM_NS_TRANSFER_BY_STATUS;
    sm_key_begin(&prefix, ns);
    if ((store && sm_key_add_u64(&prefix, store) != SM_KEY_OK) ||
        (!store && sm_key_add_u8(&prefix, (uint8_t)status) != SM_KEY_OK))
        return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, ns, sm_key_data(&prefix), sm_key_size(&prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *key; size_t len; uint64_t id; void *data = NULL; size_t data_len = 0;
        sm_transfer_order_record value;
        s = sm_repo_scan_key(scan, &key, &len); id = s == SM_REPO_OK ? tail_id(key, len) : 0;
        if (s == SM_REPO_OK && (!id || key_id(SM_NS_TRANSFER_BY_ID, id, &primary) != SM_REPO_OK)) s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = sm_repo_scan_get(scan, sm_key_data(&primary), sm_key_size(&primary), &data, &data_len);
        if (s == SM_REPO_OK && sm_transfer_order_record_decode(data, data_len, &value) != SM_CODEC_OK) s = SM_REPO_ERR_CORRUPT;
        sm_repo_value_free(data);
        if (s == SM_REPO_OK && (status < 0 || value.status == (uint8_t)status))
            s = append((void **)&values, &used, &cap, sizeof(value), &value);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(values); return s; }
    *out = values; *count = used; return SM_REPO_OK;
}

static int item_valid(const sm_transfer_item_record *v, int require_id) {
    return v && (!require_id || v->id) && v->transfer_id && v->quantity > 0 &&
           text_ok(v->product_id, sizeof(v->product_id), 1) &&
           text_ok(v->product_name, sizeof(v->product_name), 1);
}

static sm_repo_status item_insert(sm_repo_uow *uow,
                                  const sm_transfer_item_record *v,
                                  int importing) {
    sm_key_builder key; sm_repo_status s;
    if (!uow || !item_valid(v, 1)) return SM_REPO_ERR_INVALID;
    sm_key_begin(&key, SM_NS_TRANSFER_ITEM_BY_TRANSFER);
    if (sm_key_add_u64(&key, v->transfer_id) != SM_KEY_OK ||
        sm_key_add_u64(&key, v->id) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
    s = missing(uow, &key);
    if (s == SM_REPO_OK) s = put_encoded(uow, &key, (encode_fn)sm_transfer_item_record_encode, v);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_TRANSFER_ITEM, v->id + 1u);
    return s;
}

sm_repo_status sm_transfer_item_create(sm_repo_uow *uow,
                                       sm_transfer_item_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !item_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_TRANSFER_ITEM, &v->id);
    return s == SM_REPO_OK ? item_insert(uow, v, 0) : s;
}

sm_repo_status sm_transfer_item_import(sm_repo_uow *uow,
                                       const sm_transfer_item_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID : item_insert(uow, v, 1);
}

sm_repo_status sm_transfer_item_list(sm_repository *repo, uint64_t transfer,
                                     sm_transfer_item_record **out,
                                     size_t *count) {
    sm_key_builder prefix;
    if (!repo || !transfer || !out || !count) return SM_REPO_ERR_INVALID;
    sm_key_begin(&prefix, SM_NS_TRANSFER_ITEM_BY_TRANSFER);
    if (sm_key_add_u64(&prefix, transfer) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
    return scan_values(repo, SM_NS_TRANSFER_ITEM_BY_TRANSFER, &prefix,
        (decode_fn)sm_transfer_item_record_decode, sizeof(**out),
        (void **)out, count);
}

void sm_operations_array_free(void *values) { free(values); }
