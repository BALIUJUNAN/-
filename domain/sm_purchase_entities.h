#ifndef SM_PURCHASE_ENTITIES_H
#define SM_PURCHASE_ENTITIES_H

#include "domain/sm_base_entities.h"

#include <stdint.h>

#define SM_PURCHASE_SUPPLIER_ID_CAPACITY 20u
#define SM_PURCHASE_SUPPLIER_NAME_CAPACITY 100u
#define SM_BATCH_NO_CAPACITY 20u

typedef struct sm_purchase {
    uint64_t id;
    char supplier_id[SM_PURCHASE_SUPPLIER_ID_CAPACITY];
    char supplier_name[SM_PURCHASE_SUPPLIER_NAME_CAPACITY];
    uint64_t creator_id;
    uint64_t approver_id;
    uint8_t status;
    int64_t total_cents;
    int64_t created_at;
    int64_t approved_at;
    int64_t completed_at;
} sm_purchase;

typedef struct sm_purchase_item {
    uint64_t id;
    uint64_t purchase_id;
    char product_id[SM_PRODUCT_ID_CAPACITY];
    char product_name[SM_PRODUCT_NAME_CAPACITY];
    int64_t quantity_milli;
    int64_t price_cents;
    int64_t received_milli;
} sm_purchase_item;

typedef struct sm_batch {
    char batch_no[SM_BATCH_NO_CAPACITY];
    char product_id[SM_PRODUCT_ID_CAPACITY];
    char product_name[SM_PRODUCT_NAME_CAPACITY];
    int64_t quantity_milli;
    int64_t initial_quantity_milli;
    int64_t price_cents;
    int64_t production_date;
    int64_t expiry_date;
    int64_t received_date;
    char supplier_id[SM_PURCHASE_SUPPLIER_ID_CAPACITY];
    int64_t created_at;
    uint64_t source_purchase_id;
    uint64_t source_item_id;
} sm_batch;

#endif
