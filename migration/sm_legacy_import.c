#include "sm_legacy_import.h"

#include "domain/sm_base_entities.h"
#include "repo/sm_base_repository.h"
#include "storage/sm_codec.h"
#include "storage/sm_namespace.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SM_MIGRATION_CODEC_VERSION 1u
#define SM_MIGRATION_ENTITY_STATE 0x21u
#define SM_MIGRATION_STATE_COMPLETE 1u
#define SM_LEGACY_LINE_CAPACITY 4096u
#define SM_LEGACY_FIELD_CAPACITY 16u

typedef struct sm_legacy_batch {
    sm_employee *employees;
    size_t employee_count;
    size_t employee_capacity;
    sm_product *products;
    size_t product_count;
    size_t product_capacity;
    sm_supplier *suppliers;
    size_t supplier_count;
    size_t supplier_capacity;
    sm_member *members;
    size_t member_count;
    size_t member_capacity;
    sm_system_config_entity config;
    int has_config;
    size_t source_files;
    uint64_t fingerprint;
} sm_legacy_batch;

static void sm_report_reset(sm_legacy_import_report *report) {
    if (report) memset(report, 0, sizeof(*report));
}

static void sm_report_error(sm_legacy_import_report *report,
                            const char *file, size_t line,
                            const char *message) {
    if (!report) return;
    if (file) snprintf(report->error_file, sizeof(report->error_file),
                       "%s", file);
    report->error_line = line;
    snprintf(report->message, sizeof(report->message), "%s",
             message ? message : "legacy import failed");
}

static void sm_batch_dispose(sm_legacy_batch *batch) {
    if (!batch) return;
    free(batch->employees);
    free(batch->products);
    free(batch->suppliers);
    free(batch->members);
    memset(batch, 0, sizeof(*batch));
}

static int sm_array_append(void **array, size_t *count, size_t *capacity,
                           size_t element_size, const void *value) {
    void *next;
    size_t next_capacity;
    if (*count == *capacity) {
        next_capacity = *capacity == 0 ? 16u : *capacity * 2u;
        if (next_capacity < *capacity ||
            next_capacity > SIZE_MAX / element_size)
            return 0;
        next = realloc(*array, next_capacity * element_size);
        if (!next) return 0;
        *array = next;
        *capacity = next_capacity;
    }
    memcpy((uint8_t *)(*array) + *count * element_size,
           value, element_size);
    ++*count;
    return 1;
}

static uint64_t sm_fnv_update(uint64_t hash, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int sm_path_join(char *out, size_t capacity,
                        const char *directory, const char *name) {
    size_t len;
    int written;
    if (!out || !directory || !name) return 0;
    len = strlen(directory);
    written = snprintf(out, capacity, "%s%s%s", directory,
                       len > 0 && directory[len - 1] != '/' &&
                               directory[len - 1] != '\\'
                           ? "/"
                           : "",
                       name);
    return written >= 0 && (size_t)written < capacity;
}

static size_t sm_split_fields(char *line, char **fields, size_t capacity) {
    size_t count = 0;
    char *cursor = line;
    if (capacity == 0) return 0;
    fields[count++] = cursor;
    while (*cursor != '\0') {
        if (*cursor == '|') {
            *cursor = '\0';
            if (count >= capacity) return capacity + 1u;
            fields[count++] = cursor + 1;
        }
        ++cursor;
    }
    return count;
}

static int sm_copy_text(char *out, size_t capacity,
                        const char *value, int required) {
    size_t len;
    if (!out || !value) return 0;
    len = strlen(value);
    if (len >= capacity || (required && len == 0)) return 0;
    memcpy(out, value, len + 1u);
    return 1;
}

static int sm_parse_u64(const char *text, uint64_t minimum,
                        uint64_t maximum, uint64_t *out) {
    char *end;
    unsigned long long value;
    if (!text || text[0] == '\0' || !out || text[0] == '-') return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < minimum || value > maximum)
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static int sm_parse_i64(const char *text, int64_t minimum,
                        int64_t maximum, int64_t *out) {
    char *end;
    long long value;
    if (!text || text[0] == '\0' || !out) return 0;
    errno = 0;
    value = strtoll(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < minimum || value > maximum)
        return 0;
    *out = (int64_t)value;
    return 1;
}

static int sm_parse_u32(const char *text, uint32_t maximum, uint32_t *out) {
    uint64_t value;
    if (!sm_parse_u64(text, 0, maximum, &value)) return 0;
    *out = (uint32_t)value;
    return 1;
}

static int sm_parse_cents(const char *text, int64_t *out) {
    char *end;
    double value;
    double scaled;
    if (!text || text[0] == '\0' || !out) return 0;
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || *end != '\0' || !isfinite(value) || value < 0.0)
        return 0;
    scaled = value * 100.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_parse_status(const char *text, uint8_t *out) {
    uint32_t value;
    if (!sm_parse_u32(text, 1u, &value)) return 0;
    *out = (uint8_t)value;
    return 1;
}

typedef int (*sm_parse_record_fn)(char **, size_t, sm_legacy_batch *);

static int sm_parse_employee_record(char **fields, size_t count,
                                    sm_legacy_batch *batch) {
    sm_employee entity;
    uint64_t id;
    if (count != 8u) return 0;
    memset(&entity, 0, sizeof(entity));
    if (!sm_parse_u64(fields[0], 1, INT_MAX, &id) ||
        !sm_copy_text(entity.name, sizeof(entity.name), fields[1], 1) ||
        !sm_copy_text(entity.role, sizeof(entity.role), fields[2], 1) ||
        !sm_copy_text(entity.password_hash, sizeof(entity.password_hash),
                      fields[3], 0) ||
        !sm_copy_text(entity.salt, sizeof(entity.salt), fields[4], 0) ||
        !sm_parse_status(fields[5], &entity.status) ||
        !sm_parse_i64(fields[6], 0, INT64_MAX, &entity.created_at) ||
        !sm_parse_i64(fields[7], entity.created_at, INT64_MAX,
                      &entity.updated_at))
        return 0;
    entity.id = id;
    return sm_array_append((void **)&batch->employees,
                           &batch->employee_count,
                           &batch->employee_capacity,
                           sizeof(entity), &entity);
}

static int sm_parse_product_record(char **fields, size_t count,
                                   sm_legacy_batch *batch) {
    sm_product entity;
    if (count != 12u) return 0;
    memset(&entity, 0, sizeof(entity));
    if (!sm_copy_text(entity.id, sizeof(entity.id), fields[0], 1) ||
        !sm_copy_text(entity.name, sizeof(entity.name), fields[1], 1) ||
        !sm_copy_text(entity.barcode, sizeof(entity.barcode), fields[2], 0) ||
        !sm_parse_cents(fields[3], &entity.price_cents) ||
        !sm_parse_cents(fields[4], &entity.cost_cents) ||
        !sm_parse_i64(fields[5], 0, INT_MAX, &entity.stock) ||
        !sm_parse_i64(fields[6], 0, INT_MAX, &entity.min_stock) ||
        !sm_copy_text(entity.category_id, sizeof(entity.category_id),
                      fields[7], 0) ||
        !sm_copy_text(entity.supplier_id, sizeof(entity.supplier_id),
                      fields[8], 0) ||
        !sm_parse_status(fields[9], &entity.status) ||
        !sm_parse_i64(fields[10], 0, INT64_MAX, &entity.created_at) ||
        !sm_parse_i64(fields[11], entity.created_at, INT64_MAX,
                      &entity.updated_at))
        return 0;
    return sm_array_append((void **)&batch->products,
                           &batch->product_count,
                           &batch->product_capacity,
                           sizeof(entity), &entity);
}

static int sm_parse_supplier_record(char **fields, size_t count,
                                    sm_legacy_batch *batch) {
    sm_supplier entity;
    uint64_t id;
    if (count != 6u) return 0;
    memset(&entity, 0, sizeof(entity));
    if (!sm_copy_text(entity.name, sizeof(entity.name), fields[0], 1) ||
        !sm_copy_text(entity.contact, sizeof(entity.contact), fields[1], 0) ||
        !sm_copy_text(entity.phone, sizeof(entity.phone), fields[2], 0) ||
        !sm_copy_text(entity.address, sizeof(entity.address), fields[3], 0) ||
        !sm_parse_u64(fields[4], 1, INT_MAX, &id) ||
        !sm_parse_status(fields[5], &entity.status))
        return 0;
    entity.id = id;
    return sm_array_append((void **)&batch->suppliers,
                           &batch->supplier_count,
                           &batch->supplier_capacity,
                           sizeof(entity), &entity);
}

static int sm_parse_member_record(char **fields, size_t count,
                                  sm_legacy_batch *batch) {
    sm_member entity;
    uint64_t id;
    if (count != 9u) return 0;
    memset(&entity, 0, sizeof(entity));
    if (!sm_parse_u64(fields[0], 1, INT_MAX, &id) ||
        !sm_copy_text(entity.phone, sizeof(entity.phone), fields[1], 1) ||
        !sm_copy_text(entity.name, sizeof(entity.name), fields[2], 1) ||
        !sm_parse_u32(fields[3], 3u, &entity.level) ||
        !sm_parse_i64(fields[4], 0, INT_MAX, &entity.points) ||
        !sm_parse_cents(fields[5], &entity.total_consume_cents) ||
        !sm_parse_i64(fields[6], 0, INT64_MAX, &entity.created_at) ||
        !sm_parse_i64(fields[7], entity.created_at, INT64_MAX,
                      &entity.updated_at) ||
        !sm_parse_i64(fields[8], 0, INT64_MAX,
                      &entity.last_consume_at))
        return 0;
    entity.id = id;
    return sm_array_append((void **)&batch->members,
                           &batch->member_count,
                           &batch->member_capacity,
                           sizeof(entity), &entity);
}

static int sm_read_pipe_file(sm_legacy_batch *batch,
                             const char *directory, const char *name,
                             sm_parse_record_fn parse,
                             sm_legacy_import_report *report) {
    char path[512];
    char line[SM_LEGACY_LINE_CAPACITY];
    char *fields[SM_LEGACY_FIELD_CAPACITY];
    FILE *file;
    size_t line_number = 0;
    if (!sm_path_join(path, sizeof(path), directory, name)) {
        sm_report_error(report, name, 0, "legacy path is too long");
        return 0;
    }
    errno = 0;
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 1;
        sm_report_error(report, path, 0, "cannot open legacy file");
        return 0;
    }
    ++batch->source_files;
    batch->fingerprint = sm_fnv_update(batch->fingerprint,
                                       name, strlen(name) + 1u);
    while (fgets(line, sizeof(line), file)) {
        size_t len;
        size_t field_count;
        ++line_number;
        len = strlen(line);
        batch->fingerprint = sm_fnv_update(batch->fingerprint, line, len);
        if (len > 0 && line[len - 1] != '\n' && !feof(file)) {
            sm_report_error(report, path, line_number,
                            "legacy line exceeds 4095 bytes");
            fclose(file);
            return 0;
        }
        while (len > 0 && (line[len - 1] == '\n' ||
                           line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;
        field_count = sm_split_fields(line, fields,
                                      SM_LEGACY_FIELD_CAPACITY);
        if (field_count > SM_LEGACY_FIELD_CAPACITY ||
            !parse(fields, field_count, batch)) {
            sm_report_error(report, path, line_number,
                            "invalid legacy record");
            fclose(file);
            return 0;
        }
    }
    if (ferror(file)) {
        sm_report_error(report, path, line_number,
                        "error while reading legacy file");
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static char *sm_trim_ascii(char *text) {
    char *end;
    while (*text == ' ' || *text == '\t') ++text;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    return text;
}

static int sm_read_config_file(sm_legacy_batch *batch,
                               const char *directory,
                               sm_legacy_import_report *report) {
    char path[512];
    char line[SM_LEGACY_LINE_CAPACITY];
    FILE *file;
    size_t line_number = 0;
    sm_system_config_entity config;
    if (!sm_path_join(path, sizeof(path), directory, "config.txt")) {
        sm_report_error(report, "config.txt", 0, "legacy path is too long");
        return 0;
    }
    errno = 0;
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 1;
        sm_report_error(report, path, 0, "cannot open legacy config");
        return 0;
    }
    memset(&config, 0, sizeof(config));
    snprintf(config.shop_name, sizeof(config.shop_name), "超市管理系统");
    config.auto_backup_interval_minutes = 30;
    config.monthly_fixed_cost_cents = 1000000;
    ++batch->source_files;
    batch->fingerprint = sm_fnv_update(batch->fingerprint,
                                       "config.txt", sizeof("config.txt"));
    while (fgets(line, sizeof(line), file)) {
        char *separator;
        char *key;
        char *value;
        size_t len;
        ++line_number;
        len = strlen(line);
        batch->fingerprint = sm_fnv_update(batch->fingerprint, line, len);
        if (len > 0 && line[len - 1] != '\n' && !feof(file)) {
            sm_report_error(report, path, line_number,
                            "legacy config line exceeds 4095 bytes");
            fclose(file);
            return 0;
        }
        while (len > 0 && (line[len - 1] == '\n' ||
                           line[len - 1] == '\r'))
            line[--len] = '\0';
        key = sm_trim_ascii(line);
        if (*key == '\0' || *key == '#') continue;
        separator = strchr(key, '=');
        if (!separator) {
            sm_report_error(report, path, line_number,
                            "invalid legacy config entry");
            fclose(file);
            return 0;
        }
        *separator = '\0';
        value = sm_trim_ascii(separator + 1);
        key = sm_trim_ascii(key);
        if (strcmp(key, "shop_name") == 0) {
            if (!sm_copy_text(config.shop_name, sizeof(config.shop_name),
                              value, 1)) goto invalid_config;
        } else if (strcmp(key, "shop_address") == 0) {
            if (!sm_copy_text(config.shop_address,
                              sizeof(config.shop_address), value, 0))
                goto invalid_config;
        } else if (strcmp(key, "shop_phone") == 0) {
            if (!sm_copy_text(config.shop_phone, sizeof(config.shop_phone),
                              value, 0)) goto invalid_config;
        } else if (strcmp(key, "tax_rate") == 0) {
            int64_t basis_points;
            if (!sm_parse_cents(value, &basis_points) ||
                basis_points > 10000)
                goto invalid_config;
            config.tax_rate_basis_points = (uint32_t)basis_points;
        } else if (strcmp(key, "auto_backup_interval") == 0) {
            if (!sm_parse_u32(value, UINT32_MAX,
                              &config.auto_backup_interval_minutes) ||
                config.auto_backup_interval_minutes == 0)
                goto invalid_config;
        } else if (strcmp(key, "monthly_fixed_cost") == 0) {
            if (!sm_parse_cents(value, &config.monthly_fixed_cost_cents))
                goto invalid_config;
        }
        continue;

invalid_config:
        sm_report_error(report, path, line_number,
                        "invalid legacy config value");
        fclose(file);
        return 0;
    }
    if (ferror(file)) {
        sm_report_error(report, path, line_number,
                        "error while reading legacy config");
        fclose(file);
        return 0;
    }
    fclose(file);
    batch->config = config;
    batch->has_config = 1;
    return 1;
}

static sm_repo_status sm_read_migration_marker(sm_repository *repo,
                                               int *out_complete) {
    const uint8_t key[] = {SM_NS_MIGRATION_STATE};
    sm_repo_uow *uow = NULL;
    void *value = NULL;
    size_t value_len = 0;
    sm_value_reader reader;
    sm_value_field field;
    uint8_t state = 0;
    sm_repo_status status;
    *out_complete = 0;
    status = sm_repo_uow_begin(repo, 0, &uow);
    if (status != SM_REPO_OK) return status;
    status = sm_repo_get(uow, key, sizeof(key), &value, &value_len);
    if (status == SM_REPO_NOT_FOUND)
        return sm_repo_uow_commit(&uow);
    if (status != SM_REPO_OK) {
        (void)sm_repo_uow_rollback(&uow);
        return status;
    }
    if (sm_value_reader_init(&reader, value, value_len) != SM_CODEC_OK ||
        reader.version != SM_MIGRATION_CODEC_VERSION ||
        reader.entity_type != SM_MIGRATION_ENTITY_STATE ||
        sm_value_reader_find(&reader, 1, &field) != SM_CODEC_OK ||
        sm_value_field_u8(&field, &state) != SM_CODEC_OK ||
        state != SM_MIGRATION_STATE_COMPLETE) {
        sm_repo_value_free(value);
        (void)sm_repo_uow_rollback(&uow);
        return SM_REPO_ERR_CORRUPT;
    }
    sm_repo_value_free(value);
    status = sm_repo_uow_commit(&uow);
    if (status == SM_REPO_OK) *out_complete = 1;
    return status;
}

static sm_repo_status sm_write_migration_marker(sm_repo_uow *uow,
                                                const sm_legacy_batch *batch) {
    const uint8_t key[] = {SM_NS_MIGRATION_STATE};
    sm_value_writer writer;
    uint8_t *value = NULL;
    size_t value_len = 0;
    uint64_t total;
    sm_codec_status codec_status;
    sm_repo_status status;
    total = (uint64_t)batch->employee_count;
    if ((uint64_t)batch->product_count > UINT64_MAX - total)
        return SM_REPO_ERR_FULL;
    total += (uint64_t)batch->product_count;
    if ((uint64_t)batch->supplier_count > UINT64_MAX - total)
        return SM_REPO_ERR_FULL;
    total += (uint64_t)batch->supplier_count;
    if ((uint64_t)batch->member_count > UINT64_MAX - total)
        return SM_REPO_ERR_FULL;
    total += (uint64_t)batch->member_count;
    codec_status = sm_value_writer_init(&writer,
                                        SM_MIGRATION_CODEC_VERSION,
                                        SM_MIGRATION_ENTITY_STATE);
    if (codec_status == SM_CODEC_OK)
        codec_status = sm_value_write_u8(
            &writer, 1, SM_MIGRATION_STATE_COMPLETE);
    if (codec_status == SM_CODEC_OK)
        codec_status = sm_value_write_u64(&writer, 2, batch->fingerprint);
    if (codec_status == SM_CODEC_OK)
        codec_status = sm_value_write_i64(&writer, 3, (int64_t)time(NULL));
    if (codec_status == SM_CODEC_OK)
        codec_status = sm_value_write_u64(&writer, 4, total);
    if (codec_status == SM_CODEC_OK)
        codec_status = sm_value_writer_finish(&writer, &value, &value_len);
    if (codec_status != SM_CODEC_OK) {
        sm_value_writer_dispose(&writer);
        return codec_status == SM_CODEC_ERR_NOMEM ? SM_REPO_ERR_NOMEM
                                                   : SM_REPO_ERR_INVALID;
    }
    status = sm_repo_put(uow, key, sizeof(key), value, value_len);
    free(value);
    return status;
}

static sm_repo_status sm_database_base_empty(sm_repository *repo,
                                             int *out_empty) {
    sm_employee *employees = NULL;
    sm_product *products = NULL;
    sm_supplier *suppliers = NULL;
    sm_member *members = NULL;
    sm_repo_uow *uow = NULL;
    sm_system_config_entity config;
    size_t count = 0;
    sm_repo_status status;
    *out_empty = 0;
    status = sm_employee_list_all(repo, &employees, &count);
    sm_entity_array_free(employees);
    if (status != SM_REPO_OK) return status;
    if (count != 0) return SM_REPO_OK;
    status = sm_product_list_all(repo, &products, &count);
    sm_entity_array_free(products);
    if (status != SM_REPO_OK) return status;
    if (count != 0) return SM_REPO_OK;
    status = sm_supplier_list_all(repo, &suppliers, &count);
    sm_entity_array_free(suppliers);
    if (status != SM_REPO_OK) return status;
    if (count != 0) return SM_REPO_OK;
    status = sm_member_list_all(repo, &members, &count);
    sm_entity_array_free(members);
    if (status != SM_REPO_OK) return status;
    if (count != 0) return SM_REPO_OK;
    status = sm_repo_uow_begin(repo, 0, &uow);
    if (status != SM_REPO_OK) return status;
    status = sm_system_config_get(uow, &config);
    if (status == SM_REPO_NOT_FOUND) {
        status = sm_repo_uow_commit(&uow);
        if (status == SM_REPO_OK) *out_empty = 1;
        return status;
    }
    (void)sm_repo_uow_rollback(&uow);
    return status == SM_REPO_OK ? SM_REPO_OK : status;
}

static int sm_parse_all_sources(sm_legacy_batch *batch,
                                const char *directory,
                                sm_legacy_import_report *report) {
    return sm_read_pipe_file(batch, directory, "employee.txt",
                             sm_parse_employee_record, report) &&
           sm_read_pipe_file(batch, directory, "product.txt",
                             sm_parse_product_record, report) &&
           sm_read_pipe_file(batch, directory, "supplier.txt",
                             sm_parse_supplier_record, report) &&
           sm_read_pipe_file(batch, directory, "member.txt",
                             sm_parse_member_record, report) &&
           sm_read_config_file(batch, directory, report);
}

static sm_repo_status sm_import_batch(sm_repository *repo,
                                      const sm_legacy_batch *batch) {
    sm_repo_uow *uow = NULL;
    sm_repo_status status;
    size_t i;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status != SM_REPO_OK) return status;
    for (i = 0; i < batch->employee_count && status == SM_REPO_OK; ++i)
        status = sm_employee_import(uow, &batch->employees[i]);
    for (i = 0; i < batch->product_count && status == SM_REPO_OK; ++i)
        status = sm_product_import(uow, &batch->products[i]);
    for (i = 0; i < batch->supplier_count && status == SM_REPO_OK; ++i)
        status = sm_supplier_import(uow, &batch->suppliers[i]);
    for (i = 0; i < batch->member_count && status == SM_REPO_OK; ++i)
        status = sm_member_import(uow, &batch->members[i]);
    if (status == SM_REPO_OK && batch->has_config)
        status = sm_system_config_put(uow, &batch->config);
    if (status == SM_REPO_OK)
        status = sm_write_migration_marker(uow, batch);
    if (status != SM_REPO_OK) {
        (void)sm_repo_uow_rollback(&uow);
        return status;
    }
    return sm_repo_uow_commit(&uow);
}

sm_repo_status sm_legacy_import_if_needed(
    sm_repository *repo, const char *legacy_data_dir,
    sm_legacy_import_report *report) {
    sm_legacy_batch batch;
    sm_repo_status status;
    int complete = 0;
    int empty = 0;
    if (!repo || !legacy_data_dir || legacy_data_dir[0] == '\0')
        return SM_REPO_ERR_INVALID;
    sm_report_reset(report);
    memset(&batch, 0, sizeof(batch));
    batch.fingerprint = UINT64_C(14695981039346656037);
    status = sm_read_migration_marker(repo, &complete);
    if (status != SM_REPO_OK) {
        sm_report_error(report, NULL, 0, "cannot read migration state");
        return status;
    }
    if (complete) {
        if (report) {
            report->already_completed = 1;
            snprintf(report->message, sizeof(report->message),
                     "legacy import already completed");
        }
        return SM_REPO_OK;
    }
    status = sm_database_base_empty(repo, &empty);
    if (status != SM_REPO_OK) {
        sm_report_error(report, NULL, 0,
                        "cannot inspect database before import");
        return status;
    }
    if (!empty) {
        sm_report_error(report, NULL, 0,
                        "database contains base records without migration marker");
        return SM_REPO_CONFLICT;
    }
    if (!sm_parse_all_sources(&batch, legacy_data_dir, report)) {
        sm_batch_dispose(&batch);
        return SM_REPO_ERR_CORRUPT;
    }
    status = sm_import_batch(repo, &batch);
    if (report) {
        report->source_files = batch.source_files;
        report->employees = batch.employee_count;
        report->products = batch.product_count;
        report->suppliers = batch.supplier_count;
        report->members = batch.member_count;
        report->has_system_config = batch.has_config;
        report->source_fingerprint = batch.fingerprint;
        if (status == SM_REPO_OK) {
            report->imported = batch.employee_count != 0 ||
                               batch.product_count != 0 ||
                               batch.supplier_count != 0 ||
                               batch.member_count != 0 || batch.has_config;
            snprintf(report->message, sizeof(report->message),
                     "legacy import completed");
        } else {
            snprintf(report->message, sizeof(report->message),
                     "legacy import transaction failed: %s",
                     sm_repo_status_name(status));
        }
    }
    sm_batch_dispose(&batch);
    return status;
}
