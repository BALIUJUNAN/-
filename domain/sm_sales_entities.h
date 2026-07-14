#ifndef SM_SALES_ENTITIES_H
#define SM_SALES_ENTITIES_H

#include "domain/sm_base_entities.h"

#include <stdint.h>

#define SM_PAYMENT_METHOD_CAPACITY 20u

typedef struct sm_sale {
    uint64_t id;
    uint64_t cashier_id;
    uint64_t member_id;
    int64_t total_cents;
    int64_t discount_cents;
    int64_t final_cents;
    int64_t cash_received_cents;
    int64_t points_used;
    uint8_t status;
    char payment_method[SM_PAYMENT_METHOD_CAPACITY];
    int64_t created_at;
    int64_t completed_at;
} sm_sale;

typedef struct sm_sale_item {
    uint64_t id;
    uint64_t sale_id;
    char product_id[SM_PRODUCT_ID_CAPACITY];
    char product_name[SM_PRODUCT_NAME_CAPACITY];
    int64_t quantity_milli;
    int64_t price_cents;
    int64_t original_price_cents;
    int64_t subtotal_cents;
    int64_t discount_cents;
    uint8_t is_combo;
    uint64_t combo_id;
} sm_sale_item;

#endif
