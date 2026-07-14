#include "sm_inventory_codec.h"

#include <string.h>

static int sm_inventory_string(const char *value, size_t capacity) {
    return value && memchr(value, '\0', capacity) != NULL;
}

static sm_codec_status sm_required_field(const sm_value_reader *reader,
                                         uint16_t id,
                                         sm_value_field *field) {
    sm_codec_status status = sm_value_reader_find(reader, id, field);
    return status == SM_CODEC_NOT_FOUND ? SM_CODEC_ERR_FORMAT : status;
}

static sm_codec_status sm_read_u64(const sm_value_reader *reader,
                                   uint16_t id, uint64_t *out) {
    sm_value_field field;
    sm_codec_status status = sm_required_field(reader, id, &field);
    return status == SM_CODEC_OK ? sm_value_field_u64(&field, out) : status;
}

static sm_codec_status sm_read_i64(const sm_value_reader *reader,
                                   uint16_t id, int64_t *out) {
    sm_value_field field;
    sm_codec_status status = sm_required_field(reader, id, &field);
    return status == SM_CODEC_OK ? sm_value_field_i64(&field, out) : status;
}

static sm_codec_status sm_read_string(const sm_value_reader *reader,
                                      uint16_t id, char *out,
                                      size_t capacity) {
    sm_value_field field;
    const char *value;
    size_t len;
    sm_codec_status status = sm_required_field(reader, id, &field);
    if (status == SM_CODEC_OK)
        status = sm_value_field_string(&field, &value, &len);
    if (status != SM_CODEC_OK) return status;
    if (len >= capacity || memchr(value, '\0', len) != NULL)
        return SM_CODEC_ERR_TOO_LARGE;
    if (len) memcpy(out, value, len);
    out[len] = '\0';
    return SM_CODEC_OK;
}

sm_codec_status sm_stock_log_encode(const sm_stock_log *entity,
                                    uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_inventory_string(entity->product_id,
                             sizeof(entity->product_id)) ||
        !sm_inventory_string(entity->type, sizeof(entity->type)) ||
        !sm_inventory_string(entity->remark, sizeof(entity->remark)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_INVENTORY_CODEC_VERSION,
                                  SM_INVENTORY_ENTITY_STOCK_LOG);
    if (status == SM_CODEC_OK)
        status = sm_value_write_u64(&writer, 1, entity->id);
    if (status == SM_CODEC_OK)
        status = sm_value_write_string(&writer, 2, entity->product_id);
    if (status == SM_CODEC_OK)
        status = sm_value_write_string(&writer, 3, entity->type);
    if (status == SM_CODEC_OK)
        status = sm_value_write_i64(&writer, 4, entity->quantity_milli);
    if (status == SM_CODEC_OK)
        status = sm_value_write_i64(&writer, 5,
                                    entity->before_stock_milli);
    if (status == SM_CODEC_OK)
        status = sm_value_write_i64(&writer, 6,
                                    entity->after_stock_milli);
    if (status == SM_CODEC_OK)
        status = sm_value_write_u64(&writer, 7, entity->operator_id);
    if (status == SM_CODEC_OK)
        status = sm_value_write_string(&writer, 8, entity->remark);
    if (status == SM_CODEC_OK)
        status = sm_value_write_i64(&writer, 9, entity->created_at);
    if (status == SM_CODEC_OK)
        status = sm_value_writer_finish(&writer, out, out_len);
    if (status != SM_CODEC_OK) sm_value_writer_dispose(&writer);
    return status;
}

sm_codec_status sm_stock_log_decode(const void *data, size_t len,
                                    sm_stock_log *out) {
    sm_value_reader reader;
    sm_stock_log entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_value_reader_init(&reader, data, len);
    if (status == SM_CODEC_OK &&
        (reader.version != SM_INVENTORY_CODEC_VERSION ||
         reader.entity_type != SM_INVENTORY_ENTITY_STOCK_LOG))
        status = SM_CODEC_ERR_FORMAT;
    if (status == SM_CODEC_OK) status = sm_read_u64(&reader, 1, &entity.id);
    if (status == SM_CODEC_OK)
        status = sm_read_string(&reader, 2, entity.product_id,
                                sizeof(entity.product_id));
    if (status == SM_CODEC_OK)
        status = sm_read_string(&reader, 3, entity.type,
                                sizeof(entity.type));
    if (status == SM_CODEC_OK)
        status = sm_read_i64(&reader, 4, &entity.quantity_milli);
    if (status == SM_CODEC_OK)
        status = sm_read_i64(&reader, 5, &entity.before_stock_milli);
    if (status == SM_CODEC_OK)
        status = sm_read_i64(&reader, 6, &entity.after_stock_milli);
    if (status == SM_CODEC_OK)
        status = sm_read_u64(&reader, 7, &entity.operator_id);
    if (status == SM_CODEC_OK)
        status = sm_read_string(&reader, 8, entity.remark,
                                sizeof(entity.remark));
    if (status == SM_CODEC_OK)
        status = sm_read_i64(&reader, 9, &entity.created_at);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}
