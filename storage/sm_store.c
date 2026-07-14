#include "sm_store.h"

#include <abyss/abyss.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sm_store {
    abyss_db *db;
    size_t active_txns;
    size_t active_scans;
    int write_txn_active;
    int read_only;
    sm_store_status last_status;
    char last_message[256];
};

struct sm_store_txn {
    sm_store *store;
    abyss_txn *txn;
    int write;
};

struct sm_store_scan {
    sm_store *store;
    abyss_snapshot *snapshot;
    abyss_cursor *cursor;
};

static sm_store_status sm_store_map_status(abyss_status status) {
    switch (status) {
        case ABYSS_OK: return SM_STORE_OK;
        case ABYSS_NOT_FOUND: return SM_STORE_NOT_FOUND;
        case ABYSS_ITER_END: return SM_STORE_ITER_END;
        case ABYSS_ERR_IO: return SM_STORE_ERR_IO;
        case ABYSS_ERR_CORRUPT: return SM_STORE_ERR_CORRUPT;
        case ABYSS_ERR_NOMEM: return SM_STORE_ERR_NOMEM;
        case ABYSS_ERR_INVALID: return SM_STORE_ERR_INVALID;
        case ABYSS_ERR_UNSUPPORTED: return SM_STORE_ERR_UNSUPPORTED;
        case ABYSS_ERR_BUSY: return SM_STORE_ERR_BUSY;
        case ABYSS_ERR_READONLY: return SM_STORE_ERR_READONLY;
        case ABYSS_ERR_LOCKED: return SM_STORE_ERR_LOCKED;
        case ABYSS_ERR_FULL: return SM_STORE_ERR_FULL;
        case ABYSS_ERR_CHECKSUM: return SM_STORE_ERR_CHECKSUM;
        default: return SM_STORE_ERR_IO;
    }
}

static sm_store_status sm_store_record(sm_store *store, abyss_status status) {
    sm_store_status mapped = sm_store_map_status(status);
    if (!store) return mapped;

    store->last_status = mapped;
    store->last_message[0] = '\0';
    if (status == ABYSS_OK) return mapped;

    abyss_error_info info;
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.version = ABYSS_API_VERSION;
    if (abyss_last_error(store->db, &info) == ABYSS_OK && info.message[0]) {
        snprintf(store->last_message, sizeof(store->last_message), "%s",
                 info.message);
    } else {
        snprintf(store->last_message, sizeof(store->last_message), "%s",
                 sm_store_status_name(mapped));
    }
    return mapped;
}

static sm_store_status sm_store_record_local(sm_store *store,
                                             sm_store_status status) {
    if (!store) return status;
    store->last_status = status;
    snprintf(store->last_message, sizeof(store->last_message), "%s",
             sm_store_status_name(status));
    return status;
}

void sm_store_config_default(sm_store_config *config, const char *path) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->path = path;
    config->read_only = 0;
    config->create_if_missing = 1;
    config->durability = SM_STORE_DURABILITY_NORMAL;
    config->checkpoint_threshold = 1000u;
}

sm_store_status sm_store_open(const sm_store_config *config,
                              sm_store **out_store) {
    abyss_open_options options;
    abyss_status status;
    sm_store *store;

    if (!config || !out_store || !config->path || !config->path[0])
        return SM_STORE_ERR_INVALID;
    if (config->read_only && config->create_if_missing)
        return SM_STORE_ERR_INVALID;
    if (config->durability < SM_STORE_DURABILITY_FAST ||
        config->durability > SM_STORE_DURABILITY_MANUAL)
        return SM_STORE_ERR_INVALID;

    *out_store = NULL;
    status = abyss_options_default(&options);
    if (status != ABYSS_OK) return sm_store_map_status(status);

    options.flags = config->read_only ? ABYSS_OPEN_READONLY
                                      : ABYSS_OPEN_READWRITE;
    if (config->create_if_missing) options.flags |= ABYSS_OPEN_CREATE;
    options.durability_mode = (uint32_t)config->durability;
    options.checkpoint_threshold = config->checkpoint_threshold;

    store = (sm_store *)calloc(1, sizeof(*store));
    if (!store) return SM_STORE_ERR_NOMEM;

    status = abyss_open_ex(config->path, &options, &store->db);
    if (status != ABYSS_OK) {
        free(store);
        return sm_store_map_status(status);
    }

    store->last_status = SM_STORE_OK;
    store->read_only = config->read_only;
    store->last_message[0] = '\0';
    *out_store = store;
    return SM_STORE_OK;
}

sm_store_status sm_store_close(sm_store **store_ptr) {
    sm_store *store;
    abyss_status status;

    if (!store_ptr || !*store_ptr) return SM_STORE_OK;
    store = *store_ptr;
    if (store->active_txns != 0 || store->active_scans != 0)
        return sm_store_record_local(store, SM_STORE_ERR_BUSY);
    status = abyss_close(store->db);
    if (status != ABYSS_OK) return sm_store_record(store, status);

    store->db = NULL;
    free(store);
    *store_ptr = NULL;
    return SM_STORE_OK;
}

sm_store_status sm_store_checkpoint(sm_store *store) {
    if (!store) return SM_STORE_ERR_INVALID;
    if (store->read_only) return sm_store_record_local(store,
                                                       SM_STORE_ERR_READONLY);
    if (store->active_txns != 0 || store->active_scans != 0)
        return sm_store_record_local(store, SM_STORE_ERR_BUSY);
    return sm_store_record(store, abyss_checkpoint(store->db));
}

sm_store_status sm_store_txn_begin(sm_store *store, int write,
                                   sm_store_txn **out_txn) {
    sm_store_txn *wrapper;
    abyss_status status;

    if (!store || !out_txn || (write != 0 && write != 1))
        return SM_STORE_ERR_INVALID;
    *out_txn = NULL;
    if (write && store->read_only)
        return sm_store_record_local(store, SM_STORE_ERR_READONLY);
    if (write && store->write_txn_active)
        return sm_store_record_local(store, SM_STORE_ERR_BUSY);

    wrapper = (sm_store_txn *)calloc(1, sizeof(*wrapper));
    if (!wrapper) return sm_store_record(store, ABYSS_ERR_NOMEM);

    wrapper->store = store;
    wrapper->write = write;
    status = abyss_txn_begin(store->db,
                             write ? ABYSS_TXN_WRITE : ABYSS_TXN_READ,
                             &wrapper->txn);
    if (status != ABYSS_OK) {
        free(wrapper);
        return sm_store_record(store, status);
    }

    *out_txn = wrapper;
    ++store->active_txns;
    if (write) store->write_txn_active = 1;
    return sm_store_record(store, ABYSS_OK);
}

sm_store_status sm_store_txn_commit(sm_store_txn **txn_ptr) {
    sm_store_txn *wrapper;
    sm_store *store;
    abyss_status status;

    if (!txn_ptr || !*txn_ptr) return SM_STORE_ERR_INVALID;
    wrapper = *txn_ptr;
    store = wrapper->store;
    status = abyss_txn_commit(wrapper->txn);

    if (store->active_txns > 0) --store->active_txns;
    if (wrapper->write) store->write_txn_active = 0;

    wrapper->txn = NULL;
    free(wrapper);
    *txn_ptr = NULL;
    return sm_store_record(store, status);
}

sm_store_status sm_store_txn_rollback(sm_store_txn **txn_ptr) {
    sm_store_txn *wrapper;
    sm_store *store;
    abyss_status status;

    if (!txn_ptr || !*txn_ptr) return SM_STORE_OK;
    wrapper = *txn_ptr;
    store = wrapper->store;
    status = abyss_txn_rollback(wrapper->txn);

    if (store->active_txns > 0) --store->active_txns;
    if (wrapper->write) store->write_txn_active = 0;

    wrapper->txn = NULL;
    free(wrapper);
    *txn_ptr = NULL;
    return sm_store_record(store, status);
}

sm_store_status sm_store_txn_get(sm_store_txn *txn,
                                 const void *key, size_t key_len,
                                 void **out_value, size_t *out_value_len) {
    abyss_status status;
    if (!txn || !key || key_len == 0 || !out_value || !out_value_len)
        return SM_STORE_ERR_INVALID;
    *out_value = NULL;
    *out_value_len = 0;
    status = abyss_txn_get(txn->txn, key, key_len, out_value, out_value_len);
    return sm_store_record(txn->store, status);
}

sm_store_status sm_store_txn_put(sm_store_txn *txn,
                                 const void *key, size_t key_len,
                                 const void *value, size_t value_len) {
    abyss_status status;
    if (!txn || !txn->write || !key || key_len == 0 ||
        (!value && value_len != 0))
        return txn && !txn->write ? SM_STORE_ERR_READONLY
                                  : SM_STORE_ERR_INVALID;
    status = abyss_txn_put(txn->txn, key, key_len, value, value_len);
    return sm_store_record(txn->store, status);
}

sm_store_status sm_store_txn_delete(sm_store_txn *txn,
                                    const void *key, size_t key_len) {
    abyss_status status;
    if (!txn || !txn->write || !key || key_len == 0)
        return txn && !txn->write ? SM_STORE_ERR_READONLY
                                  : SM_STORE_ERR_INVALID;
    status = abyss_txn_delete(txn->txn, key, key_len);
    return sm_store_record(txn->store, status);
}

sm_store_status sm_store_scan_open(sm_store *store,
                                   uint8_t namespace_kind,
                                   const void *start_key,
                                   size_t start_key_len,
                                   const void *end_key,
                                   size_t end_key_len,
                                   sm_store_scan **out_scan) {
    sm_store_scan *scan;
    abyss_status status;

    if (!store || !out_scan || namespace_kind < ABYSS_NAMESPACE_USER_MIN ||
        (start_key == NULL && start_key_len != 0) ||
        (start_key != NULL && start_key_len == 0) ||
        (end_key == NULL && end_key_len != 0) ||
        (end_key != NULL && end_key_len == 0))
        return SM_STORE_ERR_INVALID;
    if (start_key && ((const uint8_t *)start_key)[0] != namespace_kind)
        return SM_STORE_ERR_INVALID;
    if (end_key && ((const uint8_t *)end_key)[0] != namespace_kind &&
        !(end_key_len == 1 && namespace_kind != UINT8_MAX &&
          ((const uint8_t *)end_key)[0] == (uint8_t)(namespace_kind + 1u)))
        return SM_STORE_ERR_INVALID;
    *out_scan = NULL;

    scan = (sm_store_scan *)calloc(1, sizeof(*scan));
    if (!scan) return sm_store_record(store, ABYSS_ERR_NOMEM);
    scan->store = store;

    status = abyss_snapshot_current(store->db, &scan->snapshot);
    if (status != ABYSS_OK) {
        free(scan);
        return sm_store_record(store, status);
    }
    status = abyss_cursor_open_range(scan->snapshot, namespace_kind,
                                     start_key, start_key_len,
                                     end_key, end_key_len,
                                     &scan->cursor);
    if (status != ABYSS_OK) {
        (void)abyss_snapshot_release(scan->snapshot);
        free(scan);
        return sm_store_record(store, status);
    }

    ++store->active_scans;
    *out_scan = scan;
    return sm_store_record(store, ABYSS_OK);
}

sm_store_status sm_store_scan_next(sm_store_scan *scan) {
    if (!scan || !scan->cursor) return SM_STORE_ERR_INVALID;
    return sm_store_record(scan->store, abyss_cursor_next(scan->cursor));
}

sm_store_status sm_store_scan_key(sm_store_scan *scan,
                                  const void **out_key,
                                  size_t *out_key_len) {
    if (!scan || !scan->cursor || !out_key || !out_key_len)
        return SM_STORE_ERR_INVALID;
    *out_key = NULL;
    *out_key_len = 0;
    return sm_store_record(scan->store,
                           abyss_cursor_key(scan->cursor, out_key,
                                            out_key_len));
}

sm_store_status sm_store_scan_value(sm_store_scan *scan,
                                    const void **out_value,
                                    size_t *out_value_len) {
    if (!scan || !scan->cursor || !out_value || !out_value_len)
        return SM_STORE_ERR_INVALID;
    *out_value = NULL;
    *out_value_len = 0;
    return sm_store_record(scan->store,
                           abyss_cursor_value(scan->cursor, out_value,
                                             out_value_len));
}

sm_store_status sm_store_scan_get(sm_store_scan *scan,
                                  const void *key, size_t key_len,
                                  void **out_value, size_t *out_value_len) {
    if (!scan || !scan->snapshot || !key || key_len == 0 ||
        !out_value || !out_value_len)
        return SM_STORE_ERR_INVALID;
    *out_value = NULL;
    *out_value_len = 0;
    return sm_store_record(
        scan->store,
        abyss_snapshot_get(scan->snapshot, key, key_len,
                           out_value, out_value_len));
}

sm_store_status sm_store_scan_close(sm_store_scan **scan_ptr) {
    sm_store_scan *scan;
    abyss_status status;

    if (!scan_ptr || !*scan_ptr) return SM_STORE_OK;
    scan = *scan_ptr;
    if (scan->cursor) {
        status = abyss_cursor_close(scan->cursor);
        if (status != ABYSS_OK) return sm_store_record(scan->store, status);
        scan->cursor = NULL;
    }
    if (scan->snapshot) {
        status = abyss_snapshot_release(scan->snapshot);
        if (status != ABYSS_OK) return sm_store_record(scan->store, status);
        scan->snapshot = NULL;
    }

    if (scan->store->active_scans > 0) --scan->store->active_scans;
    sm_store_record(scan->store, ABYSS_OK);
    free(scan);
    *scan_ptr = NULL;
    return SM_STORE_OK;
}

void sm_store_value_free(void *value) {
    if (value) abyss_free(value);
}

sm_store_status sm_store_verify(sm_store *store, int full) {
    abyss_status status;
    if (!store || (full != 0 && full != 1)) return SM_STORE_ERR_INVALID;
    status = abyss_verify(store->db,
                          full ? ABYSS_VERIFY_FULL : ABYSS_VERIFY_LIGHT);
    return sm_store_record(store, status);
}

sm_store_status sm_store_last_status(const sm_store *store) {
    return store ? store->last_status : SM_STORE_ERR_INVALID;
}

const char *sm_store_last_message(const sm_store *store) {
    return store ? store->last_message : "invalid store";
}

const char *sm_store_status_name(sm_store_status status) {
    switch (status) {
        case SM_STORE_OK: return "ok";
        case SM_STORE_NOT_FOUND: return "not found";
        case SM_STORE_ITER_END: return "iteration end";
        case SM_STORE_ERR_IO: return "I/O error";
        case SM_STORE_ERR_CORRUPT: return "corrupt data";
        case SM_STORE_ERR_NOMEM: return "out of memory";
        case SM_STORE_ERR_INVALID: return "invalid argument";
        case SM_STORE_ERR_UNSUPPORTED: return "unsupported";
        case SM_STORE_ERR_BUSY: return "busy";
        case SM_STORE_ERR_READONLY: return "read only";
        case SM_STORE_ERR_LOCKED: return "locked";
        case SM_STORE_ERR_FULL: return "storage full";
        case SM_STORE_ERR_CHECKSUM: return "checksum mismatch";
        default: return "unknown storage error";
    }
}
