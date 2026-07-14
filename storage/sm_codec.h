#ifndef SM_CODEC_H
#define SM_CODEC_H

#include <stddef.h>
#include <stdint.h>

#define SM_CODEC_MAX_FIELDS 256u
#define SM_CODEC_MAX_VALUE_SIZE (16u * 1024u * 1024u)
#define SM_CODEC_MAX_STRING_SIZE (1024u * 1024u)

typedef enum sm_codec_status {
    SM_CODEC_OK = 0,
    SM_CODEC_NOT_FOUND = 1,
    SM_CODEC_ERR_INVALID = -1,
    SM_CODEC_ERR_NOMEM = -2,
    SM_CODEC_ERR_TOO_LARGE = -3,
    SM_CODEC_ERR_TRUNCATED = -4,
    SM_CODEC_ERR_FORMAT = -5,
    SM_CODEC_ERR_TYPE = -6,
    SM_CODEC_ERR_UTF8 = -7,
    SM_CODEC_ERR_STATE = -8
} sm_codec_status;

typedef enum sm_wire_type {
    SM_WIRE_U8 = 1,
    SM_WIRE_U32 = 2,
    SM_WIRE_U64 = 3,
    SM_WIRE_I64 = 4,
    SM_WIRE_BYTES = 5,
    SM_WIRE_STRING = 6,
    SM_WIRE_BOOL = 7
} sm_wire_type;

typedef struct sm_value_writer {
    uint8_t *data;
    size_t len;
    size_t capacity;
    uint16_t field_count;
    uint16_t last_field_id;
    sm_codec_status status;
    int finished;
} sm_value_writer;

typedef struct sm_value_field {
    uint16_t id;
    sm_wire_type type;
    const uint8_t *data;
    uint32_t len;
} sm_value_field;

typedef struct sm_value_reader {
    const uint8_t *data;
    size_t len;
    uint8_t version;
    uint8_t entity_type;
    uint16_t field_count;
} sm_value_reader;

sm_codec_status sm_value_writer_init(sm_value_writer *writer,
                                     uint8_t version,
                                     uint8_t entity_type);
void sm_value_writer_dispose(sm_value_writer *writer);
sm_codec_status sm_value_write_u8(sm_value_writer *writer,
                                  uint16_t field_id, uint8_t value);
sm_codec_status sm_value_write_u32(sm_value_writer *writer,
                                   uint16_t field_id, uint32_t value);
sm_codec_status sm_value_write_u64(sm_value_writer *writer,
                                   uint16_t field_id, uint64_t value);
sm_codec_status sm_value_write_i64(sm_value_writer *writer,
                                   uint16_t field_id, int64_t value);
sm_codec_status sm_value_write_bool(sm_value_writer *writer,
                                    uint16_t field_id, int value);
sm_codec_status sm_value_write_bytes(sm_value_writer *writer,
                                     uint16_t field_id,
                                     const void *data, size_t len);
sm_codec_status sm_value_write_string(sm_value_writer *writer,
                                      uint16_t field_id,
                                      const char *value);
sm_codec_status sm_value_writer_finish(sm_value_writer *writer,
                                       uint8_t **out_data, size_t *out_len);

sm_codec_status sm_value_reader_init(sm_value_reader *reader,
                                     const void *data, size_t len);
sm_codec_status sm_value_reader_find(const sm_value_reader *reader,
                                     uint16_t field_id,
                                     sm_value_field *out_field);
sm_codec_status sm_value_field_u8(const sm_value_field *field,
                                  uint8_t *out_value);
sm_codec_status sm_value_field_u32(const sm_value_field *field,
                                   uint32_t *out_value);
sm_codec_status sm_value_field_u64(const sm_value_field *field,
                                   uint64_t *out_value);
sm_codec_status sm_value_field_i64(const sm_value_field *field,
                                   int64_t *out_value);
sm_codec_status sm_value_field_bool(const sm_value_field *field,
                                    int *out_value);
sm_codec_status sm_value_field_bytes(const sm_value_field *field,
                                     const uint8_t **out_data,
                                     size_t *out_len);
sm_codec_status sm_value_field_string(const sm_value_field *field,
                                      const char **out_value,
                                      size_t *out_len);

#endif
