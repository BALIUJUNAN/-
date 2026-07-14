#include "sm_base_service.h"

#include "app/sm_app_context.h"
#include "domain/sm_base_entities.h"
#include "repo/sm_base_repository.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static sm_repo_status sm_service_repo(sm_repository **out) {
    *out = sm_app_repository();
    return *out ? SM_REPO_OK : SM_REPO_ERR_INVALID;
}

static int sm_float_to_cents(float value, int64_t *out) {
    double scaled;
    if (!isfinite(value) || value < 0.0f) return 0;
    scaled = (double)value * 100.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5);
    return 1;
}

static int sm_employee_to_entity(const Employee *source, sm_employee *out) {
    if (!source || !out || source->id < 0 || source->status < 0 ||
        source->status > 1)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)source->id;
    memcpy(out->name, source->name, sizeof(out->name));
    memcpy(out->role, source->role, sizeof(out->role));
    memcpy(out->password_hash, source->password_hash,
           sizeof(out->password_hash));
    memcpy(out->salt, source->salt, sizeof(out->salt));
    out->status = (uint8_t)source->status;
    out->created_at = (int64_t)source->created_at;
    out->updated_at = (int64_t)source->updated_at;
    return 1;
}

static int sm_employee_from_entity(const sm_employee *source, Employee *out) {
    if (!source || !out || source->id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)source->id;
    memcpy(out->name, source->name, sizeof(out->name));
    memcpy(out->role, source->role, sizeof(out->role));
    memcpy(out->password_hash, source->password_hash,
           sizeof(out->password_hash));
    memcpy(out->salt, source->salt, sizeof(out->salt));
    out->status = source->status;
    out->created_at = (time_t)source->created_at;
    out->updated_at = (time_t)source->updated_at;
    return 1;
}

static int sm_product_to_entity(const Product *source, sm_product *out) {
    if (!source || !out || source->stock < 0 || source->min_stock < 0 ||
        source->status < 0 || source->status > 1)
        return 0;
    memset(out, 0, sizeof(*out));
    memcpy(out->id, source->id, sizeof(out->id));
    memcpy(out->name, source->name, sizeof(out->name));
    memcpy(out->barcode, source->barcode, sizeof(out->barcode));
    if (!sm_float_to_cents(source->price, &out->price_cents) ||
        !sm_float_to_cents(source->cost, &out->cost_cents))
        return 0;
    out->stock = source->stock;
    out->min_stock = source->min_stock;
    memcpy(out->category_id, source->category_id, sizeof(out->category_id));
    memcpy(out->supplier_id, source->supplier_id, sizeof(out->supplier_id));
    out->status = (uint8_t)source->status;
    out->created_at = (int64_t)source->created_at;
    out->updated_at = (int64_t)source->updated_at;
    return 1;
}

static int sm_product_from_entity(const sm_product *source, Product *out) {
    if (!source || !out || source->stock > INT_MAX ||
        source->min_stock > INT_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    memcpy(out->id, source->id, sizeof(out->id));
    memcpy(out->name, source->name, sizeof(out->name));
    memcpy(out->barcode, source->barcode, sizeof(out->barcode));
    out->price = (float)((double)source->price_cents / 100.0);
    out->cost = (float)((double)source->cost_cents / 100.0);
    out->stock = (int)source->stock;
    out->min_stock = (int)source->min_stock;
    memcpy(out->category_id, source->category_id, sizeof(out->category_id));
    memcpy(out->supplier_id, source->supplier_id, sizeof(out->supplier_id));
    out->status = source->status;
    out->created_at = (time_t)source->created_at;
    out->updated_at = (time_t)source->updated_at;
    return 1;
}

static int sm_supplier_to_entity(const Supplier *source, sm_supplier *out) {
    if (!source || !out || source->id < 0 || source->status < 0 ||
        source->status > 1)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)source->id;
    memcpy(out->name, source->name, sizeof(out->name));
    memcpy(out->contact, source->contact, sizeof(out->contact));
    memcpy(out->phone, source->phone, sizeof(out->phone));
    memcpy(out->address, source->address, sizeof(out->address));
    out->status = (uint8_t)source->status;
    return 1;
}

static int sm_supplier_from_entity(const sm_supplier *source, Supplier *out) {
    if (!source || !out || source->id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)source->id;
    memcpy(out->name, source->name, sizeof(out->name));
    memcpy(out->contact, source->contact, sizeof(out->contact));
    memcpy(out->phone, source->phone, sizeof(out->phone));
    memcpy(out->address, source->address, sizeof(out->address));
    out->status = source->status;
    return 1;
}

static int sm_member_to_entity(const Member *source, sm_member *out) {
    if (!source || !out || source->id < 0 || source->level < 0 ||
        source->level > 3 || source->points < 0)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)source->id;
    memcpy(out->phone, source->phone, sizeof(out->phone));
    memcpy(out->name, source->name, sizeof(out->name));
    out->level = (uint32_t)source->level;
    out->points = source->points;
    if (!sm_float_to_cents(source->total_consume,
                           &out->total_consume_cents))
        return 0;
    out->created_at = (int64_t)source->created_at;
    out->updated_at = (int64_t)source->updated_at;
    out->last_consume_at = (int64_t)source->last_consume_at;
    return 1;
}

static int sm_member_from_entity(const sm_member *source, Member *out) {
    if (!source || !out || source->id > INT_MAX ||
        source->points > INT_MAX || source->level > INT_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    out->id = (int)source->id;
    memcpy(out->phone, source->phone, sizeof(out->phone));
    memcpy(out->name, source->name, sizeof(out->name));
    out->level = (int)source->level;
    out->points = (int)source->points;
    out->total_consume =
        (float)((double)source->total_consume_cents / 100.0);
    out->created_at = (time_t)source->created_at;
    out->updated_at = (time_t)source->updated_at;
    out->last_consume_at = (time_t)source->last_consume_at;
    out->next = NULL;
    return 1;
}

static sm_repo_status sm_finish_uow(sm_repo_uow **uow,
                                    sm_repo_status status) {
    if (status == SM_REPO_OK) return sm_repo_uow_commit(uow);
    (void)sm_repo_uow_rollback(uow);
    return status;
}

sm_repo_status sm_service_employee_create(Employee *employee) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_employee entity;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !sm_employee_to_entity(employee, &entity))
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_employee_create(uow, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_employee_from_entity(&entity, employee))
        return SM_REPO_ERR_FULL;
    return status;
}

sm_repo_status sm_service_employee_get(int id, Employee *out) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_employee entity;
    sm_repo_status status;
    if (id <= 0 || !out) return SM_REPO_ERR_INVALID;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK) status = sm_repo_uow_begin(repo, 0, &uow);
    if (status == SM_REPO_OK)
        status = sm_employee_get(uow, (uint64_t)id, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_employee_from_entity(&entity, out))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_employee_update(const Employee *employee) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_employee entity;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !sm_employee_to_entity(employee, &entity) ||
        entity.id == 0)
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_employee_update(uow, &entity);
    return sm_finish_uow(&uow, status);
}

sm_repo_status sm_service_employee_list(int active_only,
                                        Employee **out, size_t *out_count) {
    sm_repository *repo;
    sm_employee *entities = NULL;
    Employee *items = NULL;
    size_t entity_count = 0;
    size_t count = 0;
    size_t i;
    sm_repo_status status;
    if (!out || !out_count || (active_only != 0 && active_only != 1))
        return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK)
        status = active_only
                     ? sm_employee_list_by_status(repo, 1, &entities,
                                                  &entity_count)
                     : sm_employee_list_all(repo, &entities, &entity_count);
    if (status == SM_REPO_OK && entity_count != 0) {
        if (entity_count > SIZE_MAX / sizeof(*items))
            status = SM_REPO_ERR_FULL;
        else if (!(items = (Employee *)calloc(entity_count,
                                              sizeof(*items))))
            status = SM_REPO_ERR_NOMEM;
    }
    for (i = 0; status == SM_REPO_OK && i < entity_count; ++i) {
        if (!sm_employee_from_entity(&entities[i], &items[count]))
            status = SM_REPO_ERR_CORRUPT;
        else
            ++count;
    }
    sm_entity_array_free(entities);
    if (status != SM_REPO_OK) {
        free(items);
        return status;
    }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

sm_repo_status sm_service_product_create(Product *product) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_product entity;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !sm_product_to_entity(product, &entity))
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_product_create(uow, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_product_from_entity(&entity, product))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_service_product_read(const char *value,
                                              int by_barcode,
                                              Product *out) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_product entity;
    sm_repo_status status;
    if (!value || value[0] == '\0' || !out) return SM_REPO_ERR_INVALID;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK) status = sm_repo_uow_begin(repo, 0, &uow);
    if (status == SM_REPO_OK)
        status = by_barcode
                     ? sm_product_get_by_barcode(uow, value, &entity)
                     : sm_product_get(uow, value, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_product_from_entity(&entity, out))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_product_get(const char *id, Product *out) {
    return sm_service_product_read(id, 0, out);
}

sm_repo_status sm_service_product_get_by_barcode(const char *barcode,
                                                 Product *out) {
    return sm_service_product_read(barcode, 1, out);
}

sm_repo_status sm_service_product_update(const Product *product) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_product entity;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !sm_product_to_entity(product, &entity) ||
        entity.id[0] == '\0')
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_product_update(uow, &entity);
    return sm_finish_uow(&uow, status);
}

static sm_repo_status sm_service_product_convert(sm_product *entities,
                                                 size_t entity_count,
                                                 Product **out,
                                                 size_t *out_count) {
    Product *items = NULL;
    size_t i;
    sm_repo_status status = SM_REPO_OK;
    if (entity_count != 0) {
        if (entity_count > SIZE_MAX / sizeof(*items))
            status = SM_REPO_ERR_FULL;
        else if (!(items = (Product *)calloc(entity_count, sizeof(*items))))
            status = SM_REPO_ERR_NOMEM;
    }
    for (i = 0; status == SM_REPO_OK && i < entity_count; ++i)
        if (!sm_product_from_entity(&entities[i], &items[i]))
            status = SM_REPO_ERR_CORRUPT;
    sm_entity_array_free(entities);
    if (status != SM_REPO_OK) {
        free(items);
        return status;
    }
    *out = items;
    *out_count = entity_count;
    return SM_REPO_OK;
}

sm_repo_status sm_service_product_list(Product **out, size_t *out_count) {
    sm_repository *repo;
    sm_product *entities = NULL;
    size_t count = 0;
    sm_repo_status status;
    if (!out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK)
        status = sm_product_list_all(repo, &entities, &count);
    return status == SM_REPO_OK
               ? sm_service_product_convert(entities, count, out, out_count)
               : status;
}

sm_repo_status sm_service_product_list_low_stock(Product **out,
                                                 size_t *out_count) {
    sm_repository *repo;
    sm_product *entities = NULL;
    size_t count = 0;
    sm_repo_status status;
    if (!out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK)
        status = sm_product_list_low_stock(repo, &entities, &count);
    return status == SM_REPO_OK
               ? sm_service_product_convert(entities, count, out, out_count)
               : status;
}

sm_repo_status sm_service_supplier_create(Supplier *supplier) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_supplier entity;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !sm_supplier_to_entity(supplier, &entity))
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_supplier_create(uow, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_supplier_from_entity(&entity, supplier))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_supplier_get(int id, Supplier *out) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_supplier entity;
    sm_repo_status status;
    if (id <= 0 || !out) return SM_REPO_ERR_INVALID;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK) status = sm_repo_uow_begin(repo, 0, &uow);
    if (status == SM_REPO_OK)
        status = sm_supplier_get(uow, (uint64_t)id, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_supplier_from_entity(&entity, out))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_supplier_list(Supplier **out, size_t *out_count) {
    sm_repository *repo;
    sm_supplier *entities = NULL;
    Supplier *items = NULL;
    size_t count = 0;
    size_t i;
    sm_repo_status status;
    if (!out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK)
        status = sm_supplier_list_all(repo, &entities, &count);
    if (status == SM_REPO_OK && count != 0) {
        if (count > SIZE_MAX / sizeof(*items))
            status = SM_REPO_ERR_FULL;
        else if (!(items = (Supplier *)calloc(count, sizeof(*items))))
            status = SM_REPO_ERR_NOMEM;
    }
    for (i = 0; status == SM_REPO_OK && i < count; ++i)
        if (!sm_supplier_from_entity(&entities[i], &items[i]))
            status = SM_REPO_ERR_CORRUPT;
    sm_entity_array_free(entities);
    if (status != SM_REPO_OK) {
        free(items);
        return status;
    }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

sm_repo_status sm_service_member_create(Member *member) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_member entity;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !sm_member_to_entity(member, &entity))
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_member_create(uow, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_member_from_entity(&entity, member))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

static sm_repo_status sm_service_member_read(const char *phone, int id,
                                             Member *out) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_member entity;
    sm_repo_status status;
    if (!out || (phone == NULL && id <= 0)) return SM_REPO_ERR_INVALID;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK) status = sm_repo_uow_begin(repo, 0, &uow);
    if (status == SM_REPO_OK)
        status = phone ? sm_member_get_by_phone(uow, phone, &entity)
                       : sm_member_get(uow, (uint64_t)id, &entity);
    status = sm_finish_uow(&uow, status);
    if (status == SM_REPO_OK && !sm_member_from_entity(&entity, out))
        return SM_REPO_ERR_CORRUPT;
    return status;
}

sm_repo_status sm_service_member_get(int id, Member *out) {
    return sm_service_member_read(NULL, id, out);
}

sm_repo_status sm_service_member_get_by_phone(const char *phone, Member *out) {
    if (!phone || phone[0] == '\0') return SM_REPO_ERR_INVALID;
    return sm_service_member_read(phone, 0, out);
}

sm_repo_status sm_service_member_update(const Member *member) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_member entity;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !sm_member_to_entity(member, &entity) ||
        entity.id == 0)
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_member_update(uow, &entity);
    return sm_finish_uow(&uow, status);
}

sm_repo_status sm_service_member_delete(int id) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_repo_status status;
    if (id <= 0) return SM_REPO_ERR_INVALID;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK) status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK)
        status = sm_member_delete(uow, (uint64_t)id);
    return sm_finish_uow(&uow, status);
}

sm_repo_status sm_service_member_list(Member **out, size_t *out_count) {
    sm_repository *repo;
    sm_member *entities = NULL;
    Member *items = NULL;
    size_t count = 0;
    size_t i;
    sm_repo_status status;
    if (!out || !out_count) return SM_REPO_ERR_INVALID;
    *out = NULL;
    *out_count = 0;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK)
        status = sm_member_list_all(repo, &entities, &count);
    if (status == SM_REPO_OK && count != 0) {
        if (count > SIZE_MAX / sizeof(*items))
            status = SM_REPO_ERR_FULL;
        else if (!(items = (Member *)calloc(count, sizeof(*items))))
            status = SM_REPO_ERR_NOMEM;
    }
    for (i = 0; status == SM_REPO_OK && i < count; ++i)
        if (!sm_member_from_entity(&entities[i], &items[i]))
            status = SM_REPO_ERR_CORRUPT;
    sm_entity_array_free(entities);
    if (status != SM_REPO_OK) {
        free(items);
        return status;
    }
    *out = items;
    *out_count = count;
    return SM_REPO_OK;
}

sm_repo_status sm_service_config_get(SystemConfig *out) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_system_config_entity entity;
    sm_repo_status status;
    if (!out) return SM_REPO_ERR_INVALID;
    status = sm_service_repo(&repo);
    if (status == SM_REPO_OK) status = sm_repo_uow_begin(repo, 0, &uow);
    if (status == SM_REPO_OK) status = sm_system_config_get(uow, &entity);
    status = sm_finish_uow(&uow, status);
    if (status != SM_REPO_OK) return status;
    memset(out, 0, sizeof(*out));
    memcpy(out->shop_name, entity.shop_name, sizeof(out->shop_name));
    memcpy(out->shop_address, entity.shop_address, sizeof(out->shop_address));
    memcpy(out->shop_phone, entity.shop_phone, sizeof(out->shop_phone));
    out->tax_rate = (float)((double)entity.tax_rate_basis_points / 100.0);
    if (entity.auto_backup_interval_minutes > INT_MAX)
        return SM_REPO_ERR_CORRUPT;
    out->auto_backup_interval =
        (int)entity.auto_backup_interval_minutes;
    out->monthly_fixed_cost =
        (float)((double)entity.monthly_fixed_cost_cents / 100.0);
    return SM_REPO_OK;
}

sm_repo_status sm_service_config_put(const SystemConfig *config) {
    sm_repository *repo;
    sm_repo_uow *uow = NULL;
    sm_system_config_entity entity;
    int64_t tax_basis_points;
    sm_repo_status status = sm_service_repo(&repo);
    if (status != SM_REPO_OK || !config || config->tax_rate < 0.0f ||
        config->auto_backup_interval <= 0 ||
        !sm_float_to_cents(config->tax_rate, &tax_basis_points) ||
        tax_basis_points > 10000)
        return status == SM_REPO_OK ? SM_REPO_ERR_INVALID : status;
    memset(&entity, 0, sizeof(entity));
    memcpy(entity.shop_name, config->shop_name, sizeof(entity.shop_name));
    memcpy(entity.shop_address, config->shop_address,
           sizeof(entity.shop_address));
    memcpy(entity.shop_phone, config->shop_phone, sizeof(entity.shop_phone));
    entity.tax_rate_basis_points = (uint32_t)tax_basis_points;
    entity.auto_backup_interval_minutes =
        (uint32_t)config->auto_backup_interval;
    if (!sm_float_to_cents(config->monthly_fixed_cost,
                           &entity.monthly_fixed_cost_cents))
        return SM_REPO_ERR_INVALID;
    status = sm_repo_uow_begin(repo, 1, &uow);
    if (status == SM_REPO_OK) status = sm_system_config_put(uow, &entity);
    return sm_finish_uow(&uow, status);
}

void sm_service_array_free(void *array) {
    free(array);
}
