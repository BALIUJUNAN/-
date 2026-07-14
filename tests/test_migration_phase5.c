#include "app/sm_app_context.h"
#include "app/sm_base_service.h"
#include "migration/sm_legacy_import.h"
#include "repo/sm_base_repository.h"

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

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);
    if (!file) return 0;
    if (fwrite(text, 1, length, file) != length) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static void cleanup_fixture(const char *directory) {
    static const char *const names[] = {
        "employee.txt", "product.txt", "supplier.txt", "member.txt",
        "config.txt", "store.abdb", "store.abdb.journal"
    };
    char path[256];
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        remove(path);
    }
    (void)TEST_RMDIR(directory);
}

static int create_valid_fixture(const char *directory) {
    char path[256];
    cleanup_fixture(directory);
    if (TEST_MKDIR(directory) != 0) return 0;
    snprintf(path, sizeof(path), "%s/employee.txt", directory);
    if (!write_text(path, "7|Alice|admin|hash|salt|1|100|101\n")) return 0;
    snprintf(path, sizeof(path), "%s/product.txt", directory);
    if (!write_text(path,
                    "P0042|Tea|6900001|12.34|8.10|3|5|drink|9|1|200|201\n"))
        return 0;
    snprintf(path, sizeof(path), "%s/supplier.txt", directory);
    if (!write_text(path, "Supplier|Bob|13800000000|Road 1|9|1\n")) return 0;
    snprintf(path, sizeof(path), "%s/member.txt", directory);
    if (!write_text(path,
                    "11|13900000000|Carol|2|88|123.45|300|301|302\n"))
        return 0;
    snprintf(path, sizeof(path), "%s/config.txt", directory);
    return write_text(path,
                      "shop_name=Phase Five\n"
                      "shop_address=Road 2\n"
                      "shop_phone=01012345678\n"
                      "tax_rate=6.00\n"
                      "auto_backup_interval=45\n"
                      "monthly_fixed_cost=9876.54\n");
}

static int test_context_import_and_counters(void) {
    const char *directory = ".phase5-valid";
    const char *database = ".phase5-valid/store.abdb";
    sm_app_context_config config;
    const sm_legacy_import_report *report;
    Employee employee;
    Product product;
    Supplier supplier;
    Member member;
    SystemConfig system_config;

    CHECK(create_valid_fixture(directory));
    sm_app_context_config_default(&config);
    config.database_path = database;
    config.legacy_data_dir = directory;
    CHECK_REPO(sm_app_context_start(&config));
    report = sm_app_import_report();
    CHECK(report && report->imported && !report->already_completed);
    CHECK(report->employees == 1 && report->products == 1);
    CHECK(report->suppliers == 1 && report->members == 1);
    CHECK(report->has_system_config);

    CHECK_REPO(sm_service_employee_get(7, &employee));
    CHECK(strcmp(employee.name, "Alice") == 0);
    CHECK_REPO(sm_service_product_get_by_barcode("6900001", &product));
    CHECK(strcmp(product.id, "P0042") == 0 && product.stock == 3);
    CHECK_REPO(sm_service_supplier_get(9, &supplier));
    CHECK(strcmp(supplier.name, "Supplier") == 0);
    CHECK_REPO(sm_service_member_get_by_phone("13900000000", &member));
    CHECK(member.id == 11 && member.points == 88);
    CHECK_REPO(sm_service_config_get(&system_config));
    CHECK(strcmp(system_config.shop_name, "Phase Five") == 0);
    CHECK(system_config.auto_backup_interval == 45);
    CHECK(system_config.monthly_fixed_cost > 9876.53f &&
          system_config.monthly_fixed_cost < 9876.55f);

    memset(&employee, 0, sizeof(employee));
    snprintf(employee.name, sizeof(employee.name), "Next employee");
    snprintf(employee.role, sizeof(employee.role), "cashier");
    employee.status = 1;
    employee.created_at = employee.updated_at = 400;
    CHECK_REPO(sm_service_employee_create(&employee));
    CHECK(employee.id == 8);

    memset(&product, 0, sizeof(product));
    snprintf(product.name, sizeof(product.name), "Next product");
    product.price = 1.0f;
    product.cost = 0.5f;
    product.status = 1;
    product.created_at = product.updated_at = 400;
    CHECK_REPO(sm_service_product_create(&product));
    CHECK(strcmp(product.id, "P0043") == 0);
    CHECK_REPO(sm_app_context_checkpoint());
    CHECK_REPO(sm_app_context_stop());

    CHECK_REPO(sm_app_context_start(&config));
    report = sm_app_import_report();
    CHECK(report && !report->imported && report->already_completed);
    CHECK_REPO(sm_service_employee_get(8, &employee));
    CHECK_REPO(sm_app_context_stop());
    cleanup_fixture(directory);
    return 0;
}

static int test_parse_failure_is_atomic(void) {
    const char *directory = ".phase5-invalid";
    const char *database = ".phase5-invalid/store.abdb";
    char employee_path[256];
    sm_repository_config config;
    sm_repository *repo = NULL;
    sm_legacy_import_report report;
    sm_employee *employees = NULL;
    size_t count = 0;

    cleanup_fixture(directory);
    CHECK(TEST_MKDIR(directory) == 0);
    snprintf(employee_path, sizeof(employee_path), "%s/employee.txt", directory);
    CHECK(write_text(employee_path, "broken|record\n"));
    sm_repository_config_default(&config, database);
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK(sm_legacy_import_if_needed(repo, directory, &report) ==
          SM_REPO_ERR_CORRUPT);
    CHECK_REPO(sm_employee_list_all(repo, &employees, &count));
    CHECK(count == 0 && employees == NULL);

    CHECK(write_text(employee_path, "3|Fixed|admin|||1|10|10\n"));
    CHECK_REPO(sm_legacy_import_if_needed(repo, directory, &report));
    CHECK(report.imported && report.employees == 1);
    CHECK_REPO(sm_employee_list_all(repo, &employees, &count));
    CHECK(count == 1 && employees[0].id == 3);
    sm_entity_array_free(employees);
    CHECK_REPO(sm_repository_close(&repo));
    cleanup_fixture(directory);
    return 0;
}

int main(void) {
    CHECK(test_context_import_and_counters() == 0);
    CHECK(test_parse_failure_is_atomic() == 0);
    printf("phase5 migration tests passed\n");
    return 0;
}
