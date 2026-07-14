#include "app/sm_app_context.h"
#include "app/sm_base_service.h"
#include "app/sm_inventory_service.h"
#include "app/sm_sales_service.h"
#include "migration/sm_inventory_import.h"
#include "repo/sm_inventory_codec.h"
#include "repo/sm_inventory_repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(path) mkdir(path, 0700)
#define TEST_RMDIR(path) rmdir(path)
#endif

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #condition);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define CHECK_REPO(call)                                                     \
    do {                                                                     \
        sm_repo_status status_ = (call);                                     \
        if (status_ != SM_REPO_OK) {                                         \
            fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__,  \
                    #call, sm_repo_status_name(status_));                    \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void remove_database(const char *path) {
    char journal[512];
    remove(path);
    snprintf(journal, sizeof(journal), "%s.journal", path);
    remove(journal);
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t len = strlen(text);
    if (!file) return 0;
    if (fwrite(text, 1, len, file) != len) { fclose(file); return 0; }
    return fclose(file) == 0;
}

static sm_stock_log make_log(const char *product, const char *type,
                             int64_t created_at) {
    sm_stock_log log;
    memset(&log, 0, sizeof(log));
    snprintf(log.product_id, sizeof(log.product_id), "%s", product);
    snprintf(log.type, sizeof(log.type), "%s", type);
    snprintf(log.remark, sizeof(log.remark), "test");
    log.quantity_milli = 2000;
    log.before_stock_milli = 10000;
    log.after_stock_milli = 8000;
    log.operator_id = 3;
    log.created_at = created_at;
    return log;
}

static int test_codec_and_indexes(void) {
    const char *database = ".phase7-repo.abdb";
    sm_repository_config config;
    sm_repository *repo = NULL;
    sm_repo_uow *uow = NULL;
    sm_stock_log first = make_log("P0001", "出库", 100);
    sm_stock_log second = make_log("P0001", "入库", 200);
    sm_stock_log third = make_log("P0002", "出库", 300);
    sm_stock_log decoded;
    sm_stock_log *logs = NULL;
    uint8_t *encoded = NULL;
    size_t encoded_len = 0, count = 0;

    CHECK(sm_stock_log_encode(&first, &encoded, &encoded_len) == SM_CODEC_OK);
    CHECK(sm_stock_log_decode(encoded, encoded_len, &decoded) == SM_CODEC_OK);
    CHECK(strcmp(decoded.product_id, first.product_id) == 0);
    encoded[0] ^= 0xffu;
    CHECK(sm_stock_log_decode(encoded, encoded_len, &decoded) != SM_CODEC_OK);
    free(encoded);

    remove_database(database);
    sm_repository_config_default(&config, database);
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow));
    CHECK_REPO(sm_stock_log_create(uow, &first));
    CHECK_REPO(sm_stock_log_create(uow, &second));
    CHECK_REPO(sm_stock_log_create(uow, &third));
    CHECK(first.id == 1 && second.id == 2 && third.id == 3);
    CHECK_REPO(sm_repo_uow_commit(&uow));

    CHECK_REPO(sm_stock_log_list(repo, "P0001", NULL, 0, 150,
                                 &logs, &count));
    CHECK(count == 1 && logs[0].id == first.id);
    sm_inventory_array_free(logs);
    CHECK_REPO(sm_stock_log_list(repo, NULL, "出库", 0, 400,
                                 &logs, &count));
    CHECK(count == 2);
    sm_inventory_array_free(logs);
    CHECK_REPO(sm_repository_checkpoint(repo));
    CHECK_REPO(sm_repository_close(&repo));
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK_REPO(sm_stock_log_list(repo, NULL, NULL, 0, 400,
                                 &logs, &count));
    CHECK(count == 3);
    sm_inventory_array_free(logs);
    CHECK_REPO(sm_repository_close(&repo));
    remove_database(database);
    return 0;
}

static int start_app(const char *database) {
    sm_app_context_config config;
    sm_app_context_config_default(&config);
    config.database_path = database;
    config.legacy_data_dir = ".";
    config.import_legacy = 0;
    return sm_app_context_start(&config) == SM_REPO_OK;
}

static int test_sale_log_is_atomic(void) {
    const char *database = ".phase7-sale.abdb";
    Employee employee;
    Product product;
    Sale sale;
    SaleItem item;
    StockLog *logs = NULL;
    sm_stock_adjustment adjustment;
    size_t count = 0;
    remove_database(database);
    CHECK(start_app(database));
    memset(&employee, 0, sizeof(employee));
    snprintf(employee.name, sizeof(employee.name), "Cashier");
    snprintf(employee.role, sizeof(employee.role), "cashier");
    employee.status = 1;
    employee.created_at = employee.updated_at = 10;
    CHECK_REPO(sm_service_employee_create(&employee));
    memset(&product, 0, sizeof(product));
    snprintf(product.name, sizeof(product.name), "Tea");
    product.price = 5.0f;
    product.cost = 3.0f;
    product.stock = 3;
    product.status = 1;
    product.created_at = product.updated_at = 10;
    CHECK_REPO(sm_service_product_create(&product));
    memset(&sale, 0, sizeof(sale));
    sale.cashier_id = employee.id;
    sale.created_at = 20;
    CHECK_REPO(sm_service_sale_create(&sale));
    memset(&item, 0, sizeof(item));
    snprintf(item.product_id, sizeof(item.product_id), "%s", product.id);
    snprintf(item.product_name, sizeof(item.product_name), "Tea");
    item.quantity = 2.0f;
    item.price = item.original_price = 5.0f;
    item.subtotal = 10.0f;
    CHECK_REPO(sm_service_sale_item_create(sale.id, &item));
    sale.total_amount = sale.final_amount = 10.0f;
    sale.status = SALE_COMPLETED;
    sale.completed_at = 30;
    snprintf(sale.payment_method, sizeof(sale.payment_method), "cash");
    memset(&adjustment, 0, sizeof(adjustment));
    snprintf(adjustment.product_id, sizeof(adjustment.product_id), "%s",
             product.id);
    adjustment.quantity = 4;
    CHECK(sm_service_sale_complete(&sale, &adjustment, 1, 0, NULL) ==
          SM_REPO_CONFLICT);
    CHECK_REPO(sm_service_stock_log_list(product.id, "出库", 0, 100,
                                         &logs, &count));
    CHECK(count == 0 && logs == NULL);
    adjustment.quantity = 2;
    CHECK_REPO(sm_service_sale_complete(&sale, &adjustment, 1, 0, NULL));
    CHECK_REPO(sm_service_stock_log_list(product.id, "出库", 0, 100,
                                         &logs, &count));
    CHECK(count == 1 && logs[0].quantity == 2.0f &&
          logs[0].before_stock == 3.0f && logs[0].after_stock == 1.0f);
    sm_service_inventory_array_free(logs);
    CHECK_REPO(sm_app_context_stop());
    remove_database(database);
    return 0;
}

static void cleanup_fixture(const char *directory) {
    char path[256];
    const char *names[] = {"stock_log.txt", "store.abdb",
                           "store.abdb.journal"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        remove(path);
    }
    (void)TEST_RMDIR(directory);
}

static int test_inventory_migration(void) {
    const char *directory = ".phase7-import";
    const char *database = ".phase7-import/store.abdb";
    char path[256];
    sm_repository_config config;
    sm_repository *repo = NULL;
    sm_inventory_import_report report;
    sm_stock_log *logs = NULL;
    size_t count = 0;
    cleanup_fixture(directory);
    CHECK(TEST_MKDIR(directory) == 0);
    snprintf(path, sizeof(path), "%s/stock_log.txt", directory);
    CHECK(write_text(path, "broken|record\n"));
    sm_repository_config_default(&config, database);
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK(sm_inventory_import_if_needed(repo, directory, &report) ==
          SM_REPO_ERR_CORRUPT);
    CHECK_REPO(sm_stock_log_list(repo, NULL, NULL, 0, INT64_MAX,
                                 &logs, &count));
    CHECK(count == 0 && logs == NULL);
    CHECK(write_text(path,
                     "9|P0001|入库|3.5|1|4.5|2|legacy|100\n"));
    CHECK_REPO(sm_inventory_import_if_needed(repo, directory, &report));
    CHECK(report.imported && report.stock_logs == 1);
    CHECK_REPO(sm_stock_log_list(repo, "P0001", NULL, 0, 200,
                                 &logs, &count));
    CHECK(count == 1 && logs[0].id == 9 && logs[0].quantity_milli == 3500);
    sm_inventory_array_free(logs);
    CHECK_REPO(sm_inventory_import_if_needed(repo, directory, &report));
    CHECK(report.already_completed);
    CHECK_REPO(sm_repository_close(&repo));
    cleanup_fixture(directory);
    return 0;
}

int main(void) {
    CHECK(test_codec_and_indexes() == 0);
    CHECK(test_sale_log_is_atomic() == 0);
    CHECK(test_inventory_migration() == 0);
    printf("phase 7 inventory ledger tests passed\n");
    return 0;
}
