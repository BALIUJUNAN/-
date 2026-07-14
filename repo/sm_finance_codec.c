#include "repo/sm_finance_codec.h"

#include <string.h>

static int bounded(const char *s, size_t n) {
    return s && memchr(s, '\0', n) != NULL;
}

static sm_codec_status required(const sm_value_reader *r, uint16_t id,
                                sm_value_field *f) {
    sm_codec_status s = sm_value_reader_find(r, id, f);
    return s == SM_CODEC_NOT_FOUND ? SM_CODEC_ERR_FORMAT : s;
}

static sm_codec_status read_u64(const sm_value_reader *r, uint16_t id,
                                uint64_t *out) {
    sm_value_field f; sm_codec_status s = required(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_u64(&f, out) : s;
}

static sm_codec_status read_i64(const sm_value_reader *r, uint16_t id,
                                int64_t *out) {
    sm_value_field f; sm_codec_status s = required(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_i64(&f, out) : s;
}

static sm_codec_status read_u32(const sm_value_reader *r, uint16_t id,
                                uint32_t *out) {
    sm_value_field f; sm_codec_status s = required(r, id, &f);
    return s == SM_CODEC_OK ? sm_value_field_u32(&f, out) : s;
}

static sm_codec_status read_u8(const sm_value_reader *r, uint16_t id,
                               uint8_t *out) {
    uint32_t value; sm_codec_status s = read_u32(r, id, &value);
    if (s == SM_CODEC_OK && value > UINT8_MAX) return SM_CODEC_ERR_FORMAT;
    if (s == SM_CODEC_OK) *out = (uint8_t)value;
    return s;
}

static sm_codec_status read_string(const sm_value_reader *r, uint16_t id,
                                   char *out, size_t cap) {
    sm_value_field f; const char *p; size_t n;
    sm_codec_status s = required(r, id, &f);
    if (s == SM_CODEC_OK) s = sm_value_field_string(&f, &p, &n);
    if (s != SM_CODEC_OK) return s;
    if (n >= cap || memchr(p, '\0', n)) return SM_CODEC_ERR_TOO_LARGE;
    if (n) memcpy(out, p, n);
    out[n] = '\0';
    return SM_CODEC_OK;
}

static sm_codec_status reader(const void *data, size_t len, uint8_t type,
                              sm_value_reader *r) {
    sm_codec_status s = sm_value_reader_init(r, data, len);
    if (s == SM_CODEC_OK && (r->version != SM_FINANCE_CODEC_VERSION ||
                             r->entity_type != type))
        s = SM_CODEC_ERR_FORMAT;
    return s;
}

static sm_codec_status finish(sm_value_writer *w, sm_codec_status s,
                              uint8_t **out, size_t *len) {
    if (s == SM_CODEC_OK) s = sm_value_writer_finish(w, out, len);
    if (s != SM_CODEC_OK) sm_value_writer_dispose(w);
    return s;
}

#define PUT(call) do { if (s == SM_CODEC_OK) s = (call); } while (0)
#define GET(call) do { if (s == SM_CODEC_OK) s = (call); } while (0)

sm_codec_status sm_supplier_finance_encode(const sm_supplier_finance *v,
                                           uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !len) return SM_CODEC_ERR_INVALID;
    *out = NULL; *len = 0;
    s = sm_value_writer_init(&w, SM_FINANCE_CODEC_VERSION,
                             SM_FINANCE_ENTITY_SUPPLIER);
    PUT(sm_value_write_u64(&w, 1, v->supplier_id));
    PUT(sm_value_write_u32(&w, 2, v->payment_days));
    PUT(sm_value_write_u32(&w, 3, v->rating));
    PUT(sm_value_write_i64(&w, 4, v->total_cents));
    PUT(sm_value_write_i64(&w, 5, v->paid_cents));
    PUT(sm_value_write_i64(&w, 6, v->pending_cents));
    PUT(sm_value_write_i64(&w, 7, v->last_payment_at));
    PUT(sm_value_write_i64(&w, 8, v->updated_at));
    return finish(&w, s, out, len);
}

sm_codec_status sm_supplier_finance_decode(const void *data, size_t len,
                                           sm_supplier_finance *out) {
    sm_value_reader r; sm_supplier_finance v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = reader(data, len, SM_FINANCE_ENTITY_SUPPLIER, &r);
    GET(read_u64(&r, 1, &v.supplier_id)); GET(read_u32(&r, 2, &v.payment_days));
    GET(read_u8(&r, 3, &v.rating)); GET(read_i64(&r, 4, &v.total_cents));
    GET(read_i64(&r, 5, &v.paid_cents)); GET(read_i64(&r, 6, &v.pending_cents));
    GET(read_i64(&r, 7, &v.last_payment_at)); GET(read_i64(&r, 8, &v.updated_at));
    if (s == SM_CODEC_OK) *out = v;
    return s;
}

sm_codec_status sm_payable_encode(const sm_payable *v, uint8_t **out,
                                  size_t *len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !len || !bounded(v->supplier_name, sizeof(v->supplier_name)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL; *len = 0;
    s = sm_value_writer_init(&w, SM_FINANCE_CODEC_VERSION,
                             SM_FINANCE_ENTITY_PAYABLE);
    PUT(sm_value_write_u64(&w, 1, v->id)); PUT(sm_value_write_u64(&w, 2, v->supplier_id));
    PUT(sm_value_write_string(&w, 3, v->supplier_name));
    PUT(sm_value_write_u64(&w, 4, v->purchase_id));
    PUT(sm_value_write_i64(&w, 5, v->amount_cents));
    PUT(sm_value_write_i64(&w, 6, v->paid_cents));
    PUT(sm_value_write_i64(&w, 7, v->pending_cents));
    PUT(sm_value_write_u32(&w, 8, v->status));
    PUT(sm_value_write_i64(&w, 9, v->due_at));
    PUT(sm_value_write_i64(&w, 10, v->created_at));
    PUT(sm_value_write_i64(&w, 11, v->paid_at));
    return finish(&w, s, out, len);
}

sm_codec_status sm_payable_decode(const void *data, size_t len, sm_payable *out) {
    sm_value_reader r; sm_payable v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = reader(data, len, SM_FINANCE_ENTITY_PAYABLE, &r);
    GET(read_u64(&r, 1, &v.id)); GET(read_u64(&r, 2, &v.supplier_id));
    GET(read_string(&r, 3, v.supplier_name, sizeof(v.supplier_name)));
    GET(read_u64(&r, 4, &v.purchase_id)); GET(read_i64(&r, 5, &v.amount_cents));
    GET(read_i64(&r, 6, &v.paid_cents)); GET(read_i64(&r, 7, &v.pending_cents));
    GET(read_u8(&r, 8, &v.status)); GET(read_i64(&r, 9, &v.due_at));
    GET(read_i64(&r, 10, &v.created_at)); GET(read_i64(&r, 11, &v.paid_at));
    if (s == SM_CODEC_OK) *out = v;
    return s;
}

sm_codec_status sm_payment_record_encode(const sm_payment_record *v,
                                         uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !len || !bounded(v->method, sizeof(v->method)) ||
        !bounded(v->reference, sizeof(v->reference)) ||
        !bounded(v->operator_name, sizeof(v->operator_name)) ||
        !bounded(v->remark, sizeof(v->remark))) return SM_CODEC_ERR_INVALID;
    *out = NULL; *len = 0;
    s = sm_value_writer_init(&w, SM_FINANCE_CODEC_VERSION,
                             SM_FINANCE_ENTITY_PAYMENT);
    PUT(sm_value_write_u64(&w, 1, v->id)); PUT(sm_value_write_u64(&w, 2, v->payable_id));
    PUT(sm_value_write_u64(&w, 3, v->supplier_id));
    PUT(sm_value_write_i64(&w, 4, v->amount_cents));
    PUT(sm_value_write_string(&w, 5, v->method));
    PUT(sm_value_write_string(&w, 6, v->reference));
    PUT(sm_value_write_u64(&w, 7, v->operator_id));
    PUT(sm_value_write_string(&w, 8, v->operator_name));
    PUT(sm_value_write_string(&w, 9, v->remark));
    PUT(sm_value_write_i64(&w, 10, v->paid_at));
    return finish(&w, s, out, len);
}

sm_codec_status sm_payment_record_decode(const void *data, size_t len,
                                         sm_payment_record *out) {
    sm_value_reader r; sm_payment_record v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = reader(data, len, SM_FINANCE_ENTITY_PAYMENT, &r);
    GET(read_u64(&r, 1, &v.id)); GET(read_u64(&r, 2, &v.payable_id));
    GET(read_u64(&r, 3, &v.supplier_id)); GET(read_i64(&r, 4, &v.amount_cents));
    GET(read_string(&r, 5, v.method, sizeof(v.method)));
    GET(read_string(&r, 6, v.reference, sizeof(v.reference)));
    GET(read_u64(&r, 7, &v.operator_id));
    GET(read_string(&r, 8, v.operator_name, sizeof(v.operator_name)));
    GET(read_string(&r, 9, v.remark, sizeof(v.remark)));
    GET(read_i64(&r, 10, &v.paid_at));
    if (s == SM_CODEC_OK) *out = v;
    return s;
}

sm_codec_status sm_vip_card_encode(const sm_vip_card *v, uint8_t **out,
                                   size_t *len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !len || !bounded(v->card_no, sizeof(v->card_no)) ||
        !bounded(v->password_hash, sizeof(v->password_hash)) ||
        !bounded(v->password_salt, sizeof(v->password_salt)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL; *len = 0;
    s = sm_value_writer_init(&w, SM_FINANCE_CODEC_VERSION,
                             SM_FINANCE_ENTITY_VIP_CARD);
    PUT(sm_value_write_string(&w, 1, v->card_no));
    PUT(sm_value_write_u64(&w, 2, v->member_id));
    PUT(sm_value_write_i64(&w, 3, v->balance_cents));
    PUT(sm_value_write_i64(&w, 4, v->total_recharged_cents));
    PUT(sm_value_write_u32(&w, 5, v->card_type)); PUT(sm_value_write_u32(&w, 6, v->status));
    PUT(sm_value_write_i64(&w, 7, v->created_at)); PUT(sm_value_write_i64(&w, 8, v->updated_at));
    PUT(sm_value_write_i64(&w, 9, v->expired_at));
    PUT(sm_value_write_string(&w, 10, v->password_hash));
    PUT(sm_value_write_string(&w, 11, v->password_salt));
    return finish(&w, s, out, len);
}

sm_codec_status sm_vip_card_decode(const void *data, size_t len, sm_vip_card *out) {
    sm_value_reader r; sm_vip_card v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = reader(data, len, SM_FINANCE_ENTITY_VIP_CARD, &r);
    GET(read_string(&r, 1, v.card_no, sizeof(v.card_no))); GET(read_u64(&r, 2, &v.member_id));
    GET(read_i64(&r, 3, &v.balance_cents)); GET(read_i64(&r, 4, &v.total_recharged_cents));
    GET(read_u8(&r, 5, &v.card_type)); GET(read_u8(&r, 6, &v.status));
    GET(read_i64(&r, 7, &v.created_at)); GET(read_i64(&r, 8, &v.updated_at));
    GET(read_i64(&r, 9, &v.expired_at));
    GET(read_string(&r, 10, v.password_hash, sizeof(v.password_hash)));
    GET(read_string(&r, 11, v.password_salt, sizeof(v.password_salt)));
    if (s == SM_CODEC_OK) *out = v;
    return s;
}

sm_codec_status sm_vip_transaction_encode(const sm_vip_transaction *v,
                                          uint8_t **out, size_t *len) {
    sm_value_writer w; sm_codec_status s;
    if (!v || !out || !len || !bounded(v->card_no, sizeof(v->card_no)) ||
        !bounded(v->operator_name, sizeof(v->operator_name)) ||
        !bounded(v->remark, sizeof(v->remark))) return SM_CODEC_ERR_INVALID;
    *out = NULL; *len = 0;
    s = sm_value_writer_init(&w, SM_FINANCE_CODEC_VERSION,
                             SM_FINANCE_ENTITY_VIP_TRANSACTION);
    PUT(sm_value_write_u64(&w, 1, v->id)); PUT(sm_value_write_string(&w, 2, v->card_no));
    PUT(sm_value_write_u32(&w, 3, v->type)); PUT(sm_value_write_i64(&w, 4, v->amount_cents));
    PUT(sm_value_write_i64(&w, 5, v->balance_before_cents));
    PUT(sm_value_write_i64(&w, 6, v->balance_after_cents));
    PUT(sm_value_write_u64(&w, 7, v->operator_id));
    PUT(sm_value_write_string(&w, 8, v->operator_name));
    PUT(sm_value_write_u64(&w, 9, v->sale_id));
    PUT(sm_value_write_string(&w, 10, v->remark));
    PUT(sm_value_write_i64(&w, 11, v->created_at));
    return finish(&w, s, out, len);
}

sm_codec_status sm_vip_transaction_decode(const void *data, size_t len,
                                          sm_vip_transaction *out) {
    sm_value_reader r; sm_vip_transaction v; sm_codec_status s;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&v, 0, sizeof(v));
    s = reader(data, len, SM_FINANCE_ENTITY_VIP_TRANSACTION, &r);
    GET(read_u64(&r, 1, &v.id)); GET(read_string(&r, 2, v.card_no, sizeof(v.card_no)));
    GET(read_u8(&r, 3, &v.type)); GET(read_i64(&r, 4, &v.amount_cents));
    GET(read_i64(&r, 5, &v.balance_before_cents));
    GET(read_i64(&r, 6, &v.balance_after_cents));
    GET(read_u64(&r, 7, &v.operator_id));
    GET(read_string(&r, 8, v.operator_name, sizeof(v.operator_name)));
    GET(read_u64(&r, 9, &v.sale_id)); GET(read_string(&r, 10, v.remark, sizeof(v.remark)));
    GET(read_i64(&r, 11, &v.created_at));
    if (s == SM_CODEC_OK) *out = v;
    return s;
}

#undef PUT
#undef GET
