#ifndef SM_OPERATIONS_SERVICE_H
#define SM_OPERATIONS_SERVICE_H

#include "domain/sm_operations_entities.h"
#include "repo/sm_repository.h"

sm_repo_status sm_service_store_create(sm_store_record *);
sm_repo_status sm_service_store_get(uint64_t, sm_store_record *);
sm_repo_status sm_service_store_update(const sm_store_record *);
sm_repo_status sm_service_store_list(int, sm_store_record **, size_t *);
sm_repo_status sm_service_store_stock_get(uint64_t, const char *, sm_store_stock_record *);
sm_repo_status sm_service_store_stock_set(const sm_store_stock_record *);
sm_repo_status sm_service_store_stock_adjust(uint64_t, const char *, int64_t,
                                             int64_t, int64_t);
sm_repo_status sm_service_store_stock_list(uint64_t, const char *,
                                           sm_store_stock_record **, size_t *);

sm_repo_status sm_service_transfer_create(sm_transfer_order_record *);
sm_repo_status sm_service_transfer_add_item(uint64_t, sm_transfer_item_record *);
sm_repo_status sm_service_transfer_get(uint64_t, sm_transfer_order_record *);
sm_repo_status sm_service_transfer_list(int, uint64_t,
                                        sm_transfer_order_record **, size_t *);
sm_repo_status sm_service_transfer_item_list(uint64_t,
                                             sm_transfer_item_record **, size_t *);
sm_repo_status sm_service_transfer_approve(uint64_t, uint64_t, const char *, int64_t);
sm_repo_status sm_service_transfer_reject(uint64_t, uint64_t, const char *, const char *, int64_t);
sm_repo_status sm_service_transfer_out(uint64_t, uint64_t, const char *, int64_t);
sm_repo_status sm_service_transfer_in(uint64_t, uint64_t, const char *, int64_t);
sm_repo_status sm_service_transfer_cancel(uint64_t, uint64_t, int64_t);

sm_repo_status sm_service_promotion_create(sm_promotion_record *);
sm_repo_status sm_service_promotion_get(uint64_t, sm_promotion_record *);
sm_repo_status sm_service_promotion_update(const sm_promotion_record *);
sm_repo_status sm_service_promotion_delete(uint64_t);
sm_repo_status sm_service_promotion_list(const char *, int64_t,
                                         sm_promotion_record **, size_t *);
sm_repo_status sm_service_combo_create(sm_combo_record *);
sm_repo_status sm_service_combo_get(uint64_t, sm_combo_record *);
sm_repo_status sm_service_combo_find_barcode(const char *, sm_combo_record *);
sm_repo_status sm_service_combo_update(const sm_combo_record *);
sm_repo_status sm_service_combo_delete(uint64_t);
sm_repo_status sm_service_combo_list(int, sm_combo_record **, size_t *);
sm_repo_status sm_service_combo_add_item(uint64_t, sm_combo_item_record *);
sm_repo_status sm_service_combo_item_list(uint64_t, sm_combo_item_record **, size_t *);

sm_repo_status sm_service_schedule_create(sm_schedule_record *);
sm_repo_status sm_service_schedule_get(uint64_t, sm_schedule_record *);
sm_repo_status sm_service_schedule_find(uint64_t, uint32_t, uint8_t, sm_schedule_record *);
sm_repo_status sm_service_schedule_update(const sm_schedule_record *);
sm_repo_status sm_service_schedule_delete(uint64_t);
sm_repo_status sm_service_schedule_list(uint64_t, uint32_t, uint8_t,
                                        sm_schedule_record **, size_t *);

sm_repo_status sm_service_settlement_create(sm_settlement_record *);
sm_repo_status sm_service_settlement_get(uint64_t, sm_settlement_record *);
sm_repo_status sm_service_settlement_find(uint64_t, int64_t, sm_settlement_record *);
sm_repo_status sm_service_settlement_update(const sm_settlement_record *);
sm_repo_status sm_service_settlement_confirm(uint64_t, const char *, int64_t);
sm_repo_status sm_service_settlement_list(uint64_t, int64_t,
                                          sm_settlement_record **, size_t *);

sm_repo_status sm_service_audit_write(const char *, uint64_t, const char *,
                                      const char *, uint64_t, int64_t,
                                      uint64_t *);
sm_repo_status sm_service_audit_list(const char *, uint64_t, int64_t, int64_t,
                                     sm_audit_record **, size_t *);

#endif
