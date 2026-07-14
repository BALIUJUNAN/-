#include "sm_base_codec.h"

#include <string.h>

static int sm_bounded_string(const char *value, size_t capacity) {
    return value && memchr(value, '\0', capacity) != NULL;
}

static sm_codec_status sm_writer_finish_or_dispose(sm_value_writer *writer,
                                                    sm_codec_status status,
                                                    uint8_t **out,
                                                    size_t *out_len) {
    if (status == SM_CODEC_OK)
        status = sm_value_writer_finish(writer, out, out_len);
    if (status != SM_CODEC_OK) sm_value_writer_dispose(writer);
    return status;
}

static sm_codec_status sm_reader_init_entity(sm_value_reader *reader,
                                             const void *data, size_t len,
                                             uint8_t entity_type) {
    sm_codec_status status = sm_value_reader_init(reader, data, len);
    if (status != SM_CODEC_OK) return status;
    if (reader->version != SM_BASE_CODEC_VERSION ||
        reader->entity_type != entity_type)
        return SM_CODEC_ERR_FORMAT;
    return SM_CODEC_OK;
}

static sm_codec_status sm_required_field(const sm_value_reader *reader,
                                         uint16_t id,
                                         sm_value_field *field) {
    sm_codec_status status = sm_value_reader_find(reader, id, field);
    return status == SM_CODEC_NOT_FOUND ? SM_CODEC_ERR_FORMAT : status;
}

static sm_codec_status sm_read_string(const sm_value_reader *reader,
                                      uint16_t id,
                                      char *out, size_t capacity) {
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

static sm_codec_status sm_read_u8(const sm_value_reader *reader,
                                  uint16_t id, uint8_t *out) {
    sm_value_field field;
    sm_codec_status status = sm_required_field(reader, id, &field);
    return status == SM_CODEC_OK ? sm_value_field_u8(&field, out) : status;
}

static sm_codec_status sm_read_u32(const sm_value_reader *reader,
                                   uint16_t id, uint32_t *out) {
    sm_value_field field;
    sm_codec_status status = sm_required_field(reader, id, &field);
    return status == SM_CODEC_OK ? sm_value_field_u32(&field, out) : status;
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

sm_codec_status sm_employee_encode(const sm_employee *entity,
                                   uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_bounded_string(entity->name, sizeof(entity->name)) ||
        !sm_bounded_string(entity->role, sizeof(entity->role)) ||
        !sm_bounded_string(entity->password_hash,
                           sizeof(entity->password_hash)) ||
        !sm_bounded_string(entity->salt, sizeof(entity->salt)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_BASE_CODEC_VERSION,
                                  SM_BASE_ENTITY_EMPLOYEE);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 1, entity->id);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 2, entity->name);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 3, entity->role);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 4, entity->password_hash);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 5, entity->salt);
    if (status == SM_CODEC_OK) status = sm_value_write_u8(&writer, 6, entity->status);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 7, entity->created_at);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 8, entity->updated_at);
    return sm_writer_finish_or_dispose(&writer, status, out, out_len);
}

sm_codec_status sm_employee_decode(const void *data, size_t len,
                                   sm_employee *out) {
    sm_value_reader reader;
    sm_employee entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_reader_init_entity(&reader, data, len,
                                   SM_BASE_ENTITY_EMPLOYEE);
    if (status == SM_CODEC_OK) status = sm_read_u64(&reader, 1, &entity.id);
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 2, entity.name, sizeof(entity.name));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 3, entity.role, sizeof(entity.role));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 4, entity.password_hash, sizeof(entity.password_hash));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 5, entity.salt, sizeof(entity.salt));
    if (status == SM_CODEC_OK) status = sm_read_u8(&reader, 6, &entity.status);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 7, &entity.created_at);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 8, &entity.updated_at);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}

sm_codec_status sm_product_encode(const sm_product *entity,
                                  uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_bounded_string(entity->id, sizeof(entity->id)) ||
        !sm_bounded_string(entity->name, sizeof(entity->name)) ||
        !sm_bounded_string(entity->barcode, sizeof(entity->barcode)) ||
        !sm_bounded_string(entity->category_id, sizeof(entity->category_id)) ||
        !sm_bounded_string(entity->supplier_id, sizeof(entity->supplier_id)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_BASE_CODEC_VERSION,
                                  SM_BASE_ENTITY_PRODUCT);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 1, entity->id);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 2, entity->name);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 3, entity->barcode);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 4, entity->price_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 5, entity->cost_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 6, entity->stock);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 7, entity->min_stock);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 8, entity->category_id);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 9, entity->supplier_id);
    if (status == SM_CODEC_OK) status = sm_value_write_u8(&writer, 10, entity->status);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 11, entity->created_at);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 12, entity->updated_at);
    return sm_writer_finish_or_dispose(&writer, status, out, out_len);
}

sm_codec_status sm_product_decode(const void *data, size_t len,
                                  sm_product *out) {
    sm_value_reader reader;
    sm_product entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_reader_init_entity(&reader, data, len,
                                   SM_BASE_ENTITY_PRODUCT);
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 1, entity.id, sizeof(entity.id));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 2, entity.name, sizeof(entity.name));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 3, entity.barcode, sizeof(entity.barcode));
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 4, &entity.price_cents);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 5, &entity.cost_cents);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 6, &entity.stock);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 7, &entity.min_stock);
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 8, entity.category_id, sizeof(entity.category_id));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 9, entity.supplier_id, sizeof(entity.supplier_id));
    if (status == SM_CODEC_OK) status = sm_read_u8(&reader, 10, &entity.status);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 11, &entity.created_at);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 12, &entity.updated_at);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}

sm_codec_status sm_supplier_encode(const sm_supplier *entity,
                                   uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_bounded_string(entity->name, sizeof(entity->name)) ||
        !sm_bounded_string(entity->contact, sizeof(entity->contact)) ||
        !sm_bounded_string(entity->phone, sizeof(entity->phone)) ||
        !sm_bounded_string(entity->address, sizeof(entity->address)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_BASE_CODEC_VERSION,
                                  SM_BASE_ENTITY_SUPPLIER);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 1, entity->id);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 2, entity->name);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 3, entity->contact);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 4, entity->phone);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 5, entity->address);
    if (status == SM_CODEC_OK) status = sm_value_write_u8(&writer, 6, entity->status);
    return sm_writer_finish_or_dispose(&writer, status, out, out_len);
}

sm_codec_status sm_supplier_decode(const void *data, size_t len,
                                   sm_supplier *out) {
    sm_value_reader reader;
    sm_supplier entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_reader_init_entity(&reader, data, len,
                                   SM_BASE_ENTITY_SUPPLIER);
    if (status == SM_CODEC_OK) status = sm_read_u64(&reader, 1, &entity.id);
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 2, entity.name, sizeof(entity.name));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 3, entity.contact, sizeof(entity.contact));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 4, entity.phone, sizeof(entity.phone));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 5, entity.address, sizeof(entity.address));
    if (status == SM_CODEC_OK) status = sm_read_u8(&reader, 6, &entity.status);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}

sm_codec_status sm_member_encode(const sm_member *entity,
                                 uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_bounded_string(entity->phone, sizeof(entity->phone)) ||
        !sm_bounded_string(entity->name, sizeof(entity->name)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_BASE_CODEC_VERSION,
                                  SM_BASE_ENTITY_MEMBER);
    if (status == SM_CODEC_OK) status = sm_value_write_u64(&writer, 1, entity->id);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 2, entity->phone);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 3, entity->name);
    if (status == SM_CODEC_OK) status = sm_value_write_u32(&writer, 4, entity->level);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 5, entity->points);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 6, entity->total_consume_cents);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 7, entity->created_at);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 8, entity->updated_at);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 9, entity->last_consume_at);
    return sm_writer_finish_or_dispose(&writer, status, out, out_len);
}

sm_codec_status sm_member_decode(const void *data, size_t len,
                                 sm_member *out) {
    sm_value_reader reader;
    sm_member entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_reader_init_entity(&reader, data, len,
                                   SM_BASE_ENTITY_MEMBER);
    if (status == SM_CODEC_OK) status = sm_read_u64(&reader, 1, &entity.id);
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 2, entity.phone, sizeof(entity.phone));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 3, entity.name, sizeof(entity.name));
    if (status == SM_CODEC_OK) status = sm_read_u32(&reader, 4, &entity.level);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 5, &entity.points);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 6, &entity.total_consume_cents);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 7, &entity.created_at);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 8, &entity.updated_at);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 9, &entity.last_consume_at);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}

sm_codec_status sm_system_config_encode(const sm_system_config_entity *entity,
                                        uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    if (!entity || !out || !out_len ||
        !sm_bounded_string(entity->shop_name, sizeof(entity->shop_name)) ||
        !sm_bounded_string(entity->shop_address, sizeof(entity->shop_address)) ||
        !sm_bounded_string(entity->shop_phone, sizeof(entity->shop_phone)))
        return SM_CODEC_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_value_writer_init(&writer, SM_BASE_CODEC_VERSION,
                                  SM_BASE_ENTITY_SYSTEM_CONFIG);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 1, entity->shop_name);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 2, entity->shop_address);
    if (status == SM_CODEC_OK) status = sm_value_write_string(&writer, 3, entity->shop_phone);
    if (status == SM_CODEC_OK) status = sm_value_write_u32(&writer, 4, entity->tax_rate_basis_points);
    if (status == SM_CODEC_OK) status = sm_value_write_u32(&writer, 5, entity->auto_backup_interval_minutes);
    if (status == SM_CODEC_OK) status = sm_value_write_i64(&writer, 6, entity->monthly_fixed_cost_cents);
    return sm_writer_finish_or_dispose(&writer, status, out, out_len);
}

sm_codec_status sm_system_config_decode(const void *data, size_t len,
                                        sm_system_config_entity *out) {
    sm_value_reader reader;
    sm_system_config_entity entity;
    sm_codec_status status;
    if (!out) return SM_CODEC_ERR_INVALID;
    memset(&entity, 0, sizeof(entity));
    status = sm_reader_init_entity(&reader, data, len,
                                   SM_BASE_ENTITY_SYSTEM_CONFIG);
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 1, entity.shop_name, sizeof(entity.shop_name));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 2, entity.shop_address, sizeof(entity.shop_address));
    if (status == SM_CODEC_OK) status = sm_read_string(&reader, 3, entity.shop_phone, sizeof(entity.shop_phone));
    if (status == SM_CODEC_OK) status = sm_read_u32(&reader, 4, &entity.tax_rate_basis_points);
    if (status == SM_CODEC_OK) status = sm_read_u32(&reader, 5, &entity.auto_backup_interval_minutes);
    if (status == SM_CODEC_OK) status = sm_read_i64(&reader, 6, &entity.monthly_fixed_cost_cents);
    if (status == SM_CODEC_OK) *out = entity;
    return status;
}
