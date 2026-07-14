#include "migration/sm_finance_import.h"

#include "domain/sm_finance_entities.h"
#include "repo/sm_finance_repository.h"
#include "storage/sm_codec.h"
#include "storage/sm_namespace.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LINE_CAP 4096u
#define MIGRATION_VERSION 1u

typedef struct import_batch {
    sm_supplier_finance *finances; size_t finance_count, finance_cap;
    sm_payable *payables; size_t payable_count, payable_cap;
    sm_payment_record *payments; size_t payment_count, payment_cap;
    sm_vip_card *cards; size_t card_count, card_cap;
    sm_vip_transaction *transactions; size_t transaction_count, transaction_cap;
    size_t source_files; uint64_t fingerprint;
} import_batch;

static const uint8_t finance_marker[] = {SM_NS_MIGRATION_STATE, 0x09u};
static const uint8_t vip_marker[] = {SM_NS_MIGRATION_STATE, 0x0au};

static void report_error(sm_finance_import_report *r, const char *file,
                         size_t line, const char *message) {
    if (!r) return;
    if (file) snprintf(r->error_file, sizeof(r->error_file), "%s", file);
    r->error_line = line;
    snprintf(r->message, sizeof(r->message), "%s", message);
}

static uint64_t fnv(uint64_t h, const void *data, size_t len) {
    const uint8_t *p = data; size_t i;
    for (i = 0; i < len; ++i) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}

static int make_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t n = strlen(dir); int written = snprintf(out, cap, "%s%s%s", dir,
        n && dir[n - 1] != '/' && dir[n - 1] != '\\' ? "/" : "", name);
    return written >= 0 && (size_t)written < cap;
}

static size_t split(char *line, char **fields, size_t cap) {
    char *p = line; size_t count = 0;
    if (!cap) return 0;
    fields[count++] = p;
    while (*p) {
        if (*p == '|') {
            *p = '\0'; if (count == cap) return cap + 1u; fields[count++] = p + 1;
        }
        ++p;
    }
    return count;
}

static int u64v(const char *text, uint64_t max, uint64_t *out) {
    char *end; unsigned long long v;
    if (!text || !*text || *text == '-') return 0;
    errno = 0; v = strtoull(text, &end, 10);
    if (errno || *end || !v || v > max) return 0;
    *out = (uint64_t)v;
    return 1;
}

static int u64z(const char *text, uint64_t max, uint64_t *out) {
    char *end; unsigned long long v;
    if (!text || !*text || *text == '-') return 0;
    errno = 0; v = strtoull(text, &end, 10);
    if (errno || *end || v > max) return 0;
    *out = (uint64_t)v; return 1;
}

static int i64v(const char *text, int64_t *out) {
    char *end; long long v;
    if (!text || !*text) return 0;
    errno = 0;
    v = strtoll(text, &end, 10);
    if (errno || *end || v < 0) return 0;
    *out = v;
    return 1;
}

static int cents(const char *text, int allow_zero, int64_t *out) {
    char *end; double v, scaled;
    if (!text || !*text) return 0;
    errno = 0;
    v = strtod(text, &end);
    if (errno || *end || !isfinite(v) || v < 0 || (!allow_zero && v == 0)) return 0;
    scaled = v * 100.0; if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5); return 1;
}

static int textv(char *out, size_t cap, const char *text, int required) {
    size_t n = text ? strlen(text) : 0;
    if (n >= cap || (required && !n)) return 0;
    memcpy(out, text, n + 1u); return 1;
}

static int grow(void **array, size_t *cap, size_t count, size_t width) {
    size_t next; void *p;
    if (count < *cap) return 1;
    next = *cap ? *cap * 2u : 16u;
    if (next < *cap || next > SIZE_MAX / width) return 0;
    p = realloc(*array, next * width); if (!p) return 0;
    *array = p; *cap = next; return 1;
}

#define UPSERT(name, field, type, array, count, cap) \
static int name(import_batch *b, const type *v) { \
    size_t i; for (i = 0; i < b->count; ++i) \
        if (b->array[i].field == v->field) { b->array[i] = *v; return 1; } \
    if (!grow((void **)&b->array, &b->cap, b->count, sizeof(*v))) return 0; \
    b->array[b->count++] = *v; return 1; \
}

UPSERT(add_finance, supplier_id, sm_supplier_finance, finances, finance_count, finance_cap)
UPSERT(add_payable, id, sm_payable, payables, payable_count, payable_cap)
UPSERT(add_payment, id, sm_payment_record, payments, payment_count, payment_cap)
UPSERT(add_transaction, id, sm_vip_transaction, transactions, transaction_count, transaction_cap)

#undef UPSERT

static int add_card_exact(import_batch *b, const sm_vip_card *v) {
    size_t i;
    for (i = 0; i < b->card_count; ++i)
        if (strcmp(b->cards[i].card_no, v->card_no) == 0) {
            b->cards[i] = *v; return 1;
        }
    if (!grow((void **)&b->cards, &b->card_cap, b->card_count, sizeof(*v))) return 0;
    b->cards[b->card_count++] = *v; return 1;
}

static int parse_finance(char **f, size_t n, import_batch *b) {
    sm_supplier_finance v; uint64_t days; int64_t ignored;
    memset(&v, 0, sizeof(v));
    if (n != 8u || !u64v(f[0], INT_MAX, &v.supplier_id) ||
        !cents(f[1], 1, &ignored) || ignored % 100 != 0 ||
        (days = (uint64_t)(ignored / 100)) > UINT32_MAX ||
        strlen(f[2]) != 1u || !cents(f[3], 1, &v.total_cents) ||
        !cents(f[4], 1, &v.paid_cents) || !cents(f[5], 1, &v.pending_cents) ||
        !i64v(f[6], &v.last_payment_at) || !i64v(f[7], &v.updated_at)) return 0;
    v.payment_days = (uint32_t)days; v.rating = (uint8_t)f[2][0];
    return add_finance(b, &v);
}

static int parse_payable(char **f, size_t n, import_batch *b) {
    sm_payable v; uint64_t status, purchase;
    memset(&v, 0, sizeof(v));
    if (n != 11u || !u64v(f[0], INT_MAX, &v.id) ||
        !u64v(f[1], INT_MAX, &v.supplier_id) ||
        !textv(v.supplier_name, sizeof(v.supplier_name), f[2], 1) ||
        (!(f[3][0] == '0' && f[3][1] == '\0') &&
         !u64v(f[3], INT_MAX, &purchase))) return 0;
    v.purchase_id = f[3][0] == '0' && f[3][1] == '\0' ? 0 : purchase;
    if (!cents(f[4], 0, &v.amount_cents) || !cents(f[5], 1, &v.paid_cents) ||
        !cents(f[6], 1, &v.pending_cents) || !u64z(f[7], 2, &status) ||
        !i64v(f[8], &v.due_at) || !i64v(f[9], &v.created_at) ||
        !i64v(f[10], &v.paid_at)) return 0;
    v.status = (uint8_t)status; return add_payable(b, &v);
}

static int parse_payment(char **f, size_t n, import_batch *b) {
    sm_payment_record v;
    memset(&v, 0, sizeof(v));
    if (n != 10u || !u64v(f[0], INT_MAX, &v.id) ||
        !u64v(f[1], INT_MAX, &v.payable_id) || !u64v(f[2], INT_MAX, &v.supplier_id) ||
        !cents(f[3], 0, &v.amount_cents) ||
        !textv(v.method, sizeof(v.method), f[4], 1) ||
        !textv(v.reference, sizeof(v.reference), f[5], 0) ||
        (!(f[6][0] == '0' && f[6][1] == '\0') &&
         !u64v(f[6], INT_MAX, &v.operator_id))) return 0;
    if (!textv(v.operator_name, sizeof(v.operator_name), f[7], 0) ||
        !textv(v.remark, sizeof(v.remark), f[8], 0) || !i64v(f[9], &v.paid_at)) return 0;
    return add_payment(b, &v);
}

static int parse_card(char **f, size_t n, import_batch *b) {
    sm_vip_card v; uint64_t member = 0, type, status;
    memset(&v, 0, sizeof(v));
    if (n != 11u || !textv(v.card_no, sizeof(v.card_no), f[0], 1) ||
        (!(f[1][0] == '0' && f[1][1] == '\0') && !u64v(f[1], INT_MAX, &member)) ||
        !cents(f[2], 1, &v.balance_cents) ||
        !cents(f[3], 1, &v.total_recharged_cents) || !u64z(f[4], 2, &type) ||
        (!(f[5][0] == '0' && f[5][1] == '\0') &&
         !u64v(f[5], 3, &status))) return 0;
    v.member_id = member; v.card_type = (uint8_t)type;
    v.status = f[5][0] == '0' && f[5][1] == '\0' ? 0 : (uint8_t)status;
    if (!i64v(f[6], &v.created_at) || !i64v(f[7], &v.updated_at) ||
        !i64v(f[8], &v.expired_at) ||
        !textv(v.password_hash, sizeof(v.password_hash), f[9], 0) ||
        !textv(v.password_salt, sizeof(v.password_salt), f[10], 0)) return 0;
    return add_card_exact(b, &v);
}

static int parse_transaction(char **f, size_t n, import_batch *b) {
    sm_vip_transaction v; uint64_t type, op = 0, sale = 0;
    memset(&v, 0, sizeof(v));
    if (n != 11u || !u64v(f[0], INT_MAX, &v.id) ||
        !textv(v.card_no, sizeof(v.card_no), f[1], 1) || !u64z(f[2], 2, &type) ||
        !cents(f[3], 0, &v.amount_cents) || !cents(f[4], 1, &v.balance_before_cents) ||
        !cents(f[5], 1, &v.balance_after_cents) ||
        (!(f[6][0] == '0' && f[6][1] == '\0') && !u64v(f[6], INT_MAX, &op)) ||
        !textv(v.operator_name, sizeof(v.operator_name), f[7], 0) ||
        (!(f[8][0] == '0' && f[8][1] == '\0') && !u64v(f[8], INT_MAX, &sale)) ||
        !textv(v.remark, sizeof(v.remark), f[9], 0) || !i64v(f[10], &v.created_at)) return 0;
    v.type = (uint8_t)type; v.operator_id = op; v.sale_id = sale;
    return add_transaction(b, &v);
}

static int read_file(import_batch *b, const char *dir, const char *name,
                     int kind, sm_finance_import_report *report) {
    char path[512], line[LINE_CAP], *fields[16]; FILE *file; size_t line_no = 0;
    if (!make_path(path, sizeof(path), dir, name)) return 0;
    errno = 0; file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 1;
        report_error(report, path, 0, "cannot open finance source"); return 0;
    }
    ++b->source_files; b->fingerprint = fnv(b->fingerprint, name, strlen(name) + 1u);
    while (fgets(line, sizeof(line), file)) {
        size_t len, count; int ok; ++line_no; len = strlen(line);
        b->fingerprint = fnv(b->fingerprint, line, len);
        if (len && line[len - 1] != '\n' && !feof(file)) {
            report_error(report, path, line_no, "finance source line too long"); fclose(file); return 0;
        }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (!len) continue;
        count = split(line, fields, 16u);
        ok = kind == 0 ? parse_finance(fields, count, b)
             : kind == 1 ? parse_payable(fields, count, b)
             : kind == 2 ? parse_payment(fields, count, b)
             : kind == 3 ? parse_card(fields, count, b)
                         : parse_transaction(fields, count, b);
        if (!ok) { report_error(report, path, line_no, "invalid finance record"); fclose(file); return 0; }
    }
    if (ferror(file)) { report_error(report, path, line_no, "cannot read finance source"); fclose(file); return 0; }
    fclose(file); return 1;
}

static sm_repo_status marker_read(sm_repository *repo, const uint8_t key[2],
                                  uint8_t entity, int *complete) {
    sm_repo_uow *uow = NULL; void *value = NULL; size_t len = 0;
    sm_value_reader r; sm_value_field f; uint8_t state; sm_repo_status s;
    *complete = 0; s = sm_repo_uow_begin(repo, 0, &uow);
    if (s == SM_REPO_OK) s = sm_repo_get(uow, key, 2u, &value, &len);
    if (s == SM_REPO_NOT_FOUND) return sm_repo_uow_commit(&uow);
    if (s == SM_REPO_OK && (sm_value_reader_init(&r, value, len) != SM_CODEC_OK ||
        r.version != MIGRATION_VERSION || r.entity_type != entity ||
        sm_value_reader_find(&r, 1, &f) != SM_CODEC_OK ||
        sm_value_field_u8(&f, &state) != SM_CODEC_OK || state != 1u))
        s = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    if (s != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return s; }
    s = sm_repo_uow_commit(&uow); if (s == SM_REPO_OK) *complete = 1; return s;
}

static sm_repo_status marker_write(sm_repo_uow *uow, const uint8_t key[2],
                                   uint8_t entity, const import_batch *b,
                                   uint64_t count) {
    sm_value_writer w; uint8_t *value = NULL; size_t len = 0; sm_codec_status c;
    c = sm_value_writer_init(&w, MIGRATION_VERSION, entity);
    if (c == SM_CODEC_OK) c = sm_value_write_u8(&w, 1, 1u);
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 2, b->fingerprint);
    if (c == SM_CODEC_OK) c = sm_value_write_i64(&w, 3, time(NULL));
    if (c == SM_CODEC_OK) c = sm_value_write_u64(&w, 4, count);
    if (c == SM_CODEC_OK) c = sm_value_writer_finish(&w, &value, &len);
    if (c != SM_CODEC_OK) { sm_value_writer_dispose(&w); return c == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM : SM_REPO_ERR_INVALID; }
    { sm_repo_status s = sm_repo_put(uow, key, 2u, value, len); free(value); return s; }
}

static sm_supplier_finance *find_finance(import_batch *b, uint64_t supplier_id) {
    size_t i;
    for (i = 0; i < b->finance_count; ++i)
        if (b->finances[i].supplier_id == supplier_id) return &b->finances[i];
    return NULL;
}

static sm_payable *find_payable(import_batch *b, uint64_t id) {
    size_t i;
    for (i = 0; i < b->payable_count; ++i)
        if (b->payables[i].id == id) return &b->payables[i];
    return NULL;
}

static int normalize_finance(import_batch *b) {
    size_t i;
    for (i = 0; i < b->finance_count; ++i) {
        b->finances[i].total_cents = b->finances[i].paid_cents = 0;
        b->finances[i].pending_cents = 0; b->finances[i].last_payment_at = 0;
    }
    for (i = 0; i < b->payment_count; ++i) {
        sm_payable *p = find_payable(b, b->payments[i].payable_id);
        if (!p || p->supplier_id != b->payments[i].supplier_id) return 0;
    }
    for (i = 0; i < b->payable_count; ++i) {
        size_t j; int64_t paid = 0, paid_at = 0;
        sm_supplier_finance *f = find_finance(b, b->payables[i].supplier_id);
        if (!f) {
            sm_supplier_finance created; memset(&created, 0, sizeof(created));
            created.supplier_id = b->payables[i].supplier_id; created.rating = 'C';
            created.updated_at = b->payables[i].created_at;
            if (!add_finance(b, &created)) return 0;
            f = find_finance(b, created.supplier_id);
        }
        for (j = 0; j < b->payment_count; ++j)
            if (b->payments[j].payable_id == b->payables[i].id) {
                if (paid > INT64_MAX - b->payments[j].amount_cents) return 0;
                paid += b->payments[j].amount_cents;
                if (b->payments[j].paid_at > paid_at) paid_at = b->payments[j].paid_at;
            }
        if (paid > b->payables[i].amount_cents) return 0;
        b->payables[i].paid_cents = paid;
        b->payables[i].pending_cents = b->payables[i].amount_cents - paid;
        b->payables[i].status = paid == 0 ? 0u
                                  : paid == b->payables[i].amount_cents
                                        ? 2u : 1u;
        b->payables[i].paid_at = b->payables[i].status == 2u ? paid_at : 0;
        if (f->total_cents > INT64_MAX - b->payables[i].amount_cents ||
            f->paid_cents > INT64_MAX - paid ||
            f->pending_cents > INT64_MAX - b->payables[i].pending_cents) return 0;
        f->total_cents += b->payables[i].amount_cents; f->paid_cents += paid;
        f->pending_cents += b->payables[i].pending_cents;
        if (paid_at > f->last_payment_at) f->last_payment_at = paid_at;
    }
    return 1;
}

static void dispose(import_batch *b) {
    free(b->finances); free(b->payables); free(b->payments);
    free(b->cards); free(b->transactions);
}

sm_repo_status sm_supplier_finance_import_if_needed(
    sm_repository *repo, const char *dir, sm_finance_import_report *report) {
    import_batch b; sm_repo_uow *uow = NULL; sm_repo_status s; size_t i;
    int complete = 0;
    if (!repo || !dir || !*dir) return SM_REPO_ERR_INVALID;
    if (report) memset(report, 0, sizeof(*report));
    memset(&b, 0, sizeof(b));
    b.fingerprint = UINT64_C(14695981039346656037);
    s = marker_read(repo, finance_marker, 0x29u, &complete);
    if (s != SM_REPO_OK || complete) {
        if (complete && report) { report->already_completed = 1; snprintf(report->message, sizeof(report->message), "supplier finance import already completed"); }
        return s;
    }
    if (!read_file(&b, dir, "supplier_finance.txt", 0, report) ||
        !read_file(&b, dir, "payable.txt", 1, report) ||
        !read_file(&b, dir, "payment_record.txt", 2, report) || !normalize_finance(&b)) {
        if (report && !report->message[0]) report_error(report, NULL, 0, "inconsistent supplier finance records");
        dispose(&b); return SM_REPO_ERR_CORRUPT;
    }
    s = sm_repo_uow_begin(repo, 1, &uow);
    for (i = 0; s == SM_REPO_OK && i < b.finance_count; ++i) s = sm_supplier_finance_put(uow, &b.finances[i]);
    for (i = 0; s == SM_REPO_OK && i < b.payable_count; ++i) s = sm_payable_import(uow, &b.payables[i]);
    for (i = 0; s == SM_REPO_OK && i < b.payment_count; ++i) s = sm_payment_record_import(uow, &b.payments[i]);
    if (s == SM_REPO_OK) s = marker_write(uow, finance_marker, 0x29u, &b,
                                           b.finance_count + b.payable_count + b.payment_count);
    if (s == SM_REPO_OK) s = sm_repo_uow_commit(&uow); else (void)sm_repo_uow_rollback(&uow);
    if (report) {
        report->source_files = b.source_files; report->supplier_finances = b.finance_count;
        report->payables = b.payable_count; report->payments = b.payment_count;
        report->source_fingerprint = b.fingerprint;
        report->imported = s == SM_REPO_OK && (b.finance_count || b.payable_count || b.payment_count);
        snprintf(report->message, sizeof(report->message), s == SM_REPO_OK ? "supplier finance import completed" : "supplier finance import failed");
    }
    dispose(&b); return s;
}

sm_repo_status sm_vip_import_if_needed(sm_repository *repo, const char *dir,
                                       sm_finance_import_report *report) {
    import_batch b; sm_repo_uow *uow = NULL; sm_repo_status s; size_t i;
    int complete = 0;
    if (!repo || !dir || !*dir) return SM_REPO_ERR_INVALID;
    if (report) memset(report, 0, sizeof(*report));
    memset(&b, 0, sizeof(b));
    b.fingerprint = UINT64_C(14695981039346656037);
    s = marker_read(repo, vip_marker, 0x2au, &complete);
    if (s != SM_REPO_OK || complete) {
        if (complete && report) { report->already_completed = 1; snprintf(report->message, sizeof(report->message), "vip import already completed"); }
        return s;
    }
    if (!read_file(&b, dir, "vipcard.txt", 3, report) ||
        !read_file(&b, dir, "vipcard_trans.txt", 4, report)) {
        dispose(&b); return SM_REPO_ERR_CORRUPT;
    }
    for (i = 0; i < b.transaction_count; ++i) {
        size_t j; int found = 0;
        for (j = 0; j < b.card_count; ++j)
            if (strcmp(b.transactions[i].card_no, b.cards[j].card_no) == 0) found = 1;
        if (!found) { report_error(report, NULL, 0, "vip transaction references missing card"); dispose(&b); return SM_REPO_ERR_CORRUPT; }
    }
    s = sm_repo_uow_begin(repo, 1, &uow);
    for (i = 0; s == SM_REPO_OK && i < b.card_count; ++i) s = sm_vip_card_import(uow, &b.cards[i]);
    for (i = 0; s == SM_REPO_OK && i < b.transaction_count; ++i) s = sm_vip_transaction_import(uow, &b.transactions[i]);
    if (s == SM_REPO_OK) s = marker_write(uow, vip_marker, 0x2au, &b,
                                           b.card_count + b.transaction_count);
    if (s == SM_REPO_OK) s = sm_repo_uow_commit(&uow); else (void)sm_repo_uow_rollback(&uow);
    if (report) {
        report->source_files = b.source_files; report->vip_cards = b.card_count;
        report->vip_transactions = b.transaction_count; report->source_fingerprint = b.fingerprint;
        report->imported = s == SM_REPO_OK && (b.card_count || b.transaction_count);
        snprintf(report->message, sizeof(report->message), s == SM_REPO_OK ? "vip import completed" : "vip import failed");
    }
    dispose(&b); return s;
}
