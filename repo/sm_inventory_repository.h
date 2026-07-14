#ifndef SM_INVENTORY_REPOSITORY_H
#define SM_INVENTORY_REPOSITORY_H

#include "domain/sm_inventory_entities.h"
#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

sm_repo_status sm_stock_log_create(sm_repo_uow *uow, sm_stock_log *log);
sm_repo_status sm_stock_log_import(sm_repo_uow *uow,
                                   const sm_stock_log *log);
sm_repo_status sm_stock_log_get(sm_repo_uow *uow, uint64_t id,
                                sm_stock_log *out);
sm_repo_status sm_stock_log_list(sm_repository *repo,
                                 const char *product_id,
                                 const char *type,
                                 int64_t start, int64_t end,
                                 sm_stock_log **out, size_t *out_count);

void sm_inventory_array_free(void *array);

#endif
