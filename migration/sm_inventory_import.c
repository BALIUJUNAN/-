#include "sm_inventory_import.h"

#include "domain/sm_inventory_entities.h"
#include "repo/sm_inventory_repository.h"
#include "storage/sm_codec.h"
#include "storage/sm_namespace.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SM_INVENTORY_MIGRATION_VERSION 1u
#define SM_INVENTORY_MIGRATION_ENTITY 0x23u
#define SM_INVENTORY_MIGRATION_COMPLETE 1u
#define SM_INVENTORY_LINE_CAPACITY 4096u

typedef struct sm_inventory_batch {
    sm_stock_log *logs;
    size_t count;
    size_t capacity;
    size_t source_files;
    uint64_t fingerprint;
} sm_inventory_batch;

static const uint8_t sm_inventory_marker_key[] = {
    SM_NS_MIGRATION_STATE, 0x07u
};

static void sm_inventory_error(sm_inventory_import_report *report,
                               const char *file, size_t line,
                               const char *message) {
    if (!report) return;
    if (file) snprintf(report->error_file, sizeof(report->error_file),
                       "%s", file);
    report->error_line = line;
    snprintf(report->message, sizeof(report->message), "%s", message);
}

static uint64_t sm_inventory_fnv(uint64_t hash, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int sm_inventory_path(char *out, size_t capacity,
                             const char *directory, const char *name) {
    size_t len = strlen(directory);
    int n = snprintf(out, capacity, "%s%s%s", directory,
                     len && directory[len - 1] != '/' &&
                             directory[len - 1] != '\\' ? "/" : "",
                     name);
    return n >= 0 && (size_t)n < capacity;
}

static size_t sm_inventory_split(char *line, char **fields,
                                 size_t capacity) {
    char *cursor = line;
    size_t count = 0;
    if (!capacity) return 0;
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

static int sm_inventory_u64(const char *text, uint64_t minimum,
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

static int sm_inventory_i64(const char *text, int64_t minimum,
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

static int sm_inventory_scaled(const char *text, int allow_zero,
                               int64_t *out) {
    char *end;
    double value, scaled;
    if (!text || !*text) return 0;
    errno = 0;
    value = strtod(text, &end);
    if (errno || *end || !isfinite(value) || value < 0.0 ||
        (!allow_zero && value == 0.0))
        return 0;
    scaled = value * 1000.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_inventory_text(char *out, size_t capacity,
                             const char *value, int required) {
    size_t len = value ? strlen(value) : 0;
    if (len >= capacity || (required && !len)) return 0;
    memcpy(out, value, len + 1u);
    return 1;
}

static int sm_inventory_append(sm_inventory_batch *batch,
                               const sm_stock_log *log) {
    sm_stock_log *next;
    size_t next_capacity;
    if (batch->count == batch->capacity) {
        next_capacity = batch->capacity ? batch->capacity * 2u : 16u;
        if (next_capacity < batch->capacity ||
            next_capacity > SIZE_MAX / sizeof(*next))
            return 0;
        next = (sm_stock_log *)realloc(
            batch->logs, next_capacity * sizeof(*next));
        if (!next) return 0;
        batch->logs = next;
        batch->capacity = next_capacity;
    }
    batch->logs[batch->count++] = *log;
    return 1;
}

static int sm_inventory_parse(char **fields, size_t count,
                              sm_inventory_batch *batch) {
    sm_stock_log log;
    if (count != 9u) return 0;
    memset(&log, 0, sizeof(log));
    if (!sm_inventory_u64(fields[0], 1, INT_MAX, &log.id) ||
        !sm_inventory_text(log.product_id, sizeof(log.product_id),
                           fields[1], 1) ||
        !sm_inventory_text(log.type, sizeof(log.type), fields[2], 1) ||
        !sm_inventory_scaled(fields[3], 0, &log.quantity_milli) ||
        !sm_inventory_scaled(fields[4], 1, &log.before_stock_milli) ||
        !sm_inventory_scaled(fields[5], 1, &log.after_stock_milli) ||
        !sm_inventory_u64(fields[6], 0, INT_MAX, &log.operator_id) ||
        !sm_inventory_text(log.remark, sizeof(log.remark), fields[7], 0) ||
        !sm_inventory_i64(fields[8], 0, INT64_MAX, &log.created_at))
        return 0;
    return sm_inventory_append(batch, &log);
}

static int sm_inventory_read(sm_inventory_batch *batch,
                             const char *directory,
                             sm_inventory_import_report *report) {
    char path[512], line[SM_INVENTORY_LINE_CAPACITY];
    char *fields[12];
    FILE *file;
    size_t line_number = 0;
    if (!sm_inventory_path(path, sizeof(path), directory, "stock_log.txt"))
        return 0;
    errno = 0;
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 1;
        sm_inventory_error(report, path, 0, "cannot open stock log source");
        return 0;
    }
    ++batch->source_files;
    batch->fingerprint = sm_inventory_fnv(
        batch->fingerprint, "stock_log.txt", sizeof("stock_log.txt"));
    while (fgets(line, sizeof(line), file)) {
        size_t len, count;
        ++line_number;
        len = strlen(line);
        batch->fingerprint = sm_inventory_fnv(batch->fingerprint, line, len);
        if (len && line[len - 1] != '\n' && !feof(file)) {
            sm_inventory_error(report, path, line_number,
                               "stock log line is too long");
            fclose(file);
            return 0;
        }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (!len) continue;
        count = sm_inventory_split(line, fields,
                                   sizeof(fields) / sizeof(fields[0]));
        if (count > sizeof(fields) / sizeof(fields[0]) ||
            !sm_inventory_parse(fields, count, batch)) {
            sm_inventory_error(report, path, line_number,
                               "invalid stock log record");
            fclose(file);
            return 0;
        }
    }
    if (ferror(file)) {
        sm_inventory_error(report, path, line_number,
                           "cannot read stock log source");
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static sm_repo_status sm_inventory_marker_read(sm_repository *repo,
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
        status = sm_repo_get(uow, sm_inventory_marker_key,
                             sizeof(sm_inventory_marker_key),
                             &value, &value_len);
    if (status == SM_REPO_NOT_FOUND) return sm_repo_uow_commit(&uow);
    if (status != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return status; }
    if (sm_value_reader_init(&reader, value, value_len) != SM_CODEC_OK ||
        reader.version != SM_INVENTORY_MIGRATION_VERSION ||
        reader.entity_type != SM_INVENTORY_MIGRATION_ENTITY ||
        sm_value_reader_find(&reader, 1, &field) != SM_CODEC_OK ||
        sm_value_field_u8(&field, &state) != SM_CODEC_OK ||
        state != SM_INVENTORY_MIGRATION_COMPLETE)
        status = SM_REPO_ERR_CORRUPT;
    sm_repo_value_free(value);
    if (status != SM_REPO_OK) { (void)sm_repo_uow_rollback(&uow); return status; }
    status = sm_repo_uow_commit(&uow);
    if (status == SM_REPO_OK) *out_complete = 1;
    return status;
}

static sm_repo_status sm_inventory_marker_write(
    sm_repo_uow *uow, const sm_inventory_batch *batch) {
    sm_value_writer writer;
    uint8_t *value = NULL;
    size_t value_len = 0;
    sm_codec_status codec = sm_value_writer_init(
        &writer, SM_INVENTORY_MIGRATION_VERSION,
        SM_INVENTORY_MIGRATION_ENTITY);
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_u8(&writer, 1,
                                  SM_INVENTORY_MIGRATION_COMPLETE);
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_u64(&writer, 2, batch->fingerprint);
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_i64(&writer, 3, (int64_t)time(NULL));
    if (codec == SM_CODEC_OK)
        codec = sm_value_write_u64(&writer, 4, (uint64_t)batch->count);
    if (codec == SM_CODEC_OK)
        codec = sm_value_writer_finish(&writer, &value, &value_len);
    if (codec != SM_CODEC_OK) {
        sm_value_writer_dispose(&writer);
        return codec == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM
                                           : SM_REPO_ERR_INVALID;
    }
    {
        sm_repo_status status = sm_repo_put(
            uow, sm_inventory_marker_key, sizeof(sm_inventory_marker_key),
            value, value_len);
        free(value);
        return status;
    }
}

sm_repo_status sm_inventory_import_if_needed(
    sm_repository *repo, const char *legacy_data_dir,
    sm_inventory_import_report *report) {
    sm_inventory_batch batch;
    sm_stock_log *existing = NULL;
    size_t existing_count = 0, i;
    sm_repo_uow *uow = NULL;
    sm_repo_status status;
    int complete = 0;
    if (!repo || !legacy_data_dir || !*legacy_data_dir)
        return SM_REPO_ERR_INVALID;
    if (report) memset(report, 0, sizeof(*report));
    memset(&batch, 0, sizeof(batch));
    batch.fingerprint = UINT64_C(14695981039346656037);
    status = sm_inventory_marker_read(repo, &complete);
    if (status != SM_REPO_OK) return status;
    if (complete) {
        if (report) {
            report->already_completed = 1;
            snprintf(report->message, sizeof(report->message),
                     "inventory import already completed");
        }
        return SM_REPO_OK;
    }
    status = sm_stock_log_list(repo, NULL, NULL, 0, INT64_MAX,
                               &existing, &existing_count);
    sm_inventory_array_free(existing);
    if (status != SM_REPO_OK || existing_count != 0) {
        sm_inventory_error(report, NULL, 0,
                           "stock log database is not empty without marker");
        return status == SM_REPO_OK ? SM_REPO_CONFLICT : status;
    }
    if (!sm_inventory_read(&batch, legacy_data_dir, report)) {
        free(batch.logs);
        return SM_REPO_ERR_CORRUPT;
    }
    status = sm_repo_uow_begin(repo, 1, &uow);
    for (i = 0; status == SM_REPO_OK && i < batch.count; ++i)
        status = sm_stock_log_import(uow, &batch.logs[i]);
    if (status == SM_REPO_OK)
        status = sm_inventory_marker_write(uow, &batch);
    if (status == SM_REPO_OK) status = sm_repo_uow_commit(&uow);
    else (void)sm_repo_uow_rollback(&uow);
    if (report) {
        report->source_files = batch.source_files;
        report->stock_logs = batch.count;
        report->source_fingerprint = batch.fingerprint;
        report->imported = status == SM_REPO_OK && batch.count != 0;
        snprintf(report->message, sizeof(report->message),
                 status == SM_REPO_OK ? "inventory import completed"
                                      : "inventory import failed");
    }
    free(batch.logs);
    return status;
}
