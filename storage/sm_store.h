#ifndef SM_STORE_H
#define SM_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sm_store sm_store;
typedef struct sm_store_txn sm_store_txn;
typedef struct sm_store_scan sm_store_scan;

typedef enum sm_store_status {
    SM_STORE_OK = 0,
    SM_STORE_NOT_FOUND = 1,
    SM_STORE_ITER_END = 2,
    SM_STORE_ERR_IO = -1,
    SM_STORE_ERR_CORRUPT = -2,
    SM_STORE_ERR_NOMEM = -3,
    SM_STORE_ERR_INVALID = -4,
    SM_STORE_ERR_UNSUPPORTED = -5,
    SM_STORE_ERR_BUSY = -6,
    SM_STORE_ERR_READONLY = -7,
    SM_STORE_ERR_LOCKED = -8,
    SM_STORE_ERR_FULL = -9,
    SM_STORE_ERR_CHECKSUM = -10
} sm_store_status;

typedef enum sm_store_durability {
    SM_STORE_DURABILITY_FAST = 0,
    SM_STORE_DURABILITY_NORMAL = 1,
    SM_STORE_DURABILITY_FULL = 2,
    SM_STORE_DURABILITY_MANUAL = 3
} sm_store_durability;

typedef struct sm_store_config {
    const char *path;
    int read_only;
    int create_if_missing;
    sm_store_durability durability;
    uint32_t checkpoint_threshold;
} sm_store_config;

void sm_store_config_default(sm_store_config *config, const char *path);

sm_store_status sm_store_open(const sm_store_config *config,
                              sm_store **out_store);
sm_store_status sm_store_close(sm_store **store);

sm_store_status sm_store_txn_begin(sm_store *store, int write,
                                   sm_store_txn **out_txn);
sm_store_status sm_store_txn_commit(sm_store_txn **txn);
sm_store_status sm_store_txn_rollback(sm_store_txn **txn);

sm_store_status sm_store_txn_get(sm_store_txn *txn,
                                 const void *key, size_t key_len,
                                 void **out_value, size_t *out_value_len);
sm_store_status sm_store_txn_put(sm_store_txn *txn,
                                 const void *key, size_t key_len,
                                 const void *value, size_t value_len);
sm_store_status sm_store_txn_delete(sm_store_txn *txn,
                                    const void *key, size_t key_len);

sm_store_status sm_store_scan_open(sm_store *store,
                                   uint8_t namespace_kind,
                                   const void *start_key,
                                   size_t start_key_len,
                                   const void *end_key,
                                   size_t end_key_len,
                                   sm_store_scan **out_scan);
sm_store_status sm_store_scan_next(sm_store_scan *scan);
sm_store_status sm_store_scan_key(sm_store_scan *scan,
                                  const void **out_key,
                                  size_t *out_key_len);
sm_store_status sm_store_scan_value(sm_store_scan *scan,
                                    const void **out_value,
                                    size_t *out_value_len);
sm_store_status sm_store_scan_get(sm_store_scan *scan,
                                  const void *key, size_t key_len,
                                  void **out_value, size_t *out_value_len);
sm_store_status sm_store_scan_close(sm_store_scan **scan);

void sm_store_value_free(void *value);
sm_store_status sm_store_verify(sm_store *store, int full);
sm_store_status sm_store_checkpoint(sm_store *store);

sm_store_status sm_store_last_status(const sm_store *store);
const char *sm_store_last_message(const sm_store *store);
const char *sm_store_status_name(sm_store_status status);

#ifdef __cplusplus
}
#endif

#endif
