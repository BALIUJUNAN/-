#include "app/sm_app_context.h"
#include "app/sm_finance_service.h"
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
    FILE *f = fopen(path, "wb"); size_t n = strlen(text);
    if (!f) return 0;
    if (fwrite(text, 1, n, f) != n) { fclose(f); return 0; }
    return fclose(f) == 0;
}

static int test_atomic_payable_and_payment(void) {
    const char *db = ".phase9-finance.abdb"; sm_repo_uow *uow = NULL;
    SupplierFinance finance; Payable created, *payables = NULL;
    PaymentRecord record, *records = NULL; size_t count = 0; int64_t applied = 0;
    remove_db(db); CHECK(start(db));
    CHECK_REPO(sm_repo_uow_begin(sm_app_repository(), 1, &uow));
    CHECK_REPO(sm_finance_payable_for_purchase_uow(uow, 1, "Supplier", 77,
                                                    1000, 100));
    CHECK_REPO(sm_repo_uow_commit(&uow));
    CHECK_REPO(sm_service_supplier_finance_get(1, &finance));
    CHECK(finance.total_amount == 10.0f && finance.pending_amount == 10.0f);
    CHECK_REPO(sm_service_payable_list(1, -1, &payables, &count));
    CHECK(count == 1 && payables[0].purchase_id == 77 && payables[0].pending_amount == 10.0f);
    CHECK_REPO(sm_service_payment_record(payables[0].id, 400, "bank", "R1", 0,
                                         "partial", &record));
    free(payables); payables = NULL;
    CHECK_REPO(sm_service_payable_list(1, -1, &payables, &count));
    CHECK(count == 1 && payables[0].status == PAYMENT_PARTIAL &&
          payables[0].paid_amount == 4.0f && payables[0].pending_amount == 6.0f);
    CHECK(sm_service_payment_record(payables[0].id, 700, "bank", "R2", 0,
                                    "too much", &record) == SM_REPO_CONFLICT);
    CHECK_REPO(sm_service_payment_list(0, 1, &records, &count));
    CHECK(count == 1 && records[0].amount == 4.0f);
    free(payables); free(records); payables = NULL; records = NULL;
    CHECK_REPO(sm_service_payable_create(1, 500, "Supplier", &created));
    CHECK_REPO(sm_service_supplier_settle(1, 800, "bank", "BULK", 0,
                                          "batch", &applied));
    CHECK(applied == 800);
    CHECK_REPO(sm_service_payable_list(1, -1, &payables, &count));
    CHECK(count == 2 && payables[0].pending_amount + payables[1].pending_amount == 3.0f);
    free(payables);
    CHECK_REPO(sm_app_context_stop()); CHECK(start(db));
    CHECK_REPO(sm_service_supplier_finance_get(1, &finance));
    CHECK(finance.total_amount == 15.0f && finance.paid_amount == 12.0f &&
          finance.pending_amount == 3.0f);
    CHECK_REPO(sm_app_context_stop()); remove_db(db); return 0;
}

static void cleanup_fixture(const char *dir) {
    const char *names[] = {"supplier_finance.txt", "payable.txt",
                           "payment_record.txt", "store.abdb",
                           "store.abdb.journal"};
    char path[256]; size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]); remove(path);
    }
    (void)RMDIR(dir);
}

static int test_migration_reconciles_totals(void) {
    const char *dir = ".phase9-import"; char path[256], db[256];
    sm_repository_config c; sm_repository *repo = NULL;
    sm_finance_import_report report; sm_payable *payables = NULL;
    sm_supplier_finance *finances = NULL; size_t count = 0;
    cleanup_fixture(dir); CHECK(MKDIR(dir) == 0);
    snprintf(path, sizeof(path), "%s/supplier_finance.txt", dir);
    CHECK(write_text(path, "1|30|A|999|999|0|0|10\n"));
    snprintf(path, sizeof(path), "%s/payable.txt", dir);
    CHECK(write_text(path, "5|1|Supplier|7|10|0|10|0|100|10|0\n"));
    snprintf(path, sizeof(path), "%s/payment_record.txt", dir);
    CHECK(write_text(path, "8|5|1|4|bank|R1|0|||20\n"));
    snprintf(db, sizeof(db), "%s/store.abdb", dir); sm_repository_config_default(&c, db);
    CHECK_REPO(sm_repository_open(&c, &repo));
    CHECK_REPO(sm_supplier_finance_import_if_needed(repo, dir, &report));
    CHECK(report.payables == 1 && report.payments == 1);
    CHECK_REPO(sm_payable_list(repo, 1, -1, &payables, &count));
    CHECK(count == 1 && payables[0].paid_cents == 400 && payables[0].pending_cents == 600);
    CHECK_REPO(sm_supplier_finance_list(repo, &finances, &count));
    CHECK(count == 1 && finances[0].total_cents == 1000 &&
          finances[0].paid_cents == 400 && finances[0].pending_cents == 600);
    free(payables); free(finances);
    CHECK_REPO(sm_repository_close(&repo)); cleanup_fixture(dir); return 0;
}

int main(void) {
    CHECK(test_atomic_payable_and_payment() == 0);
    CHECK(test_migration_reconciles_totals() == 0);
    puts("phase 9 supplier finance tests passed"); return 0;
}
