#include "storage/sm_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"
#include "storage/sm_store.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #condition);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_key_encoding(void) {
    sm_key_builder key;
    sm_key_builder larger;
    uint8_t successor[SM_KEY_MAX_SIZE];
    size_t successor_len = 0;
    const uint8_t expected[] = {
        SM_NS_PRODUCT_BY_ID, 0x00, 0x02, 'P', '1',
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };
    const uint8_t prefix[] = {SM_NS_PRODUCT_BY_ID, 0x01, 0xff};
    const uint8_t no_successor[] = {0xff, 0xff};
    char invalid_utf8[] = {(char)0xc0, (char)0x80, '\0'};

    sm_key_begin(&key, 0x20);
    CHECK(sm_key_builder_status(&key) == SM_KEY_ERR_INVALID);

    sm_key_begin(&key, SM_NS_PRODUCT_BY_ID);
    CHECK(sm_key_add_string(&key, "P1") == SM_KEY_OK);
    CHECK(sm_key_add_u64(&key, UINT64_C(0x0102030405060708)) == SM_KEY_OK);
    CHECK(sm_key_size(&key) == sizeof(expected));
    CHECK(memcmp(sm_key_data(&key), expected, sizeof(expected)) == 0);

    sm_key_begin(&larger, SM_NS_PRODUCT_BY_ID);
    CHECK(sm_key_add_string(&larger, "P1") == SM_KEY_OK);
    CHECK(sm_key_add_u64(&larger, UINT64_C(0x0102030405060709)) == SM_KEY_OK);
    CHECK(memcmp(sm_key_data(&key), sm_key_data(&larger),
                 sm_key_size(&key)) < 0);

    CHECK(sm_key_prefix_successor(prefix, sizeof(prefix), successor,
                                  sizeof(successor), &successor_len) ==
          SM_KEY_OK);
    CHECK(successor_len == 2u);
    CHECK(successor[0] == SM_NS_PRODUCT_BY_ID && successor[1] == 0x02);

    CHECK(sm_key_prefix_successor(no_successor, sizeof(no_successor),
                                  successor, sizeof(successor),
                                  &successor_len) == SM_KEY_NO_SUCCESSOR);
    CHECK(successor_len == 0u);

    sm_key_begin(&key, SM_NS_PRODUCT_BY_ID);
    CHECK(sm_key_add_string(&key, invalid_utf8) == SM_KEY_ERR_UTF8);
    CHECK(sm_key_data(&key) == NULL);
    return 0;
}

static int test_value_codec(void) {
    sm_value_writer writer;
    sm_value_reader reader;
    sm_value_field field;
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    uint8_t u8;
    uint32_t u32;
    uint64_t u64;
    int64_t i64;
    int boolean;
    const char *string_value;
    size_t string_len;
    char invalid_utf8[] = {(char)0xc0, (char)0x80, '\0'};
    const uint8_t invalid_bool[] = {
        1, 1, 0, 1,
        0, 1, SM_WIRE_BOOL, 0, 0, 0, 1, 2
    };

    CHECK(sm_value_writer_init(&writer, 1, 0x20) == SM_CODEC_OK);
    CHECK(sm_value_write_u8(&writer, 1, 7) == SM_CODEC_OK);
    CHECK(sm_value_write_u32(&writer, 2, UINT32_C(0x01020304)) == SM_CODEC_OK);
    CHECK(sm_value_write_u64(&writer, 3, UINT64_C(0x0102030405060708)) ==
          SM_CODEC_OK);
    CHECK(sm_value_write_i64(&writer, 4, INT64_C(-1234567)) == SM_CODEC_OK);
    CHECK(sm_value_write_bool(&writer, 5, 1) == SM_CODEC_OK);
    CHECK(sm_value_write_string(&writer, 6, "phase-two") == SM_CODEC_OK);
    CHECK(sm_value_writer_finish(&writer, &encoded, &encoded_len) ==
          SM_CODEC_OK);

    CHECK(sm_value_reader_init(&reader, encoded, encoded_len) == SM_CODEC_OK);
    CHECK(reader.version == 1 && reader.entity_type == 0x20);
    CHECK(reader.field_count == 6);

    CHECK(sm_value_reader_find(&reader, 1, &field) == SM_CODEC_OK);
    CHECK(sm_value_field_u8(&field, &u8) == SM_CODEC_OK && u8 == 7);
    CHECK(sm_value_reader_find(&reader, 2, &field) == SM_CODEC_OK);
    CHECK(sm_value_field_u32(&field, &u32) == SM_CODEC_OK &&
          u32 == UINT32_C(0x01020304));
    CHECK(sm_value_reader_find(&reader, 3, &field) == SM_CODEC_OK);
    CHECK(sm_value_field_u64(&field, &u64) == SM_CODEC_OK &&
          u64 == UINT64_C(0x0102030405060708));
    CHECK(sm_value_reader_find(&reader, 4, &field) == SM_CODEC_OK);
    CHECK(sm_value_field_i64(&field, &i64) == SM_CODEC_OK &&
          i64 == INT64_C(-1234567));
    CHECK(sm_value_reader_find(&reader, 5, &field) == SM_CODEC_OK);
    CHECK(sm_value_field_bool(&field, &boolean) == SM_CODEC_OK && boolean == 1);
    CHECK(sm_value_reader_find(&reader, 6, &field) == SM_CODEC_OK);
    CHECK(sm_value_field_string(&field, &string_value, &string_len) ==
          SM_CODEC_OK);
    CHECK(string_len == strlen("phase-two"));
    CHECK(memcmp(string_value, "phase-two", string_len) == 0);
    CHECK(sm_value_reader_find(&reader, 99, &field) == SM_CODEC_NOT_FOUND);
    CHECK(sm_value_reader_init(&reader, encoded, encoded_len - 1u) ==
          SM_CODEC_ERR_TRUNCATED);
    CHECK(sm_value_reader_init(&reader, invalid_bool, sizeof(invalid_bool)) ==
          SM_CODEC_ERR_FORMAT);
    free(encoded);

    CHECK(sm_value_writer_init(&writer, 1, 0x20) == SM_CODEC_OK);
    CHECK(sm_value_write_u8(&writer, 2, 1) == SM_CODEC_OK);
    CHECK(sm_value_write_u8(&writer, 1, 2) == SM_CODEC_ERR_FORMAT);
    sm_value_writer_dispose(&writer);

    CHECK(sm_value_writer_init(&writer, 1, 0x20) == SM_CODEC_OK);
    CHECK(sm_value_write_string(&writer, 1, invalid_utf8) ==
          SM_CODEC_ERR_UTF8);
    sm_value_writer_dispose(&writer);
    return 0;
}

static void remove_test_db(const char *path) {
    char journal[512];
    remove(path);
    snprintf(journal, sizeof(journal), "%s.journal", path);
    remove(journal);
}

static int build_test_record(uint8_t **out, size_t *out_len) {
    sm_value_writer writer;
    if (sm_value_writer_init(&writer, 1, 0x20) != SM_CODEC_OK)
        return 1;
    if (sm_value_write_string(&writer, 1, "P0001") != SM_CODEC_OK ||
        sm_value_write_string(&writer, 2, "Phase 2 product") != SM_CODEC_OK ||
        sm_value_write_i64(&writer, 3, 1999) != SM_CODEC_OK) {
        sm_value_writer_dispose(&writer);
        return 1;
    }
    if (sm_value_writer_finish(&writer, out, out_len) != SM_CODEC_OK) {
        sm_value_writer_dispose(&writer);
        return 1;
    }
    return 0;
}

static int test_store_round_trip(void) {
    const char *path = "phase2_storage_test.db";
    sm_store_config config;
    sm_store *store = NULL;
    sm_store_txn *txn = NULL;
    sm_store_txn *other_txn = NULL;
    sm_key_builder key;
    sm_key_builder rolled_back_key;
    uint8_t *record = NULL;
    size_t record_len = 0;
    void *loaded = NULL;
    size_t loaded_len = 0;
    sm_store_status status;

    remove_test_db(path);
    sm_store_config_default(&config, path);
    config.durability = SM_STORE_DURABILITY_FULL;
    config.checkpoint_threshold = 0;
    CHECK(sm_store_open(&config, &store) == SM_STORE_OK);

    sm_key_begin(&key, SM_NS_PRODUCT_BY_ID);
    CHECK(sm_key_add_string(&key, "P0001") == SM_KEY_OK);
    CHECK(build_test_record(&record, &record_len) == 0);

    CHECK(sm_store_txn_begin(store, 1, &txn) == SM_STORE_OK);
    CHECK(sm_store_txn_begin(store, 1, &other_txn) == SM_STORE_ERR_BUSY);
    CHECK(other_txn == NULL);
    CHECK(sm_store_txn_put(txn, sm_key_data(&key), sm_key_size(&key),
                           record, record_len) == SM_STORE_OK);
    CHECK(sm_store_txn_commit(&txn) == SM_STORE_OK);
    CHECK(txn == NULL);
    free(record);
    record = NULL;

    CHECK(sm_store_verify(store, 1) == SM_STORE_OK);

    sm_key_begin(&rolled_back_key, SM_NS_PRODUCT_BY_ID);
    CHECK(sm_key_add_string(&rolled_back_key, "P0002") == SM_KEY_OK);
    CHECK(build_test_record(&record, &record_len) == 0);
    CHECK(sm_store_txn_begin(store, 1, &txn) == SM_STORE_OK);
    CHECK(sm_store_txn_put(txn, sm_key_data(&rolled_back_key),
                           sm_key_size(&rolled_back_key), record,
                           record_len) == SM_STORE_OK);
    CHECK(sm_store_txn_rollback(&txn) == SM_STORE_OK);
    free(record);
    record = NULL;

    CHECK(sm_store_close(&store) == SM_STORE_OK);
    CHECK(store == NULL);

    sm_store_config_default(&config, path);
    config.read_only = 1;
    config.create_if_missing = 0;
    CHECK(sm_store_open(&config, &store) == SM_STORE_OK);
    CHECK(sm_store_txn_begin(store, 1, &txn) == SM_STORE_ERR_READONLY);
    CHECK(txn == NULL);
    CHECK(sm_store_txn_begin(store, 0, &txn) == SM_STORE_OK);
    CHECK(sm_store_close(&store) == SM_STORE_ERR_BUSY);
    CHECK(store != NULL);
    status = sm_store_txn_get(txn, sm_key_data(&key), sm_key_size(&key),
                              &loaded, &loaded_len);
    if (status != SM_STORE_OK)
        fprintf(stderr, "reopen get: %s (%s)\n",
                sm_store_status_name(status), sm_store_last_message(store));
    CHECK(status == SM_STORE_OK);
    CHECK(loaded != NULL && loaded_len > 0);
    sm_store_value_free(loaded);
    loaded = NULL;
    CHECK(sm_store_txn_get(txn, sm_key_data(&rolled_back_key),
                           sm_key_size(&rolled_back_key), &loaded,
                           &loaded_len) == SM_STORE_NOT_FOUND);
    CHECK(sm_store_txn_commit(&txn) == SM_STORE_OK);
    CHECK(sm_store_close(&store) == SM_STORE_OK);

    remove_test_db(path);
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_key_encoding();
    failed |= test_value_codec();
    failed |= test_store_round_trip();
    if (failed) {
        fprintf(stderr, "phase 2 storage tests failed\n");
        return 1;
    }
    printf("phase 2 storage tests passed\n");
    return 0;
}
