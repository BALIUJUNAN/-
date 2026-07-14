#ifndef SM_PURCHASE_REPOSITORY_H
#define SM_PURCHASE_REPOSITORY_H

#include "domain/sm_purchase_entities.h"
#include "repo/sm_repository.h"

sm_repo_status sm_purchase_create(sm_repo_uow *uow, sm_purchase *value);
sm_repo_status sm_purchase_import(sm_repo_uow *uow, const sm_purchase *value);
sm_repo_status sm_purchase_get(sm_repo_uow *uow, uint64_t id,
                               sm_purchase *out);
sm_repo_status sm_purchase_update(sm_repo_uow *uow,
                                  const sm_purchase *value);
sm_repo_status sm_purchase_list(sm_repository *repo, int status,
                                sm_purchase **out, size_t *out_count);

sm_repo_status sm_purchase_item_create(sm_repo_uow *uow,
                                       sm_purchase_item *value);
sm_repo_status sm_purchase_item_import(sm_repo_uow *uow,
                                       const sm_purchase_item *value);
sm_repo_status sm_purchase_item_update(sm_repo_uow *uow,
                                       const sm_purchase_item *value);
sm_repo_status sm_purchase_item_list(sm_repository *repo,
                                     uint64_t purchase_id,
                                     sm_purchase_item **out,
                                     size_t *out_count);

sm_repo_status sm_batch_create(sm_repo_uow *uow, sm_batch *value);
sm_repo_status sm_batch_import(sm_repo_uow *uow, const sm_batch *value);
sm_repo_status sm_batch_get(sm_repo_uow *uow, const char *batch_no,
                            sm_batch *out);
sm_repo_status sm_batch_update(sm_repo_uow *uow, const sm_batch *value);
sm_repo_status sm_batch_list(sm_repository *repo, const char *product_id,
                             int fifo_order, sm_batch **out,
                             size_t *out_count);

void sm_purchase_array_free(void *array);

#endif
