#include "sm_key.h"

#include <string.h>

static int sm_key_utf8_valid(const uint8_t *s, size_t len) {
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

static sm_key_status sm_key_append(sm_key_builder *builder,
                                   const void *data, size_t len) {
    if (!builder || (!data && len != 0)) return SM_KEY_ERR_INVALID;
    if (builder->status != SM_KEY_OK) return builder->status;
    if (len > SM_KEY_MAX_SIZE - builder->len) {
        builder->status = SM_KEY_ERR_TOO_LARGE;
        return builder->status;
    }
    if (len) memcpy(builder->data + builder->len, data, len);
    builder->len += len;
    return SM_KEY_OK;
}

void sm_key_begin(sm_key_builder *builder, uint8_t namespace_kind) {
    if (!builder) return;
    builder->len = 0;
    builder->status = namespace_kind < SM_KEY_NAMESPACE_MIN
                          ? SM_KEY_ERR_INVALID
                          : SM_KEY_OK;
    if (builder->status == SM_KEY_OK)
        builder->data[builder->len++] = namespace_kind;
}

sm_key_status sm_key_add_u8(sm_key_builder *builder, uint8_t value) {
    return sm_key_append(builder, &value, sizeof(value));
}

sm_key_status sm_key_add_u16(sm_key_builder *builder, uint16_t value) {
    uint8_t bytes[2];
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
    return sm_key_append(builder, bytes, sizeof(bytes));
}

sm_key_status sm_key_add_u32(sm_key_builder *builder, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
    return sm_key_append(builder, bytes, sizeof(bytes));
}

sm_key_status sm_key_add_u64(sm_key_builder *builder, uint64_t value) {
    uint8_t bytes[8];
    size_t i;
    for (i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (uint8_t)(value >> (56u - (unsigned)i * 8u));
    return sm_key_append(builder, bytes, sizeof(bytes));
}

sm_key_status sm_key_add_i64(sm_key_builder *builder, int64_t value) {
    return sm_key_add_u64(builder, ((uint64_t)value) ^ UINT64_C(0x8000000000000000));
}

sm_key_status sm_key_add_bytes(sm_key_builder *builder,
                               const void *data, size_t len) {
    sm_key_status status;
    if (!builder || (!data && len != 0)) return SM_KEY_ERR_INVALID;
    if (len > UINT32_MAX) {
        builder->status = SM_KEY_ERR_TOO_LARGE;
        return builder->status;
    }
    status = sm_key_add_u32(builder, (uint32_t)len);
    if (status != SM_KEY_OK) return status;
    return sm_key_append(builder, data, len);
}

sm_key_status sm_key_add_string(sm_key_builder *builder, const char *value) {
    size_t len;
    sm_key_status status;
    if (!builder) return SM_KEY_ERR_INVALID;
    if (!value) {
        builder->status = SM_KEY_ERR_INVALID;
        return builder->status;
    }
    len = strlen(value);
    if (len > UINT16_MAX) {
        builder->status = SM_KEY_ERR_TOO_LARGE;
        return builder->status;
    }
    if (!sm_key_utf8_valid((const uint8_t *)value, len)) {
        builder->status = SM_KEY_ERR_UTF8;
        return builder->status;
    }
    status = sm_key_add_u16(builder, (uint16_t)len);
    if (status != SM_KEY_OK) return status;
    return sm_key_append(builder, value, len);
}

const uint8_t *sm_key_data(const sm_key_builder *builder) {
    return builder && builder->status == SM_KEY_OK ? builder->data : NULL;
}

size_t sm_key_size(const sm_key_builder *builder) {
    return builder && builder->status == SM_KEY_OK ? builder->len : 0;
}

sm_key_status sm_key_builder_status(const sm_key_builder *builder) {
    return builder ? builder->status : SM_KEY_ERR_INVALID;
}

sm_key_status sm_key_prefix_successor(const void *prefix, size_t prefix_len,
                                      uint8_t *out, size_t out_capacity,
                                      size_t *out_len) {
    size_t i;
    if (!prefix || prefix_len == 0 || !out || !out_len)
        return SM_KEY_ERR_INVALID;
    *out_len = 0;
    if (out_capacity < prefix_len) return SM_KEY_ERR_TOO_LARGE;

    memmove(out, prefix, prefix_len);
    i = prefix_len;
    while (i > 0) {
        --i;
        if (out[i] != 0xffu) {
            ++out[i];
            *out_len = i + 1u;
            return SM_KEY_OK;
        }
    }
    return SM_KEY_NO_SUCCESSOR;
}

static sm_key_status sm_key_reader_take(sm_key_reader *reader,
                                        size_t len,
                                        const uint8_t **out) {
    if (!reader || !out) return SM_KEY_ERR_INVALID;
    if (reader->status != SM_KEY_OK) return reader->status;
    if (reader->offset > reader->len ||
        len > reader->len - reader->offset) {
        reader->status = SM_KEY_ERR_TRUNCATED;
        return reader->status;
    }
    *out = reader->data + reader->offset;
    reader->offset += len;
    return SM_KEY_OK;
}

sm_key_status sm_key_reader_init(sm_key_reader *reader,
                                 const void *key, size_t key_len,
                                 uint8_t expected_namespace) {
    const uint8_t *bytes = (const uint8_t *)key;
    if (!reader || !key || key_len == 0 ||
        expected_namespace < SM_KEY_NAMESPACE_MIN)
        return SM_KEY_ERR_INVALID;
    memset(reader, 0, sizeof(*reader));
    reader->data = bytes;
    reader->len = key_len;
    if (bytes[0] != expected_namespace) {
        reader->status = SM_KEY_ERR_FORMAT;
        return reader->status;
    }
    reader->offset = 1u;
    reader->status = SM_KEY_OK;
    return SM_KEY_OK;
}

sm_key_status sm_key_read_u8(sm_key_reader *reader, uint8_t *out) {
    const uint8_t *bytes;
    sm_key_status status;
    if (!out) return SM_KEY_ERR_INVALID;
    status = sm_key_reader_take(reader, 1u, &bytes);
    if (status == SM_KEY_OK) *out = bytes[0];
    return status;
}

sm_key_status sm_key_read_u16(sm_key_reader *reader, uint16_t *out) {
    const uint8_t *bytes;
    sm_key_status status;
    if (!out) return SM_KEY_ERR_INVALID;
    status = sm_key_reader_take(reader, 2u, &bytes);
    if (status == SM_KEY_OK)
        *out = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    return status;
}

sm_key_status sm_key_read_u32(sm_key_reader *reader, uint32_t *out) {
    const uint8_t *bytes;
    sm_key_status status;
    if (!out) return SM_KEY_ERR_INVALID;
    status = sm_key_reader_take(reader, 4u, &bytes);
    if (status == SM_KEY_OK)
        *out = ((uint32_t)bytes[0] << 24) |
               ((uint32_t)bytes[1] << 16) |
               ((uint32_t)bytes[2] << 8) | bytes[3];
    return status;
}

sm_key_status sm_key_read_u64(sm_key_reader *reader, uint64_t *out) {
    const uint8_t *bytes;
    sm_key_status status;
    uint64_t value = 0;
    size_t i;
    if (!out) return SM_KEY_ERR_INVALID;
    status = sm_key_reader_take(reader, 8u, &bytes);
    if (status != SM_KEY_OK) return status;
    for (i = 0; i < 8u; ++i) value = (value << 8) | bytes[i];
    *out = value;
    return SM_KEY_OK;
}

sm_key_status sm_key_read_i64(sm_key_reader *reader, int64_t *out) {
    uint64_t value;
    sm_key_status status;
    if (!out) return SM_KEY_ERR_INVALID;
    status = sm_key_read_u64(reader, &value);
    if (status == SM_KEY_OK)
        *out = (int64_t)(value ^ UINT64_C(0x8000000000000000));
    return status;
}

sm_key_status sm_key_read_string(sm_key_reader *reader,
                                 const char **out, size_t *out_len) {
    uint16_t len;
    const uint8_t *bytes;
    sm_key_status status;
    if (!out || !out_len) return SM_KEY_ERR_INVALID;
    *out = NULL;
    *out_len = 0;
    status = sm_key_read_u16(reader, &len);
    if (status != SM_KEY_OK) return status;
    status = sm_key_reader_take(reader, len, &bytes);
    if (status != SM_KEY_OK) return status;
    if (!sm_key_utf8_valid(bytes, len) || memchr(bytes, '\0', len) != NULL) {
        reader->status = SM_KEY_ERR_FORMAT;
        return reader->status;
    }
    *out = (const char *)bytes;
    *out_len = len;
    return SM_KEY_OK;
}

sm_key_status sm_key_reader_done(const sm_key_reader *reader) {
    if (!reader) return SM_KEY_ERR_INVALID;
    if (reader->status != SM_KEY_OK) return reader->status;
    return reader->offset == reader->len ? SM_KEY_OK : SM_KEY_ERR_FORMAT;
}
