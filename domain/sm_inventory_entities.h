#ifndef SM_INVENTORY_ENTITIES_H
#define SM_INVENTORY_ENTITIES_H

#include "domain/sm_base_entities.h"

#include <stdint.h>

#define SM_STOCK_LOG_TYPE_CAPACITY 20u
#define SM_STOCK_LOG_REMARK_CAPACITY 256u

typedef struct sm_stock_log {
    uint64_t id;
    char product_id[SM_PRODUCT_ID_CAPACITY];
    char type[SM_STOCK_LOG_TYPE_CAPACITY];
    int64_t quantity_milli;
    int64_t before_stock_milli;
    int64_t after_stock_milli;
    uint64_t operator_id;
    char remark[SM_STOCK_LOG_REMARK_CAPACITY];
    int64_t created_at;
} sm_stock_log;

#endif
