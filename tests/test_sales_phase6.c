#include "app/sm_app_context.h"
#include "app/sm_base_service.h"
#include "app/sm_sales_service.h"
#include "migration/sm_sales_import.h"
#include "repo/sm_sales_repository.h"

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
        sm_repo_status check_status_ = (call);                               \
        if (check_status_ != SM_REPO_OK) {                                   \
            fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__,  \
                    #call, sm_repo_status_name(check_status_));              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static const char *test_database = ".phase6-sales.abdb";

static void cleanup_database(void) {
    char journal[256];
    remove(test_database);
    snprintf(journal, sizeof(journal), "%s.journal", test_database);
    remove(journal);
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t len = strlen(text);
    if (!file) return 0;
    if (fwrite(text, 1, len, file) != len) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static void cleanup_import_fixture(const char *directory) {
    static const char *const names[] = {
        "sales.txt", "pending_sales.txt", "sale_item.txt",
        "sales.abdb", "sales.abdb.journal"
    };
    char path[256];
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        remove(path);
    }
    (void)TEST_RMDIR(directory);
}

static int start_context(void) {
    sm_app_context_config config;
    sm_app_context_config_default(&config);
    config.database_path = test_database;
    config.legacy_data_dir = ".";
    config.import_legacy = 0;
    return sm_app_context_start(&config) == SM_REPO_OK;
}

static int seed_base(Product *product, Member *member) {
    Employee employee;
    memset(&employee, 0, sizeof(employee));
    snprintf(employee.name, sizeof(employee.name), "Cashier");
    snprintf(employee.role, sizeof(employee.role), "cashier");
    employee.status = 1;
    employee.created_at = employee.updated_at = 100;
    CHECK_REPO(sm_service_employee_create(&employee));

    memset(product, 0, sizeof(*product));
    snprintf(product->name, sizeof(product->name), "Tea");
    snprintf(product->barcode, sizeof(product->barcode), "690000000001");
    product->price = 12.50f;
    product->cost = 8.00f;
    product->stock = 5;
    product->min_stock = 1;
    product->status = 1;
    product->created_at = product->updated_at = 100;
    CHECK_REPO(sm_service_product_create(product));

    memset(member, 0, sizeof(*member));
    snprintf(member->phone, sizeof(member->phone), "13900000001");
    snprintf(member->name, sizeof(member->name), "Member");
    member->points = 20;
    member->total_consume = 50.00f;
    member->created_at = member->updated_at = 100;
    CHECK_REPO(sm_service_member_create(member));
    return employee.id;
}

static int test_atomic_completion_and_reopen(void) {
    Product product, persisted_product;
    Member member, persisted_member, updated_member;
    Sale sale, persisted_sale;
    SaleItem item, *items = NULL;
    Sale *sales = NULL;
    sm_stock_adjustment adjustment;
    size_t count = 0;
    int cashier_id;

    cleanup_database();
    CHECK(start_context());
    cashier_id = seed_base(&product, &member);
    CHECK(cashier_id > 0);

    memset(&sale, 0, sizeof(sale));
    sale.cashier_id = cashier_id;
    sale.member_id = member.id;
    sale.created_at = 200;
    CHECK_REPO(sm_service_sale_create(&sale));
    CHECK(sale.id > 0 && sale.status == SALE_PENDING);

    memset(&item, 0, sizeof(item));
    snprintf(item.product_id, sizeof(item.product_id), "%s", product.id);
    snprintf(item.product_name, sizeof(item.product_name), "%s",
             product.name);
    item.quantity = 2.0f;
    item.price = item.original_price = 12.50f;
    item.subtotal = 25.00f;
    CHECK_REPO(sm_service_sale_item_create(sale.id, &item));
    CHECK(item.id > 0);
    CHECK_REPO(sm_service_sale_item_list(sale.id, &items, &count));
    CHECK(count == 1 && items[0].id == item.id);
    sm_service_sales_array_free(items);

    sale.total_amount = sale.final_amount = 25.00f;
    snprintf(sale.payment_method, sizeof(sale.payment_method), "cash");
    sale.cash_received = 30.00f;
    sale.status = SALE_COMPLETED;
    sale.completed_at = 250;
    memset(&adjustment, 0, sizeof(adjustment));
    snprintf(adjustment.product_id, sizeof(adjustment.product_id), "%s",
             product.id);
    adjustment.quantity = 8;
    CHECK(sm_service_sale_complete(&sale, &adjustment, 1, 12,
                                   &updated_member) == SM_REPO_CONFLICT);
    CHECK_REPO(sm_service_sale_get(sale.id, &persisted_sale));
    CHECK(persisted_sale.status == SALE_PENDING);
    CHECK_REPO(sm_service_product_get(product.id, &persisted_product));
    CHECK(persisted_product.stock == 5);
    CHECK_REPO(sm_service_member_get(member.id, &persisted_member));
    CHECK(persisted_member.points == 20 &&
          persisted_member.total_consume == 50.00f);

    adjustment.quantity = 2;
    CHECK_REPO(sm_service_sale_complete(&sale, &adjustment, 1, 12,
                                        &updated_member));
    CHECK(updated_member.points == 32);
    CHECK_REPO(sm_service_sale_get(sale.id, &persisted_sale));
    CHECK(persisted_sale.status == SALE_COMPLETED &&
          persisted_sale.final_amount == 25.00f);
    CHECK_REPO(sm_service_product_get(product.id, &persisted_product));
    CHECK(persisted_product.stock == 3);
    CHECK_REPO(sm_service_member_get(member.id, &persisted_member));
    CHECK(persisted_member.points == 32 &&
          persisted_member.total_consume == 75.00f);
    CHECK_REPO(sm_service_sale_list_completed(&sales, &count));
    CHECK(count == 1 && sales[0].id == sale.id);
    sm_service_sales_array_free(sales);

    CHECK_REPO(sm_app_context_checkpoint());
    CHECK_REPO(sm_app_context_stop());
    CHECK(start_context());
    CHECK_REPO(sm_service_sale_get(sale.id, &persisted_sale));
    CHECK(persisted_sale.status == SALE_COMPLETED);
    CHECK_REPO(sm_service_sale_item_list(sale.id, &items, &count));
    CHECK(count == 1 && items[0].id == item.id);
    sm_service_sales_array_free(items);
    CHECK_REPO(sm_app_context_stop());
    cleanup_database();
    return 0;
}

static int test_cancel_removes_pending_aggregate(void) {
    Product product;
    Member member;
    Sale sale;
    SaleItem item, *items = NULL;
    size_t count = 0;
    int cashier_id;
    cleanup_database();
    CHECK(start_context());
    cashier_id = seed_base(&product, &member);
    memset(&sale, 0, sizeof(sale));
    sale.cashier_id = cashier_id;
    sale.created_at = 300;
    CHECK_REPO(sm_service_sale_create(&sale));
    memset(&item, 0, sizeof(item));
    snprintf(item.product_id, sizeof(item.product_id), "%s", product.id);
    snprintf(item.product_name, sizeof(item.product_name), "%s",
             product.name);
    item.quantity = 1.0f;
    item.price = item.original_price = item.subtotal = 12.50f;
    CHECK_REPO(sm_service_sale_item_create(sale.id, &item));
    CHECK_REPO(sm_service_sale_cancel(sale.id));
    CHECK(sm_service_sale_get(sale.id, &sale) == SM_REPO_NOT_FOUND);
    CHECK_REPO(sm_service_sale_item_list(sale.id, &items, &count));
    CHECK(count == 0 && items == NULL);
    CHECK_REPO(sm_app_context_stop());
    cleanup_database();
    return 0;
}

static int test_sales_migration_is_atomic_and_reentrant(void) {
    const char *directory = ".phase6-import";
    const char *database = ".phase6-import/sales.abdb";
    char path[256];
    sm_repository_config config;
    sm_repository *repo = NULL;
    sm_sales_import_report report;
    sm_repo_uow *uow = NULL;
    sm_sale sale;
    sm_sale_item *items = NULL;
    size_t count = 0;

    cleanup_import_fixture(directory);
    CHECK(TEST_MKDIR(directory) == 0);
    snprintf(path, sizeof(path), "%s/sales.txt", directory);
    CHECK(write_text(path,
                     "40|3|0|25.00|1.00|24.00|cash|1|100|110\n"));
    snprintf(path, sizeof(path), "%s/pending_sales.txt", directory);
    CHECK(write_text(path,
                     "41|3|0|0|0|0|0|0||0|120|0\n"));
    snprintf(path, sizeof(path), "%s/sale_item.txt", directory);
    CHECK(write_text(path,
                     "90|40|P0001|Tea|2|12.50|12.50|25.00|0|0|0\n"));
    sm_repository_config_default(&config, database);
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK_REPO(sm_sales_import_if_needed(repo, directory, &report));
    CHECK(report.imported && report.sales == 2 && report.sale_items == 1);
    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow));
    CHECK_REPO(sm_sale_get(uow, 40, &sale));
    CHECK(sale.status == SALE_COMPLETED && sale.final_cents == 2400);
    CHECK_REPO(sm_repo_uow_commit(&uow));
    CHECK_REPO(sm_sale_item_list(repo, 40, &items, &count));
    CHECK(count == 1 && items[0].id == 90);
    sm_sales_array_free(items);
    CHECK_REPO(sm_sales_import_if_needed(repo, directory, &report));
    CHECK(report.already_completed && !report.imported);
    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow));
    memset(&sale, 0, sizeof(sale));
    sale.cashier_id = 3;
    sale.created_at = 130;
    CHECK_REPO(sm_sale_create(uow, &sale));
    CHECK(sale.id == 42);
    CHECK_REPO(sm_repo_uow_commit(&uow));
    CHECK_REPO(sm_repository_close(&repo));
    cleanup_import_fixture(directory);
    return 0;
}

static int test_sales_migration_bad_line_rolls_back(void) {
    const char *directory = ".phase6-import-bad";
    const char *database = ".phase6-import-bad/sales.abdb";
    char path[256];
    sm_repository_config config;
    sm_repository *repo = NULL;
    sm_sales_import_report report;
    sm_sale *sales = NULL;
    size_t count = 0;
    cleanup_import_fixture(directory);
    CHECK(TEST_MKDIR(directory) == 0);
    snprintf(path, sizeof(path), "%s/sales.txt", directory);
    CHECK(write_text(path, "broken|sale\n"));
    sm_repository_config_default(&config, database);
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK(sm_sales_import_if_needed(repo, directory, &report) ==
          SM_REPO_ERR_CORRUPT);
    CHECK_REPO(sm_sale_list_by_status(repo, SALE_COMPLETED, &sales, &count));
    CHECK(count == 0 && sales == NULL);
    CHECK(write_text(path,
                     "7|1|0|5.00|0|5.00|cash|1|10|11\n"));
    CHECK_REPO(sm_sales_import_if_needed(repo, directory, &report));
    CHECK(report.imported && report.sales == 1);
    CHECK_REPO(sm_repository_close(&repo));
    cleanup_import_fixture(directory);
    return 0;
}

int main(void) {
    CHECK(test_atomic_completion_and_reopen() == 0);
    CHECK(test_cancel_removes_pending_aggregate() == 0);
    CHECK(test_sales_migration_is_atomic_and_reentrant() == 0);
    CHECK(test_sales_migration_bad_line_rolls_back() == 0);
    printf("phase 6 sales transaction tests passed\n");
    return 0;
}
