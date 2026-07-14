#include "repo/sm_operations_repository.h"

#include "repo/sm_operations_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdlib.h>
#include <string.h>

typedef sm_codec_status (*dec_fn)(const void *, size_t, void *);
typedef sm_codec_status (*enc_fn)(const void *, uint8_t **, size_t *);

static sm_repo_status id_key(uint8_t ns, uint64_t id, sm_key_builder *key) {
    sm_key_begin(key, ns);
    return sm_key_add_u64(key, id) == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status getv(sm_repo_uow *uow, const sm_key_builder *key,
                           dec_fn decode, void *out) {
    void *value = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &value, &len);
    if (s == SM_REPO_OK && decode(value, len, out) != SM_CODEC_OK)
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value); return s;
}

static sm_repo_status putv(sm_repo_uow *uow, const sm_key_builder *key,
                           enc_fn encode, const void *record) {
    uint8_t *value = NULL; size_t len = 0; sm_repo_status s;
    sm_codec_status c = encode(record, &value, &len);
    if (c != SM_CODEC_OK) { free(value); return c == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM : SM_REPO_ERR_INVALID; }
    s = sm_repo_put(uow, sm_key_data(key), sm_key_size(key), value, len);
    free(value); return s;
}

static sm_repo_status absent(sm_repo_uow *uow, const sm_key_builder *key) {
    void *value = NULL; size_t len = 0;
    sm_repo_status s = sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                                   &value, &len);
    sm_repo_value_free(value);
    return s == SM_REPO_NOT_FOUND ? SM_REPO_OK : s == SM_REPO_OK ? SM_REPO_CONFLICT : s;
}

static sm_repo_status add(void **array, size_t *count, size_t *capacity,
                          size_t width, const void *record) {
    void *next; size_t cap;
    if (*count == *capacity) {
        cap = *capacity ? *capacity * 2u : 16u;
        if (cap < *capacity || cap > SIZE_MAX / width) return SM_REPO_ERR_FULL;
        next = realloc(*array, cap * width); if (!next) return SM_REPO_ERR_NOMEM;
        *array = next; *capacity = cap;
    }
    memcpy((uint8_t *)*array + *count * width, record, width); ++*count;
    return SM_REPO_OK;
}

static sm_repo_status scan_all(sm_repository *repo, uint8_t ns, dec_fn decode,
                               size_t width, void **out, size_t *count) {
    sm_key_builder prefix; sm_repo_scan *scan = NULL; void *array = NULL;
    size_t used = 0, cap = 0; sm_repo_status s, close_s;
    *out = NULL; *count = 0; sm_key_begin(&prefix, ns);
    s = sm_repo_scan_open_prefix(repo, ns, sm_key_data(&prefix),
                                 sm_key_size(&prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *value; size_t len; uint8_t record[4096];
        if (width > sizeof(record)) { s = SM_REPO_ERR_INVALID; break; }
        s = sm_repo_scan_value(scan, &value, &len);
        if (s == SM_REPO_OK && decode(value, len, record) != SM_CODEC_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = add(&array, &used, &cap, width, record);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(array); return s; }
    *out = array; *count = used; return SM_REPO_OK;
}

static uint64_t index_tail_id(const void *key, size_t len) {
    const uint8_t *p = key; uint64_t id = 0; size_t i;
    if (len < 8u) return 0;
    p += len - 8u;
    for (i = 0; i < 8u; ++i) id = (id << 8) | p[i];
    return id;
}

static sm_repo_status scan_index(sm_repository *repo, uint8_t index_ns,
                                 const sm_key_builder *prefix,
                                 uint8_t primary_ns, int value_is_primary,
                                 dec_fn decode, size_t width,
                                 void **out, size_t *count) {
    sm_repo_scan *scan = NULL; void *array = NULL; size_t used = 0, cap = 0;
    sm_repo_status s, close_s;
    *out = NULL; *count = 0;
    s = sm_repo_scan_open_prefix(repo, index_ns, sm_key_data(prefix),
                                 sm_key_size(prefix), &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        sm_key_builder primary; const void *key; size_t key_len;
        void *index_value = NULL, *record_value = NULL;
        size_t index_len = 0, record_len = 0; uint64_t id = 0;
        uint8_t record[4096];
        if (width > sizeof(record)) { s = SM_REPO_ERR_INVALID; break; }
        if (value_is_primary) {
            const void *value;
            s = sm_repo_scan_value(scan, &value, &index_len);
            if (s == SM_REPO_OK && index_len) {
                index_value = malloc(index_len);
                if (!index_value) s = SM_REPO_ERR_NOMEM;
                else memcpy(index_value, value, index_len);
            }
            if (s == SM_REPO_OK) {
                sm_key_reader reader;
                if (!index_value || sm_key_reader_init(&reader, index_value,
                        index_len, primary_ns) != SM_KEY_OK ||
                    sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
                    sm_key_reader_done(&reader) != SM_KEY_OK)
                    s = SM_REPO_ERR_CORRUPT;
            }
        } else {
            s = sm_repo_scan_key(scan, &key, &key_len);
            if (s == SM_REPO_OK) id = index_tail_id(key, key_len);
            if (s == SM_REPO_OK && !id) s = SM_REPO_ERR_CORRUPT;
        }
        if (s == SM_REPO_OK) s = id_key(primary_ns, id, &primary);
        if (s == SM_REPO_OK)
            s = sm_repo_scan_get(scan, sm_key_data(&primary),
                                 sm_key_size(&primary), &record_value,
                                 &record_len);
        if (s == SM_REPO_OK && decode(record_value, record_len, record) != SM_CODEC_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK) s = add(&array, &used, &cap, width, record);
        free(index_value); sm_repo_value_free(record_value);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(array); return s; }
    *out = array; *count = used; return SM_REPO_OK;
}

static int txt(const char *value, size_t cap, int required) {
    return value && memchr(value, '\0', cap) && (!required || value[0]);
}

static int promo_valid(const sm_promotion_record *v, int require_id) {
    return v && (!require_id || v->id) && txt(v->name, sizeof(v->name), 1) &&
           txt(v->product_id, sizeof(v->product_id), 0) && v->type <= 4u &&
           v->discount_bps <= 10000u && v->nth_discount_bps <= 10000u &&
           v->threshold_cents >= 0 && v->discount_cents >= 0 &&
           v->member_price_cents >= 0 && v->start_at >= 0 &&
           v->end_at >= v->start_at && v->status <= 1u;
}

static sm_repo_status promo_indexes(const sm_promotion_record *v,
                                    sm_key_builder *product,
                                    sm_key_builder *active) {
    sm_key_begin(product, SM_NS_PROMOTION_BY_PRODUCT);
    sm_key_begin(active, SM_NS_PROMOTION_BY_ACTIVE_TIME);
    return sm_key_add_string(product, v->product_id) == SM_KEY_OK &&
           sm_key_add_u32(product, UINT32_MAX - v->priority) == SM_KEY_OK &&
           sm_key_add_u64(product, v->id) == SM_KEY_OK &&
           sm_key_add_u8(active, v->status) == SM_KEY_OK &&
           sm_key_add_i64(active, v->start_at) == SM_KEY_OK &&
           sm_key_add_u64(active, v->id) == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status promo_insert(sm_repo_uow *uow,
                                   const sm_promotion_record *v,
                                   int importing) {
    sm_key_builder primary, product, active; sm_repo_status s;
    if (!uow || !promo_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = id_key(SM_NS_PROMOTION_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = absent(uow, &primary);
    if (s == SM_REPO_OK) s = promo_indexes(v, &product, &active);
    if (s == SM_REPO_OK) s = putv(uow, &primary, (enc_fn)sm_promotion_record_encode, v);
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&product), sm_key_size(&product));
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&active), sm_key_size(&active));
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_PROMOTION, v->id + 1u);
    return s;
}

sm_repo_status sm_promotion_record_create(sm_repo_uow *uow,
                                          sm_promotion_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !promo_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_PROMOTION, &v->id);
    return s == SM_REPO_OK ? promo_insert(uow, v, 0) : s;
}

sm_repo_status sm_promotion_record_import(sm_repo_uow *uow,
                                          const sm_promotion_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID : promo_insert(uow, v, 1);
}

sm_repo_status sm_promotion_record_get(sm_repo_uow *uow, uint64_t id,
                                       sm_promotion_record *out) {
    sm_key_builder key;
    if (!uow || !id || !out || id_key(SM_NS_PROMOTION_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return getv(uow, &key, (dec_fn)sm_promotion_record_decode, out);
}

sm_repo_status sm_promotion_record_update(sm_repo_uow *uow,
                                          const sm_promotion_record *v) {
    sm_promotion_record old; sm_key_builder primary, op, oa, np, na; sm_repo_status s;
    if (!uow || !promo_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_promotion_record_get(uow, v->id, &old);
    if (s == SM_REPO_OK) s = promo_indexes(&old, &op, &oa);
    if (s == SM_REPO_OK) s = promo_indexes(v, &np, &na);
    if (s == SM_REPO_OK) s = sm_repo_index_remove(uow, sm_key_data(&op), sm_key_size(&op));
    if (s == SM_REPO_OK) s = sm_repo_index_remove(uow, sm_key_data(&oa), sm_key_size(&oa));
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&np), sm_key_size(&np));
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&na), sm_key_size(&na));
    if (s == SM_REPO_OK) s = id_key(SM_NS_PROMOTION_BY_ID, v->id, &primary);
    return s == SM_REPO_OK ? putv(uow, &primary,
        (enc_fn)sm_promotion_record_encode, v) : s;
}

sm_repo_status sm_promotion_record_delete(sm_repo_uow *uow, uint64_t id) {
    sm_promotion_record old; sm_key_builder primary, product, active; sm_repo_status s;
    if (!uow || !id) return SM_REPO_ERR_INVALID;
    s = sm_promotion_record_get(uow, id, &old);
    if (s == SM_REPO_OK) s = promo_indexes(&old, &product, &active);
    if (s == SM_REPO_OK) s = sm_repo_index_remove(uow, sm_key_data(&product), sm_key_size(&product));
    if (s == SM_REPO_OK) s = sm_repo_index_remove(uow, sm_key_data(&active), sm_key_size(&active));
    if (s == SM_REPO_OK) s = id_key(SM_NS_PROMOTION_BY_ID, id, &primary);
    return s == SM_REPO_OK ? sm_repo_delete(uow, sm_key_data(&primary), sm_key_size(&primary)) : s;
}

sm_repo_status sm_promotion_record_list(sm_repository *repo,
                                        const char *product, int64_t at,
                                        sm_promotion_record **out,
                                        size_t *count) {
    sm_promotion_record *all = NULL, *filtered = NULL; size_t n = 0, used = 0, cap = 0, i;
    sm_repo_status s;
    if (!repo || !out || !count || at < 0) return SM_REPO_ERR_INVALID;
    if (product) {
        sm_key_builder prefix; sm_key_begin(&prefix, SM_NS_PROMOTION_BY_PRODUCT);
        if (sm_key_add_string(&prefix, product) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
        s = scan_index(repo, SM_NS_PROMOTION_BY_PRODUCT, &prefix,
                       SM_NS_PROMOTION_BY_ID, 0,
                       (dec_fn)sm_promotion_record_decode, sizeof(*all),
                       (void **)&all, &n);
    } else if (at) {
        sm_key_builder prefix; sm_key_begin(&prefix, SM_NS_PROMOTION_BY_ACTIVE_TIME);
        if (sm_key_add_u8(&prefix, 1u) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
        s = scan_index(repo, SM_NS_PROMOTION_BY_ACTIVE_TIME, &prefix,
                       SM_NS_PROMOTION_BY_ID, 0,
                       (dec_fn)sm_promotion_record_decode, sizeof(*all),
                       (void **)&all, &n);
    } else {
        s = scan_all(repo, SM_NS_PROMOTION_BY_ID,
                     (dec_fn)sm_promotion_record_decode, sizeof(*all),
                     (void **)&all, &n);
    }
    if ((!product && at == 0) || s != SM_REPO_OK) { *out = all; *count = n; return s; }
    for (i = 0; i < n && s == SM_REPO_OK; ++i)
        if ((!product || strcmp(all[i].product_id, product) == 0) &&
            (!at || (all[i].status == 1u && all[i].start_at <= at && all[i].end_at >= at)))
            s = add((void **)&filtered, &used, &cap, sizeof(*filtered), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(filtered); return s; }
    *out = filtered; *count = used; return SM_REPO_OK;
}

static int schedule_valid(const sm_schedule_record *v, int require_id) {
    size_t i;
    if (!v || (require_id && !v->id) || !v->employee_id || v->year < 1970u ||
        v->week < 1u || v->week > 53u || v->created_at < 0) return 0;
    for (i = 0; i < 7u; ++i) if (!memchr(v->shifts[i], '\0', 4u)) return 0;
    return 1;
}

static sm_repo_status schedule_index(uint64_t employee, uint32_t year,
                                     uint8_t week, sm_key_builder *key) {
    sm_key_begin(key, SM_NS_SCHEDULE_BY_EMPLOYEE_WEEK);
    return sm_key_add_u64(key, employee) == SM_KEY_OK &&
           sm_key_add_u32(key, year) == SM_KEY_OK &&
           sm_key_add_u8(key, week) == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status schedule_insert(sm_repo_uow *uow,
                                      const sm_schedule_record *v,
                                      int importing) {
    sm_key_builder primary, index; sm_repo_status s;
    if (!uow || !schedule_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = id_key(SM_NS_SCHEDULE_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = absent(uow, &primary);
    if (s == SM_REPO_OK) s = schedule_index(v->employee_id, v->year, v->week, &index);
    if (s == SM_REPO_OK)
        s = sm_repo_unique_claim(uow, sm_key_data(&index), sm_key_size(&index),
                                 sm_key_data(&primary), sm_key_size(&primary));
    if (s == SM_REPO_OK) s = putv(uow, &primary, (enc_fn)sm_schedule_record_encode, v);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_SCHEDULE, v->id + 1u);
    return s;
}

sm_repo_status sm_schedule_record_create(sm_repo_uow *uow,
                                         sm_schedule_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !schedule_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_SCHEDULE, &v->id);
    return s == SM_REPO_OK ? schedule_insert(uow, v, 0) : s;
}

sm_repo_status sm_schedule_record_import(sm_repo_uow *uow,
                                         const sm_schedule_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID : schedule_insert(uow, v, 1);
}

sm_repo_status sm_schedule_record_get(sm_repo_uow *uow, uint64_t id,
                                      sm_schedule_record *out) {
    sm_key_builder key;
    if (!uow || !id || !out || id_key(SM_NS_SCHEDULE_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return getv(uow, &key, (dec_fn)sm_schedule_record_decode, out);
}

static sm_repo_status unique_target(sm_repo_uow *uow, const sm_key_builder *index,
                                    uint8_t primary_ns, sm_key_builder *primary) {
    void *value = NULL; size_t len = 0; sm_repo_status s;
    s = sm_repo_get(uow, sm_key_data(index), sm_key_size(index), &value, &len);
    if (s == SM_REPO_OK) {
        sm_key_reader r; uint64_t id;
        if (sm_key_reader_init(&r, value, len, primary_ns) != SM_KEY_OK ||
            sm_key_read_u64(&r, &id) != SM_KEY_OK || sm_key_reader_done(&r) != SM_KEY_OK)
            s = SM_REPO_ERR_CORRUPT;
        else s = id_key(primary_ns, id, primary);
    }
    sm_repo_value_free(value); return s;
}

sm_repo_status sm_schedule_record_find(sm_repo_uow *uow, uint64_t employee,
                                       uint32_t year, uint8_t week,
                                       sm_schedule_record *out) {
    sm_key_builder index, primary; sm_repo_status s;
    if (!uow || !out || schedule_index(employee, year, week, &index) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    s = unique_target(uow, &index, SM_NS_SCHEDULE_BY_ID, &primary);
    return s == SM_REPO_OK ? getv(uow, &primary,
        (dec_fn)sm_schedule_record_decode, out) : s;
}

sm_repo_status sm_schedule_record_update(sm_repo_uow *uow,
                                         const sm_schedule_record *v) {
    sm_schedule_record old; sm_key_builder primary; sm_repo_status s;
    if (!uow || !schedule_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_schedule_record_get(uow, v->id, &old);
    if (s == SM_REPO_OK && (old.employee_id != v->employee_id || old.year != v->year || old.week != v->week))
        s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = id_key(SM_NS_SCHEDULE_BY_ID, v->id, &primary);
    return s == SM_REPO_OK ? putv(uow, &primary,
        (enc_fn)sm_schedule_record_encode, v) : s;
}

sm_repo_status sm_schedule_record_delete(sm_repo_uow *uow, uint64_t id) {
    sm_schedule_record old; sm_key_builder primary, index; sm_repo_status s;
    if (!uow || !id) return SM_REPO_ERR_INVALID;
    s = sm_schedule_record_get(uow, id, &old);
    if (s == SM_REPO_OK) s = schedule_index(old.employee_id, old.year, old.week, &index);
    if (s == SM_REPO_OK) s = id_key(SM_NS_SCHEDULE_BY_ID, id, &primary);
    if (s == SM_REPO_OK)
        s = sm_repo_unique_release(uow, sm_key_data(&index), sm_key_size(&index),
                                   sm_key_data(&primary), sm_key_size(&primary));
    return s == SM_REPO_OK ? sm_repo_delete(uow, sm_key_data(&primary), sm_key_size(&primary)) : s;
}

sm_repo_status sm_schedule_record_list(sm_repository *repo, uint64_t employee,
                                       uint32_t year, uint8_t week,
                                       sm_schedule_record **out,
                                       size_t *count) {
    sm_schedule_record *all = NULL, *filtered = NULL; size_t n = 0, used = 0, cap = 0, i;
    sm_repo_status s;
    if (!repo || !out || !count || (week > 53u)) return SM_REPO_ERR_INVALID;
    if (employee) {
        sm_key_builder prefix; sm_key_begin(&prefix, SM_NS_SCHEDULE_BY_EMPLOYEE_WEEK);
        if (sm_key_add_u64(&prefix, employee) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
        s = scan_index(repo, SM_NS_SCHEDULE_BY_EMPLOYEE_WEEK, &prefix,
                       SM_NS_SCHEDULE_BY_ID, 1,
                       (dec_fn)sm_schedule_record_decode, sizeof(*all),
                       (void **)&all, &n);
    } else {
        s = scan_all(repo, SM_NS_SCHEDULE_BY_ID,
                     (dec_fn)sm_schedule_record_decode, sizeof(*all),
                     (void **)&all, &n);
    }
    if (!employee && !year && !week) { *out = all; *count = n; return s; }
    for (i = 0; i < n && s == SM_REPO_OK; ++i)
        if ((!employee || all[i].employee_id == employee) &&
            (!year || all[i].year == year) && (!week || all[i].week == week))
            s = add((void **)&filtered, &used, &cap, sizeof(*filtered), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(filtered); return s; }
    *out = filtered; *count = used; return SM_REPO_OK;
}

static int settlement_valid(const sm_settlement_record *v, int require_id) {
    return v && (!require_id || v->id) && v->cashier_id &&
           txt(v->cashier_name, sizeof(v->cashier_name), 1) &&
           txt(v->remark, sizeof(v->remark), 0) && v->business_date >= 0 &&
           v->shift_start >= 0 && v->shift_end >= v->shift_start &&
           v->status <= 2u && v->created_at >= 0 && v->confirmed_at >= 0 &&
           v->system_total_cents == v->system_cash_cents + v->system_online_cents &&
           v->actual_total_cents == v->actual_cash_cents + v->actual_online_cents &&
           v->cash_diff_cents == v->actual_cash_cents - v->system_cash_cents &&
           v->online_diff_cents == v->actual_online_cents - v->system_online_cents &&
           v->total_diff_cents == v->actual_total_cents - v->system_total_cents;
}

static sm_repo_status settlement_index(uint64_t cashier, int64_t date,
                                       sm_key_builder *key) {
    sm_key_begin(key, SM_NS_SETTLEMENT_BY_CASHIER_DATE);
    return sm_key_add_u64(key, cashier) == SM_KEY_OK &&
           sm_key_add_i64(key, date) == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status settlement_insert(sm_repo_uow *uow,
                                        const sm_settlement_record *v,
                                        int importing) {
    sm_key_builder primary, index; sm_repo_status s;
    if (!uow || !settlement_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = id_key(SM_NS_SETTLEMENT_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = absent(uow, &primary);
    if (s == SM_REPO_OK) s = settlement_index(v->cashier_id, v->business_date, &index);
    if (s == SM_REPO_OK)
        s = sm_repo_unique_claim(uow, sm_key_data(&index), sm_key_size(&index),
                                 sm_key_data(&primary), sm_key_size(&primary));
    if (s == SM_REPO_OK) s = putv(uow, &primary, (enc_fn)sm_settlement_record_encode, v);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_SETTLEMENT, v->id + 1u);
    return s;
}

sm_repo_status sm_settlement_record_create(sm_repo_uow *uow,
                                           sm_settlement_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !settlement_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_SETTLEMENT, &v->id);
    return s == SM_REPO_OK ? settlement_insert(uow, v, 0) : s;
}

sm_repo_status sm_settlement_record_import(sm_repo_uow *uow,
                                           const sm_settlement_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID : settlement_insert(uow, v, 1);
}

sm_repo_status sm_settlement_record_get(sm_repo_uow *uow, uint64_t id,
                                        sm_settlement_record *out) {
    sm_key_builder key;
    if (!uow || !id || !out || id_key(SM_NS_SETTLEMENT_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return getv(uow, &key, (dec_fn)sm_settlement_record_decode, out);
}

sm_repo_status sm_settlement_record_find(sm_repo_uow *uow, uint64_t cashier,
                                         int64_t date,
                                         sm_settlement_record *out) {
    sm_key_builder index, primary; sm_repo_status s;
    if (!uow || !out || settlement_index(cashier, date, &index) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    s = unique_target(uow, &index, SM_NS_SETTLEMENT_BY_ID, &primary);
    return s == SM_REPO_OK ? getv(uow, &primary,
        (dec_fn)sm_settlement_record_decode, out) : s;
}

sm_repo_status sm_settlement_record_update(sm_repo_uow *uow,
                                           const sm_settlement_record *v) {
    sm_settlement_record old; sm_key_builder primary; sm_repo_status s;
    if (!uow || !settlement_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_settlement_record_get(uow, v->id, &old);
    if (s == SM_REPO_OK && (old.cashier_id != v->cashier_id || old.business_date != v->business_date))
        s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = id_key(SM_NS_SETTLEMENT_BY_ID, v->id, &primary);
    return s == SM_REPO_OK ? putv(uow, &primary,
        (enc_fn)sm_settlement_record_encode, v) : s;
}

sm_repo_status sm_settlement_record_list(sm_repository *repo, uint64_t cashier,
                                         int64_t date,
                                         sm_settlement_record **out,
                                         size_t *count) {
    sm_settlement_record *all = NULL, *filtered = NULL; size_t n = 0, used = 0, cap = 0, i;
    sm_repo_status s;
    if (!repo || !out || !count || date < 0) return SM_REPO_ERR_INVALID;
    if (cashier) {
        sm_key_builder prefix; sm_key_begin(&prefix, SM_NS_SETTLEMENT_BY_CASHIER_DATE);
        if (sm_key_add_u64(&prefix, cashier) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
        s = scan_index(repo, SM_NS_SETTLEMENT_BY_CASHIER_DATE, &prefix,
                       SM_NS_SETTLEMENT_BY_ID, 1,
                       (dec_fn)sm_settlement_record_decode, sizeof(*all),
                       (void **)&all, &n);
    } else {
        s = scan_all(repo, SM_NS_SETTLEMENT_BY_ID,
                     (dec_fn)sm_settlement_record_decode, sizeof(*all),
                     (void **)&all, &n);
    }
    if (!cashier && !date) { *out = all; *count = n; return s; }
    for (i = 0; i < n && s == SM_REPO_OK; ++i)
        if ((!cashier || all[i].cashier_id == cashier) && (!date || all[i].business_date == date))
            s = add((void **)&filtered, &used, &cap, sizeof(*filtered), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(filtered); return s; }
    *out = filtered; *count = used; return SM_REPO_OK;
}

static int audit_valid(const sm_audit_record *v, int require_id) {
    return v && (!require_id || v->id) && txt(v->type, sizeof(v->type), 1) &&
           txt(v->operation, sizeof(v->operation), 1) &&
           txt(v->data, sizeof(v->data), 0) && v->created_at >= 0;
}

static sm_repo_status audit_insert(sm_repo_uow *uow, const sm_audit_record *v,
                                   int importing) {
    sm_key_builder primary, time_key, op_key, type_key; sm_repo_status s;
    if (!uow || !audit_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = id_key(SM_NS_AUDIT_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = absent(uow, &primary);
    sm_key_begin(&time_key, SM_NS_AUDIT_BY_TIME);
    sm_key_begin(&op_key, SM_NS_AUDIT_BY_OPERATOR_TIME);
    sm_key_begin(&type_key, SM_NS_AUDIT_BY_TYPE_TIME);
    if (s == SM_REPO_OK && (sm_key_add_i64(&time_key, v->created_at) != SM_KEY_OK || sm_key_add_u64(&time_key, v->id) != SM_KEY_OK ||
        sm_key_add_u64(&op_key, v->operator_id) != SM_KEY_OK || sm_key_add_i64(&op_key, v->created_at) != SM_KEY_OK || sm_key_add_u64(&op_key, v->id) != SM_KEY_OK ||
        sm_key_add_string(&type_key, v->type) != SM_KEY_OK || sm_key_add_i64(&type_key, v->created_at) != SM_KEY_OK || sm_key_add_u64(&type_key, v->id) != SM_KEY_OK))
        s = SM_REPO_ERR_INVALID;
    if (s == SM_REPO_OK) s = putv(uow, &primary, (enc_fn)sm_audit_record_encode, v);
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&time_key), sm_key_size(&time_key));
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&op_key), sm_key_size(&op_key));
    if (s == SM_REPO_OK) s = sm_repo_index_add(uow, sm_key_data(&type_key), sm_key_size(&type_key));
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_AUDIT, v->id + 1u);
    return s;
}

sm_repo_status sm_audit_record_create(sm_repo_uow *uow, sm_audit_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !audit_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_AUDIT, &v->id);
    return s == SM_REPO_OK ? audit_insert(uow, v, 0) : s;
}

sm_repo_status sm_audit_record_import(sm_repo_uow *uow,
                                      const sm_audit_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID : audit_insert(uow, v, 1);
}

sm_repo_status sm_audit_record_list(sm_repository *repo, const char *type,
                                    uint64_t operator_id, int64_t start,
                                    int64_t end, sm_audit_record **out,
                                    size_t *count) {
    sm_audit_record *all = NULL, *filtered = NULL; size_t n = 0, used = 0, cap = 0, i;
    sm_repo_status s;
    if (!repo || !out || !count || start < 0 || end < start) return SM_REPO_ERR_INVALID;
    if (type) {
        sm_key_builder prefix; sm_key_begin(&prefix, SM_NS_AUDIT_BY_TYPE_TIME);
        if (sm_key_add_string(&prefix, type) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
        s = scan_index(repo, SM_NS_AUDIT_BY_TYPE_TIME, &prefix,
                       SM_NS_AUDIT_BY_ID, 0,
                       (dec_fn)sm_audit_record_decode, sizeof(*all),
                       (void **)&all, &n);
    } else if (operator_id) {
        sm_key_builder prefix; sm_key_begin(&prefix, SM_NS_AUDIT_BY_OPERATOR_TIME);
        if (sm_key_add_u64(&prefix, operator_id) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
        s = scan_index(repo, SM_NS_AUDIT_BY_OPERATOR_TIME, &prefix,
                       SM_NS_AUDIT_BY_ID, 0,
                       (dec_fn)sm_audit_record_decode, sizeof(*all),
                       (void **)&all, &n);
    } else if (start || end) {
        sm_key_builder prefix; sm_key_begin(&prefix, SM_NS_AUDIT_BY_TIME);
        s = scan_index(repo, SM_NS_AUDIT_BY_TIME, &prefix,
                       SM_NS_AUDIT_BY_ID, 0,
                       (dec_fn)sm_audit_record_decode, sizeof(*all),
                       (void **)&all, &n);
    } else {
        s = scan_all(repo, SM_NS_AUDIT_BY_ID,
                     (dec_fn)sm_audit_record_decode, sizeof(*all),
                     (void **)&all, &n);
    }
    if (!type && !operator_id && !start && !end) { *out = all; *count = n; return s; }
    for (i = 0; i < n && s == SM_REPO_OK; ++i)
        if ((!type || strcmp(all[i].type, type) == 0) &&
            (!operator_id || all[i].operator_id == operator_id) &&
            (!start || all[i].created_at >= start) && (!end || all[i].created_at <= end))
            s = add((void **)&filtered, &used, &cap, sizeof(*filtered), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(filtered); return s; }
    *out = filtered; *count = used; return SM_REPO_OK;
}

static int combo_valid(const sm_combo_record *v, int require_id) {
    return v && (!require_id || v->id) && txt(v->name, sizeof(v->name), 1) &&
           txt(v->barcode, sizeof(v->barcode), 1) && v->price_cents >= 0 &&
           v->cost_cents >= 0 && v->status <= 1u && v->created_at >= 0 &&
           v->updated_at >= 0;
}

static sm_repo_status combo_barcode_key(const char *barcode,
                                        sm_key_builder *key) {
    sm_key_begin(key, SM_NS_COMBO_BY_BARCODE);
    return sm_key_add_string(key, barcode) == SM_KEY_OK ? SM_REPO_OK
                                                        : SM_REPO_ERR_INVALID;
}

static sm_repo_status combo_insert(sm_repo_uow *uow, const sm_combo_record *v,
                                   int importing) {
    sm_key_builder primary, barcode; sm_repo_status s;
    if (!uow || !combo_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = id_key(SM_NS_COMBO_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = absent(uow, &primary);
    if (s == SM_REPO_OK) s = combo_barcode_key(v->barcode, &barcode);
    if (s == SM_REPO_OK)
        s = sm_repo_unique_claim(uow, sm_key_data(&barcode),
                                 sm_key_size(&barcode), sm_key_data(&primary),
                                 sm_key_size(&primary));
    if (s == SM_REPO_OK)
        s = putv(uow, &primary, (enc_fn)sm_combo_record_encode, v);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_COMBO, v->id + 1u);
    return s;
}

sm_repo_status sm_combo_record_create(sm_repo_uow *uow, sm_combo_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !combo_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_COMBO, &v->id);
    return s == SM_REPO_OK ? combo_insert(uow, v, 0) : s;
}

sm_repo_status sm_combo_record_import(sm_repo_uow *uow,
                                      const sm_combo_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID
                                     : combo_insert(uow, v, 1);
}

sm_repo_status sm_combo_record_get(sm_repo_uow *uow, uint64_t id,
                                   sm_combo_record *out) {
    sm_key_builder key;
    if (!uow || !id || !out || id_key(SM_NS_COMBO_BY_ID, id, &key) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    return getv(uow, &key, (dec_fn)sm_combo_record_decode, out);
}

sm_repo_status sm_combo_record_find_barcode(sm_repo_uow *uow,
                                            const char *barcode,
                                            sm_combo_record *out) {
    sm_key_builder index, primary; sm_repo_status s;
    if (!uow || !barcode || !*barcode || !out ||
        combo_barcode_key(barcode, &index) != SM_REPO_OK)
        return SM_REPO_ERR_INVALID;
    s = unique_target(uow, &index, SM_NS_COMBO_BY_ID, &primary);
    return s == SM_REPO_OK ? getv(uow, &primary,
        (dec_fn)sm_combo_record_decode, out) : s;
}

sm_repo_status sm_combo_record_update(sm_repo_uow *uow,
                                      const sm_combo_record *v) {
    sm_combo_record old; sm_key_builder primary, old_barcode, new_barcode;
    sm_repo_status s;
    if (!uow || !combo_valid(v, 1)) return SM_REPO_ERR_INVALID;
    s = sm_combo_record_get(uow, v->id, &old);
    if (s == SM_REPO_OK) s = id_key(SM_NS_COMBO_BY_ID, v->id, &primary);
    if (s == SM_REPO_OK) s = combo_barcode_key(old.barcode, &old_barcode);
    if (s == SM_REPO_OK) s = combo_barcode_key(v->barcode, &new_barcode);
    if (s == SM_REPO_OK && strcmp(old.barcode, v->barcode) != 0) {
        s = sm_repo_unique_claim(uow, sm_key_data(&new_barcode),
                                 sm_key_size(&new_barcode),
                                 sm_key_data(&primary), sm_key_size(&primary));
        if (s == SM_REPO_OK)
            s = sm_repo_unique_release(uow, sm_key_data(&old_barcode),
                                       sm_key_size(&old_barcode),
                                       sm_key_data(&primary), sm_key_size(&primary));
    }
    return s == SM_REPO_OK ? putv(uow, &primary,
        (enc_fn)sm_combo_record_encode, v) : s;
}

sm_repo_status sm_combo_record_delete(sm_repo_uow *uow, uint64_t id) {
    sm_combo_record old; sm_key_builder primary, barcode; sm_repo_status s;
    if (!uow || !id) return SM_REPO_ERR_INVALID;
    s = sm_combo_record_get(uow, id, &old);
    if (s == SM_REPO_OK) s = id_key(SM_NS_COMBO_BY_ID, id, &primary);
    if (s == SM_REPO_OK) s = combo_barcode_key(old.barcode, &barcode);
    if (s == SM_REPO_OK)
        s = sm_repo_unique_release(uow, sm_key_data(&barcode),
                                   sm_key_size(&barcode),
                                   sm_key_data(&primary), sm_key_size(&primary));
    return s == SM_REPO_OK ? sm_repo_delete(uow, sm_key_data(&primary),
                                             sm_key_size(&primary)) : s;
}

sm_repo_status sm_combo_record_list(sm_repository *repo, int status,
                                    sm_combo_record **out, size_t *count) {
    sm_combo_record *all = NULL, *filtered = NULL; size_t n = 0, used = 0, cap = 0, i;
    sm_repo_status s;
    if (!repo || !out || !count || status < -1 || status > 1)
        return SM_REPO_ERR_INVALID;
    s = scan_all(repo, SM_NS_COMBO_BY_ID, (dec_fn)sm_combo_record_decode,
                 sizeof(*all), (void **)&all, &n);
    if (status < 0 || s != SM_REPO_OK) { *out = all; *count = n; return s; }
    for (i = 0; i < n && s == SM_REPO_OK; ++i)
        if (all[i].status == (uint8_t)status)
            s = add((void **)&filtered, &used, &cap, sizeof(*filtered), &all[i]);
    free(all); if (s != SM_REPO_OK) { free(filtered); return s; }
    *out = filtered; *count = used; return SM_REPO_OK;
}

static int combo_item_valid(const sm_combo_item_record *v, int require_id) {
    return v && (!require_id || v->id) && v->combo_id && v->quantity &&
           v->ratio_bps <= 10000u && txt(v->product_id, sizeof(v->product_id), 1) &&
           txt(v->product_name, sizeof(v->product_name), 1);
}

static sm_repo_status combo_item_insert(sm_repo_uow *uow,
                                        const sm_combo_item_record *v,
                                        int importing) {
    sm_key_builder key; sm_repo_status s;
    if (!uow || !combo_item_valid(v, 1)) return SM_REPO_ERR_INVALID;
    sm_key_begin(&key, SM_NS_COMBO_ITEM_BY_COMBO);
    if (sm_key_add_u64(&key, v->combo_id) != SM_KEY_OK ||
        sm_key_add_u64(&key, v->id) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
    s = absent(uow, &key);
    if (s == SM_REPO_OK)
        s = putv(uow, &key, (enc_fn)sm_combo_item_record_encode, v);
    if (s == SM_REPO_OK && importing)
        s = sm_repo_counter_ensure_at_least(uow, SM_COUNTER_COMBO_ITEM, v->id + 1u);
    return s;
}

sm_repo_status sm_combo_item_record_create(sm_repo_uow *uow,
                                           sm_combo_item_record *v) {
    sm_repo_status s;
    if (!uow || !v || v->id || !combo_item_valid(v, 0)) return SM_REPO_ERR_INVALID;
    s = sm_repo_counter_next(uow, SM_COUNTER_COMBO_ITEM, &v->id);
    return s == SM_REPO_OK ? combo_item_insert(uow, v, 0) : s;
}

sm_repo_status sm_combo_item_record_import(sm_repo_uow *uow,
                                           const sm_combo_item_record *v) {
    return !v || v->id == UINT64_MAX ? SM_REPO_ERR_INVALID
                                     : combo_item_insert(uow, v, 1);
}

sm_repo_status sm_combo_item_record_list(sm_repository *repo, uint64_t combo,
                                         sm_combo_item_record **out,
                                         size_t *count) {
    sm_key_builder prefix; sm_repo_scan *scan = NULL;
    sm_combo_item_record *values = NULL; size_t used = 0, cap = 0;
    sm_repo_status s, close_s;
    if (!repo || !combo || !out || !count) return SM_REPO_ERR_INVALID;
    *out = NULL; *count = 0;
    sm_key_begin(&prefix, SM_NS_COMBO_ITEM_BY_COMBO);
    if (sm_key_add_u64(&prefix, combo) != SM_KEY_OK) return SM_REPO_ERR_INVALID;
    s = sm_repo_scan_open_prefix(repo, SM_NS_COMBO_ITEM_BY_COMBO,
                                 sm_key_data(&prefix), sm_key_size(&prefix),
                                 &scan);
    while (s == SM_REPO_OK && (s = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *data; size_t len; sm_combo_item_record value;
        s = sm_repo_scan_value(scan, &data, &len);
        if (s == SM_REPO_OK && sm_combo_item_record_decode(data, len, &value) != SM_CODEC_OK)
            s = SM_REPO_ERR_CORRUPT;
        if (s == SM_REPO_OK)
            s = add((void **)&values, &used, &cap, sizeof(value), &value);
    }
    if (s == SM_REPO_ITER_END) s = SM_REPO_OK;
    close_s = sm_repo_scan_close(&scan); if (s == SM_REPO_OK) s = close_s;
    if (s != SM_REPO_OK) { free(values); return s; }
    *out = values; *count = used; return SM_REPO_OK;
}
