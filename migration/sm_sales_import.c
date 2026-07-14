#include "sm_sales_import.h"

#include "domain/sm_sales_entities.h"
#include "repo/sm_sales_repository.h"
#include "storage/sm_codec.h"
#include "storage/sm_namespace.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SM_SALES_MIGRATION_VERSION 1u
#define SM_SALES_MIGRATION_ENTITY 0x22u
#define SM_SALES_MIGRATION_COMPLETE 1u
#define SM_SALES_LINE_CAPACITY 4096u
#define SM_SALES_FIELD_CAPACITY 16u

typedef struct sm_sales_batch {
    sm_sale *sales;
    size_t sale_count;
    size_t sale_capacity;
    sm_sale_item *items;
    size_t item_count;
    size_t item_capacity;
    size_t source_files;
    uint64_t fingerprint;
} sm_sales_batch;

static const uint8_t sm_sales_marker_key[] = {SM_NS_MIGRATION_STATE, 0x06u};

static void sm_sales_report_error(sm_sales_import_report *report,
                                  const char *file, size_t line,
                                  const char *message) {
    if (!report) return;
    if (file) snprintf(report->error_file, sizeof(report->error_file),
                       "%s", file);
    report->error_line = line;
    snprintf(report->message, sizeof(report->message), "%s", message);
}

static uint64_t sm_sales_fnv(uint64_t hash, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int sm_sales_path(char *out, size_t capacity, const char *directory,
                         const char *name) {
    size_t len = strlen(directory);
    int n = snprintf(out, capacity, "%s%s%s", directory,
                     len && directory[len - 1] != '/' &&
                             directory[len - 1] != '\\' ? "/" : "",
                     name);
    return n >= 0 && (size_t)n < capacity;
}

static size_t sm_sales_split(char *line, char **fields, size_t capacity) {
    char *cursor = line;
    size_t count = 0;
    if (capacity == 0) return 0;
    fields[count++] = cursor;
    while (*cursor) {
        if (*cursor == '|') {
            *cursor = '\0';
            if (count == capacity) return capacity + 1u;
            fields[count++] = cursor + 1;
        }
        ++cursor;
    }
    return count;
}

static int sm_sales_u64(const char *text, uint64_t minimum,
                        uint64_t maximum, uint64_t *out) {
    char *end;
    unsigned long long value;
    if (!text || !*text || *text == '-') return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || *end || value < minimum || value > maximum) return 0;
    *out = (uint64_t)value;
    return 1;
}

static int sm_sales_i64(const char *text, int64_t minimum,
                        int64_t maximum, int64_t *out) {
    char *end;
    long long value;
    if (!text || !*text) return 0;
    errno = 0;
    value = strtoll(text, &end, 10);
    if (errno || *end || value < minimum || value > maximum) return 0;
    *out = (int64_t)value;
    return 1;
}

static int sm_sales_scaled(const char *text, int scale, int allow_zero,
                           int64_t *out) {
    char *end;
    double value, scaled;
    if (!text || !*text) return 0;
    errno = 0;
    value = strtod(text, &end);
    if (errno || *end || !isfinite(value) || value < 0.0 ||
        (!allow_zero && value == 0.0))
        return 0;
    scaled = value * scale;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_sales_text(char *out, size_t capacity, const char *value,
                         int required) {
    size_t len = value ? strlen(value) : 0;
    if (len >= capacity || (required && len == 0)) return 0;
    memcpy(out, value, len + 1u);
    return 1;
}

static int sm_sales_append(void **array, size_t *count, size_t *capacity,
                           size_t element_size, const void *value) {
    void *next;
    size_t next_capacity;
    if (*count == *capacity) {
        next_capacity = *capacity ? *capacity * 2u : 16u;
        if (next_capacity < *capacity ||
            next_capacity > SIZE_MAX / element_size)
            return 0;
        next = realloc(*array, next_capacity * element_size);
        if (!next) return 0;
        *array = next;
        *capacity = next_capacity;
    }
    memcpy((uint8_t *)*array + *count * element_size, value, element_size);
    ++*count;
    return 1;
}

static int sm_sales_parse_sale(char **f, size_t count, int pending,
                               sm_sales_batch *batch) {
    sm_sale sale;
    uint64_t status;
    memset(&sale, 0, sizeof(sale));
    if ((!pending && count != 10u) || (pending && count != 12u)) return 0;
    if (!sm_sales_u64(f[0], 1, INT_MAX, &sale.id) ||
        !sm_sales_u64(f[1], 1, INT_MAX, &sale.cashier_id) ||
        !sm_sales_u64(f[2], 0, INT_MAX, &sale.member_id) ||
        !sm_sales_scaled(f[3], 100, 1, &sale.total_cents) ||
        !sm_sales_scaled(f[4], 100, 1, &sale.discount_cents) ||
        !sm_sales_scaled(f[5], 100, 1, &sale.final_cents))
        return 0;
    if (pending) {
        if (!sm_sales_scaled(f[6], 100, 1, &sale.cash_received_cents) ||
            !sm_sales_i64(f[7], 0, INT_MAX, &sale.points_used) ||
            !sm_sales_text(sale.payment_method,
                           sizeof(sale.payment_method), f[8], 0) ||
            !sm_sales_u64(f[9], 0, 2, &status) ||
            !sm_sales_i64(f[10], 0, INT64_MAX, &sale.created_at) ||
            !sm_sales_i64(f[11], 0, INT64_MAX, &sale.completed_at))
            return 0;
        if (status != 0) return 0;
    } else {
        if (!sm_sales_text(sale.payment_method,
                           sizeof(sale.payment_method), f[6], 0) ||
            !sm_sales_u64(f[7], 0, 2, &status) ||
            !sm_sales_i64(f[8], 0, INT64_MAX, &sale.created_at) ||
            !sm_sales_i64(f[9], sale.created_at, INT64_MAX,
                          &sale.completed_at))
            return 0;
        if (status == 0) return 0;
    }
    sale.status = (uint8_t)status;
    return sm_sales_append((void **)&batch->sales, &batch->sale_count,
                           &batch->sale_capacity, sizeof(sale), &sale);
}

static int sm_sales_parse_item(char **f, size_t count,
                               sm_sales_batch *batch) {
    sm_sale_item item;
    uint64_t flag;
    memset(&item, 0, sizeof(item));
    if (count != 11u) return 0;
    if (!sm_sales_u64(f[0], 1, INT_MAX, &item.id) ||
        !sm_sales_u64(f[1], 1, INT_MAX, &item.sale_id) ||
        !sm_sales_text(item.product_id, sizeof(item.product_id), f[2], 1) ||
        !sm_sales_text(item.product_name, sizeof(item.product_name), f[3], 1) ||
        !sm_sales_scaled(f[4], 1000, 0, &item.quantity_milli) ||
        !sm_sales_scaled(f[5], 100, 1, &item.price_cents) ||
        !sm_sales_scaled(f[6], 100, 1, &item.original_price_cents) ||
        !sm_sales_scaled(f[7], 100, 1, &item.subtotal_cents) ||
        !sm_sales_scaled(f[8], 100, 1, &item.discount_cents) ||
        !sm_sales_u64(f[9], 0, 1, &flag) ||
        !sm_sales_u64(f[10], flag ? 1 : 0, INT_MAX, &item.combo_id))
        return 0;
    item.is_combo = (uint8_t)flag;
    return sm_sales_append((void **)&batch->items, &batch->item_count,
                           &batch->item_capacity, sizeof(item), &item);
}

static int sm_sales_read_file(sm_sales_batch *batch, const char *directory,
                              const char *name, int kind,
                              sm_sales_import_report *report) {
    char path[512], line[SM_SALES_LINE_CAPACITY];
    char *fields[SM_SALES_FIELD_CAPACITY];
    FILE *file;
    size_t line_number = 0;
    if (!sm_sales_path(path, sizeof(path), directory, name)) return 0;
    errno = 0;
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 1;
        sm_sales_report_error(report, path, 0, "cannot open sales source");
        return 0;
    }
    ++batch->source_files;
    batch->fingerprint = sm_sales_fnv(batch->fingerprint, name,
                                      strlen(name) + 1u);
    while (fgets(line, sizeof(line), file)) {
        size_t len, count;
        ++line_number;
        len = strlen(line);
        batch->fingerprint = sm_sales_fnv(batch->fingerprint, line, len);
        if (len && line[len - 1] != '\n' && !feof(file)) {
            sm_sales_report_error(report, path, line_number,
                                  "sales source line is too long");
            fclose(file);
            return 0;
        }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (!len) continue;
        count = sm_sales_split(line, fields, SM_SALES_FIELD_CAPACITY);
        if (count > SM_SALES_FIELD_CAPACITY ||
            !(kind == 2 ? sm_sales_parse_item(fields, count, batch)
                        : sm_sales_parse_sale(fields, count, kind == 1,
                                              batch))) {
            sm_sales_report_error(report, path, line_number,
                                  "invalid sales source record");
            fclose(file);
            return 0;
        }
    }
    if (ferror(file)) {
        sm_sales_report_error(report, path, line_number,
                              "cannot read sales source");
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static sm_repo_status sm_sales_marker_read(sm_repository *repo,
                                           int *out_complete) {
    sm_repo_uow *uow = NULL;
    void *value = NULL;
    size_t value_len = 0;
    sm_value_reader reader;
    sm_value_field field;
    uint8_t state;
    sm_repo_status status = sm_repo_uow_begin(repo, 0, &uow);
    *out_complete = 0;
    if (status == SM_REPO_OK)
        status = sm_repo_get(uow, sm_sales_marker_key,
                             sizeof(sm_sales_marker_key), &value, &value_len);
    if (status == SM_REPO_NOT_FOUND) return sm_repo_uow_commit(&uow);
    if (status != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return status; }
    if (sm_value_reader_init(&reader, value, value_len) != SM_CODEC_OK ||
        reader.version != SM_SALES_MIGRATION_VERSION ||
        reader.entity_type != SM_SALES_MIGRATION_ENTITY ||
        sm_value_reader_find(&reader, 1, &field) != SM_CODEC_OK ||
        sm_value_field_u8(&field, &state) != SM_CODEC_OK ||
        state != SM_SALES_MIGRATION_COMPLETE)
        status = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    if (status != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return status; }
    status = sm_repo_uow_commit(&uow);
    if (status == SM_REPO_OK) *out_complete = 1;
    return status;
}

static sm_repo_status sm_sales_marker_write(sm_repo_uow *uow,
                                            const sm_sales_batch *batch) {
    sm_value_writer writer;
    uint8_t *value = NULL;
    size_t value_len = 0;
    sm_codec_status codec = sm_value_writer_init(
        &writer, SM_SALES_MIGRATION_VERSION, SM_SALES_MIGRATION_ENTITY);
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_u8(&writer, 1, SM_SALES_MIGRATION_COMPLETE);
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_u64(&writer, 2, batch->fingerprint);
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_i64(&writer, 3, (int64_t)time(NULL));
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_u64(&writer, 4,
                                   (uint64_t)batch->sale_count);
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_u64(&writer, 5,
                                   (uint64_t)batch->item_count);
    if (codec == SM_CODEC_OK)
        codec = sm_value_writer_finish(&writer, &value, &value_len);
    if (codec != SM_CODEC_OK) {
        sm_value_writer_dispose(&writer);
        return codec == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM
                                           : SM_REPO_ERR_INVALID;
    }
    {
        sm_repo_status status = sm_repo_put(uow, sm_sales_marker_key,
                                            sizeof(sm_sales_marker_key),
                                            value, value_len);
        free(value);
        return status;
    }
}

static sm_repo_status sm_sales_import_batch(sm_repository *repo,
                                            const sm_sales_batch *batch) {
    sm_repo_uow *uow = NULL;
    sm_repo_status status = sm_repo_uow_begin(repo, 1, &uow);
    size_t i;
    for (i = 0; status == SM_REPO_OK && i < batch->sale_count; ++i)
        status = sm_sale_import(uow, &batch->sales[i]);
    for (i = 0; status == SM_REPO_OK && i < batch->item_count; ++i)
        status = sm_sale_item_import(uow, &batch->items[i]);
    if (status == SM_REPO_OK) status = sm_sales_marker_write(uow, batch);
    if (status != SM_REPO_OK) {
        (void)sm_repo_uow_rollback(&uow);
        return status;
    }
    return sm_repo_uow_commit(&uow);
}

sm_repo_status sm_sales_import_if_needed(sm_repository *repo,
                                         const char *legacy_data_dir,
                                         sm_sales_import_report *report) {
    sm_sales_batch batch;
    sm_sale *existing = NULL;
    size_t existing_count = 0;
    sm_repo_status status;
    int complete = 0;
    if (!repo || !legacy_data_dir || !*legacy_data_dir)
        return SM_REPO_ERR_INVALID;
    if (report) memset(report, 0, sizeof(*report));
    memset(&batch, 0, sizeof(batch));
    batch.fingerprint = UINT64_C(14695981039346656037);
    status = sm_sales_marker_read(repo, &complete);
    if (status != SM_REPO_OK) return status;
    if (complete) {
        if (report) {
            report->already_completed = 1;
            snprintf(report->message, sizeof(report->message),
                     "sales import already completed");
        }
        return SM_REPO_OK;
    }
    status = sm_sale_list_by_status(repo, 0, &existing, &existing_count);
    sm_sales_array_free(existing);
    if (status == SM_REPO_OK && existing_count == 0) {
        status = sm_sale_list_by_status(repo, 1, &existing, &existing_count);
        sm_sales_array_free(existing);
    }
    if (status == SM_REPO_OK && existing_count == 0) {
        status = sm_sale_list_by_status(repo, 2, &existing, &existing_count);
        sm_sales_array_free(existing);
    }
    if (status != SM_REPO_OK || existing_count != 0) {
        sm_sales_report_error(report, NULL, 0,
                              "sales database is not empty without marker");
        return status == SM_REPO_OK ? SM_REPO_CONFLICT : status;
    }
    if (!sm_sales_read_file(&batch, legacy_data_dir, "sales.txt", 0, report) ||
        !sm_sales_read_file(&batch, legacy_data_dir, "pending_sales.txt", 1,
                            report) ||
        !sm_sales_read_file(&batch, legacy_data_dir, "sale_item.txt", 2,
                            report)) {
        free(batch.sales); free(batch.items);
        return SM_REPO_ERR_CORRUPT;
    }
    status = sm_sales_import_batch(repo, &batch);
    if (report) {
        report->source_files = batch.source_files;
        report->sales = batch.sale_count;
        report->sale_items = batch.item_count;
        report->source_fingerprint = batch.fingerprint;
        report->imported = status == SM_REPO_OK &&
                           (batch.sale_count != 0 || batch.item_count != 0);
        snprintf(report->message, sizeof(report->message),
                 status == SM_REPO_OK ? "sales import completed"
                                      : "sales import transaction failed: %s",
                 status == SM_REPO_OK ? "" : sm_repo_status_name(status));
    }
    free(batch.sales); free(batch.items);
    return status;
}
