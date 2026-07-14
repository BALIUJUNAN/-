#include "app/sm_app_context.h"
#include "app/sm_base_service.h"
#include "app/sm_finance_service.h"
#include "app/sm_sales_service.h"
#include "migration/sm_finance_import.h"
#include "repo/sm_finance_repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#define RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0700)
#define RMDIR(path) rmdir(path)
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#define CHECK_REPO(x) do { sm_repo_status s_ = (x); if (s_ != SM_REPO_OK) { fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__, #x, sm_repo_status_name(s_)); return 1; } } while (0)

static void remove_db(const char *path) {
    char journal[256]; remove(path); snprintf(journal, sizeof(journal), "%s.journal", path); remove(journal);
}

static int start(const char *path) {
    sm_app_context_config c; sm_app_context_config_default(&c);
    c.database_path = path; c.legacy_data_dir = "."; c.import_legacy = 0;
    return sm_app_context_start(&c) == SM_REPO_OK;
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); size_t len = strlen(text);
    if (!file) return 0;
    if (fwrite(text, 1, len, file) != len) { fclose(file); return 0; }
    return fclose(file) == 0;
}

static void prepare_sale(Sale *sale, int cashier, time_t at) {
    memset(sale, 0, sizeof(*sale)); sale->cashier_id = cashier; sale->created_at = at;
}

static int test_atomic_card_sale(void) {
    const char *db = ".phase10-vip.abdb"; Employee employee; Product product, persisted;
    VipCard card, read_card; VipCardTransaction tx, *transactions = NULL;
    Sale sale, second, read_sale; sm_stock_adjustment adjustment; size_t count = 0;
    remove_db(db); CHECK(start(db));
    memset(&employee, 0, sizeof(employee)); snprintf(employee.name, sizeof(employee.name), "Cashier");
    snprintf(employee.role, sizeof(employee.role), "cashier"); employee.status = 1;
    employee.created_at = employee.updated_at = 1; CHECK_REPO(sm_service_employee_create(&employee));
    memset(&product, 0, sizeof(product)); snprintf(product.name, sizeof(product.name), "Tea");
    product.price = 30; product.cost = 10; product.stock = 2; product.status = 1;
    product.created_at = product.updated_at = 1; CHECK_REPO(sm_service_product_create(&product));
    memset(&card, 0, sizeof(card)); snprintf(card.card_no, sizeof(card.card_no), "VC0001");
    card.status = VIPCARD_ACTIVE; card.card_type = VIPCARD_TYPE_NORMAL;
    card.created_at = card.updated_at = 1; card.expired_at = INT64_C(4102444800);
    snprintf(card.password_hash, sizeof(card.password_hash), "hash");
    snprintf(card.password_salt, sizeof(card.password_salt), "salt");
    CHECK_REPO(sm_service_vip_card_create(&card));
    CHECK_REPO(sm_service_vip_apply(card.card_no, 0, 5000, 0, employee.id,
                                    "recharge", &tx));
    CHECK_REPO(sm_service_vip_card_get(card.card_no, &read_card));
    CHECK(read_card.balance == 50.0f);

    prepare_sale(&sale, employee.id, 10); CHECK_REPO(sm_service_sale_create(&sale));
    sale.total_amount = sale.final_amount = 30.0f; sale.status = SALE_COMPLETED;
    sale.completed_at = 20; snprintf(sale.payment_method, sizeof(sale.payment_method), "stored-card");
    memset(&adjustment, 0, sizeof(adjustment)); snprintf(adjustment.product_id, sizeof(adjustment.product_id), "MISSING");
    adjustment.quantity = 1;
    CHECK(sm_service_sale_complete_with_vip(&sale, &adjustment, 1, 0,
                                            card.card_no, NULL) == SM_REPO_NOT_FOUND);
    CHECK_REPO(sm_service_vip_card_get(card.card_no, &read_card)); CHECK(read_card.balance == 50.0f);
    CHECK_REPO(sm_service_vip_transaction_list(card.card_no, &transactions, &count));
    CHECK(count == 1); free(transactions);
    CHECK_REPO(sm_service_sale_get(sale.id, &read_sale)); CHECK(read_sale.status == SALE_PENDING);

    snprintf(adjustment.product_id, sizeof(adjustment.product_id), "%s", product.id);
    CHECK_REPO(sm_service_sale_complete_with_vip(&sale, &adjustment, 1, 0,
                                                 card.card_no, NULL));
    CHECK_REPO(sm_service_vip_card_get(card.card_no, &read_card)); CHECK(read_card.balance == 20.0f);
    CHECK_REPO(sm_service_product_get(product.id, &persisted)); CHECK(persisted.stock == 1);
    CHECK_REPO(sm_service_vip_transaction_list(card.card_no, &transactions, &count));
    CHECK(count == 2 &&
          (transactions[0].sale_id == sale.id ||
           transactions[1].sale_id == sale.id));
    free(transactions);

    prepare_sale(&second, employee.id, 30); CHECK_REPO(sm_service_sale_create(&second));
    second.total_amount = second.final_amount = 30.0f; second.status = SALE_COMPLETED;
    second.completed_at = 40; snprintf(second.payment_method, sizeof(second.payment_method), "stored-card");
    CHECK(sm_service_sale_complete_with_vip(&second, &adjustment, 1, 0,
                                            card.card_no, NULL) == SM_REPO_CONFLICT);
    CHECK_REPO(sm_service_product_get(product.id, &persisted)); CHECK(persisted.stock == 1);
    CHECK_REPO(sm_service_vip_card_get(card.card_no, &read_card)); CHECK(read_card.balance == 20.0f);
    CHECK_REPO(sm_service_sale_get(second.id, &read_sale)); CHECK(read_sale.status == SALE_PENDING);
    CHECK_REPO(sm_app_context_stop()); CHECK(start(db));
    CHECK_REPO(sm_service_vip_card_get(card.card_no, &read_card)); CHECK(read_card.balance == 20.0f);
    CHECK_REPO(sm_app_context_stop()); remove_db(db); return 0;
}

static void cleanup_fixture(const char *dir) {
    const char *names[] = {"vipcard.txt", "vipcard_trans.txt", "store.abdb",
                           "store.abdb.journal"};
    char path[256]; size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]); remove(path);
    }
    (void)RMDIR(dir);
}

static int test_vip_migration_retry(void) {
    const char *dir = ".phase10-import"; char path[256], db[256];
    sm_repository_config config; sm_repository *repo = NULL;
    sm_finance_import_report report; sm_vip_card *cards = NULL;
    sm_vip_transaction *transactions = NULL; size_t count = 0;
    cleanup_fixture(dir); CHECK(MKDIR(dir) == 0);
    snprintf(path, sizeof(path), "%s/vipcard_trans.txt", dir);
    CHECK(write_text(path, "1|VCX|0|10|0|10|0||0||2\n"));
    snprintf(db, sizeof(db), "%s/store.abdb", dir);
    sm_repository_config_default(&config, db);
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK(sm_vip_import_if_needed(repo, dir, &report) == SM_REPO_ERR_CORRUPT);
    snprintf(path, sizeof(path), "%s/vipcard.txt", dir);
    CHECK(write_text(path,
                     "VCX|0|10|10|0|1|1|1|4102444800|hash|salt\n"));
    CHECK_REPO(sm_vip_import_if_needed(repo, dir, &report));
    CHECK(report.vip_cards == 1 && report.vip_transactions == 1);
    CHECK_REPO(sm_vip_card_list(repo, 0, -1, &cards, &count));
    CHECK(count == 1 && cards[0].balance_cents == 1000); free(cards);
    CHECK_REPO(sm_vip_transaction_list(repo, "VCX", &transactions, &count));
    CHECK(count == 1 && transactions[0].amount_cents == 1000); free(transactions);
    CHECK_REPO(sm_repository_close(&repo));
    CHECK_REPO(sm_repository_open(&config, &repo));
    CHECK_REPO(sm_vip_import_if_needed(repo, dir, &report));
    CHECK(report.already_completed == 1);
    CHECK_REPO(sm_repository_close(&repo)); cleanup_fixture(dir); return 0;
}

int main(void) {
    CHECK(test_atomic_card_sale() == 0);
    CHECK(test_vip_migration_retry() == 0);
    puts("phase 10 stored-value card tests passed"); return 0;
}
