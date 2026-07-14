#include "sm_repository.h"

#include "storage/sm_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SM_META_CODEC_VERSION 1u
#define SM_META_ENTITY_SCHEMA 1u
#define SM_META_ENTITY_COUNTER 2u

struct sm_repository {
    sm_store *store;
    uint32_t schema_version;
    size_t active_uows;
    size_t active_scans;
    sm_repo_status last_status;
    char last_message[256];
};

struct sm_repo_uow {
    sm_repository *repo;
    sm_store_txn *txn;
    int write;
};

struct sm_repo_scan {
    sm_repository *repo;
    sm_store_scan *scan;
};

static sm_repo_status sm_repo_from_store(sm_store_status status) {
    switch (status) {
        case SM_STORE_OK: return SM_REPO_OK;
        case SM_STORE_NOT_FOUND: return SM_REPO_NOT_FOUND;
        case SM_STORE_ITER_END: return SM_REPO_ITER_END;
        case SM_STORE_ERR_IO: return SM_REPO_ERR_IO;
        case SM_STORE_ERR_CORRUPT: return SM_REPO_ERR_CORRUPT;
        case SM_STORE_ERR_NOMEM: return SM_REPO_ERR_NOMEM;
        case SM_STORE_ERR_INVALID: return SM_REPO_ERR_INVALID;
        case SM_STORE_ERR_UNSUPPORTED: return SM_REPO_ERR_UNSUPPORTED;
        case SM_STORE_ERR_BUSY: return SM_REPO_ERR_BUSY;
        case SM_STORE_ERR_READONLY: return SM_REPO_ERR_READONLY;
        case SM_STORE_ERR_LOCKED: return SM_REPO_ERR_LOCKED;
        case SM_STORE_ERR_FULL: return SM_REPO_ERR_FULL;
        case SM_STORE_ERR_CHECKSUM: return SM_REPO_ERR_CHECKSUM;
        default: return SM_REPO_ERR_IO;
    }
}

static sm_repo_status sm_repo_record_store(sm_repository *repo,
                                           sm_store_status status) {
    sm_repo_status mapped = sm_repo_from_store(status);
    if (!repo) return mapped;
    repo->last_status = mapped;
    snprintf(repo->last_message, sizeof(repo->last_message), "%s",
             sm_store_last_message(repo->store));
    return mapped;
}

static sm_repo_status sm_repo_record_local(sm_repository *repo,
                                           sm_repo_status status) {
    if (!repo) return status;
    repo->last_status = status;
    snprintf(repo->last_message, sizeof(repo->last_message), "%s",
             sm_repo_status_name(status));
    return status;
}

static sm_repo_status sm_repo_from_codec(sm_repository *repo,
                                         sm_codec_status status) {
    switch (status) {
        case SM_CODEC_OK: return sm_repo_record_local(repo, SM_REPO_OK);
        case SM_CODEC_ERR_NOMEM:
            return sm_repo_record_local(repo, SM_REPO_ERR_NOMEM);
        case SM_CODEC_ERR_TOO_LARGE:
            return sm_repo_record_local(repo, SM_REPO_ERR_FULL);
        case SM_CODEC_ERR_INVALID:
        case SM_CODEC_ERR_STATE:
            return sm_repo_record_local(repo, SM_REPO_ERR_INVALID);
        case SM_CODEC_NOT_FOUND:
        case SM_CODEC_ERR_TRUNCATED:
        case SM_CODEC_ERR_FORMAT:
        case SM_CODEC_ERR_TYPE:
        case SM_CODEC_ERR_UTF8:
        default:
            return sm_repo_record_local(repo, SM_REPO_ERR_CORRUPT);
    }
}

static sm_repo_status sm_repo_encode_schema(uint32_t schema_version,
                                            uint8_t **out,
                                            size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    status = sm_value_writer_init(&writer, SM_META_CODEC_VERSION,
                                  SM_META_ENTITY_SCHEMA);
    if (status != SM_CODEC_OK) return SM_REPO_ERR_NOMEM;
    status = sm_value_write_u32(&writer, 1, schema_version);
    if (status == SM_CODEC_OK)
        status = sm_value_writer_finish(&writer, out, out_len);
    if (status != SM_CODEC_OK) {
        sm_value_writer_dispose(&writer);
        return status == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM
                                             : SM_REPO_ERR_CORRUPT;
    }
    return SM_REPO_OK;
}

static sm_repo_status sm_repo_decode_schema(const void *data, size_t len,
                                            uint32_t *out_version) {
    sm_value_reader reader;
    sm_value_field field;
    if (sm_value_reader_init(&reader, data, len) != SM_CODEC_OK ||
        reader.version != SM_META_CODEC_VERSION ||
        reader.entity_type != SM_META_ENTITY_SCHEMA ||
        sm_value_reader_find(&reader, 1, &field) != SM_CODEC_OK ||
        sm_value_field_u32(&field, out_version) != SM_CODEC_OK)
        return SM_REPO_ERR_CORRUPT;
    return SM_REPO_OK;
}

static sm_repo_status sm_repository_ensure_schema(sm_repository *repo,
                                                  int read_only) {
    const uint8_t key[] = {SM_NS_SCHEMA_VERSION};
    sm_store_txn *txn = NULL;
    void *value = NULL;
    size_t value_len = 0;
    sm_store_status store_status;
    sm_repo_status status;
    uint32_t found_version = 0;

    store_status = sm_store_txn_begin(repo->store, read_only ? 0 : 1, &txn);
    if (store_status != SM_STORE_OK)
        return sm_repo_record_store(repo, store_status);

    store_status = sm_store_txn_get(txn, key, sizeof(key),
                                    &value, &value_len);
    if (store_status == SM_STORE_NOT_FOUND) {
        uint8_t *encoded = NULL;
        size_t encoded_len = 0;
        if (read_only) {
            (void)sm_store_txn_rollback(&txn);
            return sm_repo_record_local(repo, SM_REPO_SCHEMA_MISMATCH);
        }
        status = sm_repo_encode_schema(repo->schema_version,
                                       &encoded, &encoded_len);
        if (status != SM_REPO_OK) {
            (void)sm_store_txn_rollback(&txn);
            return sm_repo_record_local(repo, status);
        }
        store_status = sm_store_txn_put(txn, key, sizeof(key),
                                        encoded, encoded_len);
        free(encoded);
        if (store_status != SM_STORE_OK) {
            (void)sm_store_txn_rollback(&txn);
            return sm_repo_record_store(repo, store_status);
        }
        return sm_repo_record_store(repo, sm_store_txn_commit(&txn));
    }
    if (store_status != SM_STORE_OK) {
        (void)sm_store_txn_rollback(&txn);
        return sm_repo_record_store(repo, store_status);
    }

    status = sm_repo_decode_schema(value, value_len, &found_version);
    sm_store_value_free(value);
    if (status != SM_REPO_OK) {
        (void)sm_store_txn_rollback(&txn);
        return sm_repo_record_local(repo, status);
    }
    if (found_version != repo->schema_version) {
        (void)sm_store_txn_rollback(&txn);
        return sm_repo_record_local(repo, SM_REPO_SCHEMA_MISMATCH);
    }
    return sm_repo_record_store(repo, sm_store_txn_commit(&txn));
}

void sm_repository_config_default(sm_repository_config *config,
                                  const char *path) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    sm_store_config_default(&config->store, path);
    config->schema_version = SM_REPO_SCHEMA_VERSION;
}

sm_repo_status sm_repository_open(const sm_repository_config *config,
                                  sm_repository **out_repo) {
    sm_repository *repo;
    sm_store_status store_status;
    sm_repo_status status;
    if (!config || !out_repo || config->schema_version == 0)
        return SM_REPO_ERR_INVALID;
    *out_repo = NULL;

    repo = (sm_repository *)calloc(1, sizeof(*repo));
    if (!repo) return SM_REPO_ERR_NOMEM;
    repo->schema_version = config->schema_version;
    store_status = sm_store_open(&config->store, &repo->store);
    if (store_status != SM_STORE_OK) {
        free(repo);
        return sm_repo_from_store(store_status);
    }

    status = sm_repository_ensure_schema(repo, config->store.read_only);
    if (status != SM_REPO_OK) {
        (void)sm_store_close(&repo->store);
        free(repo);
        return status;
    }
    *out_repo = repo;
    return SM_REPO_OK;
}

sm_repo_status sm_repository_close(sm_repository **repo_ptr) {
    sm_repository *repo;
    sm_store_status status;
    if (!repo_ptr || !*repo_ptr) return SM_REPO_OK;
    repo = *repo_ptr;
    if (repo->active_uows != 0 || repo->active_scans != 0)
        return sm_repo_record_local(repo, SM_REPO_ERR_BUSY);
    status = sm_store_close(&repo->store);
    if (status != SM_STORE_OK) return sm_repo_record_store(repo, status);
    free(repo);
    *repo_ptr = NULL;
    return SM_REPO_OK;
}

sm_repo_status sm_repository_verify(sm_repository *repo, int full) {
    if (!repo) return SM_REPO_ERR_INVALID;
    return sm_repo_record_store(repo, sm_store_verify(repo->store, full));
}

sm_repo_status sm_repository_checkpoint(sm_repository *repo) {
    if (!repo) return SM_REPO_ERR_INVALID;
    if (repo->active_uows != 0 || repo->active_scans != 0)
        return sm_repo_record_local(repo, SM_REPO_ERR_BUSY);
    return sm_repo_record_store(repo, sm_store_checkpoint(repo->store));
}

sm_repo_status sm_repo_uow_begin(sm_repository *repo, int write,
                                 sm_repo_uow **out_uow) {
    sm_repo_uow *uow;
    sm_store_status status;
    if (!repo || !out_uow || (write != 0 && write != 1))
        return SM_REPO_ERR_INVALID;
    *out_uow = NULL;
    uow = (sm_repo_uow *)calloc(1, sizeof(*uow));
    if (!uow) return sm_repo_record_local(repo, SM_REPO_ERR_NOMEM);
    status = sm_store_txn_begin(repo->store, write, &uow->txn);
    if (status != SM_STORE_OK) {
        free(uow);
        return sm_repo_record_store(repo, status);
    }
    uow->repo = repo;
    uow->write = write;
    ++repo->active_uows;
    *out_uow = uow;
    return sm_repo_record_local(repo, SM_REPO_OK);
}

sm_repo_status sm_repo_uow_commit(sm_repo_uow **uow_ptr) {
    sm_repo_uow *uow;
    sm_repository *repo;
    sm_store_status status;
    if (!uow_ptr || !*uow_ptr) return SM_REPO_ERR_INVALID;
    uow = *uow_ptr;
    repo = uow->repo;
    status = sm_store_txn_commit(&uow->txn);
    if (repo->active_uows > 0) --repo->active_uows;
    free(uow);
    *uow_ptr = NULL;
    return sm_repo_record_store(repo, status);
}

sm_repo_status sm_repo_uow_rollback(sm_repo_uow **uow_ptr) {
    sm_repo_uow *uow;
    sm_repository *repo;
    sm_store_status status;
    if (!uow_ptr || !*uow_ptr) return SM_REPO_OK;
    uow = *uow_ptr;
    repo = uow->repo;
    status = sm_store_txn_rollback(&uow->txn);
    if (repo->active_uows > 0) --repo->active_uows;
    free(uow);
    *uow_ptr = NULL;
    return sm_repo_record_store(repo, status);
}

sm_repo_status sm_repo_get(sm_repo_uow *uow,
                           const void *key, size_t key_len,
                           void **out_value, size_t *out_value_len) {
    if (!uow) return SM_REPO_ERR_INVALID;
    return sm_repo_record_store(
        uow->repo,
        sm_store_txn_get(uow->txn, key, key_len, out_value, out_value_len));
}

sm_repo_status sm_repo_put(sm_repo_uow *uow,
                           const void *key, size_t key_len,
                           const void *value, size_t value_len) {
    if (!uow || !uow->write) return SM_REPO_ERR_READONLY;
    return sm_repo_record_store(
        uow->repo,
        sm_store_txn_put(uow->txn, key, key_len, value, value_len));
}

sm_repo_status sm_repo_delete(sm_repo_uow *uow,
                              const void *key, size_t key_len) {
    if (!uow || !uow->write) return SM_REPO_ERR_READONLY;
    return sm_repo_record_store(
        uow->repo, sm_store_txn_delete(uow->txn, key, key_len));
}

sm_repo_status sm_repo_unique_claim(sm_repo_uow *uow,
                                    const void *index_key,
                                    size_t index_key_len,
                                    const void *primary_key,
                                    size_t primary_key_len) {
    void *owner = NULL;
    size_t owner_len = 0;
    sm_repo_status status;
    if (!uow || !uow->write || !index_key || index_key_len == 0 ||
        !primary_key || primary_key_len == 0)
        return !uow || uow->write ? SM_REPO_ERR_INVALID
                                  : SM_REPO_ERR_READONLY;

    status = sm_repo_get(uow, index_key, index_key_len, &owner, &owner_len);
    if (status == SM_REPO_NOT_FOUND)
        return sm_repo_put(uow, index_key, index_key_len,
                           primary_key, primary_key_len);
    if (status != SM_REPO_OK) return status;

    if (owner_len == primary_key_len &&
        memcmp(owner, primary_key, primary_key_len) == 0)
        status = SM_REPO_OK;
    else
        status = SM_REPO_CONFLICT;
    sm_repo_value_free(owner);
    return sm_repo_record_local(uow->repo, status);
}

sm_repo_status sm_repo_unique_release(sm_repo_uow *uow,
                                      const void *index_key,
                                      size_t index_key_len,
                                      const void *expected_primary_key,
                                      size_t expected_primary_key_len) {
    void *owner = NULL;
    size_t owner_len = 0;
    sm_repo_status status;
    if (!uow || !uow->write || !index_key || index_key_len == 0 ||
        !expected_primary_key || expected_primary_key_len == 0)
        return !uow || uow->write ? SM_REPO_ERR_INVALID
                                  : SM_REPO_ERR_READONLY;
    status = sm_repo_get(uow, index_key, index_key_len, &owner, &owner_len);
    if (status == SM_REPO_NOT_FOUND)
        return sm_repo_record_local(uow->repo, SM_REPO_ERR_CORRUPT);
    if (status != SM_REPO_OK) return status;
    if (owner_len != expected_primary_key_len ||
        memcmp(owner, expected_primary_key, expected_primary_key_len) != 0) {
        sm_repo_value_free(owner);
        return sm_repo_record_local(uow->repo, SM_REPO_ERR_CORRUPT);
    }
    sm_repo_value_free(owner);
    return sm_repo_delete(uow, index_key, index_key_len);
}

sm_repo_status sm_repo_index_add(sm_repo_uow *uow,
                                 const void *index_key,
                                 size_t index_key_len) {
    static const uint8_t marker = 1;
    return sm_repo_put(uow, index_key, index_key_len, &marker, sizeof(marker));
}

sm_repo_status sm_repo_index_remove(sm_repo_uow *uow,
                                    const void *index_key,
                                    size_t index_key_len) {
    return sm_repo_delete(uow, index_key, index_key_len);
}

static sm_repo_status sm_repo_decode_counter(sm_repository *repo,
                                             const void *data, size_t len,
                                             uint64_t *out_next) {
    sm_value_reader reader;
    sm_value_field field;
    sm_codec_status status;
    status = sm_value_reader_init(&reader, data, len);
    if (status != SM_CODEC_OK || reader.version != SM_META_CODEC_VERSION ||
        reader.entity_type != SM_META_ENTITY_COUNTER)
        return sm_repo_record_local(repo, SM_REPO_ERR_CORRUPT);
    status = sm_value_reader_find(&reader, 1, &field);
    if (status == SM_CODEC_OK) status = sm_value_field_u64(&field, out_next);
    return sm_repo_from_codec(repo, status);
}

static sm_repo_status sm_repo_encode_counter(sm_repository *repo,
                                             uint64_t next,
                                             uint8_t **out,
                                             size_t *out_len) {
    sm_value_writer writer;
    sm_codec_status status;
    status = sm_value_writer_init(&writer, SM_META_CODEC_VERSION,
                                  SM_META_ENTITY_COUNTER);
    if (status == SM_CODEC_OK)
        status = sm_value_write_u64(&writer, 1, next);
    if (status == SM_CODEC_OK)
        status = sm_value_writer_finish(&writer, out, out_len);
    if (status != SM_CODEC_OK) sm_value_writer_dispose(&writer);
    return sm_repo_from_codec(repo, status);
}

sm_repo_status sm_repo_counter_next(sm_repo_uow *uow,
                                    sm_counter_kind kind,
                                    uint64_t *out_id) {
    sm_key_builder key;
    void *value = NULL;
    size_t value_len = 0;
    uint64_t next = 1;
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    sm_repo_status status;
    if (!uow || !uow->write || !out_id || kind < SM_COUNTER_EMPLOYEE ||
        kind > SM_COUNTER_MAX)
        return !uow || uow->write ? SM_REPO_ERR_INVALID
                                  : SM_REPO_ERR_READONLY;

    sm_key_begin(&key, SM_NS_COUNTER);
    if (sm_key_add_u16(&key, (uint16_t)kind) != SM_KEY_OK)
        return sm_repo_record_local(uow->repo, SM_REPO_ERR_INVALID);
    status = sm_repo_get(uow, sm_key_data(&key), sm_key_size(&key),
                         &value, &value_len);
    if (status == SM_REPO_OK) {
        status = sm_repo_decode_counter(uow->repo, value, value_len, &next);
        sm_repo_value_free(value);
        if (status != SM_REPO_OK) return status;
    } else if (status != SM_REPO_NOT_FOUND) {
        return status;
    }
    if (next == 0 || next == UINT64_MAX)
        return sm_repo_record_local(uow->repo, SM_REPO_ERR_FULL);

    status = sm_repo_encode_counter(uow->repo, next + 1u,
                                    &encoded, &encoded_len);
    if (status != SM_REPO_OK) return status;
    status = sm_repo_put(uow, sm_key_data(&key), sm_key_size(&key),
                         encoded, encoded_len);
    free(encoded);
    if (status == SM_REPO_OK) *out_id = next;
    return status;
}

sm_repo_status sm_repo_counter_ensure_at_least(sm_repo_uow *uow,
                                               sm_counter_kind kind,
                                               uint64_t minimum_next) {
    sm_key_builder key;
    void *value = NULL;
    size_t value_len = 0;
    uint64_t current = 1;
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    sm_repo_status status;
    if (!uow || !uow->write || minimum_next == 0 ||
        kind < SM_COUNTER_EMPLOYEE || kind > SM_COUNTER_MAX)
        return !uow || uow->write ? SM_REPO_ERR_INVALID
                                  : SM_REPO_ERR_READONLY;
    sm_key_begin(&key, SM_NS_COUNTER);
    if (sm_key_add_u16(&key, (uint16_t)kind) != SM_KEY_OK)
        return sm_repo_record_local(uow->repo, SM_REPO_ERR_INVALID);
    status = sm_repo_get(uow, sm_key_data(&key), sm_key_size(&key),
                         &value, &value_len);
    if (status == SM_REPO_OK) {
        status = sm_repo_decode_counter(uow->repo, value, value_len,
                                        &current);
        sm_repo_value_free(value);
        if (status != SM_REPO_OK) return status;
        if (current == 0) return sm_repo_record_local(
            uow->repo, SM_REPO_ERR_CORRUPT);
    } else if (status != SM_REPO_NOT_FOUND) {
        return status;
    }
    if (current >= minimum_next)
        return sm_repo_record_local(uow->repo, SM_REPO_OK);
    status = sm_repo_encode_counter(uow->repo, minimum_next,
                                    &encoded, &encoded_len);
    if (status != SM_REPO_OK) return status;
    status = sm_repo_put(uow, sm_key_data(&key), sm_key_size(&key),
                         encoded, encoded_len);
    free(encoded);
    return status;
}

sm_repo_status sm_repo_scan_open(sm_repository *repo,
                                 uint8_t namespace_kind,
                                 const void *start_key,
                                 size_t start_key_len,
                                 const void *end_key,
                                 size_t end_key_len,
                                 sm_repo_scan **out_scan) {
    sm_repo_scan *scan;
    sm_store_status status;
    if (!repo || !out_scan) return SM_REPO_ERR_INVALID;
    *out_scan = NULL;
    scan = (sm_repo_scan *)calloc(1, sizeof(*scan));
    if (!scan) return sm_repo_record_local(repo, SM_REPO_ERR_NOMEM);
    status = sm_store_scan_open(repo->store, namespace_kind,
                                start_key, start_key_len,
                                end_key, end_key_len, &scan->scan);
    if (status != SM_STORE_OK) {
        free(scan);
        return sm_repo_record_store(repo, status);
    }
    scan->repo = repo;
    ++repo->active_scans;
    *out_scan = scan;
    return sm_repo_record_local(repo, SM_REPO_OK);
}

sm_repo_status sm_repo_scan_open_prefix(sm_repository *repo,
                                        uint8_t namespace_kind,
                                        const void *prefix,
                                        size_t prefix_len,
                                        sm_repo_scan **out_scan) {
    uint8_t end[SM_KEY_MAX_SIZE];
    size_t end_len = 0;
    sm_key_status status;
    if (!repo || !prefix || prefix_len == 0 || prefix_len > sizeof(end) ||
        ((const uint8_t *)prefix)[0] != namespace_kind)
        return SM_REPO_ERR_INVALID;
    status = sm_key_prefix_successor(prefix, prefix_len, end,
                                     sizeof(end), &end_len);
    if (status != SM_KEY_OK)
        return sm_repo_record_local(repo, SM_REPO_ERR_INVALID);
    return sm_repo_scan_open(repo, namespace_kind,
                             prefix, prefix_len, end, end_len, out_scan);
}

sm_repo_status sm_repo_scan_next(sm_repo_scan *scan) {
    if (!scan) return SM_REPO_ERR_INVALID;
    return sm_repo_record_store(scan->repo, sm_store_scan_next(scan->scan));
}

sm_repo_status sm_repo_scan_key(sm_repo_scan *scan,
                                const void **out_key,
                                size_t *out_key_len) {
    if (!scan) return SM_REPO_ERR_INVALID;
    return sm_repo_record_store(
        scan->repo, sm_store_scan_key(scan->scan, out_key, out_key_len));
}

sm_repo_status sm_repo_scan_value(sm_repo_scan *scan,
                                  const void **out_value,
                                  size_t *out_value_len) {
    if (!scan) return SM_REPO_ERR_INVALID;
    return sm_repo_record_store(
        scan->repo, sm_store_scan_value(scan->scan,
                                        out_value, out_value_len));
}

sm_repo_status sm_repo_scan_get(sm_repo_scan *scan,
                                const void *key, size_t key_len,
                                void **out_value, size_t *out_value_len) {
    if (!scan) return SM_REPO_ERR_INVALID;
    return sm_repo_record_store(
        scan->repo, sm_store_scan_get(scan->scan, key, key_len,
                                      out_value, out_value_len));
}

sm_repo_status sm_repo_scan_close(sm_repo_scan **scan_ptr) {
    sm_repo_scan *scan;
    sm_repository *repo;
    sm_store_status status;
    if (!scan_ptr || !*scan_ptr) return SM_REPO_OK;
    scan = *scan_ptr;
    repo = scan->repo;
    status = sm_store_scan_close(&scan->scan);
    if (status != SM_STORE_OK) return sm_repo_record_store(repo, status);
    if (repo->active_scans > 0) --repo->active_scans;
    free(scan);
    *scan_ptr = NULL;
    return sm_repo_record_local(repo, SM_REPO_OK);
}

void sm_repo_value_free(void *value) {
    sm_store_value_free(value);
}

sm_repo_status sm_repository_last_status(const sm_repository *repo) {
    return repo ? repo->last_status : SM_REPO_ERR_INVALID;
}

const char *sm_repository_last_message(const sm_repository *repo) {
    return repo ? repo->last_message : "invalid repository";
}

const char *sm_repo_status_name(sm_repo_status status) {
    switch (status) {
        case SM_REPO_OK: return "ok";
        case SM_REPO_NOT_FOUND: return "not found";
        case SM_REPO_ITER_END: return "iteration end";
        case SM_REPO_CONFLICT: return "unique index conflict";
        case SM_REPO_SCHEMA_MISMATCH: return "schema mismatch";
        case SM_REPO_ERR_IO: return "I/O error";
        case SM_REPO_ERR_CORRUPT: return "repository invariant is corrupt";
        case SM_REPO_ERR_NOMEM: return "out of memory";
        case SM_REPO_ERR_INVALID: return "invalid argument";
        case SM_REPO_ERR_UNSUPPORTED: return "unsupported";
        case SM_REPO_ERR_BUSY: return "busy";
        case SM_REPO_ERR_READONLY: return "read only";
        case SM_REPO_ERR_LOCKED: return "locked";
        case SM_REPO_ERR_FULL: return "storage full";
        case SM_REPO_ERR_CHECKSUM: return "checksum mismatch";
        default: return "unknown repository error";
    }
}
