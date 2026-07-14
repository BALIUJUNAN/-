#ifndef SM_PURCHASE_SERVICE_H
#define SM_PURCHASE_SERVICE_H

#include "repo/sm_repository.h"
#include "supermarket.h"

#include <stddef.h>

sm_repo_status sm_service_purchase_create(Purchase *value);
sm_repo_status sm_service_purchase_item_create(uint64_t purchase_id,
                                               PurchaseItem *value);
sm_repo_status sm_service_purchase_get(uint64_t id, Purchase *out);
sm_repo_status sm_service_purchase_list(int status, Purchase **out,
                                        size_t *out_count);
sm_repo_status sm_service_purchase_item_list(uint64_t purchase_id,
                                             PurchaseItem **out,
                                             size_t *out_count);
sm_repo_status sm_service_purchase_approve(uint64_t id, uint64_t approver_id,
                                           time_t approved_at);
sm_repo_status sm_service_purchase_reject(uint64_t id, uint64_t approver_id,
                                          time_t approved_at);
sm_repo_status sm_service_purchase_receive(uint64_t id, uint64_t operator_id,
                                           time_t completed_at);

sm_repo_status sm_service_batch_create(Batch *value);
sm_repo_status sm_service_batch_update(const Batch *value);
sm_repo_status sm_service_batch_get(const char *batch_no, Batch *out);
sm_repo_status sm_service_batch_list(const char *product_id, int fifo_order,
                                     Batch **out, size_t *out_count);
sm_repo_status sm_service_batch_deduct_fifo(const char *product_id,
                                            float quantity,
                                            uint64_t operator_id,
                                            time_t created_at,
                                            float *out_deducted);

void sm_service_purchase_array_free(void *array);

#endif
