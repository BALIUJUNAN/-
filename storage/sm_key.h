#ifndef SM_KEY_H
#define SM_KEY_H

#include <stddef.h>
#include <stdint.h>

#define SM_KEY_MAX_SIZE 1024u
#define SM_KEY_NAMESPACE_MIN 0x40u

typedef enum sm_key_status {
    SM_KEY_OK = 0,
    SM_KEY_ERR_INVALID = -1,
    SM_KEY_ERR_TOO_LARGE = -2,
    SM_KEY_ERR_UTF8 = -3,
    SM_KEY_NO_SUCCESSOR = -4,
    SM_KEY_ERR_TRUNCATED = -5,
    SM_KEY_ERR_FORMAT = -6
} sm_key_status;

typedef struct sm_key_builder {
    uint8_t data[SM_KEY_MAX_SIZE];
    size_t len;
    sm_key_status status;
} sm_key_builder;

typedef struct sm_key_reader {
    const uint8_t *data;
    size_t len;
    size_t offset;
    sm_key_status status;
} sm_key_reader;

void sm_key_begin(sm_key_builder *builder, uint8_t namespace_kind);
sm_key_status sm_key_add_u8(sm_key_builder *builder, uint8_t value);
sm_key_status sm_key_add_u16(sm_key_builder *builder, uint16_t value);
sm_key_status sm_key_add_u32(sm_key_builder *builder, uint32_t value);
sm_key_status sm_key_add_u64(sm_key_builder *builder, uint64_t value);
sm_key_status sm_key_add_i64(sm_key_builder *builder, int64_t value);
sm_key_status sm_key_add_bytes(sm_key_builder *builder,
                               const void *data, size_t len);
sm_key_status sm_key_add_string(sm_key_builder *builder, const char *value);

const uint8_t *sm_key_data(const sm_key_builder *builder);
size_t sm_key_size(const sm_key_builder *builder);
sm_key_status sm_key_builder_status(const sm_key_builder *builder);

sm_key_status sm_key_prefix_successor(const void *prefix, size_t prefix_len,
                                      uint8_t *out, size_t out_capacity,
                                      size_t *out_len);

sm_key_status sm_key_reader_init(sm_key_reader *reader,
                                 const void *key, size_t key_len,
                                 uint8_t expected_namespace);
sm_key_status sm_key_read_u8(sm_key_reader *reader, uint8_t *out);
sm_key_status sm_key_read_u16(sm_key_reader *reader, uint16_t *out);
sm_key_status sm_key_read_u32(sm_key_reader *reader, uint32_t *out);
sm_key_status sm_key_read_u64(sm_key_reader *reader, uint64_t *out);
sm_key_status sm_key_read_i64(sm_key_reader *reader, int64_t *out);
sm_key_status sm_key_read_string(sm_key_reader *reader,
                                 const char **out, size_t *out_len);
sm_key_status sm_key_reader_done(const sm_key_reader *reader);

#endif
