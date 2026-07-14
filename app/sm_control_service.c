#include "app/sm_operations_service.h"

#include "app/sm_app_context.h"
#include "repo/sm_operations_repository.h"

#include <stdio.h>
#include <string.h>

static sm_repo_status begin(int write, sm_repo_uow **uow) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_repo_uow_begin(repo, write, uow) : SM_REPO_ERR_INVALID;
}

static sm_repo_status finish(sm_repo_uow **uow, sm_repo_status status) {
    return status == SM_REPO_OK ? sm_repo_uow_commit(uow)
                                : ((void)sm_repo_uow_rollback(uow), status);
}

#define SIMPLE_CREATE(name, type, repo_fn) \
sm_repo_status name(type *value) { \
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); \
    if (s == SM_REPO_OK) { s = repo_fn(uow, value); } \
    return finish(&uow, s); \
}

#define SIMPLE_GET(name, type, repo_fn) \
sm_repo_status name(uint64_t id, type *out) { \
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(0, &uow); \
    if (s == SM_REPO_OK) { s = repo_fn(uow, id, out); } \
    return finish(&uow, s); \
}

#define SIMPLE_UPDATE(name, type, repo_fn) \
sm_repo_status name(const type *value) { \
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); \
    if (s == SM_REPO_OK) { s = repo_fn(uow, value); } \
    return finish(&uow, s); \
}

#define SIMPLE_DELETE(name, repo_fn) \
sm_repo_status name(uint64_t id) { \
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow); \
    if (s == SM_REPO_OK) { s = repo_fn(uow, id); } \
    return finish(&uow, s); \
}

SIMPLE_CREATE(sm_service_promotion_create, sm_promotion_record,
              sm_promotion_record_create)
SIMPLE_GET(sm_service_promotion_get, sm_promotion_record,
           sm_promotion_record_get)
SIMPLE_UPDATE(sm_service_promotion_update, sm_promotion_record,
              sm_promotion_record_update)
SIMPLE_DELETE(sm_service_promotion_delete, sm_promotion_record_delete)

sm_repo_status sm_service_promotion_list(const char *product, int64_t at,
                                         sm_promotion_record **out,
                                         size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_promotion_record_list(repo, product, at, out, count)
                : SM_REPO_ERR_INVALID;
}

SIMPLE_CREATE(sm_service_combo_create, sm_combo_record,
              sm_combo_record_create)
SIMPLE_GET(sm_service_combo_get, sm_combo_record, sm_combo_record_get)
SIMPLE_UPDATE(sm_service_combo_update, sm_combo_record,
              sm_combo_record_update)

sm_repo_status sm_service_combo_find_barcode(const char *barcode,
                                             sm_combo_record *out) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(0, &uow);
    if (s == SM_REPO_OK) s = sm_combo_record_find_barcode(uow, barcode, out);
    return finish(&uow, s);
}

sm_repo_status sm_service_combo_delete(uint64_t id) {
    sm_combo_item_record *items = NULL; size_t count = 0;
    sm_repo_uow *uow = NULL; sm_repo_status s;
    sm_repository *repo = sm_app_repository();
    if (!repo) return SM_REPO_ERR_INVALID;
    s = sm_combo_item_record_list(repo, id, &items, &count);
    sm_operations_array_free(items);
    if (s != SM_REPO_OK) return s;
    if (count) return SM_REPO_CONFLICT;
    s = begin(1, &uow);
    if (s == SM_REPO_OK) s = sm_combo_record_delete(uow, id);
    return finish(&uow, s);
}

sm_repo_status sm_service_combo_list(int status, sm_combo_record **out,
                                     size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_combo_record_list(repo, status, out, count)
                : SM_REPO_ERR_INVALID;
}

sm_repo_status sm_service_combo_add_item(uint64_t combo,
                                         sm_combo_item_record *value) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow);
    sm_combo_record record;
    if (s == SM_REPO_OK) s = sm_combo_record_get(uow, combo, &record);
    if (s == SM_REPO_OK) { value->combo_id = combo; s = sm_combo_item_record_create(uow, value); }
    return finish(&uow, s);
}

sm_repo_status sm_service_combo_item_list(uint64_t combo,
                                          sm_combo_item_record **out,
                                          size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_combo_item_record_list(repo, combo, out, count)
                : SM_REPO_ERR_INVALID;
}

SIMPLE_CREATE(sm_service_schedule_create, sm_schedule_record,
              sm_schedule_record_create)
SIMPLE_GET(sm_service_schedule_get, sm_schedule_record,
           sm_schedule_record_get)
SIMPLE_UPDATE(sm_service_schedule_update, sm_schedule_record,
              sm_schedule_record_update)
SIMPLE_DELETE(sm_service_schedule_delete, sm_schedule_record_delete)

sm_repo_status sm_service_schedule_find(uint64_t employee, uint32_t year,
                                        uint8_t week,
                                        sm_schedule_record *out) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(0, &uow);
    if (s == SM_REPO_OK)
        s = sm_schedule_record_find(uow, employee, year, week, out);
    return finish(&uow, s);
}

sm_repo_status sm_service_schedule_list(uint64_t employee, uint32_t year,
                                        uint8_t week,
                                        sm_schedule_record **out,
                                        size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_schedule_record_list(repo, employee, year, week, out, count)
                : SM_REPO_ERR_INVALID;
}

SIMPLE_CREATE(sm_service_settlement_create, sm_settlement_record,
              sm_settlement_record_create)
SIMPLE_GET(sm_service_settlement_get, sm_settlement_record,
           sm_settlement_record_get)
SIMPLE_UPDATE(sm_service_settlement_update, sm_settlement_record,
              sm_settlement_record_update)

sm_repo_status sm_service_settlement_find(uint64_t cashier, int64_t date,
                                          sm_settlement_record *out) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(0, &uow);
    if (s == SM_REPO_OK) s = sm_settlement_record_find(uow, cashier, date, out);
    return finish(&uow, s);
}

sm_repo_status sm_service_settlement_confirm(uint64_t id, const char *remark,
                                             int64_t at) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow);
    sm_settlement_record value; sm_audit_record audit;
    if (s == SM_REPO_OK) s = sm_settlement_record_get(uow, id, &value);
    if (s == SM_REPO_OK && value.status == 1u) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) {
        value.status = 1u; value.confirmed_at = at;
        if (remark) snprintf(value.remark, sizeof(value.remark), "%s", remark);
        s = sm_settlement_record_update(uow, &value);
    }
    if (s == SM_REPO_OK) {
        memset(&audit, 0, sizeof(audit));
        snprintf(audit.type, sizeof(audit.type), "%s", "SETTLEMENT");
        snprintf(audit.operation, sizeof(audit.operation), "%s", "CONFIRM");
        snprintf(audit.data, sizeof(audit.data), "%s", value.remark);
        audit.ref_id = id; audit.operator_id = value.cashier_id;
        audit.created_at = at; s = sm_audit_record_create(uow, &audit);
    }
    return finish(&uow, s);
}

sm_repo_status sm_service_settlement_list(uint64_t cashier, int64_t date,
                                          sm_settlement_record **out,
                                          size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_settlement_record_list(repo, cashier, date, out, count)
                : SM_REPO_ERR_INVALID;
}

sm_repo_status sm_service_audit_write(const char *type, uint64_t ref,
                                      const char *operation, const char *data,
                                      uint64_t operator_id, int64_t at,
                                      uint64_t *out_id) {
    sm_repo_uow *uow = NULL; sm_repo_status s = begin(1, &uow);
    sm_audit_record value;
    memset(&value, 0, sizeof(value));
    if (!type || !operation) return finish(&uow, SM_REPO_ERR_INVALID);
    snprintf(value.type, sizeof(value.type), "%s", type);
    snprintf(value.operation, sizeof(value.operation), "%s", operation);
    snprintf(value.data, sizeof(value.data), "%s", data ? data : "");
    value.ref_id = ref; value.operator_id = operator_id; value.created_at = at;
    if (s == SM_REPO_OK) s = sm_audit_record_create(uow, &value);
    if (s == SM_REPO_OK && out_id) *out_id = value.id;
    return finish(&uow, s);
}

sm_repo_status sm_service_audit_list(const char *type, uint64_t operator_id,
                                     int64_t start, int64_t end,
                                     sm_audit_record **out, size_t *count) {
    sm_repository *repo = sm_app_repository();
    return repo ? sm_audit_record_list(repo, type, operator_id, start, end,
                                       out, count) : SM_REPO_ERR_INVALID;
}

#undef SIMPLE_CREATE
#undef SIMPLE_GET
#undef SIMPLE_UPDATE
#undef SIMPLE_DELETE
