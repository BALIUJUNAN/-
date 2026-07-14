#include "repo/sm_repository.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

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

#define CHECK_REPO(expression, repo)                                         \
    do {                                                                     \
        sm_repo_status check_status = (expression);                          \
        if (check_status != SM_REPO_OK) {                                    \
            fprintf(stderr, "FAIL %s:%d: %s -> %s (%s)\n",                \
                    __FILE__, __LINE__, #expression,                         \
                    sm_repo_status_name(check_status),                       \
                    sm_repository_last_message(repo));                       \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void remove_test_db(const char *path) {
    char journal[512];
    remove(path);
    snprintf(journal, sizeof(journal), "%s.journal", path);
    remove(journal);
}

static int make_string_key(sm_key_builder *key, uint8_t ns,
                           const char *value) {
    sm_key_begin(key, ns);
    return sm_key_add_string(key, value) == SM_KEY_OK ? 0 : 1;
}

static int make_category_key(sm_key_builder *key, const char *category,
                             const char *product_id) {
    sm_key_begin(key, SM_NS_PRODUCT_BY_CATEGORY);
    if (sm_key_add_string(key, category) != SM_KEY_OK) return 1;
    if (product_id && sm_key_add_string(key, product_id) != SM_KEY_OK)
        return 1;
    return 0;
}

static int expect_record(sm_repository *repo, const sm_key_builder *key,
                         const char *expected) {
    sm_repo_uow *uow = NULL;
    void *value = NULL;
    size_t value_len = 0;
    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow), repo);
    CHECK_REPO(sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                           &value, &value_len), repo);
    CHECK(value_len == strlen(expected));
    CHECK(memcmp(value, expected, value_len) == 0);
    sm_repo_value_free(value);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    return 0;
}

static int expect_bytes(sm_repository *repo, const sm_key_builder *key,
                        const void *expected, size_t expected_len) {
    sm_repo_uow *uow = NULL;
    void *value = NULL;
    size_t value_len = 0;
    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow), repo);
    CHECK_REPO(sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                           &value, &value_len), repo);
    CHECK(value_len == expected_len);
    CHECK(memcmp(value, expected, value_len) == 0);
    sm_repo_value_free(value);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    return 0;
}

static int expect_missing(sm_repository *repo, const sm_key_builder *key) {
    sm_repo_uow *uow = NULL;
    void *value = NULL;
    size_t value_len = 0;
    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow), repo);
    CHECK(sm_repo_get(uow, sm_key_data(key), sm_key_size(key),
                      &value, &value_len) == SM_REPO_NOT_FOUND);
    CHECK(value == NULL && value_len == 0);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    return 0;
}

static int count_prefix(sm_repository *repo, const sm_key_builder *prefix,
                        size_t expected_count) {
    sm_repo_scan *scan = NULL;
    size_t count = 0;
    CHECK_REPO(sm_repo_scan_open_prefix(repo,
                                        sm_key_data(prefix)[0],
                                        sm_key_data(prefix),
                                        sm_key_size(prefix), &scan), repo);
    while (sm_repo_scan_next(scan) == SM_REPO_OK) {
        const void *key = NULL;
        const void *value = NULL;
        size_t key_len = 0;
        size_t value_len = 0;
        CHECK_REPO(sm_repo_scan_key(scan, &key, &key_len), repo);
        CHECK_REPO(sm_repo_scan_value(scan, &value, &value_len), repo);
        CHECK(key != NULL && key_len > sm_key_size(prefix));
        CHECK(value != NULL && value_len == 1 &&
              ((const uint8_t *)value)[0] == 1);
        ++count;
    }
    CHECK(sm_repository_last_status(repo) == SM_REPO_ITER_END);
    CHECK_REPO(sm_repo_scan_close(&scan), repo);
    CHECK(count == expected_count);
    return 0;
}

static int test_repository_contract(void) {
    const char *path = "phase3_repository_test.db";
    sm_repository_config config;
    sm_repository *repo = NULL;
    sm_repo_uow *uow = NULL;
    sm_repo_scan *snapshot_scan = NULL;
    sm_key_builder product1;
    sm_key_builder product2;
    sm_key_builder product3;
    sm_key_builder barcode_old;
    sm_key_builder barcode_new;
    sm_key_builder category_prefix;
    sm_key_builder category_namespace;
    sm_key_builder category1;
    sm_key_builder category2;
    sm_key_builder category3;
    uint64_t id = 0;
    size_t snapshot_count = 0;

    remove_test_db(path);
    sm_repository_config_default(&config, path);
    config.store.durability = SM_STORE_DURABILITY_FULL;
    config.store.checkpoint_threshold = 0;
    CHECK_REPO(sm_repository_open(&config, &repo), repo);

    CHECK(make_string_key(&product1, SM_NS_PRODUCT_BY_ID, "P0001") == 0);
    CHECK(make_string_key(&product2, SM_NS_PRODUCT_BY_ID, "P0002") == 0);
    CHECK(make_string_key(&product3, SM_NS_PRODUCT_BY_ID, "P0003") == 0);
    CHECK(make_string_key(&barcode_old, SM_NS_PRODUCT_BY_BARCODE, "BC-1") == 0);
    CHECK(make_string_key(&barcode_new, SM_NS_PRODUCT_BY_BARCODE, "BC-NEW") == 0);
    CHECK(make_category_key(&category_prefix, "food", NULL) == 0);
    sm_key_begin(&category_namespace, SM_NS_PRODUCT_BY_CATEGORY);
    CHECK(sm_key_builder_status(&category_namespace) == SM_KEY_OK);
    CHECK(make_category_key(&category1, "food", "P0001") == 0);
    CHECK(make_category_key(&category2, "food", "P0002") == 0);
    CHECK(make_category_key(&category3, "food", "P0003") == 0);

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_repo_counter_next(uow, SM_COUNTER_PRODUCT, &id), repo);
    CHECK(id == 1);
    CHECK_REPO(sm_repo_counter_next(uow, SM_COUNTER_PRODUCT, &id), repo);
    CHECK(id == 2);
    CHECK_REPO(sm_repo_put(uow, sm_key_data(&product1), sm_key_size(&product1),
                           "record-1", strlen("record-1")), repo);
    CHECK_REPO(sm_repo_unique_claim(uow,
                                    sm_key_data(&barcode_old),
                                    sm_key_size(&barcode_old),
                                    sm_key_data(&product1),
                                    sm_key_size(&product1)), repo);
    CHECK_REPO(sm_repo_index_add(uow, sm_key_data(&category1),
                                 sm_key_size(&category1)), repo);
    CHECK_REPO(sm_repo_put(uow, sm_key_data(&product2), sm_key_size(&product2),
                           "record-2", strlen("record-2")), repo);
    CHECK_REPO(sm_repo_index_add(uow, sm_key_data(&category2),
                                 sm_key_size(&category2)), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_repo_counter_next(uow, SM_COUNTER_PRODUCT, &id), repo);
    CHECK(id == 3);
    CHECK_REPO(sm_repo_uow_rollback(&uow), repo);
    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_repo_counter_next(uow, SM_COUNTER_PRODUCT, &id), repo);
    CHECK(id == 3);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_repo_put(uow, sm_key_data(&product3), sm_key_size(&product3),
                           "conflicting", strlen("conflicting")), repo);
    CHECK(sm_repo_unique_claim(uow,
                               sm_key_data(&barcode_old),
                               sm_key_size(&barcode_old),
                               sm_key_data(&product3),
                               sm_key_size(&product3)) == SM_REPO_CONFLICT);
    CHECK_REPO(sm_repo_uow_rollback(&uow), repo);
    CHECK(expect_missing(repo, &product3) == 0);

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_repo_unique_claim(uow,
                                    sm_key_data(&barcode_old),
                                    sm_key_size(&barcode_old),
                                    sm_key_data(&product1),
                                    sm_key_size(&product1)), repo);
    CHECK_REPO(sm_repo_unique_claim(uow,
                                    sm_key_data(&barcode_new),
                                    sm_key_size(&barcode_new),
                                    sm_key_data(&product1),
                                    sm_key_size(&product1)), repo);
    CHECK_REPO(sm_repo_unique_release(uow,
                                      sm_key_data(&barcode_old),
                                      sm_key_size(&barcode_old),
                                      sm_key_data(&product1),
                                      sm_key_size(&product1)), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    CHECK(expect_missing(repo, &barcode_old) == 0);
    CHECK(expect_bytes(repo, &barcode_new,
                       sm_key_data(&product1), sm_key_size(&product1)) == 0);

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK(sm_repo_unique_release(uow,
                                 sm_key_data(&barcode_new),
                                 sm_key_size(&barcode_new),
                                 sm_key_data(&product2),
                                 sm_key_size(&product2)) == SM_REPO_ERR_CORRUPT);
    CHECK_REPO(sm_repo_uow_rollback(&uow), repo);

    CHECK_REPO(sm_repo_scan_open_prefix(repo, SM_NS_PRODUCT_BY_CATEGORY,
                                        sm_key_data(&category_prefix),
                                        sm_key_size(&category_prefix),
                                        &snapshot_scan), repo);
    CHECK(sm_repository_close(&repo) == SM_REPO_ERR_BUSY);
    CHECK(repo != NULL);

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_repo_put(uow, sm_key_data(&product3), sm_key_size(&product3),
                           "record-3", strlen("record-3")), repo);
    CHECK_REPO(sm_repo_index_add(uow, sm_key_data(&category3),
                                 sm_key_size(&category3)), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);

    while (sm_repo_scan_next(snapshot_scan) == SM_REPO_OK)
        ++snapshot_count;
    CHECK(snapshot_count == 2);
    CHECK_REPO(sm_repo_scan_close(&snapshot_scan), repo);
    CHECK(count_prefix(repo, &category_prefix, 3) == 0);
    CHECK(count_prefix(repo, &category_namespace, 3) == 0);
    CHECK(expect_record(repo, &product1, "record-1") == 0);
    CHECK(expect_record(repo, &product2, "record-2") == 0);
    CHECK(expect_record(repo, &product3, "record-3") == 0);
    CHECK_REPO(sm_repository_verify(repo, 1), repo);
    CHECK_REPO(sm_repository_close(&repo), repo);

    sm_repository_config_default(&config, path);
    config.schema_version = SM_REPO_SCHEMA_VERSION + 1u;
    CHECK(sm_repository_open(&config, &repo) == SM_REPO_SCHEMA_MISMATCH);
    CHECK(repo == NULL);

    sm_repository_config_default(&config, path);
    config.store.read_only = 1;
    config.store.create_if_missing = 0;
    CHECK_REPO(sm_repository_open(&config, &repo), repo);
    CHECK(sm_repo_uow_begin(repo, 1, &uow) == SM_REPO_ERR_READONLY);
    CHECK(uow == NULL);
    CHECK(count_prefix(repo, &category_prefix, 3) == 0);
    CHECK_REPO(sm_repository_close(&repo), repo);

    sm_repository_config_default(&config, path);
    CHECK_REPO(sm_repository_open(&config, &repo), repo);
    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_repo_counter_next(uow, SM_COUNTER_PRODUCT, &id), repo);
    CHECK(id == 4);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    CHECK_REPO(sm_repository_close(&repo), repo);

    remove_test_db(path);
    return 0;
}

int main(void) {
    if (test_repository_contract() != 0) {
        fprintf(stderr, "phase 3 repository tests failed\n");
        return 1;
    }
    printf("phase 3 repository tests passed\n");
    return 0;
}
