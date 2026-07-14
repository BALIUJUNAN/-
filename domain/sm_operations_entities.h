#ifndef SM_OPERATIONS_ENTITIES_H
#define SM_OPERATIONS_ENTITIES_H

#include <stdint.h>

#define SM_OP_NAME_CAPACITY 100u
#define SM_OP_ADDRESS_CAPACITY 256u
#define SM_OP_PHONE_CAPACITY 20u
#define SM_OP_PERSON_CAPACITY 50u
#define SM_OP_PRODUCT_CAPACITY 32u
#define SM_OP_REMARK_CAPACITY 512u
#define SM_OP_AUDIT_TYPE_CAPACITY 20u
#define SM_OP_AUDIT_DATA_CAPACITY 1024u

typedef struct sm_store_record {
    uint64_t id;
    char name[SM_OP_NAME_CAPACITY];
    char address[SM_OP_ADDRESS_CAPACITY];
    char phone[SM_OP_PHONE_CAPACITY];
    char manager_name[SM_OP_PERSON_CAPACITY];
    uint64_t manager_id;
    uint8_t status;
    int64_t created_at;
    int64_t updated_at;
} sm_store_record;

typedef struct sm_store_stock_record {
    uint64_t store_id;
    char product_id[SM_OP_PRODUCT_CAPACITY];
    int64_t quantity;
    int64_t min_stock;
    int64_t updated_at;
} sm_store_stock_record;

typedef struct sm_transfer_order_record {
    uint64_t id;
    uint64_t from_store_id;
    uint64_t to_store_id;
    char from_store_name[SM_OP_NAME_CAPACITY];
    char to_store_name[SM_OP_NAME_CAPACITY];
    uint8_t status;
    uint64_t creator_id;
    char creator_name[SM_OP_PERSON_CAPACITY];
    uint64_t approver_id;
    char approver_name[SM_OP_PERSON_CAPACITY];
    uint64_t out_operator_id;
    char out_operator_name[SM_OP_PERSON_CAPACITY];
    uint64_t in_operator_id;
    char in_operator_name[SM_OP_PERSON_CAPACITY];
    int64_t created_at;
    int64_t approved_at;
    int64_t out_at;
    int64_t in_at;
    char remark[256];
} sm_transfer_order_record;

typedef struct sm_transfer_item_record {
    uint64_t id;
    uint64_t transfer_id;
    char product_id[SM_OP_PRODUCT_CAPACITY];
    char product_name[SM_OP_NAME_CAPACITY];
    int64_t quantity;
} sm_transfer_item_record;

typedef struct sm_promotion_record {
    uint64_t id;
    char name[SM_OP_NAME_CAPACITY];
    uint8_t type;
    char product_id[SM_OP_PRODUCT_CAPACITY];
    uint32_t discount_bps;
    int64_t threshold_cents;
    int64_t discount_cents;
    uint32_t nth_item;
    uint32_t nth_discount_bps;
    uint32_t buy_quantity;
    uint32_t free_quantity;
    uint32_t member_level;
    int64_t member_price_cents;
    int64_t start_at;
    int64_t end_at;
    uint8_t status;
    uint32_t priority;
    int64_t created_at;
} sm_promotion_record;

typedef struct sm_schedule_record {
    uint64_t id;
    uint64_t employee_id;
    uint32_t year;
    uint8_t week;
    char shifts[7][4];
    int64_t created_at;
} sm_schedule_record;

typedef struct sm_combo_record {
    uint64_t id;
    char name[SM_OP_NAME_CAPACITY];
    char barcode[32];
    int64_t price_cents;
    int64_t cost_cents;
    uint8_t status;
    int64_t created_at;
    int64_t updated_at;
} sm_combo_record;

typedef struct sm_combo_item_record {
    uint64_t id;
    uint64_t combo_id;
    char product_id[SM_OP_PRODUCT_CAPACITY];
    char product_name[SM_OP_NAME_CAPACITY];
    uint32_t quantity;
    uint32_t ratio_bps;
} sm_combo_item_record;

typedef struct sm_settlement_record {
    uint64_t id;
    uint64_t cashier_id;
    char cashier_name[SM_OP_PERSON_CAPACITY];
    int64_t business_date;
    int64_t shift_start;
    int64_t shift_end;
    uint32_t total_orders;
    int64_t system_cash_cents;
    int64_t system_online_cents;
    int64_t system_total_cents;
    int64_t actual_cash_cents;
    int64_t actual_online_cents;
    int64_t actual_total_cents;
    int64_t cash_diff_cents;
    int64_t online_diff_cents;
    int64_t total_diff_cents;
    uint8_t status;
    int64_t created_at;
    int64_t confirmed_at;
    char remark[SM_OP_REMARK_CAPACITY];
} sm_settlement_record;

typedef struct sm_audit_record {
    uint64_t id;
    char type[SM_OP_AUDIT_TYPE_CAPACITY];
    uint64_t ref_id;
    char operation[SM_OP_AUDIT_TYPE_CAPACITY];
    char data[SM_OP_AUDIT_DATA_CAPACITY];
    uint64_t operator_id;
    int64_t created_at;
} sm_audit_record;

#endif
