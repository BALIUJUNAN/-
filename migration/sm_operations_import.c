#include "migration/sm_operations_import.h"

#include "domain/sm_operations_entities.h"
#include "repo/sm_operations_repository.h"
#include "storage/sm_codec.h"
#include "storage/sm_namespace.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LINE_CAP 8192u
#define MARKER_VERSION 1u

typedef struct batch {
    sm_store_record *stores; size_t store_count, store_cap;
    sm_store_stock_record *stocks; size_t stock_count, stock_cap;
    sm_transfer_order_record *transfers; size_t transfer_count, transfer_cap;
    sm_transfer_item_record *items; size_t item_count, item_cap;
    sm_promotion_record *promotions; size_t promotion_count, promotion_cap;
    sm_combo_record *combos; size_t combo_count, combo_cap;
    sm_combo_item_record *combo_items; size_t combo_item_count, combo_item_cap;
    sm_schedule_record *schedules; size_t schedule_count, schedule_cap;
    sm_settlement_record *settlements; size_t settlement_count, settlement_cap;
    sm_audit_record *audits; size_t audit_count, audit_cap;
    size_t source_files; uint64_t fingerprint;
} batch;

static const uint8_t phase11_marker[] = {SM_NS_MIGRATION_STATE, 0x0bu};
static const uint8_t phase12_marker[] = {SM_NS_MIGRATION_STATE, 0x0cu};

static void fail(sm_operations_import_report *r, const char *file,
                 size_t line, const char *message) {
    if (!r) return;
    if (file) snprintf(r->error_file, sizeof(r->error_file), "%s", file);
    r->error_line = line; snprintf(r->message, sizeof(r->message), "%s", message);
}

static uint64_t hash_bytes(uint64_t h, const void *data, size_t len) {
    const uint8_t *p = data; size_t i;
    for (i = 0; i < len; ++i) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}

static int path_join(char *out, size_t cap, const char *dir, const char *name) {
    size_t n = strlen(dir); int written = snprintf(out, cap, "%s%s%s", dir,
        n && dir[n - 1] != '/' && dir[n - 1] != '\\' ? "/" : "", name);
    return written >= 0 && (size_t)written < cap;
}

static size_t fields(char *line, char **out, size_t cap) {
    char *p = line; size_t n = 0;
    if (!cap) return 0;
    out[n++] = p;
    while (*p) { if (*p == '|') { *p = '\0'; if (n == cap) return cap + 1u; out[n++] = p + 1; } ++p; }
    return n;
}

static int uintv(const char *s, uint64_t max, int allow_zero, uint64_t *out) {
    char *end; unsigned long long value;
    if (!s || !*s || *s == '-') return 0;
    errno = 0; value = strtoull(s, &end, 10);
    if (errno || *end || (!allow_zero && !value) || value > max) return 0;
    *out = (uint64_t)value; return 1;
}

static int intv(const char *s, int64_t min, int64_t max, int64_t *out) {
    char *end; long long value;
    if (!s || !*s) return 0;
    errno = 0; value = strtoll(s, &end, 10);
    if (errno || *end || value < min || value > max) return 0;
    *out = value; return 1;
}

static int money(const char *s, int64_t *out) {
    char *end; double value, scaled;
    if (!s || !*s) return 0;
    errno = 0; value = strtod(s, &end);
    if (errno || *end || !isfinite(value)) return 0;
    scaled = value * 100.0;
    if (scaled < (double)INT64_MIN || scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)(scaled >= 0 ? floor(scaled + 0.5) : ceil(scaled - 0.5));
    return 1;
}

static int bps(const char *s, uint32_t *out) {
    char *end; double value, scaled;
    if (!s || !*s) return 0;
    errno = 0; value = strtod(s, &end);
    if (errno || *end || !isfinite(value) || value < 0 || value > 1.0) return 0;
    scaled = value * 10000.0; *out = (uint32_t)floor(scaled + 0.5); return 1;
}

static int strv(char *out, size_t cap, const char *s, int required) {
    size_t n = s ? strlen(s) : 0;
    if (n >= cap || (required && !n)) return 0;
    memcpy(out, s, n + 1u); return 1;
}

static int grow(void **array, size_t *cap, size_t count, size_t width) {
    size_t next; void *p;
    if (count < *cap) return 1;
    next = *cap ? *cap * 2u : 16u;
    if (next < *cap || next > SIZE_MAX / width) return 0;
    p = realloc(*array, next * width); if (!p) return 0;
    *array = p; *cap = next; return 1;
}

#define UPSERT_ID(fn, type, array, count, cap) \
static int fn(batch *b, const type *v) { size_t i; \
    for (i = 0; i < b->count; ++i) if (b->array[i].id == v->id) { b->array[i] = *v; return 1; } \
    if (!grow((void **)&b->array, &b->cap, b->count, sizeof(*v))) return 0; \
    b->array[b->count++] = *v; return 1; }

UPSERT_ID(add_store, sm_store_record, stores, store_count, store_cap)
UPSERT_ID(add_transfer, sm_transfer_order_record, transfers, transfer_count, transfer_cap)
UPSERT_ID(add_item, sm_transfer_item_record, items, item_count, item_cap)
UPSERT_ID(add_promotion, sm_promotion_record, promotions, promotion_count, promotion_cap)
UPSERT_ID(add_combo, sm_combo_record, combos, combo_count, combo_cap)
UPSERT_ID(add_schedule, sm_schedule_record, schedules, schedule_count, schedule_cap)
UPSERT_ID(add_settlement, sm_settlement_record, settlements, settlement_count, settlement_cap)
UPSERT_ID(add_audit, sm_audit_record, audits, audit_count, audit_cap)
#undef UPSERT_ID

static int add_stock(batch *b, const sm_store_stock_record *v) {
    size_t i; for (i = 0; i < b->stock_count; ++i)
        if (b->stocks[i].store_id == v->store_id && strcmp(b->stocks[i].product_id, v->product_id) == 0) {
            b->stocks[i] = *v; return 1;
        }
    if (!grow((void **)&b->stocks, &b->stock_cap, b->stock_count, sizeof(*v))) return 0;
    b->stocks[b->stock_count++] = *v; return 1;
}

static int parse_store(char **f, size_t n, batch *b) {
    sm_store_record v; uint64_t status;
    memset(&v, 0, sizeof(v));
    if (n != 9u || !strv(v.name, sizeof(v.name), f[0], 1) ||
        !strv(v.address, sizeof(v.address), f[1], 0) ||
        !strv(v.phone, sizeof(v.phone), f[2], 0) ||
        !strv(v.manager_name, sizeof(v.manager_name), f[3], 0) ||
        !uintv(f[4], INT_MAX, 0, &v.id) || !uintv(f[5], INT_MAX, 1, &v.manager_id) ||
        !uintv(f[6], 1, 1, &status) || !intv(f[7], 0, INT64_MAX, &v.created_at) ||
        !intv(f[8], 0, INT64_MAX, &v.updated_at)) return 0;
    v.status = (uint8_t)status; return add_store(b, &v);
}

static int parse_stock(char **f, size_t n, batch *b) {
    sm_store_stock_record v; uint64_t ignored;
    memset(&v, 0, sizeof(v));
    if (n != 6u || !uintv(f[0], INT_MAX, 0, &v.store_id) ||
        !strv(v.product_id, sizeof(v.product_id), f[1], 1) ||
        !intv(f[2], 0, INT_MAX, &v.quantity) || !intv(f[3], 0, INT_MAX, &v.min_stock) ||
        !uintv(f[4], INT_MAX, 1, &ignored) || !intv(f[5], 0, INT64_MAX, &v.updated_at)) return 0;
    return add_stock(b, &v);
}

static int parse_transfer(char **f, size_t n, batch *b) {
    sm_transfer_order_record v; uint64_t status;
    memset(&v, 0, sizeof(v));
    if (n != 19u || !uintv(f[0], INT_MAX, 0, &v.id) ||
        !uintv(f[1], INT_MAX, 0, &v.from_store_id) || !uintv(f[2], INT_MAX, 0, &v.to_store_id) ||
        !strv(v.from_store_name, sizeof(v.from_store_name), f[3], 1) ||
        !strv(v.to_store_name, sizeof(v.to_store_name), f[4], 1) ||
        !uintv(f[5], 4, 1, &status) || !uintv(f[6], INT_MAX, 1, &v.creator_id) ||
        !strv(v.creator_name, sizeof(v.creator_name), f[7], 0) ||
        !uintv(f[8], INT_MAX, 1, &v.approver_id) || !strv(v.approver_name, sizeof(v.approver_name), f[9], 0) ||
        !uintv(f[10], INT_MAX, 1, &v.out_operator_id) || !strv(v.out_operator_name, sizeof(v.out_operator_name), f[11], 0) ||
        !uintv(f[12], INT_MAX, 1, &v.in_operator_id) || !strv(v.in_operator_name, sizeof(v.in_operator_name), f[13], 0) ||
        !intv(f[14], 0, INT64_MAX, &v.created_at) || !intv(f[15], 0, INT64_MAX, &v.approved_at) ||
        !intv(f[16], 0, INT64_MAX, &v.out_at) || !intv(f[17], 0, INT64_MAX, &v.in_at) ||
        !strv(v.remark, sizeof(v.remark), f[18], 0)) return 0;
    v.status = (uint8_t)status; return add_transfer(b, &v);
}

static int parse_item(char **f, size_t n, batch *b) {
    sm_transfer_item_record v;
    memset(&v, 0, sizeof(v));
    if (n != 5u || !uintv(f[0], INT_MAX, 0, &v.transfer_id) ||
        !strv(v.product_id, sizeof(v.product_id), f[1], 1) ||
        !strv(v.product_name, sizeof(v.product_name), f[2], 1) ||
        !intv(f[3], 1, INT_MAX, &v.quantity) || !uintv(f[4], INT_MAX, 0, &v.id)) return 0;
    return add_item(b, &v);
}

static int parse_promotion(char **f, size_t n, batch *b) {
    sm_promotion_record v; uint64_t type, status, u;
    memset(&v, 0, sizeof(v));
    if (n != 18u || !uintv(f[0], INT_MAX, 0, &v.id) || !strv(v.name, sizeof(v.name), f[1], 1) ||
        !uintv(f[2], 4, 1, &type) || !strv(v.product_id, sizeof(v.product_id), f[3], 0) ||
        !bps(f[4], &v.discount_bps) || !money(f[5], &v.threshold_cents) || !money(f[6], &v.discount_cents) ||
        !intv(f[7], 0, INT64_MAX, &v.start_at) || !intv(f[8], 0, INT64_MAX, &v.end_at) ||
        !uintv(f[9], 1, 1, &status) || !uintv(f[10], UINT32_MAX, 1, &u)) return 0;
    v.type = (uint8_t)type; v.status = (uint8_t)status; v.priority = (uint32_t)u;
    if (!intv(f[11], 0, INT64_MAX, &v.created_at) || !uintv(f[12], UINT32_MAX, 1, &u)) return 0;
    v.nth_item = (uint32_t)u;
    if (!bps(f[13], &v.nth_discount_bps) || !uintv(f[14], UINT32_MAX, 1, &u)) return 0;
    v.buy_quantity = (uint32_t)u;
    if (!uintv(f[15], UINT32_MAX, 1, &u)) return 0;
    v.free_quantity = (uint32_t)u;
    if (!uintv(f[16], UINT32_MAX, 1, &u)) return 0;
    v.member_level = (uint32_t)u;
    if (!money(f[17], &v.member_price_cents)) return 0;
    return add_promotion(b, &v);
}

static int parse_schedule(char **f, size_t n, batch *b) {
    sm_schedule_record v; uint64_t year, week; size_t i;
    memset(&v, 0, sizeof(v));
    if (n != 12u || !uintv(f[0], INT_MAX, 0, &v.id) || !uintv(f[1], INT_MAX, 0, &v.employee_id) ||
        !uintv(f[2], UINT32_MAX, 0, &year) || !uintv(f[3], 53, 0, &week)) return 0;
    v.year = (uint32_t)year; v.week = (uint8_t)week;
    for (i = 0; i < 7u; ++i) if (!strv(v.shifts[i], sizeof(v.shifts[i]), f[4u + i], 0)) return 0;
    if (!intv(f[11], 0, INT64_MAX, &v.created_at)) return 0;
    return add_schedule(b, &v);
}

static int parse_combo(char **f, size_t n, batch *b) {
    sm_combo_record v; uint64_t status;
    memset(&v, 0, sizeof(v));
    if (n != 8u || !uintv(f[0], INT_MAX, 0, &v.id) ||
        !strv(v.name, sizeof(v.name), f[1], 1) ||
        !strv(v.barcode, sizeof(v.barcode), f[2], 1) ||
        !money(f[3], &v.price_cents) || !money(f[4], &v.cost_cents) ||
        !uintv(f[5], 1, 1, &status) ||
        !intv(f[6], 0, INT64_MAX, &v.created_at) ||
        !intv(f[7], 0, INT64_MAX, &v.updated_at)) return 0;
    v.status = (uint8_t)status; return add_combo(b, &v);
}

static int parse_combo_item(char **f, size_t n, batch *b) {
    sm_combo_item_record v; uint64_t quantity; uint32_t ratio;
    memset(&v, 0, sizeof(v));
    if (n != 5u || !uintv(f[0], INT_MAX, 0, &v.combo_id) ||
        !strv(v.product_id, sizeof(v.product_id), f[1], 1) ||
        !strv(v.product_name, sizeof(v.product_name), f[2], 1) ||
        !uintv(f[3], UINT32_MAX, 0, &quantity) || !bps(f[4], &ratio) ||
        b->combo_item_count == UINT64_MAX) return 0;
    if (!grow((void **)&b->combo_items, &b->combo_item_cap,
              b->combo_item_count, sizeof(v))) return 0;
    v.id = b->combo_item_count + 1u; v.quantity = (uint32_t)quantity;
    v.ratio_bps = ratio; b->combo_items[b->combo_item_count++] = v; return 1;
}

static int parse_settlement(char **f, size_t n, batch *b) {
    sm_settlement_record v; uint64_t status, orders;
    memset(&v, 0, sizeof(v));
    if (n != 20u || !uintv(f[0], INT_MAX, 0, &v.id) || !uintv(f[1], INT_MAX, 0, &v.cashier_id) ||
        !strv(v.cashier_name, sizeof(v.cashier_name), f[2], 1) || !intv(f[3], 0, INT64_MAX, &v.business_date) ||
        !intv(f[4], 0, INT64_MAX, &v.shift_start) || !intv(f[5], 0, INT64_MAX, &v.shift_end) ||
        !uintv(f[6], UINT32_MAX, 1, &orders) || !money(f[7], &v.system_cash_cents) ||
        !money(f[8], &v.system_online_cents) || !money(f[9], &v.system_total_cents) ||
        !money(f[10], &v.actual_cash_cents) || !money(f[11], &v.actual_online_cents) ||
        !money(f[12], &v.actual_total_cents) || !money(f[13], &v.cash_diff_cents) ||
        !money(f[14], &v.online_diff_cents) || !money(f[15], &v.total_diff_cents) ||
        !uintv(f[16], 2, 1, &status) || !intv(f[17], 0, INT64_MAX, &v.created_at) ||
        !intv(f[18], 0, INT64_MAX, &v.confirmed_at) || !strv(v.remark, sizeof(v.remark), f[19], 0)) return 0;
    v.total_orders = (uint32_t)orders; v.status = (uint8_t)status;
    /* Old aggregate columns are normalized from their source components. */
    v.system_total_cents = v.system_cash_cents + v.system_online_cents;
    v.actual_total_cents = v.actual_cash_cents + v.actual_online_cents;
    v.cash_diff_cents = v.actual_cash_cents - v.system_cash_cents;
    v.online_diff_cents = v.actual_online_cents - v.system_online_cents;
    v.total_diff_cents = v.actual_total_cents - v.system_total_cents;
    return add_settlement(b, &v);
}

static int parse_audit(char **f, size_t n, batch *b) {
    sm_audit_record v;
    memset(&v, 0, sizeof(v));
    if (n != 7u || !uintv(f[0], INT_MAX, 0, &v.id) || !strv(v.type, sizeof(v.type), f[1], 1) ||
        !uintv(f[2], INT_MAX, 1, &v.ref_id) || !strv(v.operation, sizeof(v.operation), f[3], 1) ||
        !strv(v.data, sizeof(v.data), f[4], 0) || !uintv(f[5], INT_MAX, 1, &v.operator_id) ||
        !intv(f[6], 0, INT64_MAX, &v.created_at)) return 0;
    return add_audit(b, &v);
}

static int read_source(batch *b, const char *dir, const char *name, int kind,
                       sm_operations_import_report *report) {
    char path[512], line[LINE_CAP], *f[24]; FILE *file; size_t line_no = 0;
    if (!path_join(path, sizeof(path), dir, name)) return 0;
    errno = 0; file = fopen(path, "rb");
    if (!file) { if (errno == ENOENT) return 1; fail(report, path, 0, "cannot open legacy source"); return 0; }
    ++b->source_files; b->fingerprint = hash_bytes(b->fingerprint, name, strlen(name) + 1u);
    while (fgets(line, sizeof(line), file)) {
        size_t len, n; int ok; ++line_no; len = strlen(line); b->fingerprint = hash_bytes(b->fingerprint, line, len);
        if (len && line[len - 1] != '\n' && !feof(file)) { fail(report, path, line_no, "legacy line too long"); fclose(file); return 0; }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (!len) continue;
        n = fields(line, f, 24u);
        ok = kind == 0 ? parse_store(f, n, b) : kind == 1 ? parse_stock(f, n, b) :
             kind == 2 ? parse_transfer(f, n, b) : kind == 3 ? parse_item(f, n, b) :
             kind == 4 ? parse_promotion(f, n, b) : kind == 5 ? parse_schedule(f, n, b) :
             kind == 6 ? parse_settlement(f, n, b) : kind == 7 ? parse_audit(f, n, b) :
             kind == 8 ? parse_combo(f, n, b) : parse_combo_item(f, n, b);
        if (!ok) { fail(report, path, line_no, "invalid legacy record"); fclose(file); return 0; }
    }
    if (ferror(file)) { fail(report, path, line_no, "cannot read legacy source"); fclose(file); return 0; }
    fclose(file); return 1;
}

static sm_repo_status marker_get(sm_repository *repo, const uint8_t key[2],
                                 uint8_t entity, int *complete) {
    sm_repo_uow *uow = NULL; void *value = NULL; size_t len = 0; sm_repo_status s;
    sm_value_reader r; sm_value_field f; uint8_t state; *complete = 0;
    s = sm_repo_uow_begin(repo, 0, &uow);
    if (s == SM_REPO_OK) s = sm_repo_get(uow, key, 2u, &value, &len);
    if (s == SM_REPO_NOT_FOUND) return sm_repo_uow_commit(&uow);
    if (s == SM_REPO_OK && (sm_value_reader_init(&r, value, len) != SM_CODEC_OK ||
        r.version != MARKER_VERSION || r.entity_type != entity ||
        sm_value_reader_find(&r, 1, &f) != SM_CODEC_OK ||
        sm_value_field_u8(&f, &state) != SM_CODEC_OK || state != 1u)) s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    if (s != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return s; }
    s = sm_repo_uow_commit(&uow); if (s == SM_REPO_OK) *complete = 1; return s;
}

static sm_repo_status marker_put(sm_repo_uow *uow, const uint8_t key[2],
                                 uint8_t entity, const batch *b, uint64_t count) {
    sm_value_writer w; sm_codec_status c; uint8_t *value = NULL; size_t len = 0; sm_repo_status s;
    c = sm_value_writer_init(&w, MARKER_VERSION, entity);
    if (c == SM_CODEC_OK) c = sm_value_write_u8(&w, 1, 1u);
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 2, b->fingerprint);
    if (c == SM_CODEC_OK) c = sm_value_write_i64(&w, 3, (int64_t)time(NULL));
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 4, count);
    if (c == SM_CODEC_OK) c = sm_value_writer_finish(&w, &value, &len);
    if (c != SM_CODEC_OK) { sm_value_writer_dispose(&w); return c == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM : SM_REPO_ERR_INVALID; }
    s = sm_repo_put(uow, key, 2u, value, len); free(value); return s;
}

static int store_exists(const batch *b, uint64_t id) { size_t i; for (i = 0; i < b->store_count; ++i) if (b->stores[i].id == id) return 1; return 0; }
static int transfer_exists(const batch *b, uint64_t id) { size_t i; for (i = 0; i < b->transfer_count; ++i) if (b->transfers[i].id == id) return 1; return 0; }

static void dispose(batch *b) {
    free(b->stores); free(b->stocks); free(b->transfers); free(b->items);
    free(b->promotions); free(b->combos); free(b->combo_items);
    free(b->schedules); free(b->settlements); free(b->audits);
}

static int combo_exists(const batch *b, uint64_t id) {
    size_t i; for (i = 0; i < b->combo_count; ++i) if (b->combos[i].id == id) return 1;
    return 0;
}

sm_repo_status sm_store_transfer_import_if_needed(sm_repository *repo,
                                                  const char *dir,
                                                  sm_operations_import_report *report) {
    batch b; sm_repo_uow *uow = NULL; sm_repo_status s; size_t i; int complete = 0;
    if (!repo || !dir || !*dir) return SM_REPO_ERR_INVALID;
    memset(&b, 0, sizeof(b)); b.fingerprint = UINT64_C(14695981039346656037);
    if (report) memset(report, 0, sizeof(*report));
    s = marker_get(repo, phase11_marker, 0x2bu, &complete);
    if (s != SM_REPO_OK || complete) { if (complete && report) { report->already_completed = 1; snprintf(report->message, sizeof(report->message), "store/transfer import already completed"); } return s; }
    if (!read_source(&b, dir, "store.txt", 0, report) || !read_source(&b, dir, "store_stock.txt", 1, report) ||
        !read_source(&b, dir, "transfer.txt", 2, report) || !read_source(&b, dir, "transfer_item.txt", 3, report)) {
        dispose(&b); return SM_REPO_ERR_CORRUPT;
    }
    for (i = 0; i < b.stock_count; ++i) if (!store_exists(&b, b.stocks[i].store_id)) { fail(report, NULL, 0, "store stock references missing store"); dispose(&b); return SM_REPO_ERR_CORRUPT; }
    for (i = 0; i < b.transfer_count; ++i) if (!store_exists(&b, b.transfers[i].from_store_id) || !store_exists(&b, b.transfers[i].to_store_id)) { fail(report, NULL, 0, "transfer references missing store"); dispose(&b); return SM_REPO_ERR_CORRUPT; }
    for (i = 0; i < b.item_count; ++i) if (!transfer_exists(&b, b.items[i].transfer_id)) { fail(report, NULL, 0, "transfer item references missing order"); dispose(&b); return SM_REPO_ERR_CORRUPT; }
    s = sm_repo_uow_begin(repo, 1, &uow);
    for (i = 0; s == SM_REPO_OK && i < b.store_count; ++i) s = sm_store_record_import(uow, &b.stores[i]);
    for (i = 0; s == SM_REPO_OK && i < b.stock_count; ++i) s = sm_store_stock_put(uow, &b.stocks[i]);
    for (i = 0; s == SM_REPO_OK && i < b.transfer_count; ++i) s = sm_transfer_order_import(uow, &b.transfers[i]);
    for (i = 0; s == SM_REPO_OK && i < b.item_count; ++i) s = sm_transfer_item_import(uow, &b.items[i]);
    if (s == SM_REPO_OK) s = marker_put(uow, phase11_marker, 0x2bu, &b,
        b.store_count + b.stock_count + b.transfer_count + b.item_count);
    if (s == SM_REPO_OK) s = sm_repo_uow_commit(&uow); else (void)sm_repo_uow_rollback(&uow);
    if (report) { report->source_files = b.source_files; report->stores = b.store_count; report->store_stocks = b.stock_count; report->transfers = b.transfer_count; report->transfer_items = b.item_count; report->source_fingerprint = b.fingerprint; report->imported = s == SM_REPO_OK && (b.store_count || b.stock_count || b.transfer_count || b.item_count); snprintf(report->message, sizeof(report->message), s == SM_REPO_OK ? "store/transfer import completed" : "store/transfer import failed"); }
    dispose(&b); return s;
}

sm_repo_status sm_control_import_if_needed(sm_repository *repo, const char *dir,
                                           sm_operations_import_report *report) {
    batch b; sm_repo_uow *uow = NULL; sm_repo_status s; size_t i; int complete = 0;
    if (!repo || !dir || !*dir) return SM_REPO_ERR_INVALID;
    memset(&b, 0, sizeof(b)); b.fingerprint = UINT64_C(14695981039346656037);
    if (report) memset(report, 0, sizeof(*report));
    s = marker_get(repo, phase12_marker, 0x2cu, &complete);
    if (s != SM_REPO_OK || complete) { if (complete && report) { report->already_completed = 1; snprintf(report->message, sizeof(report->message), "control import already completed"); } return s; }
    if (!read_source(&b, dir, "promotion.txt", 4, report) ||
        !read_source(&b, dir, "combo.txt", 8, report) ||
        !read_source(&b, dir, "combo_item.txt", 9, report) ||
        !read_source(&b, dir, "schedule.txt", 5, report) ||
        !read_source(&b, dir, "daily_settlement.txt", 6, report) || !read_source(&b, dir, "transaction.log", 7, report)) {
        dispose(&b); return SM_REPO_ERR_CORRUPT;
    }
    for (i = 0; i < b.combo_item_count; ++i)
        if (!combo_exists(&b, b.combo_items[i].combo_id)) {
            fail(report, NULL, 0, "combo item references missing combo");
            dispose(&b); return SM_REPO_ERR_CORRUPT;
        }
    s = sm_repo_uow_begin(repo, 1, &uow);
    for (i = 0; s == SM_REPO_OK && i < b.promotion_count; ++i) s = sm_promotion_record_import(uow, &b.promotions[i]);
    for (i = 0; s == SM_REPO_OK && i < b.combo_count; ++i) s = sm_combo_record_import(uow, &b.combos[i]);
    for (i = 0; s == SM_REPO_OK && i < b.combo_item_count; ++i) s = sm_combo_item_record_import(uow, &b.combo_items[i]);
    for (i = 0; s == SM_REPO_OK && i < b.schedule_count; ++i) s = sm_schedule_record_import(uow, &b.schedules[i]);
    for (i = 0; s == SM_REPO_OK && i < b.settlement_count; ++i) s = sm_settlement_record_import(uow, &b.settlements[i]);
    for (i = 0; s == SM_REPO_OK && i < b.audit_count; ++i) s = sm_audit_record_import(uow, &b.audits[i]);
    if (s == SM_REPO_OK) s = marker_put(uow, phase12_marker, 0x2cu, &b,
        b.promotion_count + b.combo_count + b.combo_item_count +
        b.schedule_count + b.settlement_count + b.audit_count);
    if (s == SM_REPO_OK) s = sm_repo_uow_commit(&uow); else (void)sm_repo_uow_rollback(&uow);
    if (report) { report->source_files = b.source_files; report->promotions = b.promotion_count; report->combos = b.combo_count; report->combo_items = b.combo_item_count; report->schedules = b.schedule_count; report->settlements = b.settlement_count; report->audits = b.audit_count; report->source_fingerprint = b.fingerprint; report->imported = s == SM_REPO_OK && (b.promotion_count || b.combo_count || b.combo_item_count || b.schedule_count || b.settlement_count || b.audit_count); snprintf(report->message, sizeof(report->message), s == SM_REPO_OK ? "control import completed" : "control import failed"); }
    dispose(&b); return s;
}
