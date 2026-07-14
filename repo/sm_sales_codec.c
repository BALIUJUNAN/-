#include "sm_sales_codec.h"

#include <string.h>

static int sm_sales_bounded_string(const char *value, size_t capacity) {
    return value && memchr(value, '\0', capacity) != NULL;
}

static sm_codec_status sm_sales_finish(sm_value_writer *writer,
                                       sm_codec_status status,
                                       uint8_t **out, size_t *out_len) {
    if (status == SM_CODEC_OK)
        status = sm_value_writer_finish(writer, out, out_len);
    if (status != SM_CODEC_OK) sm_value_writer_dispose(writer);
    return status;
}

static sm_codec_status sm_sales_reader(sm_value_reader *reader,
                                       const void *data, size_t len,
                                       uint8_t entity_type) {
    sm_codec_status status = sm_value_reader_init(reader, data, len);
    if (status != SM_CODEC_OK) return status;
    return reader->version == SM_SALES_CODEC_VERSION &&
                   reader->entity_type == entity_type
               ? SM_CODEC_OK
               : SM_CODEC_ERR_FORMAT;
}

static sm_codec_status sm_sales_field(const sm_value_reader *reader,
                                      uint16_t id, sm_value_field *field) {
    sm_codec_status status = sm_value_reader_find(reader, id, field);
    return status == SM_CODEC_NOT_FOUND ? SM_CODEC_ERR_FORMAT : status;
}

static sm_codec_status sm_sales_read_u8(const sm_value_reader *reader,
                                        uint16_t id, uint8_t *out) {
    sm_value_field field;
    sm_codec_status status = sm_sales_field(reader, id, &field);
    return status == SM_CODEC_OK ? sm_value_field_u8(&field, out) : status;
}

static sm_codec_status sm_sales_read_u64(const sm_value_reader *reader,
                                         uint16_t id, uint64_t *out) {
    sm_value_field field;
    sm_codec_status status = sm_sales_field(reader, id, &field);
    return status == SM_CODEC_OK ? sm_value_field_u64(&field, out) : status;
}

static sm_codec_status sm_sales_read_i64(const sm_value_reader *reader,
                                         uint16_t id, int64_t *out) {
    sm_value_field field;
    sm_codec_status status = sm_sales_field(reader, id, &field);
    return status == SM_CODEC_OK ? sm_value_field_i64(&field, out) : status;
}

static sm_codec_status sm_sales_read_string(const sm_value_reader *reader,
                                            uint16_t id, char *out,
                                            size_t capacity) {
    sm_value_field field;
    const char *value;
    size_t len;
    sm_codec_status status = sm_sales_field(reader, id, &field);
    if (status == SM_CODEC_OK)
        status = sm_value_field_string(&field, &value, &len);
    if (status != SM_CODEC_OK) return status;
    if (len >= capacity || memchr(value, '\0', len) != NULL)
        return SM_CODEC_ERR_TOO_LARGE;
    if (len) memcpy(out, value, len);
    out[len] = '\0';
    return SM_CODEC_OK;
}

sm_codec_status sm_sale_encode(const sm_sale *entity,
                               uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_sales_bounded_string(entity->payment_method,
                                 sizeof(entity->payment_method)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_SALES_CODEC_VERSION,
                                  SM_SALES_ENTITY_SALE);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 1, entity->id);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 2, entity->cashier_id);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 3, entity->member_id);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 4, entity->total_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 5, entity->discount_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 6, entity->final_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 7, entity->cash_received_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 8, entity->points_used);
    if (status == SM_CODEC_OK) status = sm_value_write_u8(&writer, 9, entity->status);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 10, entity->payment_method);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 11, entity->created_at);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 12, entity->completed_at);
    return sm_sales_finish(&writer, status, out, out_len);
}

sm_codec_status sm_sale_decode(const void *data, size_t len, sm_sale *out) {
    sm_value_reader reader;
    sm_sale entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_sales_reader(&reader, data, len, SM_SALES_ENTITY_SALE);
    if (status == SM_CODEC_OK) status = sm_sales_read_u64(&reader, 1, &entity.id);
    if (status == SM_CODEC_OK) status = sm_sales_read_u64(&reader, 2, &entity.cashier_id);
    if (status == SM_CODEC_OK) status = sm_sales_read_u64(&reader, 3, &entity.member_id);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 4, &entity.total_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 5, &entity.discount_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 6, &entity.final_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 7, &entity.cash_received_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 8, &entity.points_used);
    if (status == SM_CODEC_OK) status = sm_sales_read_u8(&reader, 9, &entity.status);
    if (status == SM_CODEC_OK) status = sm_sales_read_string(&reader, 10, entity.payment_method, sizeof(entity.payment_method));
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 11, &entity.created_at);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 12, &entity.completed_at);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}

sm_codec_status sm_sale_item_encode(const sm_sale_item *entity,
                                    uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_sales_bounded_string(entity->product_id,
                                 sizeof(entity->product_id)) ||
        !sm_sales_bounded_string(entity->product_name,
                                 sizeof(entity->product_name)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_SALES_CODEC_VERSION,
                                  SM_SALES_ENTITY_ITEM);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 1, entity->id);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 2, entity->sale_id);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 3, entity->product_id);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 4, entity->product_name);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 5, entity->quantity_milli);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 6, entity->price_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 7, entity->original_price_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 8, entity->subtotal_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 9, entity->discount_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_u8(&writer, 10, entity->is_combo);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 11, entity->combo_id);
    return sm_sales_finish(&writer, status, out, out_len);
}

sm_codec_status sm_sale_item_decode(const void *data, size_t len,
                                    sm_sale_item *out) {
    sm_value_reader reader;
    sm_sale_item entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_sales_reader(&reader, data, len, SM_SALES_ENTITY_ITEM);
    if (status == SM_CODEC_OK) status = sm_sales_read_u64(&reader, 1, &entity.id);
    if (status == SM_CODEC_OK) status = sm_sales_read_u64(&reader, 2, &entity.sale_id);
    if (status == SM_CODEC_OK) status = sm_sales_read_string(&reader, 3, entity.product_id, sizeof(entity.product_id));
    if (status == SM_CODEC_OK) status = sm_sales_read_string(&reader, 4, entity.product_name, sizeof(entity.product_name));
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 5, &entity.quantity_milli);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 6, &entity.price_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 7, &entity.original_price_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 8, &entity.subtotal_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_i64(&reader, 9, &entity.discount_cents);
    if (status == SM_CODEC_OK) status = sm_sales_read_u8(&reader, 10, &entity.is_combo);
    if (status == SM_CODEC_OK) status = sm_sales_read_u64(&reader, 11, &entity.combo_id);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}
