#ifndef SM_REPOSITORY_H
#define SM_REPOSITORY_H

#include "storage/sm_store.h"

#include <stddef.h>
#include <stdint.h>

#define SM_REPO_SCHEMA_VERSION 1u

typedef struct sm_repository sm_repository;
typedef struct sm_repo_uow sm_repo_uow;
typedef struct sm_repo_scan sm_repo_scan;

typedef enum sm_repo_status {
    SM_REPO_OK = 0,
    SM_REPO_NOT_FOUND = 1,
    SM_REPO_ITER_END = 2,
    SM_REPO_CONFLICT = 3,
    SM_REPO_SCHEMA_MISMATCH = 4,
    SM_REPO_ERR_IO = -1,
    SM_REPO_ERR_CORRUPT = -2,
    SM_REPO_ERR_NOMEM = -3,
    SM_REPO_ERR_INVALID = -4,
    SM_REPO_ERR_UNSUPPORTED = -5,
    SM_REPO_ERR_BUSY = -6,
    SM_REPO_ERR_READONLY = -7,
    SM_REPO_ERR_LOCKED = -8,
    SM_REPO_ERR_FULL = -9,
    SM_REPO_ERR_CHECKSUM = -10
} sm_repo_status;

typedef enum sm_counter_kind {
    SM_COUNTER_EMPLOYEE = 1,
    SM_COUNTER_PRODUCT = 2,
    SM_COUNTER_SUPPLIER = 3,
    SM_COUNTER_MEMBER = 4,
    SM_COUNTER_SALE = 5,
    SM_COUNTER_SALE_ITEM = 6,
    SM_COUNTER_STOCK_LOG = 7,
    SM_COUNTER_PURCHASE = 8,
    SM_COUNTER_PURCHASE_ITEM = 9,
    SM_COUNTER_SCHEDULE = 10,
    SM_COUNTER_PROMOTION = 11,
    SM_COUNTER_COMBO = 12,
    SM_COUNTER_COMBO_ITEM = 13,
    SM_COUNTER_SETTLEMENT = 14,
    SM_COUNTER_STORE = 15,
    SM_COUNTER_TRANSFER = 16,
    SM_COUNTER_TRANSFER_ITEM = 17,
    SM_COUNTER_PAYABLE = 18,
    SM_COUNTER_PAYMENT = 19,
    SM_COUNTER_VIP_TRANSACTION = 20,
    SM_COUNTER_AUDIT = 21,
    SM_COUNTER_MEMBER_CONSUME = 22,
    SM_COUNTER_BATCH = 23,
    SM_COUNTER_MAX = SM_COUNTER_BATCH
} sm_counter_kind;

typedef struct sm_repository_config {
    sm_store_config store;
    uint32_t schema_version;
} sm_repository_config;

void sm_repository_config_default(sm_repository_config *config,
                                  const char *path);
sm_repo_status sm_repository_open(const sm_repository_config *config,
                                  sm_repository **out_repo);
sm_repo_status sm_repository_close(sm_repository **repo);
sm_repo_status sm_repository_verify(sm_repository *repo, int full);
sm_repo_status sm_repository_checkpoint(sm_repository *repo);

sm_repo_status sm_repo_uow_begin(sm_repository *repo, int write,
                                 sm_repo_uow **out_uow);
sm_repo_status sm_repo_uow_commit(sm_repo_uow **uow);
sm_repo_status sm_repo_uow_rollback(sm_repo_uow **uow);

sm_repo_status sm_repo_get(sm_repo_uow *uow,
                           const void *key, size_t key_len,
                           void **out_value, size_t *out_value_len);
sm_repo_status sm_repo_put(sm_repo_uow *uow,
                           const void *key, size_t key_len,
                           const void *value, size_t value_len);
sm_repo_status sm_repo_delete(sm_repo_uow *uow,
                              const void *key, size_t key_len);

sm_repo_status sm_repo_unique_claim(sm_repo_uow *uow,
                                    const void *index_key,
                                    size_t index_key_len,
                                    const void *primary_key,
                                    size_t primary_key_len);
sm_repo_status sm_repo_unique_release(sm_repo_uow *uow,
                                      const void *index_key,
                                      size_t index_key_len,
                                      const void *expected_primary_key,
                                      size_t expected_primary_key_len);
sm_repo_status sm_repo_index_add(sm_repo_uow *uow,
                                 const void *index_key,
                                 size_t index_key_len);
sm_repo_status sm_repo_index_remove(sm_repo_uow *uow,
                                    const void *index_key,
                                    size_t index_key_len);

sm_repo_status sm_repo_counter_next(sm_repo_uow *uow,
                                    sm_counter_kind kind,
                                    uint64_t *out_id);
sm_repo_status sm_repo_counter_ensure_at_least(sm_repo_uow *uow,
                                               sm_counter_kind kind,
                                               uint64_t minimum_next);

sm_repo_status sm_repo_scan_open(sm_repository *repo,
                                 uint8_t namespace_kind,
                                 const void *start_key,
                                 size_t start_key_len,
                                 const void *end_key,
                                 size_t end_key_len,
                                 sm_repo_scan **out_scan);
sm_repo_status sm_repo_scan_open_prefix(sm_repository *repo,
                                        uint8_t namespace_kind,
                                        const void *prefix,
                                        size_t prefix_len,
                                        sm_repo_scan **out_scan);
sm_repo_status sm_repo_scan_next(sm_repo_scan *scan);
sm_repo_status sm_repo_scan_key(sm_repo_scan *scan,
                                const void **out_key,
                                size_t *out_key_len);
sm_repo_status sm_repo_scan_value(sm_repo_scan *scan,
                                  const void **out_value,
                                  size_t *out_value_len);
sm_repo_status sm_repo_scan_get(sm_repo_scan *scan,
                                const void *key, size_t key_len,
                                void **out_value, size_t *out_value_len);
sm_repo_status sm_repo_scan_close(sm_repo_scan **scan);

void sm_repo_value_free(void *value);
sm_repo_status sm_repository_last_status(const sm_repository *repo);
const char *sm_repository_last_message(const sm_repository *repo);
const char *sm_repo_status_name(sm_repo_status status);

#endif
