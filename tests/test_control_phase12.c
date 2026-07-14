#include "app/sm_app_context.h"
#include "app/sm_operations_service.h"
#include "repo/sm_operations_codec.h"
#include "repo/sm_operations_repository.h"
#include "migration/sm_operations_import.h"

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

static int test_control_records(void) {
    const char *db = ".phase12.abdb"; sm_promotion_record promo, decoded, *promos = NULL;
    sm_schedule_record schedule, found; sm_settlement_record settlement, read;
    sm_combo_record combo, found_combo; sm_combo_item_record combo_item, *combo_items = NULL;
    sm_audit_record *audits = NULL; uint8_t *encoded = NULL; size_t len = 0, count = 0;
    remove_db(db); CHECK(start(db));
    memset(&promo, 0, sizeof(promo)); snprintf(promo.name, sizeof(promo.name), "Tea 20%% off");
    snprintf(promo.product_id, sizeof(promo.product_id), "P1"); promo.type = 0;
    promo.discount_bps = 8000; promo.start_at = 10; promo.end_at = 100; promo.status = 1;
    promo.priority = 5; promo.created_at = 1;
    CHECK(sm_promotion_record_encode(&promo, &encoded, &len) == SM_CODEC_OK);
    CHECK(sm_promotion_record_decode(encoded, len, &decoded) == SM_CODEC_OK && decoded.discount_bps == 8000);
    encoded[1] ^= 0xffu; CHECK(sm_promotion_record_decode(encoded, len, &decoded) != SM_CODEC_OK); free(encoded);
    OK(sm_service_promotion_create(&promo));
    OK(sm_service_promotion_list("P1", 50, &promos, &count)); CHECK(count == 1 && promos[0].id == promo.id);
    sm_operations_array_free(promos);

    memset(&combo, 0, sizeof(combo)); snprintf(combo.name, sizeof(combo.name), "Tea Set");
    snprintf(combo.barcode, sizeof(combo.barcode), "C100"); combo.price_cents = 2500;
    combo.cost_cents = 1500; combo.status = 1; combo.created_at = combo.updated_at = 1;
    OK(sm_service_combo_create(&combo));
    memset(&combo_item, 0, sizeof(combo_item)); snprintf(combo_item.product_id, sizeof(combo_item.product_id), "P1");
    snprintf(combo_item.product_name, sizeof(combo_item.product_name), "Tea");
    combo_item.quantity = 2; combo_item.ratio_bps = 10000;
    OK(sm_service_combo_add_item(combo.id, &combo_item));
    OK(sm_service_combo_find_barcode("C100", &found_combo)); CHECK(found_combo.id == combo.id);
    OK(sm_service_combo_item_list(combo.id, &combo_items, &count)); CHECK(count == 1 && combo_items[0].quantity == 2);
    sm_operations_array_free(combo_items);

    memset(&schedule, 0, sizeof(schedule)); schedule.employee_id = 9; schedule.year = 2026; schedule.week = 30;
    snprintf(schedule.shifts[0], sizeof(schedule.shifts[0]), "AM");
    snprintf(schedule.shifts[1], sizeof(schedule.shifts[1]), "PM"); schedule.created_at = 2;
    OK(sm_service_schedule_create(&schedule));
    CHECK(sm_service_schedule_create(&schedule) == SM_REPO_ERR_INVALID);
    OK(sm_service_schedule_find(9, 2026, 30, &found)); CHECK(found.id == schedule.id);

    memset(&settlement, 0, sizeof(settlement)); settlement.cashier_id = 9;
    snprintf(settlement.cashier_name, sizeof(settlement.cashier_name), "Cashier");
    settlement.business_date = 1721088000; settlement.shift_start = 1721088000;
    settlement.shift_end = 1721174399; settlement.total_orders = 3;
    settlement.system_cash_cents = 1000; settlement.system_online_cents = 2000;
    settlement.system_total_cents = 3000; settlement.actual_cash_cents = 900;
    settlement.actual_online_cents = 2100; settlement.actual_total_cents = 3000;
    settlement.cash_diff_cents = -100; settlement.online_diff_cents = 100;
    settlement.total_diff_cents = 0; settlement.status = 0; settlement.created_at = 3;
    OK(sm_service_settlement_create(&settlement));
    OK(sm_service_settlement_confirm(settlement.id, "checked", 4));
    OK(sm_service_settlement_get(settlement.id, &read)); CHECK(read.status == 1 && !strcmp(read.remark, "checked"));
    OK(sm_service_audit_write("AUTH", 9, "LOGIN", "ok", 9, 5, NULL));
    OK(sm_service_audit_list(NULL, 9, 0, INT64_MAX, &audits, &count)); CHECK(count == 2);
    sm_operations_array_free(audits);
    OK(sm_app_context_stop()); CHECK(start(db));
    OK(sm_service_schedule_find(9, 2026, 30, &found));
    OK(sm_service_settlement_get(settlement.id, &read)); CHECK(read.status == 1);
    OK(sm_app_context_stop()); remove_db(db); return 0;
}

static int test_control_migration(void) {
    const char *dir = ".phase12-import"; char path[256], db[256];
    sm_repository_config config; sm_repository *repo = NULL;
    sm_operations_import_report report; sm_settlement_record *values = NULL;
    size_t count = 0;
    const char *names[] = {"promotion.txt", "combo.txt", "combo_item.txt", "schedule.txt", "daily_settlement.txt", "transaction.log"};
    size_t i;
    for (i = 0; i < 6; ++i) { snprintf(path, sizeof(path), "%s/%s", dir, names[i]); remove(path); }
    snprintf(db, sizeof(db), "%s/store.abdb", dir); remove_db(db); (void)RMDIR(dir);
    CHECK(MKDIR(dir) == 0);
    snprintf(path, sizeof(path), "%s/promotion.txt", dir);
    CHECK(write_text(path, "1|Promo|0|P1|0.80|0|0|1|100|1|5|1|0|0|0|0|0|0\n"));
    snprintf(path, sizeof(path), "%s/schedule.txt", dir);
    CHECK(write_text(path, "1|9|2026|30|AM|PM|OFF|OFF|AM|PM|OFF|1\n"));
    snprintf(path, sizeof(path), "%s/daily_settlement.txt", dir);
    CHECK(write_text(path, "1|9|Cashier|10|10|20|1|10|20|999|9|21|999|0|0|0|0|1|0|legacy\n"));
    snprintf(path, sizeof(path), "%s/transaction.log", dir);
    CHECK(write_text(path, "1|AUTH|9|LOGIN|ok|9|2\n"));
    snprintf(path, sizeof(path), "%s/combo.txt", dir);
    CHECK(write_text(path, "2|Set|C200|25|15|1|1|1\n"));
    snprintf(path, sizeof(path), "%s/combo_item.txt", dir);
    CHECK(write_text(path, "2|P1|Tea|2|1.0\n"));
    sm_repository_config_default(&config, db); OK(sm_repository_open(&config, &repo));
    OK(sm_control_import_if_needed(repo, dir, &report));
    CHECK(report.promotions == 1 && report.combos == 1 && report.combo_items == 1 &&
          report.schedules == 1 && report.settlements == 1 && report.audits == 1);
    OK(sm_settlement_record_list(repo, 0, 0, &values, &count));
    CHECK(count == 1 && values[0].system_total_cents == 3000 && values[0].actual_total_cents == 3000);
    sm_operations_array_free(values); OK(sm_repository_close(&repo));
    for (i = 0; i < 6; ++i) { snprintf(path, sizeof(path), "%s/%s", dir, names[i]); remove(path); }
    remove_db(db); (void)RMDIR(dir); return 0;
}

int main(void) {
    if (test_control_records() || test_control_migration()) return 1;
    puts("phase 12 control/audit tests passed"); return 0;
}
