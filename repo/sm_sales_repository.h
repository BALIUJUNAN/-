#ifndef SM_SALES_REPOSITORY_H
#define SM_SALES_REPOSITORY_H

#include "domain/sm_sales_entities.h"
#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

sm_repo_status sm_sale_create(sm_repo_uow *uow, sm_sale *sale);
sm_repo_status sm_sale_import(sm_repo_uow *uow, const sm_sale *sale);
sm_repo_status sm_sale_get(sm_repo_uow *uow, uint64_t id, sm_sale *out);
sm_repo_status sm_sale_update(sm_repo_uow *uow, const sm_sale *sale);
sm_repo_status sm_sale_delete(sm_repo_uow *uow, const sm_sale *sale);

sm_repo_status sm_sale_item_create(sm_repo_uow *uow, sm_sale_item *item);
sm_repo_status sm_sale_item_import(sm_repo_uow *uow,
                                   const sm_sale_item *item);
sm_repo_status sm_sale_item_get(sm_repo_uow *uow, uint64_t sale_id,
                                uint64_t item_id, sm_sale_item *out);
sm_repo_status sm_sale_item_delete(sm_repo_uow *uow,
                                   uint64_t sale_id, uint64_t item_id);

sm_repo_status sm_sale_list_by_status(sm_repository *repo, uint8_t status,
                                      sm_sale **out, size_t *out_count);
sm_repo_status sm_sale_list_completed(sm_repository *repo,
                                      sm_sale **out, size_t *out_count);
sm_repo_status sm_sale_item_list(sm_repository *repo, uint64_t sale_id,
                                 sm_sale_item **out, size_t *out_count);

void sm_sales_array_free(void *array);

#endif
