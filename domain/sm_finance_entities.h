#ifndef SM_FINANCE_ENTITIES_H
#define SM_FINANCE_ENTITIES_H

#include "domain/sm_base_entities.h"

#include <stdint.h>

#define SM_FINANCE_SUPPLIER_NAME_CAPACITY 100u
#define SM_PAYMENT_METHOD_CAPACITY 20u
#define SM_PAYMENT_REFERENCE_CAPACITY 50u
#define SM_OPERATOR_NAME_CAPACITY 50u
#define SM_FINANCE_REMARK_CAPACITY 256u
#define SM_VIP_CARD_NO_CAPACITY 30u

typedef struct sm_supplier_finance {
    uint64_t supplier_id;
    uint32_t payment_days;
    uint8_t rating;
    int64_t total_cents;
    int64_t paid_cents;
    int64_t pending_cents;
    int64_t last_payment_at;
    int64_t updated_at;
} sm_supplier_finance;

typedef struct sm_payable {
    uint64_t id;
    uint64_t supplier_id;
    char supplier_name[SM_FINANCE_SUPPLIER_NAME_CAPACITY];
    uint64_t purchase_id;
    int64_t amount_cents;
    int64_t paid_cents;
    int64_t pending_cents;
    uint8_t status;
    int64_t due_at;
    int64_t created_at;
    int64_t paid_at;
} sm_payable;

typedef struct sm_payment_record {
    uint64_t id;
    uint64_t payable_id;
    uint64_t supplier_id;
    int64_t amount_cents;
    char method[SM_PAYMENT_METHOD_CAPACITY];
    char reference[SM_PAYMENT_REFERENCE_CAPACITY];
    uint64_t operator_id;
    char operator_name[SM_OPERATOR_NAME_CAPACITY];
    char remark[SM_FINANCE_REMARK_CAPACITY];
    int64_t paid_at;
} sm_payment_record;

typedef struct sm_vip_card {
    char card_no[SM_VIP_CARD_NO_CAPACITY];
    uint64_t member_id;
    int64_t balance_cents;
    int64_t total_recharged_cents;
    uint8_t card_type;
    uint8_t status;
    int64_t created_at;
    int64_t updated_at;
    int64_t expired_at;
    char password_hash[SM_PASSWORD_HASH_CAPACITY];
    char password_salt[SM_PASSWORD_SALT_CAPACITY];
} sm_vip_card;

typedef struct sm_vip_transaction {
    uint64_t id;
    char card_no[SM_VIP_CARD_NO_CAPACITY];
    uint8_t type;
    int64_t amount_cents;
    int64_t balance_before_cents;
    int64_t balance_after_cents;
    uint64_t operator_id;
    char operator_name[SM_OPERATOR_NAME_CAPACITY];
    uint64_t sale_id;
    char remark[SM_FINANCE_REMARK_CAPACITY];
    int64_t created_at;
} sm_vip_transaction;

#endif
