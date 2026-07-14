#ifndef SM_BASE_ENTITIES_H
#define SM_BASE_ENTITIES_H

#include <stdint.h>

#define SM_EMPLOYEE_NAME_CAPACITY 50u
#define SM_EMPLOYEE_ROLE_CAPACITY 20u
#define SM_PASSWORD_HASH_CAPACITY 65u
#define SM_PASSWORD_SALT_CAPACITY 33u

#define SM_PRODUCT_ID_CAPACITY 20u
#define SM_PRODUCT_NAME_CAPACITY 50u
#define SM_PRODUCT_BARCODE_CAPACITY 30u
#define SM_CATEGORY_ID_CAPACITY 20u
#define SM_SUPPLIER_ID_CAPACITY 20u

#define SM_SUPPLIER_NAME_CAPACITY 50u
#define SM_SUPPLIER_CONTACT_CAPACITY 50u
#define SM_PHONE_CAPACITY 20u
#define SM_ADDRESS_CAPACITY 256u

#define SM_MEMBER_NAME_CAPACITY 50u

#define SM_SHOP_NAME_CAPACITY 100u

typedef struct sm_employee {
    uint64_t id;
    char name[SM_EMPLOYEE_NAME_CAPACITY];
    char role[SM_EMPLOYEE_ROLE_CAPACITY];
    char password_hash[SM_PASSWORD_HASH_CAPACITY];
    char salt[SM_PASSWORD_SALT_CAPACITY];
    uint8_t status;
    int64_t created_at;
    int64_t updated_at;
} sm_employee;

typedef struct sm_product {
    char id[SM_PRODUCT_ID_CAPACITY];
    char name[SM_PRODUCT_NAME_CAPACITY];
    char barcode[SM_PRODUCT_BARCODE_CAPACITY];
    int64_t price_cents;
    int64_t cost_cents;
    int64_t stock;
    int64_t min_stock;
    char category_id[SM_CATEGORY_ID_CAPACITY];
    char supplier_id[SM_SUPPLIER_ID_CAPACITY];
    uint8_t status;
    int64_t created_at;
    int64_t updated_at;
} sm_product;

typedef struct sm_supplier {
    uint64_t id;
    char name[SM_SUPPLIER_NAME_CAPACITY];
    char contact[SM_SUPPLIER_CONTACT_CAPACITY];
    char phone[SM_PHONE_CAPACITY];
    char address[SM_ADDRESS_CAPACITY];
    uint8_t status;
} sm_supplier;

typedef struct sm_member {
    uint64_t id;
    char phone[SM_PHONE_CAPACITY];
    char name[SM_MEMBER_NAME_CAPACITY];
    uint32_t level;
    int64_t points;
    int64_t total_consume_cents;
    int64_t created_at;
    int64_t updated_at;
    int64_t last_consume_at;
} sm_member;

typedef struct sm_system_config_entity {
    char shop_name[SM_SHOP_NAME_CAPACITY];
    char shop_address[SM_ADDRESS_CAPACITY];
    char shop_phone[SM_PHONE_CAPACITY];
    uint32_t tax_rate_basis_points;
    uint32_t auto_backup_interval_minutes;
    int64_t monthly_fixed_cost_cents;
} sm_system_config_entity;

#endif
