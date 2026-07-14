#include "app/sm_app_context.h"
#include "app/sm_base_service.h"
#include "app/sm_inventory_service.h"
#include "app/sm_purchase_service.h"
#include "app/sm_sales_service.h"
#include "migration/sm_purchase_import.h"
#include "repo/sm_purchase_codec.h"
#include "repo/sm_purchase_repository.h"

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

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define CHECK_REPO(c) do { sm_repo_status s_ = (c); if (s_ != SM_REPO_OK) { fprintf(stderr, "FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__, #c, sm_repo_status_name(s_)); return 1; } } while (0)

static void remove_db(const char *path) {
    char journal[512]; remove(path); snprintf(journal, sizeof(journal), "%s.journal", path); remove(journal);
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb"); size_t len = strlen(text);
    if (!f) return 0;
    if (fwrite(text, 1, len, f) != len) { fclose(f); return 0; }
    return fclose(f) == 0;
}

static int start_app(const char *database, const char *dir, int import) {
    sm_app_context_config c; sm_app_context_config_default(&c);
    c.database_path = database; c.legacy_data_dir = dir; c.import_legacy = import;
    return sm_app_context_start(&c) == SM_REPO_OK;
}

static int test_codec_and_repository(void) {
    const char *db_path = ".phase8-repo.abdb";
    sm_repository_config config; sm_repository *repo = NULL; sm_repo_uow *uow = NULL;
    sm_purchase order, decoded, *orders = NULL; sm_purchase_item item, *items = NULL;
    sm_batch first, second, *batches = NULL; uint8_t *encoded = NULL;
    size_t len = 0, count = 0;
    memset(&order, 0, sizeof(order)); order.id = 9;
    snprintf(order.supplier_id, sizeof(order.supplier_id), "S1");
    snprintf(order.supplier_name, sizeof(order.supplier_name), "Supplier");
    order.creator_id = 1; order.status = 0; order.created_at = 10;
    CHECK(sm_purchase_encode(&order, &encoded, &len) == SM_CODEC_OK);
    CHECK(sm_purchase_decode(encoded, len, &decoded) == SM_CODEC_OK && decoded.id == 9);
    encoded[1] ^= 0xffu; CHECK(sm_purchase_decode(encoded, len, &decoded) != SM_CODEC_OK); free(encoded);

    remove_db(db_path); sm_repository_config_default(&config, db_path);
    CHECK_REPO(sm_repository_open(&config, &repo)); CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow));
    CHECK_REPO(sm_purchase_import(uow, &order));
    memset(&item, 0, sizeof(item)); item.id = 11; item.purchase_id = 9;
    snprintf(item.product_id, sizeof(item.product_id), "P1");
    snprintf(item.product_name, sizeof(item.product_name), "Tea");
    item.quantity_milli = 2000; item.price_cents = 300;
    CHECK_REPO(sm_purchase_item_import(uow, &item));
    memset(&first, 0, sizeof(first)); snprintf(first.batch_no, sizeof(first.batch_no), "OLD");
    snprintf(first.product_id, sizeof(first.product_id), "P1"); snprintf(first.product_name, sizeof(first.product_name), "Tea");
    first.quantity_milli = first.initial_quantity_milli = 1000; first.price_cents = 300;
    first.production_date = first.received_date = first.created_at = 20;
    CHECK_REPO(sm_batch_import(uow, &first));
    second = first; snprintf(second.batch_no, sizeof(second.batch_no), "NEW"); second.received_date = second.created_at = 30;
    CHECK_REPO(sm_batch_import(uow, &second)); CHECK_REPO(sm_repo_uow_commit(&uow));
    CHECK_REPO(sm_purchase_list(repo, 0, &orders, &count)); CHECK(count == 1 && orders[0].id == 9); sm_purchase_array_free(orders);
    CHECK_REPO(sm_purchase_item_list(repo, 9, &items, &count)); CHECK(count == 1 && items[0].id == 11); sm_purchase_array_free(items);
    CHECK_REPO(sm_batch_list(repo, "P1", 1, &batches, &count));
    CHECK(count == 2 && !strcmp(batches[0].batch_no, "OLD") && !strcmp(batches[1].batch_no, "NEW")); sm_purchase_array_free(batches);
    CHECK_REPO(sm_repository_checkpoint(repo)); CHECK_REPO(sm_repository_close(&repo));
    CHECK_REPO(sm_repository_open(&config, &repo)); CHECK_REPO(sm_purchase_list(repo, -1, &orders, &count));
    CHECK(count == 1); sm_purchase_array_free(orders); CHECK_REPO(sm_repository_close(&repo)); remove_db(db_path); return 0;
}

static void fill_purchase(Purchase *p, int creator, const char *supplier, time_t at) {
    memset(p, 0, sizeof(*p)); snprintf(p->supplier_id, sizeof(p->supplier_id), "%s", supplier);
    snprintf(p->supplier_name, sizeof(p->supplier_name), "Supplier"); p->creator_id = creator; p->created_at = at;
}

static void fill_item(PurchaseItem *item, const char *product, float qty) {
    memset(item, 0, sizeof(*item)); snprintf(item->product_id, sizeof(item->product_id), "%s", product);
    snprintf(item->product_name, sizeof(item->product_name), "Tea"); item->quantity = qty; item->price = 3.0f;
}

static int test_atomic_receiving_and_fifo(void) {
    const char *db = ".phase8-service.abdb"; Employee employee; Product product, read_product;
    Purchase bad, good1, good2, read_order; PurchaseItem item, *items = NULL;
    Sale sale;
    sm_stock_adjustment adjustment;
    Batch *batches = NULL; StockLog *logs = NULL; size_t count = 0; float deducted = 0;
    char latest_batch_no[20];
    remove_db(db); CHECK(start_app(db, ".", 0));
    memset(&employee, 0, sizeof(employee)); snprintf(employee.name, sizeof(employee.name), "Manager");
    snprintf(employee.role, sizeof(employee.role), "manager"); employee.status = 1; employee.created_at = employee.updated_at = 1;
    CHECK_REPO(sm_service_employee_create(&employee));
    memset(&product, 0, sizeof(product)); snprintf(product.name, sizeof(product.name), "Tea");
    product.price = 5; product.cost = 3; product.stock = 1; product.status = 1; product.created_at = product.updated_at = 1;
    CHECK_REPO(sm_service_product_create(&product));

    fill_purchase(&bad, employee.id, "S1", 10); CHECK_REPO(sm_service_purchase_create(&bad));
    fill_item(&item, product.id, 2); CHECK_REPO(sm_service_purchase_item_create(bad.id, &item));
    fill_item(&item, "MISSING", 1); CHECK_REPO(sm_service_purchase_item_create(bad.id, &item));
    CHECK_REPO(sm_service_purchase_approve(bad.id, employee.id, 20));
    CHECK(sm_service_purchase_receive(bad.id, employee.id, 30) == SM_REPO_NOT_FOUND);
    CHECK_REPO(sm_service_product_get(product.id, &read_product)); CHECK(read_product.stock == 1);
    CHECK_REPO(sm_service_purchase_get(bad.id, &read_order)); CHECK(read_order.status == PURCHASE_APPROVED);
    CHECK_REPO(sm_service_purchase_item_list(bad.id, &items, &count));
    CHECK(count == 2 && items[0].received_qty == 0 && items[1].received_qty == 0); sm_service_purchase_array_free(items);
    CHECK_REPO(sm_service_batch_list(product.id, 1, &batches, &count)); CHECK(count == 0 && batches == NULL);
    CHECK_REPO(sm_service_stock_log_list(product.id, "入库", 0, 100, &logs, &count)); CHECK(count == 0 && logs == NULL);

    fill_purchase(&good1, employee.id, "S1", 11); CHECK_REPO(sm_service_purchase_create(&good1));
    fill_item(&item, product.id, 2); CHECK_REPO(sm_service_purchase_item_create(good1.id, &item));
    CHECK_REPO(sm_service_purchase_approve(good1.id, employee.id, 21));
    CHECK_REPO(sm_service_purchase_receive(good1.id, employee.id, 31));
    fill_purchase(&good2, employee.id, "S1", 12); CHECK_REPO(sm_service_purchase_create(&good2));
    fill_item(&item, product.id, 3); CHECK_REPO(sm_service_purchase_item_create(good2.id, &item));
    CHECK_REPO(sm_service_purchase_approve(good2.id, employee.id, 22));
    CHECK_REPO(sm_service_purchase_receive(good2.id, employee.id, 51));
    CHECK_REPO(sm_service_product_get(product.id, &read_product)); CHECK(read_product.stock == 6);
    CHECK_REPO(sm_service_batch_list(product.id, 1, &batches, &count));
    CHECK(count == 2 && batches[0].received_date == 31 && batches[1].received_date == 51);
    snprintf(latest_batch_no, sizeof(latest_batch_no), "%s",
             batches[1].batch_no);
    sm_service_purchase_array_free(batches);
    memset(&sale, 0, sizeof(sale)); sale.cashier_id = employee.id; sale.created_at = 55;
    CHECK_REPO(sm_service_sale_create(&sale));
    sale.status = SALE_COMPLETED; sale.completed_at = 60;
    snprintf(sale.payment_method, sizeof(sale.payment_method), "cash");
    memset(&adjustment, 0, sizeof(adjustment));
    snprintf(adjustment.product_id, sizeof(adjustment.product_id), "%s", product.id);
    adjustment.quantity = 6;
    CHECK(sm_service_sale_complete(&sale, &adjustment, 1, 0, NULL) == SM_REPO_CONFLICT);
    CHECK_REPO(sm_service_product_get(product.id, &read_product)); CHECK(read_product.stock == 6);
    adjustment.quantity = 4;
    CHECK_REPO(sm_service_sale_complete(&sale, &adjustment, 1, 0, NULL));
    CHECK_REPO(sm_service_batch_list(product.id, 1, &batches, &count));
    CHECK(count == 2 && batches[0].quantity == 0 && batches[1].quantity == 1); sm_service_purchase_array_free(batches);
    CHECK_REPO(sm_service_product_get(product.id, &read_product)); CHECK(read_product.stock == 2);
    CHECK_REPO(sm_service_batch_deduct_fifo(product.id, 1, employee.id, 61, &deducted)); CHECK(deducted == 1);
    CHECK_REPO(sm_service_product_get(product.id, &read_product)); CHECK(read_product.stock == 1);
    {
        sm_repo_uow *uow = NULL;
        sm_batch persisted;
        CHECK_REPO(sm_repo_uow_begin(sm_app_repository(), 0, &uow));
        CHECK_REPO(sm_batch_get(uow, latest_batch_no, &persisted));
        CHECK(persisted.source_purchase_id == (uint64_t)good2.id);
        CHECK_REPO(sm_repo_uow_commit(&uow));
    }
    CHECK_REPO(sm_app_context_stop()); remove_db(db); return 0;
}

static void cleanup_fixture(const char *dir) {
    char path[256]; const char *files[] = {"purchase.txt", "purchase_item.txt", "purchase_item.txt.journal", "batch.txt", "store.abdb", "store.abdb.journal"}; size_t i;
    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) { snprintf(path, sizeof(path), "%s/%s", dir, files[i]); remove(path); }
    (void)TEST_RMDIR(dir);
}

static int test_migration(void) {
    const char *dir = ".phase8-migration"; char path[256], db_path[256]; sm_app_context_config c;
    Purchase *orders = NULL; Batch *batches = NULL; size_t count = 0;
    cleanup_fixture(dir); CHECK(TEST_MKDIR(dir) == 0);
    snprintf(path, sizeof(path), "%s/purchase.txt", dir);
    CHECK(write_text(path, "5|S1|Supplier|1|2|1|20.00|10|20|0\n"));
    snprintf(path, sizeof(path), "%s/purchase_item.txt", dir);
    CHECK(write_text(path, "bad-line\n"));
    snprintf(path, sizeof(path), "%s/batch.txt", dir);
    CHECK(write_text(path, "LOT1|P1|Tea|2|2|10|5|100|20|1|20\n"));
    sm_app_context_config_default(&c);
    snprintf(db_path, sizeof(db_path), "%s/store.abdb", dir);
    c.database_path = db_path; c.legacy_data_dir = dir; c.import_legacy = 1;
    CHECK(sm_app_context_start(&c) == SM_REPO_ERR_CORRUPT);
    snprintf(path, sizeof(path), "%s/purchase_item.txt", dir);
    CHECK(write_text(path, "7|5|P1|Tea|2|10|0\n"));
    CHECK_REPO(sm_app_context_start(&c));
    CHECK_REPO(sm_service_purchase_list(-1, &orders, &count)); CHECK(count == 1 && orders[0].id == 5); sm_service_purchase_array_free(orders);
    CHECK_REPO(sm_service_batch_list(NULL, 0, &batches, &count)); CHECK(count == 1 && !strcmp(batches[0].batch_no, "LOT1")); sm_service_purchase_array_free(batches);
    CHECK(sm_app_purchase_import_report()->purchases == 1 && sm_app_purchase_import_report()->batches == 1);
    CHECK_REPO(sm_app_context_stop()); CHECK_REPO(sm_app_context_start(&c));
    CHECK(sm_app_purchase_import_report()->already_completed == 1);
    CHECK_REPO(sm_app_context_stop()); cleanup_fixture(dir); return 0;
}

int main(void) {
    CHECK(test_codec_and_repository() == 0);
    CHECK(test_atomic_receiving_and_fifo() == 0);
    CHECK(test_migration() == 0);
    puts("phase 8 purchase and FIFO tests passed"); return 0;
}
