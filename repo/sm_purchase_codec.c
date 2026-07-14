#include "sm_purchase_codec.h"

#include <string.h>

static sm_codec_status sm_finish(sm_value_writer *writer,
                                 sm_codec_status status,
                                 uint8_t **out, size_t *out_len) {
    if (status == SM_CODEC_OK)
        status = sm_value_writer_finish(writer, out, out_len);
    if (status != SM_CODEC_OK) sm_value_writer_dispose(writer);
    return status;
}

static sm_codec_status sm_reader(sm_value_reader *reader,
                                 const void *data, size_t len,
                                 uint8_t entity_type) {
    sm_codec_status status = sm_value_reader_init(reader, data, len);
    if (status != SM_CODEC_OK) return status;
    return reader->version == SM_PURCHASE_CODEC_VERSION &&
                   reader->entity_type == entity_type
               ? SM_CODEC_OK : SM_CODEC_ERR_FORMAT;
}

static sm_codec_status sm_field(const sm_value_reader *reader, uint16_t id,
                                sm_value_field *field) {
    sm_codec_status status = sm_value_reader_find(reader, id, field);
    return status == SM_CODEC_NOT_FOUND ? SM_CODEC_ERR_FORMAT : status;
}

static sm_codec_status sm_u8(const sm_value_reader *r, uint16_t id,
                             uint8_t *out) {
    sm_value_field f; sm_codec_status s = sm_field(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_u8(&f, out) : s;
}

static sm_codec_status sm_u64(const sm_value_reader *r, uint16_t id,
                              uint64_t *out) {
    sm_value_field f; sm_codec_status s = sm_field(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_u64(&f, out) : s;
}

static sm_codec_status sm_i64(const sm_value_reader *r, uint16_t id,
                              int64_t *out) {
    sm_value_field f; sm_codec_status s = sm_field(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_i64(&f, out) : s;
}

static sm_codec_status sm_text(const sm_value_reader *r, uint16_t id,
                               char *out, size_t capacity) {
    sm_value_field f; const char *value; size_t len;
    sm_codec_status s = sm_field(r, id, &f);
    if (s == SM_CODEC_OK) s = sm_value_field_string(&f, &value, &len);
    if (s != SM_CODEC_OK) return s;
    if (len >= capacity || memchr(value, '\0', len))
        return SM_CODEC_ERR_TOO_LARGE;
    if (len) memcpy(out, value, len);
    out[len] = '\0';
    return SM_CODEC_OK;
}

sm_codec_status sm_purchase_encode(const sm_purchase *v,
                                   uint8_t **out, size_t *out_len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !out_len) return SM_CODEC_ERR_INVALID;
    *out = NULL; *out_len = 0;
    s = sm_value_writer_init(&w, SM_PURCHASE_CODEC_VERSION,
                             SM_PURCHASE_ENTITY_ORDER);
    if (s == SM_CODEC_OK) s = sm_value_write_u64(&w, 1, v->id);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 2, v->supplier_id);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 3, v->supplier_name);
    if (s == SM_CODEC_OK) s = sm_value_write_u64(&w, 4, v->creator_id);
    if (s == SM_CODEC_OK) s = sm_value_write_u64(&w, 5, v->approver_id);
    if (s == SM_CODEC_OK) s = sm_value_write_u8(&w, 6, v->status);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 7, v->total_cents);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 8, v->created_at);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 9, v->approved_at);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 10, v->completed_at);
    return sm_finish(&w, s, out, out_len);
}

sm_codec_status sm_purchase_decode(const void *data, size_t len,
                                   sm_purchase *out) {
    sm_value_reader r; sm_purchase v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = sm_reader(&r, data, len, SM_PURCHASE_ENTITY_ORDER);
    if (s == SM_CODEC_OK) s = sm_u64(&r, 1, &v.id);
    if (s == SM_CODEC_OK) s = sm_text(&r, 2, v.supplier_id, sizeof(v.supplier_id));
    if (s == SM_CODEC_OK) s = sm_text(&r, 3, v.supplier_name, sizeof(v.supplier_name));
    if (s == SM_CODEC_OK) s = sm_u64(&r, 4, &v.creator_id);
    if (s == SM_CODEC_OK) s = sm_u64(&r, 5, &v.approver_id);
    if (s == SM_CODEC_OK) s = sm_u8(&r, 6, &v.status);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 7, &v.total_cents);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 8, &v.created_at);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 9, &v.approved_at);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 10, &v.completed_at);
    if (s == SM_CODEC_OK) *out = v;
    return s;
}

sm_codec_status sm_purchase_item_encode(const sm_purchase_item *v,
                                        uint8_t **out, size_t *out_len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !out_len) return SM_CODEC_ERR_INVALID;
    *out = NULL; *out_len = 0;
    s = sm_value_writer_init(&w, SM_PURCHASE_CODEC_VERSION,
                             SM_PURCHASE_ENTITY_ITEM);
    if (s == SM_CODEC_OK) s = sm_value_write_u64(&w, 1, v->id);
    if (s == SM_CODEC_OK) s = sm_value_write_u64(&w, 2, v->purchase_id);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 3, v->product_id);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 4, v->product_name);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 5, v->quantity_milli);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 6, v->price_cents);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 7, v->received_milli);
    return sm_finish(&w, s, out, out_len);
}

sm_codec_status sm_purchase_item_decode(const void *data, size_t len,
                                        sm_purchase_item *out) {
    sm_value_reader r; sm_purchase_item v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = sm_reader(&r, data, len, SM_PURCHASE_ENTITY_ITEM);
    if (s == SM_CODEC_OK) s = sm_u64(&r, 1, &v.id);
    if (s == SM_CODEC_OK) s = sm_u64(&r, 2, &v.purchase_id);
    if (s == SM_CODEC_OK) s = sm_text(&r, 3, v.product_id, sizeof(v.product_id));
    if (s == SM_CODEC_OK) s = sm_text(&r, 4, v.product_name, sizeof(v.product_name));
    if (s == SM_CODEC_OK) s = sm_i64(&r, 5, &v.quantity_milli);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 6, &v.price_cents);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 7, &v.received_milli);
    if (s == SM_CODEC_OK) *out = v;
    return s;
}

sm_codec_status sm_batch_encode(const sm_batch *v,
                                uint8_t **out, size_t *out_len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !out_len) return SM_CODEC_ERR_INVALID;
    *out = NULL; *out_len = 0;
    s = sm_value_writer_init(&w, SM_PURCHASE_CODEC_VERSION,
                             SM_PURCHASE_ENTITY_BATCH);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 1, v->batch_no);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 2, v->product_id);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 3, v->product_name);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 4, v->quantity_milli);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 5, v->initial_quantity_milli);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 6, v->price_cents);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 7, v->production_date);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 8, v->expiry_date);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 9, v->received_date);
    if (s == SM_CODEC_OK) s = sm_value_write_string(&w, 10, v->supplier_id);
    if (s == SM_CODEC_OK) s = sm_value_write_i64(&w, 11, v->created_at);
    if (s == SM_CODEC_OK) s = sm_value_write_u64(&w, 12, v->source_purchase_id);
    if (s == SM_CODEC_OK) s = sm_value_write_u64(&w, 13, v->source_item_id);
    return sm_finish(&w, s, out, out_len);
}

sm_codec_status sm_batch_decode(const void *data, size_t len,
                                sm_batch *out) {
    sm_value_reader r; sm_batch v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = sm_reader(&r, data, len, SM_PURCHASE_ENTITY_BATCH);
    if (s == SM_CODEC_OK) s = sm_text(&r, 1, v.batch_no, sizeof(v.batch_no));
    if (s == SM_CODEC_OK) s = sm_text(&r, 2, v.product_id, sizeof(v.product_id));
    if (s == SM_CODEC_OK) s = sm_text(&r, 3, v.product_name, sizeof(v.product_name));
    if (s == SM_CODEC_OK) s = sm_i64(&r, 4, &v.quantity_milli);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 5, &v.initial_quantity_milli);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 6, &v.price_cents);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 7, &v.production_date);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 8, &v.expiry_date);
    if (s == SM_CODEC_OK) s = sm_i64(&r, 9, &v.received_date);
    if (s == SM_CODEC_OK) s = sm_text(&r, 10, v.supplier_id, sizeof(v.supplier_id));
    if (s == SM_CODEC_OK) s = sm_i64(&r, 11, &v.created_at);
    if (s == SM_CODEC_OK) s = sm_u64(&r, 12, &v.source_purchase_id);
    if (s == SM_CODEC_OK) s = sm_u64(&r, 13, &v.source_item_id);
    if (s == SM_CODEC_OK) *out = v;
    return s;
}
