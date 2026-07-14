#include "app/sm_app_context.h"
#include "app/sm_operations_service.h"
#include "migration/sm_operations_import.h"
#include "repo/sm_operations_repository.h"

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
#define OK(x) do { sm_repo_status s_ = (x); if (s_ != SM_REPO_OK) { fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__, #x, sm_repo_status_name(s_)); return 1; } } while (0)

static void remove_db(const char *path) {
    char journal[256]; remove(path); snprintf(journal, sizeof(journal), "%s.journal", path); remove(journal);
}

static int start(const char *path) {
    sm_app_context_config c; sm_app_context_config_default(&c);
    c.database_path = path; c.legacy_data_dir = "."; c.import_legacy = 0;
    return sm_app_context_start(&c) == SM_REPO_OK;
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb"); size_t len = strlen(text);
    if (!f) return 0;
    if (fwrite(text, 1, len, f) != len) { fclose(f); return 0; }
    return fclose(f) == 0;
}

static void fill_store(sm_store_record *v, const char *name) {
    memset(v, 0, sizeof(*v)); snprintf(v->name, sizeof(v->name), "%s", name);
    v->status = 1; v->created_at = v->updated_at = 1;
}

static int test_atomic_transfer(void) {
    const char *db = ".phase11.abdb"; sm_store_record a, b;
    sm_store_stock_record stock; sm_transfer_order_record order, read;
    sm_transfer_item_record item; sm_audit_record *logs = NULL; size_t count = 0;
    remove_db(db); CHECK(start(db));
    fill_store(&a, "North"); fill_store(&b, "South");
    OK(sm_service_store_create(&a)); OK(sm_service_store_create(&b));
    memset(&stock, 0, sizeof(stock)); stock.store_id = a.id;
    snprintf(stock.product_id, sizeof(stock.product_id), "P1"); stock.quantity = 10;
    stock.min_stock = 2; stock.updated_at = 2; OK(sm_service_store_stock_set(&stock));
    memset(&order, 0, sizeof(order)); order.from_store_id = a.id; order.to_store_id = b.id;
    order.creator_id = 7; snprintf(order.creator_name, sizeof(order.creator_name), "Creator");
    snprintf(order.remark, sizeof(order.remark), "move stock"); order.created_at = 3;
    OK(sm_service_transfer_create(&order));
    memset(&item, 0, sizeof(item)); snprintf(item.product_id, sizeof(item.product_id), "P1");
    snprintf(item.product_name, sizeof(item.product_name), "Tea"); item.quantity = 4;
    OK(sm_service_transfer_add_item(order.id, &item));
    OK(sm_service_transfer_approve(order.id, 8, "Manager", 4));
    OK(sm_service_transfer_out(order.id, 9, "Warehouse", 5));
    OK(sm_service_store_stock_get(a.id, "P1", &stock)); CHECK(stock.quantity == 10);
    CHECK(sm_service_store_stock_get(b.id, "P1", &stock) == SM_REPO_NOT_FOUND);
    OK(sm_service_transfer_in(order.id, 10, "Receiver", 6));
    OK(sm_service_store_stock_get(a.id, "P1", &stock)); CHECK(stock.quantity == 6);
    OK(sm_service_store_stock_get(b.id, "P1", &stock)); CHECK(stock.quantity == 4);
    OK(sm_service_transfer_get(order.id, &read)); CHECK(read.status == 2u);
    OK(sm_service_audit_list("TRANSFER", 0, 0, INT64_MAX, &logs, &count));
    CHECK(count == 4); sm_operations_array_free(logs);
    OK(sm_app_context_stop()); CHECK(start(db));
    OK(sm_service_store_stock_get(a.id, "P1", &stock)); CHECK(stock.quantity == 6);
    OK(sm_service_transfer_get(order.id, &read)); CHECK(read.status == 2u);
    OK(sm_app_context_stop()); remove_db(db); return 0;
}

static int test_failed_move_rolls_back(void) {
    const char *db = ".phase11-rollback.abdb"; sm_store_record a, b;
    sm_store_stock_record stock; sm_transfer_order_record order, read; sm_transfer_item_record item;
    remove_db(db); CHECK(start(db)); fill_store(&a, "A"); fill_store(&b, "B");
    OK(sm_service_store_create(&a)); OK(sm_service_store_create(&b));
    memset(&stock, 0, sizeof(stock)); stock.store_id = a.id; snprintf(stock.product_id, sizeof(stock.product_id), "P1");
    stock.quantity = 2; stock.updated_at = 1; OK(sm_service_store_stock_set(&stock));
    memset(&order, 0, sizeof(order)); order.from_store_id = a.id; order.to_store_id = b.id;
    order.creator_id = 1; order.created_at = 2; OK(sm_service_transfer_create(&order));
    memset(&item, 0, sizeof(item)); snprintf(item.product_id, sizeof(item.product_id), "P1");
    snprintf(item.product_name, sizeof(item.product_name), "Tea"); item.quantity = 3;
    OK(sm_service_transfer_add_item(order.id, &item)); OK(sm_service_transfer_approve(order.id, 2, "M", 3));
    CHECK(sm_service_transfer_out(order.id, 3, "W", 4) == SM_REPO_CONFLICT);
    OK(sm_service_transfer_get(order.id, &read)); CHECK(read.status == 0u);
    OK(sm_service_store_stock_get(a.id, "P1", &stock)); CHECK(stock.quantity == 2);
    CHECK(sm_service_store_stock_get(b.id, "P1", &stock) == SM_REPO_NOT_FOUND);
    OK(sm_app_context_stop()); remove_db(db); return 0;
}

static int test_migration_retry(void) {
    const char *dir = ".phase11-import"; char path[256], db[256];
    sm_repository_config config; sm_repository *repo = NULL;
    sm_operations_import_report report; sm_store_record *stores = NULL;
    size_t count = 0;
    snprintf(path, sizeof(path), "%s/store_stock.txt", dir); remove(path);
    snprintf(path, sizeof(path), "%s/store.txt", dir); remove(path);
    snprintf(db, sizeof(db), "%s/store.abdb", dir); remove_db(db); (void)RMDIR(dir);
    CHECK(MKDIR(dir) == 0);
    snprintf(path, sizeof(path), "%s/store_stock.txt", dir);
    CHECK(write_text(path, "9|P1|4|1|1|2\n"));
    sm_repository_config_default(&config, db); OK(sm_repository_open(&config, &repo));
    CHECK(sm_store_transfer_import_if_needed(repo, dir, &report) == SM_REPO_ERR_CORRUPT);
    snprintf(path, sizeof(path), "%s/store.txt", dir);
    CHECK(write_text(path, "Main||||9|0|1|1|2\n"));
    OK(sm_store_transfer_import_if_needed(repo, dir, &report));
    CHECK(report.stores == 1 && report.store_stocks == 1);
    OK(sm_store_record_list(repo, -1, &stores, &count)); CHECK(count == 1 && stores[0].id == 9);
    sm_operations_array_free(stores); OK(sm_repository_close(&repo));
    snprintf(path, sizeof(path), "%s/store_stock.txt", dir); remove(path);
    snprintf(path, sizeof(path), "%s/store.txt", dir); remove(path); remove_db(db); (void)RMDIR(dir);
    return 0;
}

int main(void) {
    if (test_atomic_transfer() || test_failed_move_rolls_back() ||
        test_migration_retry()) return 1;
    puts("phase 11 store/transfer tests passed"); return 0;
}
