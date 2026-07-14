#include "sm_base_repository.h"

#include "repo/sm_base_codec.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef sm_codec_status (*sm_encode_fn)(const void *, uint8_t **, size_t *);
typedef sm_codec_status (*sm_decode_fn)(const void *, size_t, void *);

static int sm_text_valid(const char *value, size_t capacity, int required) {
    const char *end;
    if (!value) return 0;
    end = (const char *)memchr(value, '\0', capacity);
    return end != NULL && (!required || end != value);
}

static sm_repo_status sm_encode_result(sm_codec_status status) {
    if (status == SM_CODEC_OK) return SM_REPO_OK;
    if (status == SM_CODEC_ERR_NOMEM) return SM_REPO_ERR_NOMEM;
    if (status == SM_CODEC_ERR_TOO_LARGE) return SM_REPO_ERR_FULL;
    return SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_decode_result(sm_codec_status status) {
    return status == SM_CODEC_OK ? SM_REPO_OK : SM_REPO_ERR_CORRUPT;
}

static sm_repo_status sm_key_result(sm_key_status status) {
    return status == SM_KEY_OK ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static sm_repo_status sm_make_u64_key(uint8_t namespace_kind, uint64_t id,
                                      sm_key_builder *key) {
    sm_key_begin(key, namespace_kind);
    return sm_key_result(sm_key_add_u64(key, id));
}

static sm_repo_status sm_make_string_key(uint8_t namespace_kind,
                                         const char *value,
                                         sm_key_builder *key) {
    sm_key_begin(key, namespace_kind);
    return sm_key_result(sm_key_add_string(key, value));
}

static sm_repo_status sm_make_string_u64_key(uint8_t namespace_kind,
                                             const char *value, uint64_t id,
                                             sm_key_builder *key) {
    sm_key_begin(key, namespace_kind);
    if (sm_key_add_string(key, value) != SM_KEY_OK ||
        sm_key_add_u64(key, id) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return SM_REPO_OK;
}

static sm_repo_status sm_make_u8_u64_key(uint8_t namespace_kind,
                                         uint8_t value, uint64_t id,
                                         sm_key_builder *key) {
    sm_key_begin(key, namespace_kind);
    if (sm_key_add_u8(key, value) != SM_KEY_OK ||
        sm_key_add_u64(key, id) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return SM_REPO_OK;
}

static sm_repo_status sm_make_u32_u64_key(uint8_t namespace_kind,
                                          uint32_t value, uint64_t id,
                                          sm_key_builder *key) {
    sm_key_begin(key, namespace_kind);
    if (sm_key_add_u32(key, value) != SM_KEY_OK ||
        sm_key_add_u64(key, id) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return SM_REPO_OK;
}

static sm_repo_status sm_make_two_string_key(uint8_t namespace_kind,
                                             const char *first,
                                             const char *second,
                                             sm_key_builder *key) {
    sm_key_begin(key, namespace_kind);
    if (sm_key_add_string(key, first) != SM_KEY_OK ||
        sm_key_add_string(key, second) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return SM_REPO_OK;
}

static sm_repo_status sm_make_u8_string_key(uint8_t namespace_kind,
                                            uint8_t value,
                                            const char *text,
                                            sm_key_builder *key) {
    sm_key_begin(key, namespace_kind);
    if (sm_key_add_u8(key, value) != SM_KEY_OK ||
        sm_key_add_string(key, text) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return SM_REPO_OK;
}

static sm_repo_status sm_expect_missing(sm_repo_uow *uow,
                                        const sm_key_builder *key) {
    void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status = sm_repo_get(uow, sm_key_data(key),
                                        sm_key_size(key),
                                        &value, &value_len);
    if (status == SM_REPO_NOT_FOUND) return SM_REPO_OK;
    if (status == SM_REPO_OK) {
        sm_repo_value_free(value);
        return SM_REPO_CONFLICT;
    }
    return status;
}

static sm_repo_status sm_put_encoded(sm_repo_uow *uow,
                                     const sm_key_builder *key,
                                     const void *entity,
                                     sm_encode_fn encode) {
    uint8_t *value = NULL;
    size_t value_len = 0;
    sm_codec_status codec_status = encode(entity, &value, &value_len);
    sm_repo_status status = sm_encode_result(codec_status);
    if (status == SM_REPO_OK)
        status = sm_repo_put(uow, sm_key_data(key), sm_key_size(key),
                             value, value_len);
    free(value);
    return status;
}

static sm_repo_status sm_get_decoded(sm_repo_uow *uow,
                                     const sm_key_builder *key,
                                     void *out, sm_decode_fn decode) {
    void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status = sm_repo_get(uow, sm_key_data(key),
                                        sm_key_size(key),
                                        &value, &value_len);
    if (status != SM_REPO_OK) return status;
    status = sm_decode_result(decode(value, value_len, out));
    sm_repo_value_free(value);
    return status;
}

static sm_repo_status sm_remove_index(sm_repo_uow *uow,
                                      const sm_key_builder *key) {
    sm_repo_status status = sm_repo_index_remove(
        uow, sm_key_data(key), sm_key_size(key));
    return status == SM_REPO_NOT_FOUND ? SM_REPO_ERR_CORRUPT : status;
}

static int sm_normalize_name(const char *name, char *out, size_t capacity) {
    const unsigned char *start;
    const unsigned char *end;
    size_t len;
    size_t i;
    if (!name || !out || capacity == 0) return 0;
    start = (const unsigned char *)name;
    while (*start != '\0' && *start <= 0x7fu && isspace(*start)) ++start;
    end = start + strlen((const char *)start);
    while (end > start && end[-1] <= 0x7fu && isspace(end[-1])) --end;
    len = (size_t)(end - start);
    if (len == 0 || len >= capacity) return 0;
    for (i = 0; i < len; ++i)
        out[i] = start[i] <= 0x7fu
                     ? (char)tolower((unsigned char)start[i])
                     : (char)start[i];
    out[len] = '\0';
    return 1;
}

static int sm_employee_valid(const sm_employee *entity, int require_id) {
    return entity && (!require_id || entity->id != 0) &&
           sm_text_valid(entity->name, sizeof(entity->name), 1) &&
           sm_text_valid(entity->role, sizeof(entity->role), 1) &&
           sm_text_valid(entity->password_hash,
                         sizeof(entity->password_hash), 0) &&
           sm_text_valid(entity->salt, sizeof(entity->salt), 0) &&
           entity->status <= 1u && entity->created_at >= 0 &&
           entity->updated_at >= entity->created_at;
}

static sm_codec_status sm_employee_encode_any(const void *entity,
                                              uint8_t **out,
                                              size_t *out_len) {
    return sm_employee_encode((const sm_employee *)entity, out, out_len);
}

static sm_codec_status sm_employee_decode_any(const void *data, size_t len,
                                              void *out) {
    return sm_employee_decode(data, len, (sm_employee *)out);
}

sm_repo_status sm_employee_get(sm_repo_uow *uow, uint64_t id,
                               sm_employee *out) {
    sm_key_builder primary;
    sm_repo_status status;
    if (!uow || id == 0 || !out) return SM_REPO_ERR_INVALID;
    status = sm_make_u64_key(SM_NS_EMPLOYEE_BY_ID, id, &primary);
    if (status != SM_REPO_OK) return status;
    status = sm_get_decoded(uow, &primary, out, sm_employee_decode_any);
    if (status == SM_REPO_OK && (out->id != id ||
                                 !sm_employee_valid(out, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_employee_create(sm_repo_uow *uow, sm_employee *entity) {
    sm_employee candidate;
    sm_key_builder primary;
    sm_key_builder role_index;
    sm_key_builder status_index;
    uint64_t id;
    sm_repo_status status;
    if (!uow || !entity || entity->id != 0 ||
        !sm_employee_valid(entity, 0))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_counter_next(uow, SM_COUNTER_EMPLOYEE, &id);
    if (status != SM_REPO_OK) return status;
    candidate = *entity;
    candidate.id = id;
    if (!sm_employee_valid(&candidate, 1)) return SM_REPO_ERR_INVALID;
    if ((status = sm_make_u64_key(SM_NS_EMPLOYEE_BY_ID, id, &primary)) != SM_REPO_OK ||
        (status = sm_make_string_u64_key(SM_NS_EMPLOYEE_BY_ROLE,
                                         candidate.role, id,
                                         &role_index)) != SM_REPO_OK ||
        (status = sm_make_u8_u64_key(SM_NS_EMPLOYEE_BY_STATUS,
                                     candidate.status, id,
                                     &status_index)) != SM_REPO_OK)
        return status;
    status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, &candidate,
                                sm_employee_encode_any);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&role_index),
                                   sm_key_size(&role_index));
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&status_index),
                                   sm_key_size(&status_index));
    if (status == SM_REPO_OK) *entity = candidate;
    return status;
}

sm_repo_status sm_employee_import(sm_repo_uow *uow,
                                  const sm_employee *entity) {
    sm_key_builder primary;
    sm_key_builder role_index;
    sm_key_builder status_index;
    sm_repo_status status;
    if (!uow || !sm_employee_valid(entity, 1) || entity->id == UINT64_MAX)
        return SM_REPO_ERR_INVALID;
    if ((status = sm_make_u64_key(SM_NS_EMPLOYEE_BY_ID, entity->id,
                                  &primary)) != SM_REPO_OK ||
        (status = sm_make_string_u64_key(SM_NS_EMPLOYEE_BY_ROLE,
                                         entity->role, entity->id,
                                         &role_index)) != SM_REPO_OK ||
        (status = sm_make_u8_u64_key(SM_NS_EMPLOYEE_BY_STATUS,
                                     entity->status, entity->id,
                                     &status_index)) != SM_REPO_OK)
        return status;
    status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_repo_counter_ensure_at_least(
            uow, SM_COUNTER_EMPLOYEE, entity->id + 1u);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, entity,
                                sm_employee_encode_any);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&role_index),
                                   sm_key_size(&role_index));
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&status_index),
                                   sm_key_size(&status_index));
    return status;
}

sm_repo_status sm_employee_update(sm_repo_uow *uow,
                                  const sm_employee *entity) {
    sm_employee old;
    sm_key_builder primary;
    sm_key_builder old_index;
    sm_key_builder new_index;
    sm_repo_status status;
    if (!uow || !sm_employee_valid(entity, 1)) return SM_REPO_ERR_INVALID;
    status = sm_employee_get(uow, entity->id, &old);
    if (status != SM_REPO_OK) return status;
    status = sm_make_u64_key(SM_NS_EMPLOYEE_BY_ID, entity->id, &primary);
    if (status != SM_REPO_OK) return status;
    if (strcmp(old.role, entity->role) != 0) {
        if ((status = sm_make_string_u64_key(SM_NS_EMPLOYEE_BY_ROLE,
                                             old.role, old.id,
                                             &old_index)) != SM_REPO_OK ||
            (status = sm_make_string_u64_key(SM_NS_EMPLOYEE_BY_ROLE,
                                             entity->role, entity->id,
                                             &new_index)) != SM_REPO_OK)
            return status;
        status = sm_remove_index(uow, &old_index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&new_index),
                                       sm_key_size(&new_index));
        if (status != SM_REPO_OK) return status;
    }
    if (old.status != entity->status) {
        if ((status = sm_make_u8_u64_key(SM_NS_EMPLOYEE_BY_STATUS,
                                         old.status, old.id,
                                         &old_index)) != SM_REPO_OK ||
            (status = sm_make_u8_u64_key(SM_NS_EMPLOYEE_BY_STATUS,
                                         entity->status, entity->id,
                                         &new_index)) != SM_REPO_OK)
            return status;
        status = sm_remove_index(uow, &old_index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&new_index),
                                       sm_key_size(&new_index));
        if (status != SM_REPO_OK) return status;
    }
    return sm_put_encoded(uow, &primary, entity, sm_employee_encode_any);
}

sm_repo_status sm_employee_delete(sm_repo_uow *uow, uint64_t id) {
    sm_employee old;
    sm_key_builder primary;
    sm_key_builder role_index;
    sm_key_builder status_index;
    sm_repo_status status = sm_employee_get(uow, id, &old);
    if (status != SM_REPO_OK) return status;
    if ((status = sm_make_u64_key(SM_NS_EMPLOYEE_BY_ID, id, &primary)) != SM_REPO_OK ||
        (status = sm_make_string_u64_key(SM_NS_EMPLOYEE_BY_ROLE,
                                         old.role, id,
                                         &role_index)) != SM_REPO_OK ||
        (status = sm_make_u8_u64_key(SM_NS_EMPLOYEE_BY_STATUS,
                                     old.status, id,
                                     &status_index)) != SM_REPO_OK)
        return status;
    status = sm_remove_index(uow, &role_index);
    if (status == SM_REPO_OK) status = sm_remove_index(uow, &status_index);
    if (status == SM_REPO_OK)
        status = sm_repo_delete(uow, sm_key_data(&primary),
                                sm_key_size(&primary));
    return status;
}

static int sm_product_valid(const sm_product *entity, int require_id) {
    return entity &&
           sm_text_valid(entity->id, sizeof(entity->id), require_id) &&
           sm_text_valid(entity->name, sizeof(entity->name), 1) &&
           sm_text_valid(entity->barcode, sizeof(entity->barcode), 0) &&
           sm_text_valid(entity->category_id,
                         sizeof(entity->category_id), 0) &&
           sm_text_valid(entity->supplier_id,
                         sizeof(entity->supplier_id), 0) &&
           entity->price_cents >= 0 && entity->cost_cents >= 0 &&
           entity->stock >= 0 && entity->min_stock >= 0 &&
           entity->status <= 1u && entity->created_at >= 0 &&
           entity->updated_at >= entity->created_at;
}

static uint8_t sm_product_low_stock(const sm_product *entity) {
    return entity->stock <= entity->min_stock ? 1u : 0u;
}

static sm_codec_status sm_product_encode_any(const void *entity,
                                             uint8_t **out,
                                             size_t *out_len) {
    return sm_product_encode((const sm_product *)entity, out, out_len);
}

static sm_codec_status sm_product_decode_any(const void *data, size_t len,
                                             void *out) {
    return sm_product_decode(data, len, (sm_product *)out);
}

sm_repo_status sm_product_get(sm_repo_uow *uow, const char *id,
                              sm_product *out) {
    sm_key_builder primary;
    sm_repo_status status;
    if (!uow || !id || id[0] == '\0' || !out) return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_ID, id, &primary);
    if (status != SM_REPO_OK) return status;
    status = sm_get_decoded(uow, &primary, out, sm_product_decode_any);
    if (status == SM_REPO_OK &&
        (strcmp(out->id, id) != 0 || !sm_product_valid(out, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_product_primary_from_index_value(
    sm_repo_uow *uow, const void *value, size_t value_len, sm_product *out) {
    sm_key_reader reader;
    const char *id;
    size_t id_len;
    char id_buffer[SM_PRODUCT_ID_CAPACITY];
    if (sm_key_reader_init(&reader, value, value_len,
                           SM_NS_PRODUCT_BY_ID) != SM_KEY_OK ||
        sm_key_read_string(&reader, &id, &id_len) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK ||
        id_len >= sizeof(id_buffer))
        return SM_REPO_ERR_CORRUPT;
    memcpy(id_buffer, id, id_len);
    id_buffer[id_len] = '\0';
    return sm_product_get(uow, id_buffer, out);
}

sm_repo_status sm_product_get_by_barcode(sm_repo_uow *uow,
                                         const char *barcode,
                                         sm_product *out) {
    sm_key_builder index;
    void *owner = NULL;
    size_t owner_len = 0;
    sm_repo_status status;
    if (!uow || !barcode || barcode[0] == '\0' || !out)
        return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_BARCODE, barcode, &index);
    if (status != SM_REPO_OK) return status;
    status = sm_repo_get(uow, sm_key_data(&index), sm_key_size(&index),
                         &owner, &owner_len);
    if (status != SM_REPO_OK) return status;
    status = sm_product_primary_from_index_value(uow, owner, owner_len, out);
    sm_repo_value_free(owner);
    if (status == SM_REPO_OK && strcmp(out->barcode, barcode) != 0)
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_product_add_indexes(sm_repo_uow *uow,
                                             const sm_product *entity,
                                             const sm_key_builder *primary) {
    sm_key_builder index;
    sm_repo_status status = SM_REPO_OK;
    if (entity->barcode[0] != '\0') {
        status = sm_make_string_key(SM_NS_PRODUCT_BY_BARCODE,
                                    entity->barcode, &index);
        if (status == SM_REPO_OK)
            status = sm_repo_unique_claim(
                uow, sm_key_data(&index), sm_key_size(&index),
                sm_key_data(primary), sm_key_size(primary));
    }
    if (status == SM_REPO_OK && entity->category_id[0] != '\0') {
        status = sm_make_two_string_key(SM_NS_PRODUCT_BY_CATEGORY,
                                        entity->category_id, entity->id,
                                        &index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&index),
                                       sm_key_size(&index));
    }
    if (status == SM_REPO_OK && entity->supplier_id[0] != '\0') {
        status = sm_make_two_string_key(SM_NS_PRODUCT_BY_SUPPLIER,
                                        entity->supplier_id, entity->id,
                                        &index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&index),
                                       sm_key_size(&index));
    }
    if (status == SM_REPO_OK) {
        status = sm_make_u8_string_key(SM_NS_PRODUCT_LOW_STOCK,
                                       sm_product_low_stock(entity),
                                       entity->id, &index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&index),
                                       sm_key_size(&index));
    }
    return status;
}

static sm_repo_status sm_product_remove_indexes(sm_repo_uow *uow,
                                                const sm_product *entity,
                                                const sm_key_builder *primary) {
    sm_key_builder index;
    sm_repo_status status = SM_REPO_OK;
    if (entity->barcode[0] != '\0') {
        status = sm_make_string_key(SM_NS_PRODUCT_BY_BARCODE,
                                    entity->barcode, &index);
        if (status == SM_REPO_OK)
            status = sm_repo_unique_release(
                uow, sm_key_data(&index), sm_key_size(&index),
                sm_key_data(primary), sm_key_size(primary));
    }
    if (status == SM_REPO_OK && entity->category_id[0] != '\0') {
        status = sm_make_two_string_key(SM_NS_PRODUCT_BY_CATEGORY,
                                        entity->category_id, entity->id,
                                        &index);
        if (status == SM_REPO_OK) status = sm_remove_index(uow, &index);
    }
    if (status == SM_REPO_OK && entity->supplier_id[0] != '\0') {
        status = sm_make_two_string_key(SM_NS_PRODUCT_BY_SUPPLIER,
                                        entity->supplier_id, entity->id,
                                        &index);
        if (status == SM_REPO_OK) status = sm_remove_index(uow, &index);
    }
    if (status == SM_REPO_OK) {
        status = sm_make_u8_string_key(SM_NS_PRODUCT_LOW_STOCK,
                                       sm_product_low_stock(entity),
                                       entity->id, &index);
        if (status == SM_REPO_OK) status = sm_remove_index(uow, &index);
    }
    return status;
}

sm_repo_status sm_product_create(sm_repo_uow *uow, sm_product *entity) {
    sm_product candidate;
    sm_key_builder primary;
    uint64_t sequence;
    int written;
    sm_repo_status status;
    if (!uow || !entity || entity->id[0] != '\0' ||
        !sm_product_valid(entity, 0))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_counter_next(uow, SM_COUNTER_PRODUCT, &sequence);
    if (status != SM_REPO_OK) return status;
    candidate = *entity;
    written = snprintf(candidate.id, sizeof(candidate.id), "P%04llu",
                       (unsigned long long)sequence);
    if (written < 0 || (size_t)written >= sizeof(candidate.id))
        return SM_REPO_ERR_FULL;
    if ((status = sm_make_string_key(SM_NS_PRODUCT_BY_ID,
                                     candidate.id, &primary)) != SM_REPO_OK)
        return status;
    status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, &candidate,
                                sm_product_encode_any);
    if (status == SM_REPO_OK)
        status = sm_product_add_indexes(uow, &candidate, &primary);
    if (status == SM_REPO_OK) *entity = candidate;
    return status;
}

static sm_repo_status sm_product_import_counter(sm_repo_uow *uow,
                                                const char *id) {
    uint64_t sequence = 0;
    const unsigned char *p;
    if (!id || (id[0] != 'P' && id[0] != 'p') || id[1] == '\0')
        return SM_REPO_OK;
    p = (const unsigned char *)(id + 1);
    while (*p != '\0') {
        unsigned digit;
        if (*p < '0' || *p > '9') return SM_REPO_OK;
        digit = (unsigned)(*p - '0');
        if (sequence > (UINT64_MAX - digit) / 10u)
            return SM_REPO_ERR_FULL;
        sequence = sequence * 10u + digit;
        ++p;
    }
    if (sequence == UINT64_MAX) return SM_REPO_ERR_FULL;
    return sm_repo_counter_ensure_at_least(uow, SM_COUNTER_PRODUCT,
                                           sequence + 1u);
}

sm_repo_status sm_product_import(sm_repo_uow *uow,
                                 const sm_product *entity) {
    sm_key_builder primary;
    sm_repo_status status;
    if (!uow || !sm_product_valid(entity, 1)) return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_ID, entity->id, &primary);
    if (status == SM_REPO_OK) status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_product_import_counter(uow, entity->id);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, entity,
                                sm_product_encode_any);
    if (status == SM_REPO_OK)
        status = sm_product_add_indexes(uow, entity, &primary);
    return status;
}

static sm_repo_status sm_product_replace_marker(sm_repo_uow *uow,
                                                uint8_t namespace_kind,
                                                const char *old_prefix,
                                                const char *new_prefix,
                                                const char *id) {
    sm_key_builder old_index;
    sm_key_builder new_index;
    sm_repo_status status = SM_REPO_OK;
    if (old_prefix[0] != '\0') {
        status = sm_make_two_string_key(namespace_kind, old_prefix, id,
                                        &old_index);
        if (status == SM_REPO_OK) status = sm_remove_index(uow, &old_index);
    }
    if (status == SM_REPO_OK && new_prefix[0] != '\0') {
        status = sm_make_two_string_key(namespace_kind, new_prefix, id,
                                        &new_index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&new_index),
                                       sm_key_size(&new_index));
    }
    return status;
}

sm_repo_status sm_product_update(sm_repo_uow *uow,
                                 const sm_product *entity) {
    sm_product old;
    sm_key_builder primary;
    sm_key_builder index;
    sm_repo_status status;
    if (!uow || !sm_product_valid(entity, 1)) return SM_REPO_ERR_INVALID;
    status = sm_product_get(uow, entity->id, &old);
    if (status != SM_REPO_OK) return status;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_ID, entity->id, &primary);
    if (status != SM_REPO_OK) return status;
    if (strcmp(old.barcode, entity->barcode) != 0) {
        if (entity->barcode[0] != '\0') {
            status = sm_make_string_key(SM_NS_PRODUCT_BY_BARCODE,
                                        entity->barcode, &index);
            if (status == SM_REPO_OK)
                status = sm_repo_unique_claim(
                    uow, sm_key_data(&index), sm_key_size(&index),
                    sm_key_data(&primary), sm_key_size(&primary));
            if (status != SM_REPO_OK) return status;
        }
        if (old.barcode[0] != '\0') {
            status = sm_make_string_key(SM_NS_PRODUCT_BY_BARCODE,
                                        old.barcode, &index);
            if (status == SM_REPO_OK)
                status = sm_repo_unique_release(
                    uow, sm_key_data(&index), sm_key_size(&index),
                    sm_key_data(&primary), sm_key_size(&primary));
            if (status != SM_REPO_OK) return status;
        }
    }
    if (strcmp(old.category_id, entity->category_id) != 0) {
        status = sm_product_replace_marker(uow, SM_NS_PRODUCT_BY_CATEGORY,
                                           old.category_id,
                                           entity->category_id, entity->id);
        if (status != SM_REPO_OK) return status;
    }
    if (strcmp(old.supplier_id, entity->supplier_id) != 0) {
        status = sm_product_replace_marker(uow, SM_NS_PRODUCT_BY_SUPPLIER,
                                           old.supplier_id,
                                           entity->supplier_id, entity->id);
        if (status != SM_REPO_OK) return status;
    }
    if (sm_product_low_stock(&old) != sm_product_low_stock(entity)) {
        sm_key_builder old_index;
        status = sm_make_u8_string_key(SM_NS_PRODUCT_LOW_STOCK,
                                       sm_product_low_stock(&old),
                                       old.id, &old_index);
        if (status == SM_REPO_OK) status = sm_remove_index(uow, &old_index);
        if (status == SM_REPO_OK) {
            status = sm_make_u8_string_key(SM_NS_PRODUCT_LOW_STOCK,
                                           sm_product_low_stock(entity),
                                           entity->id, &index);
            if (status == SM_REPO_OK)
                status = sm_repo_index_add(uow, sm_key_data(&index),
                                           sm_key_size(&index));
        }
        if (status != SM_REPO_OK) return status;
    }
    return sm_put_encoded(uow, &primary, entity, sm_product_encode_any);
}

sm_repo_status sm_product_delete(sm_repo_uow *uow, const char *id) {
    sm_product old;
    sm_key_builder primary;
    sm_repo_status status;
    if (!uow || !id || id[0] == '\0') return SM_REPO_ERR_INVALID;
    status = sm_product_get(uow, id, &old);
    if (status != SM_REPO_OK) return status;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_ID, id, &primary);
    if (status == SM_REPO_OK)
        status = sm_product_remove_indexes(uow, &old, &primary);
    if (status == SM_REPO_OK)
        status = sm_repo_delete(uow, sm_key_data(&primary),
                                sm_key_size(&primary));
    return status;
}

static int sm_supplier_valid(const sm_supplier *entity, int require_id) {
    char normalized[SM_SUPPLIER_NAME_CAPACITY];
    return entity && (!require_id || entity->id != 0) &&
           sm_text_valid(entity->name, sizeof(entity->name), 1) &&
           sm_text_valid(entity->contact, sizeof(entity->contact), 0) &&
           sm_text_valid(entity->phone, sizeof(entity->phone), 0) &&
           sm_text_valid(entity->address, sizeof(entity->address), 0) &&
           entity->status <= 1u &&
           sm_normalize_name(entity->name, normalized, sizeof(normalized));
}

static sm_codec_status sm_supplier_encode_any(const void *entity,
                                              uint8_t **out,
                                              size_t *out_len) {
    return sm_supplier_encode((const sm_supplier *)entity, out, out_len);
}

static sm_codec_status sm_supplier_decode_any(const void *data, size_t len,
                                              void *out) {
    return sm_supplier_decode(data, len, (sm_supplier *)out);
}

static sm_repo_status sm_supplier_name_key(const sm_supplier *entity,
                                           sm_key_builder *key) {
    char normalized[SM_SUPPLIER_NAME_CAPACITY];
    if (!sm_normalize_name(entity->name, normalized, sizeof(normalized)))
        return SM_REPO_ERR_INVALID;
    return sm_make_string_u64_key(SM_NS_SUPPLIER_BY_NAME, normalized,
                                  entity->id, key);
}

sm_repo_status sm_supplier_get(sm_repo_uow *uow, uint64_t id,
                               sm_supplier *out) {
    sm_key_builder primary;
    sm_repo_status status;
    if (!uow || id == 0 || !out) return SM_REPO_ERR_INVALID;
    status = sm_make_u64_key(SM_NS_SUPPLIER_BY_ID, id, &primary);
    if (status != SM_REPO_OK) return status;
    status = sm_get_decoded(uow, &primary, out, sm_supplier_decode_any);
    if (status == SM_REPO_OK &&
        (out->id != id || !sm_supplier_valid(out, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_supplier_create(sm_repo_uow *uow, sm_supplier *entity) {
    sm_supplier candidate;
    sm_key_builder primary;
    sm_key_builder name_index;
    uint64_t id;
    sm_repo_status status;
    if (!uow || !entity || entity->id != 0 ||
        !sm_supplier_valid(entity, 0))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_counter_next(uow, SM_COUNTER_SUPPLIER, &id);
    if (status != SM_REPO_OK) return status;
    candidate = *entity;
    candidate.id = id;
    if ((status = sm_make_u64_key(SM_NS_SUPPLIER_BY_ID, id,
                                  &primary)) != SM_REPO_OK ||
        (status = sm_supplier_name_key(&candidate,
                                       &name_index)) != SM_REPO_OK)
        return status;
    status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, &candidate,
                                sm_supplier_encode_any);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&name_index),
                                   sm_key_size(&name_index));
    if (status == SM_REPO_OK) *entity = candidate;
    return status;
}

sm_repo_status sm_supplier_import(sm_repo_uow *uow,
                                  const sm_supplier *entity) {
    sm_key_builder primary;
    sm_key_builder name_index;
    sm_repo_status status;
    if (!uow || !sm_supplier_valid(entity, 1) || entity->id == UINT64_MAX)
        return SM_REPO_ERR_INVALID;
    if ((status = sm_make_u64_key(SM_NS_SUPPLIER_BY_ID, entity->id,
                                  &primary)) != SM_REPO_OK ||
        (status = sm_supplier_name_key(entity,
                                       &name_index)) != SM_REPO_OK)
        return status;
    status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_repo_counter_ensure_at_least(
            uow, SM_COUNTER_SUPPLIER, entity->id + 1u);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, entity,
                                sm_supplier_encode_any);
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&name_index),
                                   sm_key_size(&name_index));
    return status;
}

sm_repo_status sm_supplier_update(sm_repo_uow *uow,
                                  const sm_supplier *entity) {
    sm_supplier old;
    sm_key_builder primary;
    sm_key_builder old_index;
    sm_key_builder new_index;
    sm_repo_status status;
    char old_name[SM_SUPPLIER_NAME_CAPACITY];
    char new_name[SM_SUPPLIER_NAME_CAPACITY];
    if (!uow || !sm_supplier_valid(entity, 1)) return SM_REPO_ERR_INVALID;
    status = sm_supplier_get(uow, entity->id, &old);
    if (status != SM_REPO_OK) return status;
    if (!sm_normalize_name(old.name, old_name, sizeof(old_name)) ||
        !sm_normalize_name(entity->name, new_name, sizeof(new_name)))
        return SM_REPO_ERR_CORRUPT;
    if (strcmp(old_name, new_name) != 0) {
        status = sm_supplier_name_key(&old, &old_index);
        if (status == SM_REPO_OK) status = sm_remove_index(uow, &old_index);
        if (status == SM_REPO_OK)
            status = sm_supplier_name_key(entity, &new_index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&new_index),
                                       sm_key_size(&new_index));
        if (status != SM_REPO_OK) return status;
    }
    status = sm_make_u64_key(SM_NS_SUPPLIER_BY_ID, entity->id, &primary);
    return status == SM_REPO_OK
               ? sm_put_encoded(uow, &primary, entity,
                                sm_supplier_encode_any)
               : status;
}

sm_repo_status sm_supplier_delete(sm_repo_uow *uow, uint64_t id) {
    sm_supplier old;
    sm_key_builder primary;
    sm_key_builder name_index;
    sm_repo_status status = sm_supplier_get(uow, id, &old);
    if (status != SM_REPO_OK) return status;
    if ((status = sm_make_u64_key(SM_NS_SUPPLIER_BY_ID, id,
                                  &primary)) != SM_REPO_OK ||
        (status = sm_supplier_name_key(&old, &name_index)) != SM_REPO_OK)
        return status;
    status = sm_remove_index(uow, &name_index);
    if (status == SM_REPO_OK)
        status = sm_repo_delete(uow, sm_key_data(&primary),
                                sm_key_size(&primary));
    return status;
}

static int sm_member_valid(const sm_member *entity, int require_id) {
    return entity && (!require_id || entity->id != 0) &&
           sm_text_valid(entity->phone, sizeof(entity->phone), 1) &&
           sm_text_valid(entity->name, sizeof(entity->name), 1) &&
           entity->level <= 3u && entity->points >= 0 &&
           entity->total_consume_cents >= 0 && entity->created_at >= 0 &&
           entity->updated_at >= entity->created_at &&
           entity->last_consume_at >= 0;
}

static sm_codec_status sm_member_encode_any(const void *entity,
                                            uint8_t **out,
                                            size_t *out_len) {
    return sm_member_encode((const sm_member *)entity, out, out_len);
}

static sm_codec_status sm_member_decode_any(const void *data, size_t len,
                                            void *out) {
    return sm_member_decode(data, len, (sm_member *)out);
}

sm_repo_status sm_member_get(sm_repo_uow *uow, uint64_t id,
                             sm_member *out) {
    sm_key_builder primary;
    sm_repo_status status;
    if (!uow || id == 0 || !out) return SM_REPO_ERR_INVALID;
    status = sm_make_u64_key(SM_NS_MEMBER_BY_ID, id, &primary);
    if (status != SM_REPO_OK) return status;
    status = sm_get_decoded(uow, &primary, out, sm_member_decode_any);
    if (status == SM_REPO_OK &&
        (out->id != id || !sm_member_valid(out, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_member_primary_from_index_value(
    sm_repo_uow *uow, const void *value, size_t value_len, sm_member *out) {
    sm_key_reader reader;
    uint64_t id;
    if (sm_key_reader_init(&reader, value, value_len,
                           SM_NS_MEMBER_BY_ID) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    return sm_member_get(uow, id, out);
}

sm_repo_status sm_member_get_by_phone(sm_repo_uow *uow,
                                      const char *phone,
                                      sm_member *out) {
    sm_key_builder index;
    void *owner = NULL;
    size_t owner_len = 0;
    sm_repo_status status;
    if (!uow || !phone || phone[0] == '\0' || !out)
        return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_MEMBER_BY_PHONE, phone, &index);
    if (status != SM_REPO_OK) return status;
    status = sm_repo_get(uow, sm_key_data(&index), sm_key_size(&index),
                         &owner, &owner_len);
    if (status != SM_REPO_OK) return status;
    status = sm_member_primary_from_index_value(uow, owner, owner_len, out);
    sm_repo_value_free(owner);
    if (status == SM_REPO_OK && strcmp(out->phone, phone) != 0)
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_member_create(sm_repo_uow *uow, sm_member *entity) {
    sm_member candidate;
    sm_key_builder primary;
    sm_key_builder phone_index;
    sm_key_builder level_index;
    uint64_t id;
    sm_repo_status status;
    if (!uow || !entity || entity->id != 0 ||
        !sm_member_valid(entity, 0))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_counter_next(uow, SM_COUNTER_MEMBER, &id);
    if (status != SM_REPO_OK) return status;
    candidate = *entity;
    candidate.id = id;
    if ((status = sm_make_u64_key(SM_NS_MEMBER_BY_ID, id,
                                  &primary)) != SM_REPO_OK ||
        (status = sm_make_string_key(SM_NS_MEMBER_BY_PHONE,
                                     candidate.phone,
                                     &phone_index)) != SM_REPO_OK ||
        (status = sm_make_u32_u64_key(SM_NS_MEMBER_BY_LEVEL,
                                      candidate.level, id,
                                      &level_index)) != SM_REPO_OK)
        return status;
    status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, &candidate,
                                sm_member_encode_any);
    if (status == SM_REPO_OK)
        status = sm_repo_unique_claim(
            uow, sm_key_data(&phone_index), sm_key_size(&phone_index),
            sm_key_data(&primary), sm_key_size(&primary));
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&level_index),
                                   sm_key_size(&level_index));
    if (status == SM_REPO_OK) *entity = candidate;
    return status;
}

sm_repo_status sm_member_import(sm_repo_uow *uow,
                                const sm_member *entity) {
    sm_key_builder primary;
    sm_key_builder phone_index;
    sm_key_builder level_index;
    sm_repo_status status;
    if (!uow || !sm_member_valid(entity, 1) || entity->id == UINT64_MAX)
        return SM_REPO_ERR_INVALID;
    if ((status = sm_make_u64_key(SM_NS_MEMBER_BY_ID, entity->id,
                                  &primary)) != SM_REPO_OK ||
        (status = sm_make_string_key(SM_NS_MEMBER_BY_PHONE, entity->phone,
                                     &phone_index)) != SM_REPO_OK ||
        (status = sm_make_u32_u64_key(SM_NS_MEMBER_BY_LEVEL, entity->level,
                                      entity->id,
                                      &level_index)) != SM_REPO_OK)
        return status;
    status = sm_expect_missing(uow, &primary);
    if (status == SM_REPO_OK)
        status = sm_repo_counter_ensure_at_least(
            uow, SM_COUNTER_MEMBER, entity->id + 1u);
    if (status == SM_REPO_OK)
        status = sm_put_encoded(uow, &primary, entity,
                                sm_member_encode_any);
    if (status == SM_REPO_OK)
        status = sm_repo_unique_claim(
            uow, sm_key_data(&phone_index), sm_key_size(&phone_index),
            sm_key_data(&primary), sm_key_size(&primary));
    if (status == SM_REPO_OK)
        status = sm_repo_index_add(uow, sm_key_data(&level_index),
                                   sm_key_size(&level_index));
    return status;
}

sm_repo_status sm_member_update(sm_repo_uow *uow,
                                const sm_member *entity) {
    sm_member old;
    sm_key_builder primary;
    sm_key_builder old_index;
    sm_key_builder new_index;
    sm_repo_status status;
    if (!uow || !sm_member_valid(entity, 1)) return SM_REPO_ERR_INVALID;
    status = sm_member_get(uow, entity->id, &old);
    if (status != SM_REPO_OK) return status;
    status = sm_make_u64_key(SM_NS_MEMBER_BY_ID, entity->id, &primary);
    if (status != SM_REPO_OK) return status;
    if (strcmp(old.phone, entity->phone) != 0) {
        status = sm_make_string_key(SM_NS_MEMBER_BY_PHONE, entity->phone,
                                    &new_index);
        if (status == SM_REPO_OK)
            status = sm_repo_unique_claim(
                uow, sm_key_data(&new_index), sm_key_size(&new_index),
                sm_key_data(&primary), sm_key_size(&primary));
        if (status == SM_REPO_OK)
            status = sm_make_string_key(SM_NS_MEMBER_BY_PHONE, old.phone,
                                        &old_index);
        if (status == SM_REPO_OK)
            status = sm_repo_unique_release(
                uow, sm_key_data(&old_index), sm_key_size(&old_index),
                sm_key_data(&primary), sm_key_size(&primary));
        if (status != SM_REPO_OK) return status;
    }
    if (old.level != entity->level) {
        status = sm_make_u32_u64_key(SM_NS_MEMBER_BY_LEVEL, old.level,
                                     old.id, &old_index);
        if (status == SM_REPO_OK) status = sm_remove_index(uow, &old_index);
        if (status == SM_REPO_OK)
            status = sm_make_u32_u64_key(SM_NS_MEMBER_BY_LEVEL,
                                         entity->level, entity->id,
                                         &new_index);
        if (status == SM_REPO_OK)
            status = sm_repo_index_add(uow, sm_key_data(&new_index),
                                       sm_key_size(&new_index));
        if (status != SM_REPO_OK) return status;
    }
    return sm_put_encoded(uow, &primary, entity, sm_member_encode_any);
}

sm_repo_status sm_member_delete(sm_repo_uow *uow, uint64_t id) {
    sm_member old;
    sm_key_builder primary;
    sm_key_builder phone_index;
    sm_key_builder level_index;
    sm_repo_status status = sm_member_get(uow, id, &old);
    if (status != SM_REPO_OK) return status;
    if ((status = sm_make_u64_key(SM_NS_MEMBER_BY_ID, id,
                                  &primary)) != SM_REPO_OK ||
        (status = sm_make_string_key(SM_NS_MEMBER_BY_PHONE, old.phone,
                                     &phone_index)) != SM_REPO_OK ||
        (status = sm_make_u32_u64_key(SM_NS_MEMBER_BY_LEVEL, old.level,
                                      id, &level_index)) != SM_REPO_OK)
        return status;
    status = sm_repo_unique_release(
        uow, sm_key_data(&phone_index), sm_key_size(&phone_index),
        sm_key_data(&primary), sm_key_size(&primary));
    if (status == SM_REPO_OK) status = sm_remove_index(uow, &level_index);
    if (status == SM_REPO_OK)
        status = sm_repo_delete(uow, sm_key_data(&primary),
                                sm_key_size(&primary));
    return status;
}

static int sm_system_config_valid(const sm_system_config_entity *entity) {
    return entity &&
           sm_text_valid(entity->shop_name, sizeof(entity->shop_name), 1) &&
           sm_text_valid(entity->shop_address,
                         sizeof(entity->shop_address), 0) &&
           sm_text_valid(entity->shop_phone, sizeof(entity->shop_phone), 0) &&
           entity->tax_rate_basis_points <= 10000u &&
           entity->auto_backup_interval_minutes > 0 &&
           entity->monthly_fixed_cost_cents >= 0;
}

sm_repo_status sm_system_config_get(sm_repo_uow *uow,
                                    sm_system_config_entity *out) {
    const uint8_t key[] = {SM_NS_SYSTEM_CONFIG};
    void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status;
    if (!uow || !out) return SM_REPO_ERR_INVALID;
    status = sm_repo_get(uow, key, sizeof(key), &value, &value_len);
    if (status != SM_REPO_OK) return status;
    status = sm_decode_result(sm_system_config_decode(value, value_len, out));
    sm_repo_value_free(value);
    if (status == SM_REPO_OK && !sm_system_config_valid(out))
        status = SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_system_config_put(sm_repo_uow *uow,
                                    const sm_system_config_entity *entity) {
    const uint8_t key[] = {SM_NS_SYSTEM_CONFIG};
    uint8_t *value = NULL;
    size_t value_len = 0;
    sm_repo_status status;
    if (!uow || !sm_system_config_valid(entity)) return SM_REPO_ERR_INVALID;
    status = sm_encode_result(sm_system_config_encode(entity, &value,
                                                       &value_len));
    if (status == SM_REPO_OK)
        status = sm_repo_put(uow, key, sizeof(key), value, value_len);
    free(value);
    return status;
}

void sm_entity_array_free(void *array) {
    free(array);
}

typedef sm_repo_status (*sm_scan_load_fn)(sm_repo_scan *,
                                          const void *, size_t, void *);

static sm_repo_status sm_scan_get_decoded(sm_repo_scan *scan,
                                          const sm_key_builder *primary,
                                          void *out,
                                          sm_decode_fn decode) {
    void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status = sm_repo_scan_get(
        scan, sm_key_data(primary), sm_key_size(primary),
        &value, &value_len);
    if (status != SM_REPO_OK)
        return status == SM_REPO_NOT_FOUND ? SM_REPO_ERR_CORRUPT : status;
    status = sm_decode_result(decode(value, value_len, out));
    sm_repo_value_free(value);
    return status;
}

static sm_repo_status sm_collect_index(sm_repository *repo,
                                       uint8_t namespace_kind,
                                       const sm_key_builder *prefix,
                                       size_t element_size,
                                       sm_scan_load_fn load,
                                       void **out, size_t *out_count) {
    sm_repo_scan *scan = NULL;
    void *items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    sm_repo_status status;
    sm_repo_status close_status;
    if (!repo || !prefix || !load || !out || !out_count || element_size == 0)
        return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_repo_scan_open_prefix(repo, namespace_kind,
                                      sm_key_data(prefix),
                                      sm_key_size(prefix), &scan);
    if (status != SM_REPO_OK) return status;
    while ((status = sm_repo_scan_next(scan)) == SM_REPO_OK) {
        const void *key = NULL;
        size_t key_len = 0;
        void *next;
        status = sm_repo_scan_key(scan, &key, &key_len);
        if (status != SM_REPO_OK) break;
        if (count == capacity) {
            size_t next_capacity = capacity == 0 ? 8u : capacity * 2u;
            if (next_capacity < capacity ||
                next_capacity > SIZE_MAX / element_size) {
                status = SM_REPO_ERR_FULL;
                break;
            }
            next = realloc(items, next_capacity * element_size);
            if (!next) {
                status = SM_REPO_ERR_NOMEM;
                break;
            }
            items = next;
            capacity = next_capacity;
        }
        status = load(scan, key, key_len,
                      (uint8_t *)items + count * element_size);
        if (status != SM_REPO_OK) break;
        ++count;
    }
    if (status == SM_REPO_ITER_END) status = SM_REPO_OK;
    close_status = sm_repo_scan_close(&scan);
    if (status == SM_REPO_OK) status = close_status;
    if (status != SM_REPO_OK) {
        free(items);
        return status;
    }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

static sm_repo_status sm_load_employee_role(sm_repo_scan *scan,
                                            const void *key, size_t key_len,
                                            void *out) {
    sm_key_reader reader;
    const char *role;
    size_t role_len;
    uint64_t id;
    sm_key_builder primary;
    sm_employee *entity = (sm_employee *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_EMPLOYEE_BY_ROLE) != SM_KEY_OK ||
        sm_key_read_string(&reader, &role, &role_len) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    status = sm_make_u64_key(SM_NS_EMPLOYEE_BY_ID, id, &primary);
    if (status == SM_REPO_OK)
        status = sm_scan_get_decoded(scan, &primary, entity,
                                     sm_employee_decode_any);
    if (status == SM_REPO_OK &&
        (entity->id != id || strlen(entity->role) != role_len ||
         memcmp(entity->role, role, role_len) != 0 ||
         !sm_employee_valid(entity, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_load_employee_status(sm_repo_scan *scan,
                                              const void *key,
                                              size_t key_len, void *out) {
    sm_key_reader reader;
    uint8_t indexed_status;
    uint64_t id;
    sm_key_builder primary;
    sm_employee *entity = (sm_employee *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_EMPLOYEE_BY_STATUS) != SM_KEY_OK ||
        sm_key_read_u8(&reader, &indexed_status) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    status = sm_make_u64_key(SM_NS_EMPLOYEE_BY_ID, id, &primary);
    if (status == SM_REPO_OK)
        status = sm_scan_get_decoded(scan, &primary, entity,
                                     sm_employee_decode_any);
    if (status == SM_REPO_OK &&
        (entity->id != id || entity->status != indexed_status ||
         !sm_employee_valid(entity, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_employee_list_by_role(sm_repository *repo,
                                        const char *role,
                                        sm_employee **out,
                                        size_t *out_count) {
    sm_key_builder prefix;
    sm_repo_status status;
    if (!role || role[0] == '\0') return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_EMPLOYEE_BY_ROLE, role, &prefix);
    return status == SM_REPO_OK
               ? sm_collect_index(repo, SM_NS_EMPLOYEE_BY_ROLE, &prefix,
                                  sizeof(**out), sm_load_employee_role,
                                  (void **)out, out_count)
               : status;
}

sm_repo_status sm_employee_list_by_status(sm_repository *repo,
                                          uint8_t status_value,
                                          sm_employee **out,
                                          size_t *out_count) {
    sm_key_builder prefix;
    if (status_value > 1u) return SM_REPO_ERR_INVALID;
    sm_key_begin(&prefix, SM_NS_EMPLOYEE_BY_STATUS);
    if (sm_key_add_u8(&prefix, status_value) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return sm_collect_index(repo, SM_NS_EMPLOYEE_BY_STATUS, &prefix,
                            sizeof(**out), sm_load_employee_status,
                            (void **)out, out_count);
}

static sm_repo_status sm_copy_index_string(const char *value, size_t len,
                                           char *out, size_t capacity) {
    if (len == 0 || len >= capacity) return SM_REPO_ERR_CORRUPT;
    memcpy(out, value, len);
    out[len] = '\0';
    return SM_REPO_OK;
}

static sm_repo_status sm_load_product_text_index(
    sm_repo_scan *scan, const void *key, size_t key_len, void *out,
    uint8_t namespace_kind) {
    sm_key_reader reader;
    const char *indexed_value;
    const char *id;
    size_t indexed_len;
    size_t id_len;
    char id_buffer[SM_PRODUCT_ID_CAPACITY];
    sm_key_builder primary;
    sm_product *entity = (sm_product *)out;
    sm_repo_status status;
    const char *actual;
    if (sm_key_reader_init(&reader, key, key_len,
                           namespace_kind) != SM_KEY_OK ||
        sm_key_read_string(&reader, &indexed_value,
                           &indexed_len) != SM_KEY_OK ||
        sm_key_read_string(&reader, &id, &id_len) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK)
        return SM_REPO_ERR_CORRUPT;
    status = sm_copy_index_string(id, id_len, id_buffer,
                                  sizeof(id_buffer));
    if (status != SM_REPO_OK) return status;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_ID, id_buffer, &primary);
    if (status == SM_REPO_OK)
        status = sm_scan_get_decoded(scan, &primary, entity,
                                     sm_product_decode_any);
    actual = namespace_kind == SM_NS_PRODUCT_BY_CATEGORY
                 ? entity->category_id
                 : entity->supplier_id;
    if (status == SM_REPO_OK &&
        (strcmp(entity->id, id_buffer) != 0 ||
         strlen(actual) != indexed_len ||
         memcmp(actual, indexed_value, indexed_len) != 0 ||
         !sm_product_valid(entity, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_load_product_category(sm_repo_scan *scan,
                                               const void *key,
                                               size_t key_len, void *out) {
    return sm_load_product_text_index(scan, key, key_len, out,
                                      SM_NS_PRODUCT_BY_CATEGORY);
}

static sm_repo_status sm_load_product_supplier(sm_repo_scan *scan,
                                               const void *key,
                                               size_t key_len, void *out) {
    return sm_load_product_text_index(scan, key, key_len, out,
                                      SM_NS_PRODUCT_BY_SUPPLIER);
}

static sm_repo_status sm_load_product_low_stock(sm_repo_scan *scan,
                                                const void *key,
                                                size_t key_len, void *out) {
    sm_key_reader reader;
    uint8_t indexed_flag;
    const char *id;
    size_t id_len;
    char id_buffer[SM_PRODUCT_ID_CAPACITY];
    sm_key_builder primary;
    sm_product *entity = (sm_product *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_PRODUCT_LOW_STOCK) != SM_KEY_OK ||
        sm_key_read_u8(&reader, &indexed_flag) != SM_KEY_OK ||
        sm_key_read_string(&reader, &id, &id_len) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK)
        return SM_REPO_ERR_CORRUPT;
    status = sm_copy_index_string(id, id_len, id_buffer,
                                  sizeof(id_buffer));
    if (status != SM_REPO_OK) return status;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_ID, id_buffer, &primary);
    if (status == SM_REPO_OK)
        status = sm_scan_get_decoded(scan, &primary, entity,
                                     sm_product_decode_any);
    if (status == SM_REPO_OK &&
        (strcmp(entity->id, id_buffer) != 0 ||
         sm_product_low_stock(entity) != indexed_flag ||
         !sm_product_valid(entity, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_product_list_by_category(sm_repository *repo,
                                           const char *category_id,
                                           sm_product **out,
                                           size_t *out_count) {
    sm_key_builder prefix;
    sm_repo_status status;
    if (!category_id || category_id[0] == '\0') return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_CATEGORY, category_id,
                                &prefix);
    return status == SM_REPO_OK
               ? sm_collect_index(repo, SM_NS_PRODUCT_BY_CATEGORY, &prefix,
                                  sizeof(**out), sm_load_product_category,
                                  (void **)out, out_count)
               : status;
}

sm_repo_status sm_product_list_by_supplier(sm_repository *repo,
                                           const char *supplier_id,
                                           sm_product **out,
                                           size_t *out_count) {
    sm_key_builder prefix;
    sm_repo_status status;
    if (!supplier_id || supplier_id[0] == '\0') return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_PRODUCT_BY_SUPPLIER, supplier_id,
                                &prefix);
    return status == SM_REPO_OK
               ? sm_collect_index(repo, SM_NS_PRODUCT_BY_SUPPLIER, &prefix,
                                  sizeof(**out), sm_load_product_supplier,
                                  (void **)out, out_count)
               : status;
}

sm_repo_status sm_product_list_low_stock(sm_repository *repo,
                                         sm_product **out,
                                         size_t *out_count) {
    sm_key_builder prefix;
    sm_key_begin(&prefix, SM_NS_PRODUCT_LOW_STOCK);
    if (sm_key_add_u8(&prefix, 1u) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return sm_collect_index(repo, SM_NS_PRODUCT_LOW_STOCK, &prefix,
                            sizeof(**out), sm_load_product_low_stock,
                            (void **)out, out_count);
}

static sm_repo_status sm_load_supplier_name(sm_repo_scan *scan,
                                            const void *key, size_t key_len,
                                            void *out) {
    sm_key_reader reader;
    const char *indexed_name;
    size_t indexed_len;
    uint64_t id;
    sm_key_builder primary;
    sm_supplier *entity = (sm_supplier *)out;
    char normalized[SM_SUPPLIER_NAME_CAPACITY];
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_SUPPLIER_BY_NAME) != SM_KEY_OK ||
        sm_key_read_string(&reader, &indexed_name,
                           &indexed_len) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    status = sm_make_u64_key(SM_NS_SUPPLIER_BY_ID, id, &primary);
    if (status == SM_REPO_OK)
        status = sm_scan_get_decoded(scan, &primary, entity,
                                     sm_supplier_decode_any);
    if (status == SM_REPO_OK &&
        (!sm_normalize_name(entity->name, normalized, sizeof(normalized)) ||
         entity->id != id || strlen(normalized) != indexed_len ||
         memcmp(normalized, indexed_name, indexed_len) != 0 ||
         !sm_supplier_valid(entity, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_supplier_list_by_name(sm_repository *repo,
                                        const char *name,
                                        sm_supplier **out,
                                        size_t *out_count) {
    char normalized[SM_SUPPLIER_NAME_CAPACITY];
    sm_key_builder prefix;
    sm_repo_status status;
    if (!sm_normalize_name(name, normalized, sizeof(normalized)))
        return SM_REPO_ERR_INVALID;
    status = sm_make_string_key(SM_NS_SUPPLIER_BY_NAME, normalized, &prefix);
    return status == SM_REPO_OK
               ? sm_collect_index(repo, SM_NS_SUPPLIER_BY_NAME, &prefix,
                                  sizeof(**out), sm_load_supplier_name,
                                  (void **)out, out_count)
               : status;
}

static sm_repo_status sm_load_member_level(sm_repo_scan *scan,
                                           const void *key, size_t key_len,
                                           void *out) {
    sm_key_reader reader;
    uint32_t indexed_level;
    uint64_t id;
    sm_key_builder primary;
    sm_member *entity = (sm_member *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_MEMBER_BY_LEVEL) != SM_KEY_OK ||
        sm_key_read_u32(&reader, &indexed_level) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    status = sm_make_u64_key(SM_NS_MEMBER_BY_ID, id, &primary);
    if (status == SM_REPO_OK)
        status = sm_scan_get_decoded(scan, &primary, entity,
                                     sm_member_decode_any);
    if (status == SM_REPO_OK &&
        (entity->id != id || entity->level != indexed_level ||
         !sm_member_valid(entity, 1)))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_member_list_by_level(sm_repository *repo,
                                       uint32_t level,
                                       sm_member **out,
                                       size_t *out_count) {
    sm_key_builder prefix;
    if (level > 3u) return SM_REPO_ERR_INVALID;
    sm_key_begin(&prefix, SM_NS_MEMBER_BY_LEVEL);
    if (sm_key_add_u32(&prefix, level) != SM_KEY_OK)
        return SM_REPO_ERR_INVALID;
    return sm_collect_index(repo, SM_NS_MEMBER_BY_LEVEL, &prefix,
                            sizeof(**out), sm_load_member_level,
                            (void **)out, out_count);
}

static sm_repo_status sm_scan_value_decoded(sm_repo_scan *scan, void *out,
                                            sm_decode_fn decode) {
    const void *value = NULL;
    size_t value_len = 0;
    sm_repo_status status = sm_repo_scan_value(scan, &value, &value_len);
    return status == SM_REPO_OK
               ? sm_decode_result(decode(value, value_len, out))
               : status;
}

static sm_repo_status sm_load_employee_primary(sm_repo_scan *scan,
                                               const void *key,
                                               size_t key_len, void *out) {
    sm_key_reader reader;
    uint64_t id;
    sm_employee *entity = (sm_employee *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_EMPLOYEE_BY_ID) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    status = sm_scan_value_decoded(scan, entity, sm_employee_decode_any);
    return status == SM_REPO_OK &&
                   (entity->id != id || !sm_employee_valid(entity, 1))
               ? SM_REPO_ERR_CORRUPT
               : status;
}

static sm_repo_status sm_load_product_primary(sm_repo_scan *scan,
                                              const void *key,
                                              size_t key_len, void *out) {
    sm_key_reader reader;
    const char *id;
    size_t id_len;
    sm_product *entity = (sm_product *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_PRODUCT_BY_ID) != SM_KEY_OK ||
        sm_key_read_string(&reader, &id, &id_len) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK)
        return SM_REPO_ERR_CORRUPT;
    status = sm_scan_value_decoded(scan, entity, sm_product_decode_any);
    return status == SM_REPO_OK &&
                   (strlen(entity->id) != id_len ||
                    memcmp(entity->id, id, id_len) != 0 ||
                    !sm_product_valid(entity, 1))
               ? SM_REPO_ERR_CORRUPT
               : status;
}

static sm_repo_status sm_load_supplier_primary(sm_repo_scan *scan,
                                               const void *key,
                                               size_t key_len, void *out) {
    sm_key_reader reader;
    uint64_t id;
    sm_supplier *entity = (sm_supplier *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_SUPPLIER_BY_ID) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    status = sm_scan_value_decoded(scan, entity, sm_supplier_decode_any);
    return status == SM_REPO_OK &&
                   (entity->id != id || !sm_supplier_valid(entity, 1))
               ? SM_REPO_ERR_CORRUPT
               : status;
}

static sm_repo_status sm_load_member_primary(sm_repo_scan *scan,
                                             const void *key,
                                             size_t key_len, void *out) {
    sm_key_reader reader;
    uint64_t id;
    sm_member *entity = (sm_member *)out;
    sm_repo_status status;
    if (sm_key_reader_init(&reader, key, key_len,
                           SM_NS_MEMBER_BY_ID) != SM_KEY_OK ||
        sm_key_read_u64(&reader, &id) != SM_KEY_OK ||
        sm_key_reader_done(&reader) != SM_KEY_OK || id == 0)
        return SM_REPO_ERR_CORRUPT;
    status = sm_scan_value_decoded(scan, entity, sm_member_decode_any);
    return status == SM_REPO_OK &&
                   (entity->id != id || !sm_member_valid(entity, 1))
               ? SM_REPO_ERR_CORRUPT
               : status;
}

sm_repo_status sm_employee_list_all(sm_repository *repo,
                                    sm_employee **out,
                                    size_t *out_count) {
    sm_key_builder prefix;
    sm_key_begin(&prefix, SM_NS_EMPLOYEE_BY_ID);
    return sm_collect_index(repo, SM_NS_EMPLOYEE_BY_ID, &prefix,
                            sizeof(**out), sm_load_employee_primary,
                            (void **)out, out_count);
}

sm_repo_status sm_product_list_all(sm_repository *repo,
                                   sm_product **out,
                                   size_t *out_count) {
    sm_key_builder prefix;
    sm_key_begin(&prefix, SM_NS_PRODUCT_BY_ID);
    return sm_collect_index(repo, SM_NS_PRODUCT_BY_ID, &prefix,
                            sizeof(**out), sm_load_product_primary,
                            (void **)out, out_count);
}

sm_repo_status sm_supplier_list_all(sm_repository *repo,
                                    sm_supplier **out,
                                    size_t *out_count) {
    sm_key_builder prefix;
    sm_key_begin(&prefix, SM_NS_SUPPLIER_BY_ID);
    return sm_collect_index(repo, SM_NS_SUPPLIER_BY_ID, &prefix,
                            sizeof(**out), sm_load_supplier_primary,
                            (void **)out, out_count);
}

sm_repo_status sm_member_list_all(sm_repository *repo,
                                  sm_member **out,
                                  size_t *out_count) {
    sm_key_builder prefix;
    sm_key_begin(&prefix, SM_NS_MEMBER_BY_ID);
    return sm_collect_index(repo, SM_NS_MEMBER_BY_ID, &prefix,
                            sizeof(**out), sm_load_member_primary,
                            (void **)out, out_count);
}
