#include "repo/sm_operations_codec.h"

#include <string.h>

enum {
    ENTITY_STORE = 0x60,
    ENTITY_STORE_STOCK,
    ENTITY_TRANSFER,
    ENTITY_TRANSFER_ITEM,
    ENTITY_PROMOTION,
    ENTITY_SCHEDULE,
    ENTITY_SETTLEMENT,
    ENTITY_AUDIT,
    ENTITY_COMBO,
    ENTITY_COMBO_ITEM
};

#define WRITE(call) do { status = (call); if (status != SM_CODEC_OK) goto fail; } while (0)

static sm_codec_status finish(sm_value_writer *w, uint8_t **out, size_t *len) {
    sm_codec_status status = sm_value_writer_finish(w, out, len);
    if (status != SM_CODEC_OK) sm_value_writer_dispose(w);
    return status;
}

static sm_codec_status reader(const void *data, size_t len, uint8_t entity,
                              sm_value_reader *r) {
    sm_codec_status status = sm_value_reader_init(r, data, len);
    if (status != SM_CODEC_OK) return status;
    return r->version == SM_OPERATIONS_CODEC_VERSION &&
           r->entity_type == entity ? SM_CODEC_OK : SM_CODEC_ERR_FORMAT;
}

static sm_codec_status u8f(const sm_value_reader *r, uint16_t id, uint8_t *v) {
    sm_value_field f; sm_codec_status s = sm_value_reader_find(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_u8(&f, v) : s;
}

static sm_codec_status u32f(const sm_value_reader *r, uint16_t id, uint32_t *v) {
    sm_value_field f; sm_codec_status s = sm_value_reader_find(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_u32(&f, v) : s;
}

static sm_codec_status u64f(const sm_value_reader *r, uint16_t id, uint64_t *v) {
    sm_value_field f; sm_codec_status s = sm_value_reader_find(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_u64(&f, v) : s;
}

static sm_codec_status i64f(const sm_value_reader *r, uint16_t id, int64_t *v) {
    sm_value_field f; sm_codec_status s = sm_value_reader_find(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_i64(&f, v) : s;
}

static sm_codec_status strf(const sm_value_reader *r, uint16_t id,
                            char *out, size_t cap) {
    sm_value_field f; const char *value; size_t len;
    sm_codec_status s = sm_value_reader_find(r, id, &f);
    if (s != SM_CODEC_OK) return s;
    s = sm_value_field_string(&f, &value, &len);
    if (s != SM_CODEC_OK) return s;
    if (len >= cap) return SM_CODEC_ERR_TOO_LARGE;
    memcpy(out, value, len); out[len] = '\0'; return SM_CODEC_OK;
}

sm_codec_status sm_store_record_encode(const sm_store_record *v,
                                       uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_STORE));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_string(&w, 2, v->name));
    WRITE(sm_value_write_string(&w, 3, v->address));
    WRITE(sm_value_write_string(&w, 4, v->phone));
    WRITE(sm_value_write_string(&w, 5, v->manager_name));
    WRITE(sm_value_write_u64(&w, 6, v->manager_id));
    WRITE(sm_value_write_u8(&w, 7, v->status));
    WRITE(sm_value_write_i64(&w, 8, v->created_at));
    WRITE(sm_value_write_i64(&w, 9, v->updated_at));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_store_record_decode(const void *data, size_t len,
                                       sm_store_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_STORE, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = strf(&r, 2, v->name, sizeof(v->name))) != SM_CODEC_OK ||
        (s = strf(&r, 3, v->address, sizeof(v->address))) != SM_CODEC_OK ||
        (s = strf(&r, 4, v->phone, sizeof(v->phone))) != SM_CODEC_OK ||
        (s = strf(&r, 5, v->manager_name, sizeof(v->manager_name))) != SM_CODEC_OK ||
        (s = u64f(&r, 6, &v->manager_id)) != SM_CODEC_OK ||
        (s = u8f(&r, 7, &v->status)) != SM_CODEC_OK ||
        (s = i64f(&r, 8, &v->created_at)) != SM_CODEC_OK)
        return s;
    return i64f(&r, 9, &v->updated_at);
}

sm_codec_status sm_store_stock_record_encode(const sm_store_stock_record *v,
                                             uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_STORE_STOCK));
    WRITE(sm_value_write_u64(&w, 1, v->store_id));
    WRITE(sm_value_write_string(&w, 2, v->product_id));
    WRITE(sm_value_write_i64(&w, 3, v->quantity));
    WRITE(sm_value_write_i64(&w, 4, v->min_stock));
    WRITE(sm_value_write_i64(&w, 5, v->updated_at));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_store_stock_record_decode(const void *data, size_t len,
                                             sm_store_stock_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_STORE_STOCK, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->store_id)) != SM_CODEC_OK ||
        (s = strf(&r, 2, v->product_id, sizeof(v->product_id))) != SM_CODEC_OK ||
        (s = i64f(&r, 3, &v->quantity)) != SM_CODEC_OK ||
        (s = i64f(&r, 4, &v->min_stock)) != SM_CODEC_OK)
        return s;
    return i64f(&r, 5, &v->updated_at);
}

sm_codec_status sm_transfer_order_record_encode(const sm_transfer_order_record *v,
                                                uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_TRANSFER));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_u64(&w, 2, v->from_store_id));
    WRITE(sm_value_write_u64(&w, 3, v->to_store_id));
    WRITE(sm_value_write_string(&w, 4, v->from_store_name));
    WRITE(sm_value_write_string(&w, 5, v->to_store_name));
    WRITE(sm_value_write_u8(&w, 6, v->status));
    WRITE(sm_value_write_u64(&w, 7, v->creator_id));
    WRITE(sm_value_write_string(&w, 8, v->creator_name));
    WRITE(sm_value_write_u64(&w, 9, v->approver_id));
    WRITE(sm_value_write_string(&w, 10, v->approver_name));
    WRITE(sm_value_write_u64(&w, 11, v->out_operator_id));
    WRITE(sm_value_write_string(&w, 12, v->out_operator_name));
    WRITE(sm_value_write_u64(&w, 13, v->in_operator_id));
    WRITE(sm_value_write_string(&w, 14, v->in_operator_name));
    WRITE(sm_value_write_i64(&w, 15, v->created_at));
    WRITE(sm_value_write_i64(&w, 16, v->approved_at));
    WRITE(sm_value_write_i64(&w, 17, v->out_at));
    WRITE(sm_value_write_i64(&w, 18, v->in_at));
    WRITE(sm_value_write_string(&w, 19, v->remark));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_transfer_order_record_decode(const void *data, size_t len,
                                                sm_transfer_order_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_TRANSFER, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = u64f(&r, 2, &v->from_store_id)) != SM_CODEC_OK ||
        (s = u64f(&r, 3, &v->to_store_id)) != SM_CODEC_OK ||
        (s = strf(&r, 4, v->from_store_name, sizeof(v->from_store_name))) != SM_CODEC_OK ||
        (s = strf(&r, 5, v->to_store_name, sizeof(v->to_store_name))) != SM_CODEC_OK ||
        (s = u8f(&r, 6, &v->status)) != SM_CODEC_OK ||
        (s = u64f(&r, 7, &v->creator_id)) != SM_CODEC_OK ||
        (s = strf(&r, 8, v->creator_name, sizeof(v->creator_name))) != SM_CODEC_OK ||
        (s = u64f(&r, 9, &v->approver_id)) != SM_CODEC_OK ||
        (s = strf(&r, 10, v->approver_name, sizeof(v->approver_name))) != SM_CODEC_OK ||
        (s = u64f(&r, 11, &v->out_operator_id)) != SM_CODEC_OK ||
        (s = strf(&r, 12, v->out_operator_name, sizeof(v->out_operator_name))) != SM_CODEC_OK ||
        (s = u64f(&r, 13, &v->in_operator_id)) != SM_CODEC_OK ||
        (s = strf(&r, 14, v->in_operator_name, sizeof(v->in_operator_name))) != SM_CODEC_OK ||
        (s = i64f(&r, 15, &v->created_at)) != SM_CODEC_OK ||
        (s = i64f(&r, 16, &v->approved_at)) != SM_CODEC_OK ||
        (s = i64f(&r, 17, &v->out_at)) != SM_CODEC_OK ||
        (s = i64f(&r, 18, &v->in_at)) != SM_CODEC_OK)
        return s;
    return strf(&r, 19, v->remark, sizeof(v->remark));
}

sm_codec_status sm_transfer_item_record_encode(const sm_transfer_item_record *v,
                                               uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_TRANSFER_ITEM));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_u64(&w, 2, v->transfer_id));
    WRITE(sm_value_write_string(&w, 3, v->product_id));
    WRITE(sm_value_write_string(&w, 4, v->product_name));
    WRITE(sm_value_write_i64(&w, 5, v->quantity));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_transfer_item_record_decode(const void *data, size_t len,
                                               sm_transfer_item_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_TRANSFER_ITEM, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = u64f(&r, 2, &v->transfer_id)) != SM_CODEC_OK ||
        (s = strf(&r, 3, v->product_id, sizeof(v->product_id))) != SM_CODEC_OK ||
        (s = strf(&r, 4, v->product_name, sizeof(v->product_name))) != SM_CODEC_OK)
        return s;
    return i64f(&r, 5, &v->quantity);
}

sm_codec_status sm_promotion_record_encode(const sm_promotion_record *v,
                                           uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_PROMOTION));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_string(&w, 2, v->name));
    WRITE(sm_value_write_u8(&w, 3, v->type));
    WRITE(sm_value_write_string(&w, 4, v->product_id));
    WRITE(sm_value_write_u32(&w, 5, v->discount_bps));
    WRITE(sm_value_write_i64(&w, 6, v->threshold_cents));
    WRITE(sm_value_write_i64(&w, 7, v->discount_cents));
    WRITE(sm_value_write_u32(&w, 8, v->nth_item));
    WRITE(sm_value_write_u32(&w, 9, v->nth_discount_bps));
    WRITE(sm_value_write_u32(&w, 10, v->buy_quantity));
    WRITE(sm_value_write_u32(&w, 11, v->free_quantity));
    WRITE(sm_value_write_u32(&w, 12, v->member_level));
    WRITE(sm_value_write_i64(&w, 13, v->member_price_cents));
    WRITE(sm_value_write_i64(&w, 14, v->start_at));
    WRITE(sm_value_write_i64(&w, 15, v->end_at));
    WRITE(sm_value_write_u8(&w, 16, v->status));
    WRITE(sm_value_write_u32(&w, 17, v->priority));
    WRITE(sm_value_write_i64(&w, 18, v->created_at));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_promotion_record_decode(const void *data, size_t len,
                                           sm_promotion_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_PROMOTION, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = strf(&r, 2, v->name, sizeof(v->name))) != SM_CODEC_OK ||
        (s = u8f(&r, 3, &v->type)) != SM_CODEC_OK ||
        (s = strf(&r, 4, v->product_id, sizeof(v->product_id))) != SM_CODEC_OK ||
        (s = u32f(&r, 5, &v->discount_bps)) != SM_CODEC_OK ||
        (s = i64f(&r, 6, &v->threshold_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 7, &v->discount_cents)) != SM_CODEC_OK ||
        (s = u32f(&r, 8, &v->nth_item)) != SM_CODEC_OK ||
        (s = u32f(&r, 9, &v->nth_discount_bps)) != SM_CODEC_OK ||
        (s = u32f(&r, 10, &v->buy_quantity)) != SM_CODEC_OK ||
        (s = u32f(&r, 11, &v->free_quantity)) != SM_CODEC_OK ||
        (s = u32f(&r, 12, &v->member_level)) != SM_CODEC_OK ||
        (s = i64f(&r, 13, &v->member_price_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 14, &v->start_at)) != SM_CODEC_OK ||
        (s = i64f(&r, 15, &v->end_at)) != SM_CODEC_OK ||
        (s = u8f(&r, 16, &v->status)) != SM_CODEC_OK ||
        (s = u32f(&r, 17, &v->priority)) != SM_CODEC_OK)
        return s;
    return i64f(&r, 18, &v->created_at);
}

sm_codec_status sm_schedule_record_encode(const sm_schedule_record *v,
                                          uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_SCHEDULE));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_u64(&w, 2, v->employee_id));
    WRITE(sm_value_write_u32(&w, 3, v->year));
    WRITE(sm_value_write_u8(&w, 4, v->week));
    WRITE(sm_value_write_bytes(&w, 5, v->shifts, sizeof(v->shifts)));
    WRITE(sm_value_write_i64(&w, 6, v->created_at));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_schedule_record_decode(const void *data, size_t len,
                                          sm_schedule_record *v) {
    sm_value_reader r; sm_value_field f; const uint8_t *bytes; size_t n;
    sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_SCHEDULE, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = u64f(&r, 2, &v->employee_id)) != SM_CODEC_OK ||
        (s = u32f(&r, 3, &v->year)) != SM_CODEC_OK ||
        (s = u8f(&r, 4, &v->week)) != SM_CODEC_OK ||
        (s = sm_value_reader_find(&r, 5, &f)) != SM_CODEC_OK ||
        (s = sm_value_field_bytes(&f, &bytes, &n)) != SM_CODEC_OK)
        return s;
    if (n != sizeof(v->shifts)) return SM_CODEC_ERR_FORMAT;
    memcpy(v->shifts, bytes, n);
    return i64f(&r, 6, &v->created_at);
}

sm_codec_status sm_combo_record_encode(const sm_combo_record *v,
                                       uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_COMBO));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_string(&w, 2, v->name));
    WRITE(sm_value_write_string(&w, 3, v->barcode));
    WRITE(sm_value_write_i64(&w, 4, v->price_cents));
    WRITE(sm_value_write_i64(&w, 5, v->cost_cents));
    WRITE(sm_value_write_u8(&w, 6, v->status));
    WRITE(sm_value_write_i64(&w, 7, v->created_at));
    WRITE(sm_value_write_i64(&w, 8, v->updated_at));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_combo_record_decode(const void *data, size_t len,
                                       sm_combo_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_COMBO, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = strf(&r, 2, v->name, sizeof(v->name))) != SM_CODEC_OK ||
        (s = strf(&r, 3, v->barcode, sizeof(v->barcode))) != SM_CODEC_OK ||
        (s = i64f(&r, 4, &v->price_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 5, &v->cost_cents)) != SM_CODEC_OK ||
        (s = u8f(&r, 6, &v->status)) != SM_CODEC_OK ||
        (s = i64f(&r, 7, &v->created_at)) != SM_CODEC_OK)
        return s;
    return i64f(&r, 8, &v->updated_at);
}

sm_codec_status sm_combo_item_record_encode(const sm_combo_item_record *v,
                                            uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_COMBO_ITEM));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_u64(&w, 2, v->combo_id));
    WRITE(sm_value_write_string(&w, 3, v->product_id));
    WRITE(sm_value_write_string(&w, 4, v->product_name));
    WRITE(sm_value_write_u32(&w, 5, v->quantity));
    WRITE(sm_value_write_u32(&w, 6, v->ratio_bps));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_combo_item_record_decode(const void *data, size_t len,
                                            sm_combo_item_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_COMBO_ITEM, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = u64f(&r, 2, &v->combo_id)) != SM_CODEC_OK ||
        (s = strf(&r, 3, v->product_id, sizeof(v->product_id))) != SM_CODEC_OK ||
        (s = strf(&r, 4, v->product_name, sizeof(v->product_name))) != SM_CODEC_OK ||
        (s = u32f(&r, 5, &v->quantity)) != SM_CODEC_OK)
        return s;
    return u32f(&r, 6, &v->ratio_bps);
}

#define SETTLE_WRITE(w, v) \
    WRITE(sm_value_write_u64((w), 1, (v)->id)); \
    WRITE(sm_value_write_u64((w), 2, (v)->cashier_id)); \
    WRITE(sm_value_write_string((w), 3, (v)->cashier_name)); \
    WRITE(sm_value_write_i64((w), 4, (v)->business_date)); \
    WRITE(sm_value_write_i64((w), 5, (v)->shift_start)); \
    WRITE(sm_value_write_i64((w), 6, (v)->shift_end)); \
    WRITE(sm_value_write_u32((w), 7, (v)->total_orders)); \
    WRITE(sm_value_write_i64((w), 8, (v)->system_cash_cents)); \
    WRITE(sm_value_write_i64((w), 9, (v)->system_online_cents)); \
    WRITE(sm_value_write_i64((w), 10, (v)->system_total_cents)); \
    WRITE(sm_value_write_i64((w), 11, (v)->actual_cash_cents)); \
    WRITE(sm_value_write_i64((w), 12, (v)->actual_online_cents)); \
    WRITE(sm_value_write_i64((w), 13, (v)->actual_total_cents)); \
    WRITE(sm_value_write_i64((w), 14, (v)->cash_diff_cents)); \
    WRITE(sm_value_write_i64((w), 15, (v)->online_diff_cents)); \
    WRITE(sm_value_write_i64((w), 16, (v)->total_diff_cents)); \
    WRITE(sm_value_write_u8((w), 17, (v)->status)); \
    WRITE(sm_value_write_i64((w), 18, (v)->created_at)); \
    WRITE(sm_value_write_i64((w), 19, (v)->confirmed_at)); \
    WRITE(sm_value_write_string((w), 20, (v)->remark))

sm_codec_status sm_settlement_record_encode(const sm_settlement_record *v,
                                            uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_SETTLEMENT));
    SETTLE_WRITE(&w, v);
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_settlement_record_decode(const void *data, size_t len,
                                            sm_settlement_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_SETTLEMENT, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = u64f(&r, 2, &v->cashier_id)) != SM_CODEC_OK ||
        (s = strf(&r, 3, v->cashier_name, sizeof(v->cashier_name))) != SM_CODEC_OK ||
        (s = i64f(&r, 4, &v->business_date)) != SM_CODEC_OK ||
        (s = i64f(&r, 5, &v->shift_start)) != SM_CODEC_OK ||
        (s = i64f(&r, 6, &v->shift_end)) != SM_CODEC_OK ||
        (s = u32f(&r, 7, &v->total_orders)) != SM_CODEC_OK ||
        (s = i64f(&r, 8, &v->system_cash_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 9, &v->system_online_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 10, &v->system_total_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 11, &v->actual_cash_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 12, &v->actual_online_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 13, &v->actual_total_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 14, &v->cash_diff_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 15, &v->online_diff_cents)) != SM_CODEC_OK ||
        (s = i64f(&r, 16, &v->total_diff_cents)) != SM_CODEC_OK ||
        (s = u8f(&r, 17, &v->status)) != SM_CODEC_OK ||
        (s = i64f(&r, 18, &v->created_at)) != SM_CODEC_OK ||
        (s = i64f(&r, 19, &v->confirmed_at)) != SM_CODEC_OK)
        return s;
    return strf(&r, 20, v->remark, sizeof(v->remark));
}

sm_codec_status sm_audit_record_encode(const sm_audit_record *v,
                                       uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status status;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    WRITE(sm_value_writer_init(&w, SM_OPERATIONS_CODEC_VERSION, ENTITY_AUDIT));
    WRITE(sm_value_write_u64(&w, 1, v->id));
    WRITE(sm_value_write_string(&w, 2, v->type));
    WRITE(sm_value_write_u64(&w, 3, v->ref_id));
    WRITE(sm_value_write_string(&w, 4, v->operation));
    WRITE(sm_value_write_string(&w, 5, v->data));
    WRITE(sm_value_write_u64(&w, 6, v->operator_id));
    WRITE(sm_value_write_i64(&w, 7, v->created_at));
    return finish(&w, out, len);
fail: sm_value_writer_dispose(&w); return status;
}

sm_codec_status sm_audit_record_decode(const void *data, size_t len,
                                       sm_audit_record *v) {
    sm_value_reader r; sm_codec_status s;
    if (!v) return SM_CODEC_ERR_INVALID;
    memset(v, 0, sizeof(*v));
    if ((s = reader(data, len, ENTITY_AUDIT, &r)) != SM_CODEC_OK ||
        (s = u64f(&r, 1, &v->id)) != SM_CODEC_OK ||
        (s = strf(&r, 2, v->type, sizeof(v->type))) != SM_CODEC_OK ||
        (s = u64f(&r, 3, &v->ref_id)) != SM_CODEC_OK ||
        (s = strf(&r, 4, v->operation, sizeof(v->operation))) != SM_CODEC_OK ||
        (s = strf(&r, 5, v->data, sizeof(v->data))) != SM_CODEC_OK ||
        (s = u64f(&r, 6, &v->operator_id)) != SM_CODEC_OK)
        return s;
    return i64f(&r, 7, &v->created_at);
}

#undef SETTLE_WRITE
#undef WRITE
