#include "sm_codec.h"

#include <stdlib.h>
#include <string.h>

#define SM_VALUE_HEADER_SIZE 4u
#define SM_FIELD_HEADER_SIZE 7u

static uint16_t sm_read_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t sm_read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t sm_read_u64(const uint8_t *p) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}

static void sm_write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void sm_write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void sm_write_u64(uint8_t *p, uint64_t value) {
    size_t i;
    for (i = 0; i < 8; ++i)
        p[i] = (uint8_t)(value >> (56u - (unsigned)i * 8u));
}

static int sm_utf8_valid(const uint8_t *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = s[i++];
        uint32_t code;
        size_t need;
        if (c <= 0x7fu) continue;
        if (c >= 0xc2u && c <= 0xdfu) {
            code = c & 0x1fu;
            need = 1;
        } else if (c >= 0xe0u && c <= 0xefu) {
            code = c & 0x0fu;
            need = 2;
        } else if (c >= 0xf0u && c <= 0xf4u) {
            code = c & 0x07u;
            need = 3;
        } else {
            return 0;
        }
        if (len - i < need) return 0;
        while (need--) {
            uint8_t next = s[i++];
            if ((next & 0xc0u) != 0x80u) return 0;
            code = (code << 6) | (uint32_t)(next & 0x3fu);
        }
        if ((code >= 0xd800u && code <= 0xdfffu) || code > 0x10ffffu)
            return 0;
        if ((code <= 0x7ffu && c >= 0xe0u) ||
            (code <= 0xffffu && c >= 0xf0u))
            return 0;
    }
    return 1;
}

static sm_codec_status sm_writer_reserve(sm_value_writer *writer,
                                         size_t additional) {
    size_t needed;
    size_t capacity;
    uint8_t *next;
    if (!writer || writer->finished) return SM_CODEC_ERR_STATE;
    if (writer->status != SM_CODEC_OK) return writer->status;
    if (additional > SM_CODEC_MAX_VALUE_SIZE - writer->len) {
        writer->status = SM_CODEC_ERR_TOO_LARGE;
        return writer->status;
    }
    needed = writer->len + additional;
    if (needed <= writer->capacity) return SM_CODEC_OK;

    capacity = writer->capacity ? writer->capacity : 64u;
    while (capacity < needed) {
        if (capacity > SM_CODEC_MAX_VALUE_SIZE / 2u) {
            capacity = SM_CODEC_MAX_VALUE_SIZE;
            break;
        }
        capacity *= 2u;
    }
    next = (uint8_t *)realloc(writer->data, capacity);
    if (!next) {
        writer->status = SM_CODEC_ERR_NOMEM;
        return writer->status;
    }
    writer->data = next;
    writer->capacity = capacity;
    return SM_CODEC_OK;
}

static sm_codec_status sm_write_field(sm_value_writer *writer,
                                      uint16_t field_id,
                                      sm_wire_type type,
                                      const void *data, size_t len) {
    sm_codec_status status;
    if (!writer || field_id == 0 || (!data && len != 0))
        return SM_CODEC_ERR_INVALID;
    if (field_id <= writer->last_field_id) {
        writer->status = SM_CODEC_ERR_FORMAT;
        return writer->status;
    }
    if (writer->field_count >= SM_CODEC_MAX_FIELDS || len > UINT32_MAX) {
        writer->status = SM_CODEC_ERR_TOO_LARGE;
        return writer->status;
    }
    status = sm_writer_reserve(writer, SM_FIELD_HEADER_SIZE + len);
    if (status != SM_CODEC_OK) return status;

    sm_write_u16(writer->data + writer->len, field_id);
    writer->data[writer->len + 2] = (uint8_t)type;
    sm_write_u32(writer->data + writer->len + 3, (uint32_t)len);
    writer->len += SM_FIELD_HEADER_SIZE;
    if (len) memcpy(writer->data + writer->len, data, len);
    writer->len += len;
    ++writer->field_count;
    writer->last_field_id = field_id;
    sm_write_u16(writer->data + 2, writer->field_count);
    return SM_CODEC_OK;
}

sm_codec_status sm_value_writer_init(sm_value_writer *writer,
                                     uint8_t version,
                                     uint8_t entity_type) {
    if (!writer || version == 0 || entity_type == 0)
        return SM_CODEC_ERR_INVALID;
    memset(writer, 0, sizeof(*writer));
    writer->status = SM_CODEC_OK;
    if (sm_writer_reserve(writer, SM_VALUE_HEADER_SIZE) != SM_CODEC_OK)
        return writer->status;
    writer->data[0] = version;
    writer->data[1] = entity_type;
    writer->data[2] = 0;
    writer->data[3] = 0;
    writer->len = SM_VALUE_HEADER_SIZE;
    return SM_CODEC_OK;
}

void sm_value_writer_dispose(sm_value_writer *writer) {
    if (!writer) return;
    free(writer->data);
    memset(writer, 0, sizeof(*writer));
}

sm_codec_status sm_value_write_u8(sm_value_writer *writer,
                                  uint16_t field_id, uint8_t value) {
    return sm_write_field(writer, field_id, SM_WIRE_U8, &value, 1u);
}

sm_codec_status sm_value_write_u32(sm_value_writer *writer,
                                   uint16_t field_id, uint32_t value) {
    uint8_t bytes[4];
    sm_write_u32(bytes, value);
    return sm_write_field(writer, field_id, SM_WIRE_U32, bytes, sizeof(bytes));
}

sm_codec_status sm_value_write_u64(sm_value_writer *writer,
                                   uint16_t field_id, uint64_t value) {
    uint8_t bytes[8];
    sm_write_u64(bytes, value);
    return sm_write_field(writer, field_id, SM_WIRE_U64, bytes, sizeof(bytes));
}

sm_codec_status sm_value_write_i64(sm_value_writer *writer,
                                   uint16_t field_id, int64_t value) {
    uint8_t bytes[8];
    sm_write_u64(bytes, (uint64_t)value);
    return sm_write_field(writer, field_id, SM_WIRE_I64, bytes, sizeof(bytes));
}

sm_codec_status sm_value_write_bool(sm_value_writer *writer,
                                    uint16_t field_id, int value) {
    uint8_t byte;
    if (value != 0 && value != 1) return SM_CODEC_ERR_INVALID;
    byte = (uint8_t)value;
    return sm_write_field(writer, field_id, SM_WIRE_BOOL, &byte, 1u);
}

sm_codec_status sm_value_write_bytes(sm_value_writer *writer,
                                     uint16_t field_id,
                                     const void *data, size_t len) {
    return sm_write_field(writer, field_id, SM_WIRE_BYTES, data, len);
}

sm_codec_status sm_value_write_string(sm_value_writer *writer,
                                      uint16_t field_id,
                                      const char *value) {
    size_t len;
    if (!value) return SM_CODEC_ERR_INVALID;
    len = strlen(value);
    if (len > SM_CODEC_MAX_STRING_SIZE) return SM_CODEC_ERR_TOO_LARGE;
    if (!sm_utf8_valid((const uint8_t *)value, len)) return SM_CODEC_ERR_UTF8;
    return sm_write_field(writer, field_id, SM_WIRE_STRING, value, len);
}

sm_codec_status sm_value_writer_finish(sm_value_writer *writer,
                                       uint8_t **out_data, size_t *out_len) {
    if (!writer || !out_data || !out_len) return SM_CODEC_ERR_INVALID;
    *out_data = NULL;
    *out_len = 0;
    if (writer->finished || writer->status != SM_CODEC_OK)
        return writer->finished ? SM_CODEC_ERR_STATE : writer->status;

    *out_data = writer->data;
    *out_len = writer->len;
    writer->data = NULL;
    writer->len = 0;
    writer->capacity = 0;
    writer->finished = 1;
    return SM_CODEC_OK;
}

static int sm_wire_valid(uint8_t type, uint32_t len) {
    switch ((sm_wire_type)type) {
        case SM_WIRE_U8:
        case SM_WIRE_BOOL: return len == 1u;
        case SM_WIRE_U32: return len == 4u;
        case SM_WIRE_U64:
        case SM_WIRE_I64: return len == 8u;
        case SM_WIRE_BYTES: return 1;
        case SM_WIRE_STRING: return len <= SM_CODEC_MAX_STRING_SIZE;
        default: return 0;
    }
}

sm_codec_status sm_value_reader_init(sm_value_reader *reader,
                                     const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t offset;
    uint16_t count;
    uint16_t i;
    uint16_t previous_id = 0;
    if (!reader || !data) return SM_CODEC_ERR_INVALID;
    memset(reader, 0, sizeof(*reader));
    if (len < SM_VALUE_HEADER_SIZE) return SM_CODEC_ERR_TRUNCATED;
    if (len > SM_CODEC_MAX_VALUE_SIZE) return SM_CODEC_ERR_TOO_LARGE;
    if (bytes[0] == 0 || bytes[1] == 0) return SM_CODEC_ERR_FORMAT;

    count = sm_read_u16(bytes + 2);
    if (count > SM_CODEC_MAX_FIELDS) return SM_CODEC_ERR_TOO_LARGE;
    offset = SM_VALUE_HEADER_SIZE;
    for (i = 0; i < count; ++i) {
        uint16_t field_id;
        uint8_t type;
        uint32_t field_len;
        if (len - offset < SM_FIELD_HEADER_SIZE)
            return SM_CODEC_ERR_TRUNCATED;
        field_id = sm_read_u16(bytes + offset);
        type = bytes[offset + 2];
        field_len = sm_read_u32(bytes + offset + 3);
        offset += SM_FIELD_HEADER_SIZE;
        if (field_id == 0 || field_id <= previous_id ||
            !sm_wire_valid(type, field_len))
            return SM_CODEC_ERR_FORMAT;
        previous_id = field_id;
        if ((size_t)field_len > len - offset)
            return SM_CODEC_ERR_TRUNCATED;
        if (type == SM_WIRE_BOOL && bytes[offset] > 1u)
            return SM_CODEC_ERR_FORMAT;
        if (type == SM_WIRE_STRING &&
            !sm_utf8_valid(bytes + offset, field_len))
            return SM_CODEC_ERR_UTF8;
        offset += field_len;
    }
    if (offset != len) return SM_CODEC_ERR_FORMAT;

    reader->data = bytes;
    reader->len = len;
    reader->version = bytes[0];
    reader->entity_type = bytes[1];
    reader->field_count = count;
    return SM_CODEC_OK;
}

sm_codec_status sm_value_reader_find(const sm_value_reader *reader,
                                     uint16_t field_id,
                                     sm_value_field *out_field) {
    size_t offset;
    uint16_t i;
    if (!reader || !reader->data || field_id == 0 || !out_field)
        return SM_CODEC_ERR_INVALID;
    offset = SM_VALUE_HEADER_SIZE;
    for (i = 0; i < reader->field_count; ++i) {
        uint16_t current_id = sm_read_u16(reader->data + offset);
        uint8_t type = reader->data[offset + 2];
        uint32_t len = sm_read_u32(reader->data + offset + 3);
        offset += SM_FIELD_HEADER_SIZE;
        if (current_id == field_id) {
            out_field->id = current_id;
            out_field->type = (sm_wire_type)type;
            out_field->data = reader->data + offset;
            out_field->len = len;
            return SM_CODEC_OK;
        }
        offset += len;
    }
    return SM_CODEC_NOT_FOUND;
}

sm_codec_status sm_value_field_u8(const sm_value_field *field,
                                  uint8_t *out_value) {
    if (!field || !out_value) return SM_CODEC_ERR_INVALID;
    if (field->type != SM_WIRE_U8 || field->len != 1u)
        return SM_CODEC_ERR_TYPE;
    *out_value = field->data[0];
    return SM_CODEC_OK;
}

sm_codec_status sm_value_field_u32(const sm_value_field *field,
                                   uint32_t *out_value) {
    if (!field || !out_value) return SM_CODEC_ERR_INVALID;
    if (field->type != SM_WIRE_U32 || field->len != 4u)
        return SM_CODEC_ERR_TYPE;
    *out_value = sm_read_u32(field->data);
    return SM_CODEC_OK;
}

sm_codec_status sm_value_field_u64(const sm_value_field *field,
                                   uint64_t *out_value) {
    if (!field || !out_value) return SM_CODEC_ERR_INVALID;
    if (field->type != SM_WIRE_U64 || field->len != 8u)
        return SM_CODEC_ERR_TYPE;
    *out_value = sm_read_u64(field->data);
    return SM_CODEC_OK;
}

sm_codec_status sm_value_field_i64(const sm_value_field *field,
                                   int64_t *out_value) {
    if (!field || !out_value) return SM_CODEC_ERR_INVALID;
    if (field->type != SM_WIRE_I64 || field->len != 8u)
        return SM_CODEC_ERR_TYPE;
    *out_value = (int64_t)sm_read_u64(field->data);
    return SM_CODEC_OK;
}

sm_codec_status sm_value_field_bool(const sm_value_field *field,
                                    int *out_value) {
    if (!field || !out_value) return SM_CODEC_ERR_INVALID;
    if (field->type != SM_WIRE_BOOL || field->len != 1u || field->data[0] > 1u)
        return SM_CODEC_ERR_TYPE;
    *out_value = field->data[0] != 0;
    return SM_CODEC_OK;
}

sm_codec_status sm_value_field_bytes(const sm_value_field *field,
                                     const uint8_t **out_data,
                                     size_t *out_len) {
    if (!field || !out_data || !out_len) return SM_CODEC_ERR_INVALID;
    if (field->type != SM_WIRE_BYTES) return SM_CODEC_ERR_TYPE;
    *out_data = field->data;
    *out_len = field->len;
    return SM_CODEC_OK;
}

sm_codec_status sm_value_field_string(const sm_value_field *field,
                                      const char **out_value,
                                      size_t *out_len) {
    if (!field || !out_value || !out_len) return SM_CODEC_ERR_INVALID;
    if (field->type != SM_WIRE_STRING) return SM_CODEC_ERR_TYPE;
    *out_value = (const char *)field->data;
    *out_len = field->len;
    return SM_CODEC_OK;
}
