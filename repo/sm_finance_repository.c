#include "repo/sm_finance_repository.h"

#include "repo/sm_finance_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdlib.h>
#include <string.h>

typedef sm_codec_status (*decode_fn)(const void *, size_t, void *);
typedef sm_codec_status (*encode_fn)(const void *, uint8_t **, size_t *);

static sm_repo_status key_u64(uint8_t ns, uint64_t id, sm_key_builder *key) {
    sm_key_begin(key, ns);
    return sm_key_add_u64(key, id) == SM_KEY_OK ? SM_REPO_OK
                                                 : SM_REPO_ERR_INVALID;
}

static sm_repo_status key_string(uint8_t ns, const char *value,
                                 sm_key_builder *key) {
    sm_key_begin(key, ns);
    return sm_key_add_string(key, value) == SM_KEY_OK ? SM_REPO_OK
                                                       : SM_REPO_ERR_INVALID;
}

static sm_repo_status expect_missing(sm_repo_uow *uow,
                                     const sm_key_builder *key) {
    void *value = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &value, &len);
    if (s == SM_REPO_NOT_FOUND) return SM_REPO_OK;
    if (s == SM_REPO_OK) { sm_repo_value_free(value); return SM_REPO_CONFLICT; }
    return s;
}

static sm_repo_status decode_get(sm_repo_uow *uow, const sm_key_builder *key,
                                 decode_fn decode, void *out) {
    void *value = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &value, &len);
    if (s == SM_REPO_OK && decode(value, len, out) != SM_CODEC_OK)
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value); return s;
}

static sm_repo_status encode_put(sm_repo_uow *uow, const sm_key_builder *key,
                                 encode_fn encode, const void *entity) {
    uint8_t *value = NULL; size_t len = 0;
    sm_codec_status codec = encode(entity, &value, &len);
    sm_repo_status s;
    if (codec != SM_CODEC_OK) {
        free(value);
        return codec == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM
                                            : SM_REPO_ERR_INVALID;
    }
    s = sm_repo_put(uow, sm_key_data(key), sm_key_size(key), value, len);
    free(value); return s;
}

static int finance_valid(const sm_supplier_finance *v) {
    return v && v->supplier_id && v->rating && v->total_cents >= 0 &&
           v->paid_cents >= 0 && v->pending_cents >= 0 &&
           v->paid_cents <= v->total_cents &&
           v->pending_cents == v->total_cents - v->paid_cents &&
           v->last_payment_at >= 0 && v->updated_at >= 0;
}

sm_repo_status sm_supplier_finance_get(sm_repo_uow *uow, uint64_t id,
                                       sm_supplier_finance *out) {
    sm_key_builder key;
    if (!uow || !id || !out) return SM_REPO_ERR_INVALID;
    if (key_u64(SM_NS_SUPPLIER_FINANCE, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return decode_get(uow, &key, (decode_fn)sm_supplier_finance_decode, out);
}

sm_repo_status sm_supplier_finance_put(sm_repo_uow *uow,
                                       const sm_supplier_finance *v) {
    sm_key_builder key;
    if (!uow || !finance_valid(v)) return SM_REPO_ERR_INVALID;
    if (key_u64(SM_NS_SUPPLIER_FINANCE, v->supplier_id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return encode_put(uow, &key, (encode_fn)sm_supplier_finance_encode, v);
}

static sm_repo_status append(void **array, size_t *count, size_t *capacity,
                             size_t width, const void *value) {
    void *next; size_t cap;
    if (*count == *capacity) {
        cap = *capacity ? *capacity * 2u : 16u;
        if (cap < *capacity || cap > SIZE_MAX / width) return SM_REPO_ERR_FULL;
        next = realloc(*array, cap * width);
        if (!next) return SM_REPO_ERR_NOMEM;
        *array = next; *capacity = cap;
    }
    memcpy((uint8_t *)(*array) + *count * width, value, width); ++*count;
    return SM_REPO_OK;
}

static sm_repo_status scan_primary(sm_repository *repo, uint8_t ns,
                                   decode_fn decode, size_t width,
                                   void **out, size_t *out_count) {
    sm_repo_scan *scan = NULL; void *items = NULL; size_t count = 0, cap = 0;
    uint8_t record[1024], prefix = ns; sm_repo_status s, close_s;
    if (!repo || !out || !out_count || width > sizeof(record))
        return SM_REPO_ERR_INVALID;
    *out = NULL; *out_count = 0;
    s = sm_repo_scan_open_prefix(repo, ns, &prefix, sizeof(prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *value; size_t len;
        s = sm_repo_scan_value(scan, &value, &len);
        if (s == SM_REPO_OK && decode(value, len, record) != SM_CODEC_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = append(&items, &count, &cap, width, record);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *out_count = count; return SM_REPO_OK;
}

static sm_repo_status scan_decode_primary(sm_repo_scan *scan,
                                          const sm_key_builder *primary,
                                          decode_fn decode, void *out) {
    void *value = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_scan_get(scan, sm_key_data(primary),
                                        sm_key_size(primary), &value, &len);
    if (s == SM_REPO_OK && decode(value, len, out) != SM_CODEC_OK)
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value); return s;
}

sm_repo_status sm_supplier_finance_list(sm_repository *repo,
                                        sm_supplier_finance **out,
                                        size_t *count) {
    return scan_primary(repo, SM_NS_SUPPLIER_FINANCE,
                        (decode_fn)sm_supplier_finance_decode, sizeof(**out),
                        (void **)out, count);
}

static int payable_valid(const sm_payable *v, int require_id) {
    return v && (!require_id || v->id) && v->supplier_id &&
           memchr(v->supplier_name, '\0', sizeof(v->supplier_name)) &&
           v->supplier_name[0] && v->amount_cents > 0 && v->paid_cents >= 0 &&
           v->pending_cents >= 0 && v->paid_cents + v->pending_cents == v->amount_cents &&
           v->status <= 2u && v->due_at >= 0 && v->created_at >= 0 && v->paid_at >= 0;
}

static sm_repo_status payable_indexes(const sm_payable *v,
                                      sm_key_builder *status,
                                      sm_key_builder *supplier,
                                      sm_key_builder *purchase) {
    sm_key_begin(status, SM_NS_PAYABLE_BY_STATUS_DUE);
    sm_key_begin(supplier, SM_NS_PAYABLE_BY_SUPPLIER);
    sm_key_begin(purchase, SM_NS_PAYABLE_BY_PURCHASE);
    return sm_key_add_u8(status, v->status) == SM_KEY_OK &&
           sm_key_add_i64(status, v->due_at) == SM_KEY_OK &&
           sm_key_add_u64(status, v->id) == SM_KEY_OK &&
           sm_key_add_u64(supplier, v->supplier_id) == SM_KEY_OK &&
           sm_key_add_u64(supplier, v->id) == SM_KEY_OK &&
           (!v->purchase_id || sm_key_add_u64(purchase, v->purchase_id) == SM_KEY_OK)
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status payable_insert(sm_repo_uow *uow, const sm_payable *v,
                                     int importing) {
    sm_key_builder primary, status_key, supplier_key, purchase_key;
    sm_repo_status s;
    if (!uow || !payable_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = key_u64(SM_NS_PAYABLE_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = expect_missing(uow, &primary);
    if (s == SM_REPO_OK) s = payable_indexes(v, &status_key, &supplier_key, &purchase_key);
    if (s == SM_REPO_OK && v->purchase_id)
        s = sm_repo_unique_claim(uow, sm_key_data(&purchase_key),
                                 sm_key_size(&purchase_key), sm_key_data(&primary),
                                 sm_key_size(&primary));
    if (s == SM_REPO_OK)
        s = encode_put(uow, &primary, (encode_fn)sm_payable_encode, v);
    if (s == SM_REPO_OK)
        s = sm_repo_index_add(uow, sm_key_data(&status_key), sm_key_size(&status_key));
    if (s == SM_REPO_OK)
        s = sm_repo_index_add(uow, sm_key_data(&supplier_key), sm_key_size(&supplier_key));
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_PAYABLE, v->id + 1u);
    return s;
}

sm_repo_status sm_payable_create(sm_repo_uow *uow, sm_payable *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !payable_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_PAYABLE, &v->id);
    return s == SM_REPO_OK ? payable_insert(uow, v, 0) : s;
}

sm_repo_status sm_payable_import(sm_repo_uow *uow, const sm_payable *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID
                                     : payable_insert(uow, v, 1);
}

sm_repo_status sm_payable_get(sm_repo_uow *uow, uint64_t id, sm_payable *out) {
    sm_key_builder key;
    if (!uow || !id || !out) return SM_REPO_ERR_INVALID;
    if (key_u64(SM_NS_PAYABLE_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return decode_get(uow, &key, (decode_fn)sm_payable_decode, out);
}

sm_repo_status sm_payable_find_purchase(sm_repo_uow *uow, uint64_t purchase_id,
                                        sm_payable *out) {
    sm_key_builder index; void *primary = NULL; size_t primary_len = 0;
    sm_repo_status s;
    if (!uow || !purchase_id || !out) return SM_REPO_ERR_INVALID;
    s = key_u64(SM_NS_PAYABLE_BY_PURCHASE, purchase_id, &index);
    if (s == SM_REPO_OK)
        s = sm_repo_get(uow, sm_key_data(&index), sm_key_size(&index),
                        &primary, &primary_len);
    if (s == SM_REPO_OK) {
        sm_key_reader r; uint64_t id;
        if (sm_key_reader_init(&r, primary, primary_len, SM_NS_PAYABLE_BY_ID) != SM_KEY_OK ||
            sm_key_read_u64(&r, &id) != SM_KEY_OK || sm_key_reader_done(&r) != SM_KEY_OK)
            s = SM_REPO_ERR_CORRUPT;
        else s = sm_payable_get(uow, id, out);
    }
    sm_repo_value_free(primary); return s;
}

sm_repo_status sm_payable_update(sm_repo_uow *uow, const sm_payable *v) {
    sm_payable old; sm_key_builder primary, old_status, old_supplier, old_purchase;
    sm_key_builder new_status, new_supplier, new_purchase;
    sm_repo_status s;
    if (!uow || !payable_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_payable_get(uow, v->id, &old);
    if (s == SM_REPO_OK && (old.supplier_id != v->supplier_id ||
                            old.purchase_id != v->purchase_id ||
                            old.amount_cents != v->amount_cents))
        s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = payable_indexes(&old, &old_status, &old_supplier, &old_purchase);
    if (s == SM_REPO_OK) s = payable_indexes(v, &new_status, &new_supplier, &new_purchase);
    if (s == SM_REPO_OK && (sm_key_size(&old_status) != sm_key_size(&new_status) ||
                            memcmp(sm_key_data(&old_status), sm_key_data(&new_status),
                                   sm_key_size(&old_status)) != 0)) {
        s = sm_repo_index_remove(uow, sm_key_data(&old_status), sm_key_size(&old_status));
        if (s == SM_REPO_OK)
            s = sm_repo_index_add(uow, sm_key_data(&new_status), sm_key_size(&new_status));
    }
    if (s == SM_REPO_OK) s = key_u64(SM_NS_PAYABLE_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK)
        s = encode_put(uow, &primary, (encode_fn)sm_payable_encode, v);
    return s;
}

sm_repo_status sm_payable_list(sm_repository *repo, uint64_t supplier_id,
                               int status, sm_payable **out, size_t *count) {
    sm_payable *items = NULL; size_t used = 0, cap = 0;
    sm_repo_scan *scan = NULL; sm_key_builder prefix, primary;
    uint8_t ns; sm_repo_status s, close_s;
    if (!repo || !out || !count || status < -1 || status > 2)
        return SM_REPO_ERR_INVALID;
    *out = NULL; *count = 0;
    if (!supplier_id && status < 0)
        return scan_primary(repo, SM_NS_PAYABLE_BY_ID,
                            (decode_fn)sm_payable_decode, sizeof(**out),
                            (void **)out, count);
    ns = supplier_id ? SM_NS_PAYABLE_BY_SUPPLIER
                     : SM_NS_PAYABLE_BY_STATUS_DUE;
    sm_key_begin(&prefix, ns);
    if ((supplier_id && sm_key_add_u64(&prefix, supplier_id) != SM_KEY_OK) ||
        (!supplier_id && sm_key_add_u8(&prefix, (uint8_t)status) != SM_KEY_OK))
        return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, ns, sm_key_data(&prefix),
                                 sm_key_size(&prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *key; size_t key_len; sm_key_reader reader;
        uint64_t id, ignored_supplier; uint8_t ignored_status; int64_t ignored_due;
        sm_payable value;
        s = sm_repo_scan_key(scan, &key, &key_len);
        if (s == SM_REPO_OK && sm_key_reader_init(&reader, key, key_len, ns) != SM_KEY_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK && supplier_id &&
            (sm_key_read_u64(&reader, &ignored_supplier) != SM_KEY_OK ||
             sm_key_read_u64(&reader, &id) != SM_KEY_OK)) s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK && !supplier_id &&
            (sm_key_read_u8(&reader, &ignored_status) != SM_KEY_OK ||
             sm_key_read_i64(&reader, &ignored_due) != SM_KEY_OK ||
             sm_key_read_u64(&reader, &id) != SM_KEY_OK)) s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK && sm_key_reader_done(&reader) != SM_KEY_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = key_u64(SM_NS_PAYABLE_BY_ID, id, &primary);
        if (s == SM_REPO_OK)
            s = scan_decode_primary(scan, &primary,
                                    (decode_fn)sm_payable_decode, &value);
        if (s == SM_REPO_OK && (!supplier_id || status < 0 ||
                                value.status == (uint8_t)status))
            s = append((void **)&items, &used, &cap, sizeof(value), &value);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *count = used; return SM_REPO_OK;
}

static int payment_valid(const sm_payment_record *v, int require_id) {
    return v && (!require_id || v->id) && v->payable_id && v->supplier_id &&
           v->amount_cents > 0 && memchr(v->method, '\0', sizeof(v->method)) &&
           v->method[0] && memchr(v->reference, '\0', sizeof(v->reference)) &&
           memchr(v->operator_name, '\0', sizeof(v->operator_name)) &&
           memchr(v->remark, '\0', sizeof(v->remark)) && v->paid_at >= 0;
}

static sm_repo_status payment_insert(sm_repo_uow *uow,
                                     const sm_payment_record *v,
                                     int importing) {
    sm_key_builder primary, index;
    sm_repo_status s;
    if (!uow || !payment_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = key_u64(SM_NS_PAYMENT_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = expect_missing(uow, &primary);
    sm_key_begin(&index, SM_NS_PAYMENT_BY_PAYABLE);
    if (s == SM_REPO_OK &&
        (sm_key_add_u64(&index, v->payable_id) != SM_KEY_OK ||
         sm_key_add_i64(&index, v->paid_at) != SM_KEY_OK ||
         sm_key_add_u64(&index, v->id) != SM_KEY_OK)) s = SM_REPO_ERR_INVALID;
    if (s == SM_REPO_OK)
        s = encode_put(uow, &primary, (encode_fn)sm_payment_record_encode, v);
    if (s == SM_REPO_OK)
        s = sm_repo_index_add(uow, sm_key_data(&index), sm_key_size(&index));
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_PAYMENT, v->id + 1u);
    return s;
}

sm_repo_status sm_payment_record_create(sm_repo_uow *uow, sm_payment_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !payment_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_PAYMENT, &v->id);
    return s == SM_REPO_OK ? payment_insert(uow, v, 0) : s;
}

sm_repo_status sm_payment_record_import(sm_repo_uow *uow,
                                        const sm_payment_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID
                                     : payment_insert(uow, v, 1);
}

sm_repo_status sm_payment_record_list(sm_repository *repo, uint64_t payable_id,
                                      uint64_t supplier_id,
                                      sm_payment_record **out, size_t *count) {
    sm_repo_scan *scan = NULL; sm_key_builder prefix, primary;
    sm_payment_record *items = NULL; size_t used = 0, cap = 0;
    sm_repo_status s, close_s;
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    if (!payable_id) {
    sm_payment_record *all = NULL, *items = NULL;
    size_t n = 0, used = 0, cap = 0, i; sm_repo_status s;
    s = scan_primary(repo, SM_NS_PAYMENT_BY_ID,
                     (decode_fn)sm_payment_record_decode, sizeof(*all),
                     (void **)&all, &n);
    for (i = 0; s == SM_REPO_OK && i < n; ++i)
        if ((!payable_id || all[i].payable_id == payable_id) &&
            (!supplier_id || all[i].supplier_id == supplier_id))
            s = append((void **)&items, &used, &cap, sizeof(*items), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *count = used; return SM_REPO_OK;
    }
    *out = NULL; *count = 0;
    sm_key_begin(&prefix, SM_NS_PAYMENT_BY_PAYABLE);
    if (sm_key_add_u64(&prefix, payable_id) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, SM_NS_PAYMENT_BY_PAYABLE,
                                 sm_key_data(&prefix), sm_key_size(&prefix),
                                 &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *key; size_t key_len; sm_key_reader reader;
        uint64_t ignored_payable, id; int64_t ignored_time;
        sm_payment_record value;
        s = sm_repo_scan_key(scan, &key, &key_len);
        if (s == SM_REPO_OK &&
            (sm_key_reader_init(&reader, key, key_len,
                                SM_NS_PAYMENT_BY_PAYABLE) != SM_KEY_OK ||
             sm_key_read_u64(&reader, &ignored_payable) != SM_KEY_OK ||
             sm_key_read_i64(&reader, &ignored_time) != SM_KEY_OK ||
             sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
             sm_key_reader_done(&reader) != SM_KEY_OK)) s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = key_u64(SM_NS_PAYMENT_BY_ID, id, &primary);
        if (s == SM_REPO_OK)
            s = scan_decode_primary(scan, &primary,
                                    (decode_fn)sm_payment_record_decode, &value);
        if (s == SM_REPO_OK && (!supplier_id || value.supplier_id == supplier_id))
            s = append((void **)&items, &used, &cap, sizeof(value), &value);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *count = used; return SM_REPO_OK;
}

static int card_valid(const sm_vip_card *v) {
    return v && memchr(v->card_no, '\0', sizeof(v->card_no)) && v->card_no[0] &&
           v->balance_cents >= 0 && v->total_recharged_cents >= 0 &&
           v->card_type <= 2u && v->status <= 3u && v->created_at >= 0 &&
           v->updated_at >= 0 && v->expired_at >= 0 &&
           memchr(v->password_hash, '\0', sizeof(v->password_hash)) &&
           memchr(v->password_salt, '\0', sizeof(v->password_salt));
}

static sm_repo_status card_indexes(const sm_vip_card *v, sm_key_builder *member,
                                   sm_key_builder *status) {
    sm_key_begin(member, SM_NS_VIP_CARD_BY_MEMBER);
    sm_key_begin(status, SM_NS_VIP_CARD_BY_STATUS);
    return sm_key_add_u64(member, v->member_id) == SM_KEY_OK &&
           sm_key_add_string(member, v->card_no) == SM_KEY_OK &&
           sm_key_add_u8(status, v->status) == SM_KEY_OK &&
           sm_key_add_string(status, v->card_no) == SM_KEY_OK
               ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status card_insert(sm_repo_uow *uow, const sm_vip_card *v) {
    sm_key_builder primary, member, status_key; sm_repo_status s;
    if (!uow || !card_valid(v)) return SM_REPO_ERR_INVALID;
    s = key_string(SM_NS_VIP_CARD_BY_NO, v->card_no, &primary);
    if (s == SM_REPO_OK) s = expect_missing(uow, &primary);
    if (s == SM_REPO_OK) s = card_indexes(v, &member, &status_key);
    if (s == SM_REPO_OK)
        s = encode_put(uow, &primary, (encode_fn)sm_vip_card_encode, v);
    if (s == SM_REPO_OK)
        s = sm_repo_index_add(uow, sm_key_data(&member), sm_key_size(&member));
    if (s == SM_REPO_OK)
        s = sm_repo_index_add(uow, sm_key_data(&status_key), sm_key_size(&status_key));
    return s;
}

sm_repo_status sm_vip_card_create(sm_repo_uow *uow, const sm_vip_card *v) {
    return card_insert(uow, v);
}

sm_repo_status sm_vip_card_import(sm_repo_uow *uow, const sm_vip_card *v) {
    return card_insert(uow, v);
}

sm_repo_status sm_vip_card_get(sm_repo_uow *uow, const char *no,
                               sm_vip_card *out) {
    sm_key_builder key;
    if (!uow || !no || !*no || !out) return SM_REPO_ERR_INVALID;
    if (key_string(SM_NS_VIP_CARD_BY_NO, no, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return decode_get(uow, &key, (decode_fn)sm_vip_card_decode, out);
}

sm_repo_status sm_vip_card_update(sm_repo_uow *uow, const sm_vip_card *v) {
    sm_vip_card old; sm_key_builder primary, old_member, old_status;
    sm_key_builder new_member, new_status;
    sm_repo_status s;
    if (!uow || !card_valid(v)) return SM_REPO_ERR_INVALID;
    s = sm_vip_card_get(uow, v->card_no, &old);
    if (s == SM_REPO_OK) s = card_indexes(&old, &old_member, &old_status);
    if (s == SM_REPO_OK) s = card_indexes(v, &new_member, &new_status);
    if (s == SM_REPO_OK && (old.member_id != v->member_id)) {
        s = sm_repo_index_remove(uow, sm_key_data(&old_member), sm_key_size(&old_member));
        if (s == SM_REPO_OK)
            s = sm_repo_index_add(uow, sm_key_data(&new_member), sm_key_size(&new_member));
    }
    if (s == SM_REPO_OK && old.status != v->status) {
        s = sm_repo_index_remove(uow, sm_key_data(&old_status), sm_key_size(&old_status));
        if (s == SM_REPO_OK)
            s = sm_repo_index_add(uow, sm_key_data(&new_status), sm_key_size(&new_status));
    }
    if (s == SM_REPO_OK) s = key_string(SM_NS_VIP_CARD_BY_NO, v->card_no, &primary);
    if (s == SM_REPO_OK)
        s = encode_put(uow, &primary, (encode_fn)sm_vip_card_encode, v);
    return s;
}

sm_repo_status sm_vip_card_list(sm_repository *repo, uint64_t member_id,
                                int status, sm_vip_card **out, size_t *count) {
    sm_repo_scan *scan = NULL; sm_key_builder prefix, primary;
    sm_vip_card *items = NULL; size_t used = 0, cap = 0;
    uint8_t ns; sm_repo_status s, close_s;
    if (!repo || !out || !count || status < -1 || status > 3)
        return SM_REPO_ERR_INVALID;
    if (!member_id && status < 0)
        return scan_primary(repo, SM_NS_VIP_CARD_BY_NO,
                            (decode_fn)sm_vip_card_decode, sizeof(**out),
                            (void **)out, count);
    *out = NULL; *count = 0;
    ns = member_id ? SM_NS_VIP_CARD_BY_MEMBER : SM_NS_VIP_CARD_BY_STATUS;
    sm_key_begin(&prefix, ns);
    if ((member_id && sm_key_add_u64(&prefix, member_id) != SM_KEY_OK) ||
        (!member_id && sm_key_add_u8(&prefix, (uint8_t)status) != SM_KEY_OK))
        return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, ns, sm_key_data(&prefix),
                                 sm_key_size(&prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *key; size_t key_len; sm_key_reader reader;
        const char *no; size_t no_len; uint64_t ignored_member; uint8_t ignored_status;
        char card_no[SM_VIP_CARD_NO_CAPACITY]; sm_vip_card value;
        s = sm_repo_scan_key(scan, &key, &key_len);
        if (s == SM_REPO_OK && sm_key_reader_init(&reader, key, key_len, ns) != SM_KEY_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK && member_id && sm_key_read_u64(&reader, &ignored_member) != SM_KEY_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK && !member_id && sm_key_read_u8(&reader, &ignored_status) != SM_KEY_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK &&
            (sm_key_read_string(&reader, &no, &no_len) != SM_KEY_OK ||
             no_len >= sizeof(card_no) || sm_key_reader_done(&reader) != SM_KEY_OK))
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) { memcpy(card_no, no, no_len); card_no[no_len] = '\0'; s = key_string(SM_NS_VIP_CARD_BY_NO, card_no, &primary); }
        if (s == SM_REPO_OK)
            s = scan_decode_primary(scan, &primary,
                                    (decode_fn)sm_vip_card_decode, &value);
        if (s == SM_REPO_OK && (!member_id || status < 0 || value.status == (uint8_t)status))
            s = append((void **)&items, &used, &cap, sizeof(value), &value);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *count = used; return SM_REPO_OK;
}

static int tx_valid(const sm_vip_transaction *v, int require_id) {
    return v && (!require_id || v->id) &&
           memchr(v->card_no, '\0', sizeof(v->card_no)) && v->card_no[0] &&
           v->type <= 2u && v->amount_cents > 0 &&
           v->balance_before_cents >= 0 && v->balance_after_cents >= 0 &&
           memchr(v->operator_name, '\0', sizeof(v->operator_name)) &&
           memchr(v->remark, '\0', sizeof(v->remark)) && v->created_at >= 0;
}

static sm_repo_status tx_insert(sm_repo_uow *uow, const sm_vip_transaction *v,
                                int importing) {
    sm_key_builder primary, index;
    sm_repo_status s;
    if (!uow || !tx_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = key_u64(SM_NS_VIP_TX_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = expect_missing(uow, &primary);
    sm_key_begin(&index, SM_NS_VIP_TX_BY_CARD_TIME);
    if (s == SM_REPO_OK &&
        (sm_key_add_string(&index, v->card_no) != SM_KEY_OK ||
         sm_key_add_i64(&index, v->created_at) != SM_KEY_OK ||
         sm_key_add_u64(&index, v->id) != SM_KEY_OK)) s = SM_REPO_ERR_INVALID;
    if (s == SM_REPO_OK)
        s = encode_put(uow, &primary, (encode_fn)sm_vip_transaction_encode, v);
    if (s == SM_REPO_OK)
        s = sm_repo_index_add(uow, sm_key_data(&index), sm_key_size(&index));
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_VIP_TRANSACTION,
                                            v->id + 1u);
    return s;
}

sm_repo_status sm_vip_transaction_create(sm_repo_uow *uow,
                                         sm_vip_transaction *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !tx_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_VIP_TRANSACTION, &v->id);
    return s == SM_REPO_OK ? tx_insert(uow, v, 0) : s;
}

sm_repo_status sm_vip_transaction_import(sm_repo_uow *uow,
                                         const sm_vip_transaction *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID
                                     : tx_insert(uow, v, 1);
}

sm_repo_status sm_vip_transaction_list(sm_repository *repo, const char *card_no,
                                       sm_vip_transaction **out,
                                       size_t *count) {
    sm_repo_scan *scan = NULL; sm_key_builder prefix, primary;
    sm_vip_transaction *items = NULL; size_t used = 0, cap = 0;
    sm_repo_status s, close_s;
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    if (!card_no)
        return scan_primary(repo, SM_NS_VIP_TX_BY_ID,
                            (decode_fn)sm_vip_transaction_decode,
                            sizeof(**out), (void **)out, count);
    *out = NULL; *count = 0;
    sm_key_begin(&prefix, SM_NS_VIP_TX_BY_CARD_TIME);
    if (sm_key_add_string(&prefix, card_no) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, SM_NS_VIP_TX_BY_CARD_TIME,
                                 sm_key_data(&prefix), sm_key_size(&prefix),
                                 &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *key; size_t key_len; sm_key_reader reader;
        const char *ignored_no; size_t ignored_len; int64_t ignored_time;
        uint64_t id; sm_vip_transaction value;
        s = sm_repo_scan_key(scan, &key, &key_len);
        if (s == SM_REPO_OK &&
            (sm_key_reader_init(&reader, key, key_len,
                                SM_NS_VIP_TX_BY_CARD_TIME) != SM_KEY_OK ||
             sm_key_read_string(&reader, &ignored_no, &ignored_len) != SM_KEY_OK ||
             sm_key_read_i64(&reader, &ignored_time) != SM_KEY_OK ||
             sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
             sm_key_reader_done(&reader) != SM_KEY_OK)) s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = key_u64(SM_NS_VIP_TX_BY_ID, id, &primary);
        if (s == SM_REPO_OK)
            s = scan_decode_primary(scan, &primary,
                                    (decode_fn)sm_vip_transaction_decode, &value);
        if (s == SM_REPO_OK)
            s = append((void **)&items, &used, &cap, sizeof(value), &value);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(items); return s; }
    *out = items; *count = used; return SM_REPO_OK;
}

void sm_finance_array_free(void *array) { free(array); }
