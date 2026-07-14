#include "sm_purchase_import.h"

#include "domain/sm_purchase_entities.h"
#include "repo/sm_purchase_repository.h"
#include "storage/sm_codec.h"
#include "storage/sm_namespace.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SM_PURCHASE_MIGRATION_VERSION 1u
#define SM_PURCHASE_MIGRATION_ENTITY 0x24u
#define SM_PURCHASE_LINE_CAPACITY 4096u

typedef struct sm_purchase_batch {
    sm_purchase *orders; size_t order_count, order_capacity;
    sm_purchase_item *items; size_t item_count, item_capacity;
    sm_batch *batches; size_t batch_count, batch_capacity;
    size_t source_files; uint64_t fingerprint;
} sm_purchase_batch;

static const uint8_t sm_purchase_marker_key[] = {SM_NS_MIGRATION_STATE, 0x08u};

static void sm_error(sm_purchase_import_report *r, const char *file,
                     size_t line, const char *message) {
    if (!r) return;
    if (file) snprintf(r->error_file, sizeof(r->error_file), "%s", file);
    r->error_line = line; snprintf(r->message, sizeof(r->message), "%s", message);
}

static uint64_t sm_fnv(uint64_t hash, const void *data, size_t len) {
    const uint8_t *p = data; size_t i;
    for (i = 0; i < len; ++i) { hash ^= p[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}

static int sm_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t len = strlen(dir); int n = snprintf(out, cap, "%s%s%s", dir,
        len && dir[len - 1] != '/' && dir[len - 1] != '\\' ? "/" : "", name);
    return n >= 0 && (size_t)n < cap;
}

static size_t sm_split(char *line, char **fields, size_t cap) {
    char *p = line; size_t count = 0;
    if (!cap) return 0;
    fields[count++] = p;
    while (*p) { if (*p == '|') { *p = '\0'; if (count == cap) return cap + 1u; fields[count++] = p + 1; } ++p; }
    return count;
}

static int sm_u64(const char *text, uint64_t min, uint64_t max, uint64_t *out) {
    char *end; unsigned long long v;
    if (!text || !*text || *text == '-') return 0;
    errno = 0; v = strtoull(text, &end, 10);
    if (errno || *end || v < min || v > max) return 0;
    *out = (uint64_t)v;
    return 1;
}

static int sm_i64(const char *text, int64_t min, int64_t max, int64_t *out) {
    char *end; long long v;
    if (!text || !*text) return 0;
    errno = 0;
    v = strtoll(text, &end, 10);
    if (errno || *end || v < min || v > max) return 0;
    *out = (int64_t)v;
    return 1;
}

static int sm_scaled(const char *text, int scale, int allow_zero, int64_t *out) {
    char *end; double v, scaled;
    if (!text || !*text) return 0;
    errno = 0;
    v = strtod(text, &end);
    if (errno || *end || !isfinite(v) || v < 0 || (!allow_zero && v == 0)) return 0;
    scaled = v * scale; if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5); return 1;
}

static int sm_text(char *out, size_t cap, const char *value, int required) {
    size_t len = value ? strlen(value) : 0;
    if (len >= cap || (required && !len)) return 0;
    memcpy(out, value, len + 1u); return 1;
}

static int sm_grow(void **array, size_t *cap, size_t count, size_t item_size) {
    size_t next; void *p;
    if (count < *cap) return 1;
    next = *cap ? *cap * 2u : 16u;
    if (next < *cap || next > SIZE_MAX / item_size) return 0;
    p = realloc(*array, next * item_size); if (!p) return 0;
    *array = p; *cap = next; return 1;
}

static int sm_order_add(sm_purchase_batch *b, const sm_purchase *v) {
    size_t i; for (i = 0; i < b->order_count; ++i)
        if (b->orders[i].id == v->id) { b->orders[i] = *v; return 1; }
    if (!sm_grow((void **)&b->orders, &b->order_capacity, b->order_count, sizeof(*v))) return 0;
    b->orders[b->order_count++] = *v; return 1;
}

static int sm_item_add(sm_purchase_batch *b, const sm_purchase_item *v) {
    size_t i; for (i = 0; i < b->item_count; ++i)
        if (b->items[i].purchase_id == v->purchase_id && b->items[i].id == v->id) {
            b->items[i] = *v; return 1;
        }
    if (!sm_grow((void **)&b->items, &b->item_capacity, b->item_count, sizeof(*v))) return 0;
    b->items[b->item_count++] = *v; return 1;
}

static int sm_batch_add(sm_purchase_batch *b, const sm_batch *v) {
    size_t i; for (i = 0; i < b->batch_count; ++i)
        if (!strcmp(b->batches[i].batch_no, v->batch_no)) { b->batches[i] = *v; return 1; }
    if (!sm_grow((void **)&b->batches, &b->batch_capacity, b->batch_count, sizeof(*v))) return 0;
    b->batches[b->batch_count++] = *v; return 1;
}

static int sm_parse_order(char **f, size_t n, sm_purchase_batch *b) {
    sm_purchase v; uint64_t status;
    memset(&v, 0, sizeof(v));
    if (n != 10u || !sm_u64(f[0], 1, INT_MAX, &v.id) ||
        !sm_text(v.supplier_id, sizeof(v.supplier_id), f[1], 1) ||
        !sm_text(v.supplier_name, sizeof(v.supplier_name), f[2], 1) ||
        !sm_u64(f[3], 1, INT_MAX, &v.creator_id) ||
        !sm_u64(f[4], 0, INT_MAX, &v.approver_id) ||
        !sm_u64(f[5], 0, 3, &status) || !sm_scaled(f[6], 100, 1, &v.total_cents) ||
        !sm_i64(f[7], 0, INT64_MAX, &v.created_at) ||
        !sm_i64(f[8], 0, INT64_MAX, &v.approved_at) ||
        !sm_i64(f[9], 0, INT64_MAX, &v.completed_at)) return 0;
    v.status = (uint8_t)status; return sm_order_add(b, &v);
}

static int sm_parse_item(char **f, size_t n, sm_purchase_batch *b) {
    sm_purchase_item v; memset(&v, 0, sizeof(v));
    if (n != 7u || !sm_u64(f[0], 1, INT_MAX, &v.id) ||
        !sm_u64(f[1], 1, INT_MAX, &v.purchase_id) ||
        !sm_text(v.product_id, sizeof(v.product_id), f[2], 1) ||
        !sm_text(v.product_name, sizeof(v.product_name), f[3], 1) ||
        !sm_scaled(f[4], 1000, 0, &v.quantity_milli) ||
        !sm_scaled(f[5], 100, 1, &v.price_cents) ||
        !sm_scaled(f[6], 1000, 1, &v.received_milli) ||
        v.received_milli > v.quantity_milli) return 0;
    return sm_item_add(b, &v);
}

static int sm_parse_batch(char **f, size_t n, sm_purchase_batch *b) {
    sm_batch v; uint64_t supplier; memset(&v, 0, sizeof(v));
    if (n != 11u || !sm_text(v.batch_no, sizeof(v.batch_no), f[0], 1) ||
        !sm_text(v.product_id, sizeof(v.product_id), f[1], 1) ||
        !sm_text(v.product_name, sizeof(v.product_name), f[2], 1) ||
        !sm_scaled(f[3], 1000, 1, &v.quantity_milli) ||
        !sm_scaled(f[4], 1000, 1, &v.initial_quantity_milli) ||
        v.quantity_milli > v.initial_quantity_milli ||
        !sm_scaled(f[5], 100, 1, &v.price_cents) ||
        !sm_i64(f[6], 0, INT64_MAX, &v.production_date) ||
        !sm_i64(f[7], 0, INT64_MAX, &v.expiry_date) ||
        !sm_i64(f[8], 0, INT64_MAX, &v.received_date) ||
        !sm_u64(f[9], 0, INT_MAX, &supplier) ||
        !sm_i64(f[10], 0, INT64_MAX, &v.created_at)) return 0;
    snprintf(v.supplier_id, sizeof(v.supplier_id), "%llu", (unsigned long long)supplier);
    return sm_batch_add(b, &v);
}

static int sm_read(sm_purchase_batch *b, const char *dir, const char *name,
                   int kind, sm_purchase_import_report *report) {
    char path[512], line[SM_PURCHASE_LINE_CAPACITY], *fields[16];
    FILE *file; size_t line_no = 0;
    if (!sm_path(path, sizeof(path), dir, name)) return 0;
    errno = 0; file = fopen(path, "rb");
    if (!file) { if (errno == ENOENT) return 1; sm_error(report, path, 0, "cannot open purchase source"); return 0; }
    ++b->source_files; b->fingerprint = sm_fnv(b->fingerprint, name, strlen(name) + 1u);
    while (fgets(line, sizeof(line), file)) {
        size_t len, count; int ok; ++line_no; len = strlen(line);
        b->fingerprint = sm_fnv(b->fingerprint, line, len);
        if (len && line[len - 1] != '\n' && !feof(file)) { sm_error(report, path, line_no, "purchase source line too long"); fclose(file); return 0; }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (!len) continue;
        count = sm_split(line, fields, 16u);
        ok = kind == 0 ? sm_parse_order(fields, count, b)
             : kind == 1 ? sm_parse_item(fields, count, b)
                         : sm_parse_batch(fields, count, b);
        if (!ok) { sm_error(report, path, line_no, "invalid purchase or batch record"); fclose(file); return 0; }
    }
    if (ferror(file)) { sm_error(report, path, line_no, "cannot read purchase source"); fclose(file); return 0; }
    fclose(file); return 1;
}

static sm_repo_status sm_marker_read(sm_repository *repo, int *complete) {
    sm_repo_uow *uow = NULL; void *value = NULL; size_t len = 0;
    sm_value_reader reader; sm_value_field field; uint8_t state;
    sm_repo_status s = sm_repo_uow_begin(repo, 0, &uow); *complete = 0;
    if (s == SM_REPO_OK) s = sm_repo_get(uow, sm_purchase_marker_key, sizeof(sm_purchase_marker_key), &value, &len);
    if (s == SM_REPO_NOT_FOUND) return sm_repo_uow_commit(&uow);
    if (s != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return s; }
    if (sm_value_reader_init(&reader, value, len) != SM_CODEC_OK ||
        reader.version != SM_PURCHASE_MIGRATION_VERSION ||
        reader.entity_type != SM_PURCHASE_MIGRATION_ENTITY ||
        sm_value_reader_find(&reader, 1, &field) != SM_CODEC_OK ||
        sm_value_field_u8(&field, &state) != SM_CODEC_OK || state != 1u)
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    if (s != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return s; }
    s = sm_repo_uow_commit(&uow); if (s == SM_REPO_OK) *complete = 1; return s;
}

static sm_repo_status sm_marker_write(sm_repo_uow *uow, const sm_purchase_batch *b) {
    sm_value_writer w; uint8_t *value = NULL; size_t len = 0; sm_codec_status c;
    c = sm_value_writer_init(&w, SM_PURCHASE_MIGRATION_VERSION, SM_PURCHASE_MIGRATION_ENTITY);
    if (c == SM_CODEC_OK) c = sm_value_write_u8(&w, 1, 1u);
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 2, b->fingerprint);
    if (c == SM_CODEC_OK) c = sm_value_write_i64(&w, 3, (int64_t)time(NULL));
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 4, b->order_count);
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 5, b->item_count);
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 6, b->batch_count);
    if (c == SM_CODEC_OK) c = sm_value_writer_finish(&w, &value, &len);
    if (c != SM_CODEC_OK) { sm_value_writer_dispose(&w); return c == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM : SM_REPO_ERR_INVALID; }
    { sm_repo_status s = sm_repo_put(uow, sm_purchase_marker_key, sizeof(sm_purchase_marker_key), value, len); free(value); return s; }
}

static sm_repo_status sm_namespace_nonempty(sm_repository *repo, uint8_t ns,
                                            int *out_nonempty) {
    uint8_t prefix[1];
    sm_repo_scan *scan = NULL;
    sm_repo_status s, close_s;
    prefix[0] = ns;
    *out_nonempty = 0;
    s = sm_repo_scan_open_prefix(repo, ns, prefix, sizeof(prefix), &scan);
    if (s == SM_REPO_OK) {
        s = sm_repo_scan_next(scan);
        if (s == SM_REPO_OK) {
            *out_nonempty = 1;
            s = SM_REPO_OK;
        } else if (s == SM_REPO_ITER_END) {
            s = SM_REPO_OK;
        }
    }
    close_s = sm_repo_scan_close(&scan);
    return s == SM_REPO_OK ? close_s : s;
}

sm_repo_status sm_purchase_import_if_needed(sm_repository *repo, const char *dir,
                                            sm_purchase_import_report *report) {
    sm_purchase_batch b; sm_purchase *orders = NULL; sm_batch *batches = NULL;
    size_t order_count = 0, batch_count = 0, i; sm_repo_uow *uow = NULL;
    sm_repo_status s; int complete = 0;
    int item_nonempty = 0;
    if (!repo || !dir || !*dir) return SM_REPO_ERR_INVALID;
    if (report) memset(report, 0, sizeof(*report));
    memset(&b, 0, sizeof(b));
    b.fingerprint = UINT64_C(14695981039346656037);
    s = sm_marker_read(repo, &complete); if (s != SM_REPO_OK) return s;
    if (complete) { if (report) { report->already_completed = 1; snprintf(report->message, sizeof(report->message), "purchase import already completed"); } return SM_REPO_OK; }
    s = sm_purchase_list(repo, -1, &orders, &order_count); sm_purchase_array_free(orders);
    if (s == SM_REPO_OK)
        s = sm_batch_list(repo, NULL, 0, &batches, &batch_count);
    sm_purchase_array_free(batches);
    if (s == SM_REPO_OK)
        s = sm_namespace_nonempty(repo, SM_NS_PURCHASE_ITEM_BY_PURCHASE,
                                  &item_nonempty);
    if (s != SM_REPO_OK || order_count || batch_count || item_nonempty) {
        sm_error(report, NULL, 0,
                 "purchase database is not empty without marker");
        return s == SM_REPO_OK ? SM_REPO_CONFLICT : s;
    }
    if (!sm_read(&b, dir, "purchase.txt", 0, report) ||
        !sm_read(&b, dir, "purchase_item.txt", 1, report) ||
        !sm_read(&b, dir, "batch.txt", 2, report)) {
        free(b.orders); free(b.items); free(b.batches); return SM_REPO_ERR_CORRUPT;
    }
    s = sm_repo_uow_begin(repo, 1, &uow);
    for (i = 0; s == SM_REPO_OK && i < b.order_count; ++i) s = sm_purchase_import(uow, &b.orders[i]);
    for (i = 0; s == SM_REPO_OK && i < b.item_count; ++i) s = sm_purchase_item_import(uow, &b.items[i]);
    for (i = 0; s == SM_REPO_OK && i < b.batch_count; ++i) s = sm_batch_import(uow, &b.batches[i]);
    if (s == SM_REPO_OK) s = sm_marker_write(uow, &b);
    if (s == SM_REPO_OK) s = sm_repo_uow_commit(&uow); else (void)sm_repo_uow_rollback(&uow);
    if (report) {
        report->source_files = b.source_files; report->purchases = b.order_count;
        report->purchase_items = b.item_count; report->batches = b.batch_count;
        report->source_fingerprint = b.fingerprint;
        report->imported = s == SM_REPO_OK && (b.order_count || b.item_count || b.batch_count);
        snprintf(report->message, sizeof(report->message), s == SM_REPO_OK ? "purchase import completed" : "purchase import failed");
    }
    free(b.orders); free(b.items); free(b.batches); return s;
}
