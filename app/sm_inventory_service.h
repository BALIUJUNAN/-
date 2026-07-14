#ifndef SM_INVENTORY_SERVICE_H
#define SM_INVENTORY_SERVICE_H

#include "repo/sm_repository.h"
#include "supermarket.h"

#include <stddef.h>

sm_repo_status sm_service_stock_log_create(StockLog *log);
sm_repo_status sm_service_stock_log_list(const char *product_id,
                                         const char *type,
                                         time_t start, time_t end,
                                         StockLog **out,
                                         size_t *out_count);
void sm_service_inventory_array_free(void *array);

#endif
