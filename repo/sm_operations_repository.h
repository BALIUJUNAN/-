#ifndef SM_OPERATIONS_REPOSITORY_H
#define SM_OPERATIONS_REPOSITORY_H

#include "domain/sm_operations_entities.h"
#include "repo/sm_repository.h"

sm_repo_status sm_store_record_create(sm_repo_uow *, sm_store_record *);
sm_repo_status sm_store_record_import(sm_repo_uow *, const sm_store_record *);
sm_repo_status sm_store_record_get(sm_repo_uow *, uint64_t, sm_store_record *);
sm_repo_status sm_store_record_update(sm_repo_uow *, const sm_store_record *);
sm_repo_status sm_store_record_list(sm_repository *, int, sm_store_record **, size_t *);

sm_repo_status sm_store_stock_get(sm_repo_uow *, uint64_t, const char *, sm_store_stock_record *);
sm_repo_status sm_store_stock_put(sm_repo_uow *, const sm_store_stock_record *);
sm_repo_status sm_store_stock_list(sm_repository *, uint64_t, const char *, sm_store_stock_record **, size_t *);

sm_repo_status sm_transfer_order_create(sm_repo_uow *, sm_transfer_order_record *);
sm_repo_status sm_transfer_order_import(sm_repo_uow *, const sm_transfer_order_record *);
sm_repo_status sm_transfer_order_get(sm_repo_uow *, uint64_t, sm_transfer_order_record *);
sm_repo_status sm_transfer_order_update(sm_repo_uow *, const sm_transfer_order_record *);
sm_repo_status sm_transfer_order_list(sm_repository *, int, uint64_t, sm_transfer_order_record **, size_t *);
sm_repo_status sm_transfer_item_create(sm_repo_uow *, sm_transfer_item_record *);
sm_repo_status sm_transfer_item_import(sm_repo_uow *, const sm_transfer_item_record *);
sm_repo_status sm_transfer_item_list(sm_repository *, uint64_t, sm_transfer_item_record **, size_t *);

sm_repo_status sm_promotion_record_create(sm_repo_uow *, sm_promotion_record *);
sm_repo_status sm_promotion_record_import(sm_repo_uow *, const sm_promotion_record *);
sm_repo_status sm_promotion_record_get(sm_repo_uow *, uint64_t, sm_promotion_record *);
sm_repo_status sm_promotion_record_update(sm_repo_uow *, const sm_promotion_record *);
sm_repo_status sm_promotion_record_delete(sm_repo_uow *, uint64_t);
sm_repo_status sm_promotion_record_list(sm_repository *, const char *, int64_t, sm_promotion_record **, size_t *);

sm_repo_status sm_combo_record_create(sm_repo_uow *, sm_combo_record *);
sm_repo_status sm_combo_record_import(sm_repo_uow *, const sm_combo_record *);
sm_repo_status sm_combo_record_get(sm_repo_uow *, uint64_t, sm_combo_record *);
sm_repo_status sm_combo_record_find_barcode(sm_repo_uow *, const char *, sm_combo_record *);
sm_repo_status sm_combo_record_update(sm_repo_uow *, const sm_combo_record *);
sm_repo_status sm_combo_record_delete(sm_repo_uow *, uint64_t);
sm_repo_status sm_combo_record_list(sm_repository *, int, sm_combo_record **, size_t *);
sm_repo_status sm_combo_item_record_create(sm_repo_uow *, sm_combo_item_record *);
sm_repo_status sm_combo_item_record_import(sm_repo_uow *, const sm_combo_item_record *);
sm_repo_status sm_combo_item_record_list(sm_repository *, uint64_t, sm_combo_item_record **, size_t *);

sm_repo_status sm_schedule_record_create(sm_repo_uow *, sm_schedule_record *);
sm_repo_status sm_schedule_record_import(sm_repo_uow *, const sm_schedule_record *);
sm_repo_status sm_schedule_record_get(sm_repo_uow *, uint64_t, sm_schedule_record *);
sm_repo_status sm_schedule_record_find(sm_repo_uow *, uint64_t, uint32_t, uint8_t, sm_schedule_record *);
sm_repo_status sm_schedule_record_update(sm_repo_uow *, const sm_schedule_record *);
sm_repo_status sm_schedule_record_delete(sm_repo_uow *, uint64_t);
sm_repo_status sm_schedule_record_list(sm_repository *, uint64_t, uint32_t, uint8_t, sm_schedule_record **, size_t *);

sm_repo_status sm_settlement_record_create(sm_repo_uow *, sm_settlement_record *);
sm_repo_status sm_settlement_record_import(sm_repo_uow *, const sm_settlement_record *);
sm_repo_status sm_settlement_record_get(sm_repo_uow *, uint64_t, sm_settlement_record *);
sm_repo_status sm_settlement_record_find(sm_repo_uow *, uint64_t, int64_t, sm_settlement_record *);
sm_repo_status sm_settlement_record_update(sm_repo_uow *, const sm_settlement_record *);
sm_repo_status sm_settlement_record_list(sm_repository *, uint64_t, int64_t, sm_settlement_record **, size_t *);

sm_repo_status sm_audit_record_create(sm_repo_uow *, sm_audit_record *);
sm_repo_status sm_audit_record_import(sm_repo_uow *, const sm_audit_record *);
sm_repo_status sm_audit_record_list(sm_repository *, const char *, uint64_t,
                                    int64_t, int64_t, sm_audit_record **, size_t *);

void sm_operations_array_free(void *);

#endif
