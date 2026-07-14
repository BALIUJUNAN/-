#include "repo/sm_base_codec.h"
#include "repo/sm_base_repository.h"
#include "storage/sm_key.h"
#include "storage/sm_namespace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #condition);                                             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define CHECK_REPO(call, repo)                                               \
    do {                                                                     \
        sm_repo_status check_status_ = (call);                               \
        if (check_status_ != SM_REPO_OK) {                                   \
            fprintf(stderr, "FAIL %s:%d: %s -> %s (%s)\n",                \
                    __FILE__, __LINE__, #call,                               \
                    sm_repo_status_name(check_status_),                      \
                    (repo) ? sm_repository_last_message(repo) : "no repo"); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void remove_test_db(const char *path) {
    char journal[512];
    remove(path);
    snprintf(journal, sizeof(journal), "%s.journal", path);
    remove(journal);
}

static sm_employee make_employee(const char *name, const char *role,
                                 uint8_t status) {
    sm_employee entity;
    memset(&entity, 0, sizeof(entity));
    snprintf(entity.name, sizeof(entity.name), "%s", name);
    snprintf(entity.role, sizeof(entity.role), "%s", role);
    snprintf(entity.password_hash, sizeof(entity.password_hash), "hash-%s",
             name);
    snprintf(entity.salt, sizeof(entity.salt), "salt-%s", name);
    entity.status = status;
    entity.created_at = 100;
    entity.updated_at = 100;
    return entity;
}

static sm_product make_product(const char *name, const char *barcode,
                               const char *category,
                               const char *supplier,
                               int64_t stock, int64_t min_stock) {
    sm_product entity;
    memset(&entity, 0, sizeof(entity));
    snprintf(entity.name, sizeof(entity.name), "%s", name);
    snprintf(entity.barcode, sizeof(entity.barcode), "%s", barcode);
    snprintf(entity.category_id, sizeof(entity.category_id), "%s", category);
    snprintf(entity.supplier_id, sizeof(entity.supplier_id), "%s", supplier);
    entity.price_cents = 1299;
    entity.cost_cents = 800;
    entity.stock = stock;
    entity.min_stock = min_stock;
    entity.status = 1;
    entity.created_at = 200;
    entity.updated_at = 200;
    return entity;
}

static sm_supplier make_supplier(const char *name, const char *phone) {
    sm_supplier entity;
    memset(&entity, 0, sizeof(entity));
    snprintf(entity.name, sizeof(entity.name), "%s", name);
    snprintf(entity.contact, sizeof(entity.contact), "contact-%s", phone);
    snprintf(entity.phone, sizeof(entity.phone), "%s", phone);
    snprintf(entity.address, sizeof(entity.address), "address-%s", phone);
    entity.status = 1;
    return entity;
}

static sm_member make_member(const char *name, const char *phone,
                             uint32_t level) {
    sm_member entity;
    memset(&entity, 0, sizeof(entity));
    snprintf(entity.name, sizeof(entity.name), "%s", name);
    snprintf(entity.phone, sizeof(entity.phone), "%s", phone);
    entity.level = level;
    entity.points = 10;
    entity.total_consume_cents = 5000;
    entity.created_at = 300;
    entity.updated_at = 300;
    return entity;
}

static int test_codecs(void) {
    sm_employee employee = make_employee("Alice", "cashier", 1);
    sm_employee employee_out;
    sm_product product = make_product("Milk", "690001", "food", "S1", 2, 5);
    sm_product product_out;
    sm_supplier supplier = make_supplier("Alpha", "10086");
    sm_supplier supplier_out;
    sm_member member = make_member("Mia", "13800000000", 2);
    sm_member member_out;
    sm_system_config_entity config;
    sm_system_config_entity config_out;
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;

    employee.id = 7;
    CHECK(sm_employee_encode(&employee, &encoded, &encoded_len) == SM_CODEC_OK);
    CHECK(sm_employee_decode(encoded, encoded_len, &employee_out) == SM_CODEC_OK);
    CHECK(employee_out.id == 7 && strcmp(employee_out.name, "Alice") == 0 &&
          strcmp(employee_out.role, "cashier") == 0);
    CHECK(sm_employee_decode(encoded, encoded_len - 1u, &employee_out) ==
          SM_CODEC_ERR_TRUNCATED);
    encoded[6] = SM_WIRE_I64;
    CHECK(sm_employee_decode(encoded, encoded_len, &employee_out) ==
          SM_CODEC_ERR_TYPE);
    free(encoded);
    encoded = NULL;

    snprintf(product.id, sizeof(product.id), "P0001");
    CHECK(sm_product_encode(&product, &encoded, &encoded_len) == SM_CODEC_OK);
    CHECK(sm_product_decode(encoded, encoded_len, &product_out) == SM_CODEC_OK);
    CHECK(strcmp(product_out.id, "P0001") == 0 &&
          product_out.price_cents == 1299 && product_out.stock == 2);
    CHECK(sm_member_decode(encoded, encoded_len, &member_out) ==
          SM_CODEC_ERR_FORMAT);
    free(encoded);
    encoded = NULL;

    supplier.id = 9;
    CHECK(sm_supplier_encode(&supplier, &encoded, &encoded_len) == SM_CODEC_OK);
    CHECK(sm_supplier_decode(encoded, encoded_len, &supplier_out) == SM_CODEC_OK);
    CHECK(supplier_out.id == 9 && strcmp(supplier_out.name, "Alpha") == 0);
    free(encoded);
    encoded = NULL;

    member.id = 11;
    CHECK(sm_member_encode(&member, &encoded, &encoded_len) == SM_CODEC_OK);
    CHECK(sm_member_decode(encoded, encoded_len, &member_out) == SM_CODEC_OK);
    CHECK(member_out.id == 11 && member_out.level == 2 &&
          member_out.total_consume_cents == 5000);
    free(encoded);
    encoded = NULL;

    memset(&config, 0, sizeof(config));
    snprintf(config.shop_name, sizeof(config.shop_name), "Phase Four Shop");
    snprintf(config.shop_address, sizeof(config.shop_address), "Road 1");
    snprintf(config.shop_phone, sizeof(config.shop_phone), "12345");
    config.tax_rate_basis_points = 600;
    config.auto_backup_interval_minutes = 30;
    config.monthly_fixed_cost_cents = 1000000;
    CHECK(sm_system_config_encode(&config, &encoded, &encoded_len) ==
          SM_CODEC_OK);
    CHECK(sm_system_config_decode(encoded, encoded_len, &config_out) ==
          SM_CODEC_OK);
    CHECK(config_out.tax_rate_basis_points == 600 &&
          config_out.monthly_fixed_cost_cents == 1000000 &&
          strcmp(config_out.shop_name, "Phase Four Shop") == 0);
    free(encoded);
    return 0;
}

static int test_repository(void) {
    const char *path = "phase4_base_entities_test.db";
    sm_repository_config repo_config;
    sm_repository *repo = NULL;
    sm_repo_uow *uow = NULL;
    sm_employee rolled_back = make_employee("Rollback", "cashier", 1);
    sm_employee employee1 = make_employee("Alice", "cashier", 1);
    sm_employee employee2 = make_employee("Bob", "cashier", 1);
    sm_product product1 = make_product("Milk", "BC-1", "food", "S1", 2, 5);
    sm_product product2 = make_product("Tea", "BC-2", "food", "S1", 20, 5);
    sm_supplier supplier1 = make_supplier(" Alpha Co ", "10001");
    sm_supplier supplier2 = make_supplier("alpha co", "10002");
    sm_member member1 = make_member("Mia", "13800000001", 1);
    sm_member member2 = make_member("Noah", "13800000002", 1);
    sm_system_config_entity config;
    sm_employee loaded_employee;
    sm_product loaded_product;
    sm_supplier loaded_supplier;
    sm_member loaded_member;
    sm_system_config_entity loaded_config;
    sm_repo_scan *snapshot_scan = NULL;
    sm_employee *employees = NULL;
    sm_product *products = NULL;
    sm_supplier *suppliers = NULL;
    sm_member *members = NULL;
    size_t count = 0;

    remove_test_db(path);
    sm_repository_config_default(&repo_config, path);
    repo_config.store.durability = SM_STORE_DURABILITY_FULL;
    repo_config.store.checkpoint_threshold = 0;
    CHECK_REPO(sm_repository_open(&repo_config, &repo), repo);

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_employee_create(uow, &rolled_back), repo);
    CHECK(rolled_back.id == 1);
    CHECK_REPO(sm_repo_uow_rollback(&uow), repo);

    memset(&config, 0, sizeof(config));
    snprintf(config.shop_name, sizeof(config.shop_name), "Abyss Market");
    snprintf(config.shop_address, sizeof(config.shop_address), "Main Road");
    snprintf(config.shop_phone, sizeof(config.shop_phone), "400-100");
    config.tax_rate_basis_points = 500;
    config.auto_backup_interval_minutes = 30;
    config.monthly_fixed_cost_cents = 1200000;

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_employee_create(uow, &employee1), repo);
    CHECK_REPO(sm_employee_create(uow, &employee2), repo);
    CHECK_REPO(sm_product_create(uow, &product1), repo);
    CHECK_REPO(sm_product_create(uow, &product2), repo);
    CHECK_REPO(sm_supplier_create(uow, &supplier1), repo);
    CHECK_REPO(sm_supplier_create(uow, &supplier2), repo);
    CHECK_REPO(sm_member_create(uow, &member1), repo);
    CHECK_REPO(sm_member_create(uow, &member2), repo);
    CHECK_REPO(sm_system_config_put(uow, &config), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    CHECK(employee1.id == 1 && employee2.id == 2);
    CHECK(strcmp(product1.id, "P0001") == 0 &&
          strcmp(product2.id, "P0002") == 0);
    CHECK(supplier1.id == 1 && supplier2.id == 2);
    CHECK(member1.id == 1 && member2.id == 2);

    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow), repo);
    CHECK_REPO(sm_employee_get(uow, employee1.id, &loaded_employee), repo);
    CHECK_REPO(sm_product_get_by_barcode(uow, "BC-1", &loaded_product), repo);
    CHECK_REPO(sm_supplier_get(uow, supplier1.id, &loaded_supplier), repo);
    CHECK_REPO(sm_member_get_by_phone(uow, "13800000001", &loaded_member), repo);
    CHECK_REPO(sm_system_config_get(uow, &loaded_config), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    CHECK(strcmp(loaded_employee.name, "Alice") == 0);
    CHECK(strcmp(loaded_product.id, product1.id) == 0);
    CHECK(strcmp(loaded_supplier.name, " Alpha Co ") == 0);
    CHECK(loaded_member.id == member1.id);
    CHECK(strcmp(loaded_config.shop_name, "Abyss Market") == 0);

    CHECK_REPO(sm_employee_list_by_role(repo, "cashier", &employees, &count), repo);
    CHECK(count == 2);
    sm_entity_array_free(employees);
    employees = NULL;
    CHECK_REPO(sm_product_list_by_category(repo, "food", &products, &count), repo);
    CHECK(count == 2);
    sm_entity_array_free(products);
    products = NULL;
    CHECK_REPO(sm_product_list_by_supplier(repo, "S1", &products, &count), repo);
    CHECK(count == 2);
    sm_entity_array_free(products);
    products = NULL;
    CHECK_REPO(sm_product_list_low_stock(repo, &products, &count), repo);
    CHECK(count == 1 && strcmp(products[0].id, product1.id) == 0);
    sm_entity_array_free(products);
    products = NULL;
    CHECK_REPO(sm_supplier_list_by_name(repo, "ALPHA CO", &suppliers, &count), repo);
    CHECK(count == 2);
    sm_entity_array_free(suppliers);
    suppliers = NULL;
    CHECK_REPO(sm_member_list_by_level(repo, 1, &members, &count), repo);
    CHECK(count == 2);
    sm_entity_array_free(members);
    members = NULL;

    {
        sm_key_builder role_prefix;
        sm_key_builder employee_primary;
        void *snapshot_value = NULL;
        size_t snapshot_value_len = 0;
        sm_employee snapshot_employee;
        sm_key_begin(&role_prefix, SM_NS_EMPLOYEE_BY_ROLE);
        CHECK(sm_key_add_string(&role_prefix, "cashier") == SM_KEY_OK);
        sm_key_begin(&employee_primary, SM_NS_EMPLOYEE_BY_ID);
        CHECK(sm_key_add_u64(&employee_primary, employee1.id) == SM_KEY_OK);
        CHECK_REPO(sm_repo_scan_open_prefix(repo, SM_NS_EMPLOYEE_BY_ROLE,
                                             sm_key_data(&role_prefix),
                                             sm_key_size(&role_prefix),
                                             &snapshot_scan), repo);
        snprintf(employee1.name, sizeof(employee1.name), "Alice Updated");
        employee1.updated_at = 350;
        CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
        CHECK_REPO(sm_employee_update(uow, &employee1), repo);
        CHECK_REPO(sm_repo_uow_commit(&uow), repo);
        CHECK_REPO(sm_repo_scan_get(snapshot_scan,
                                    sm_key_data(&employee_primary),
                                    sm_key_size(&employee_primary),
                                    &snapshot_value,
                                    &snapshot_value_len), repo);
        CHECK(sm_employee_decode(snapshot_value, snapshot_value_len,
                                 &snapshot_employee) == SM_CODEC_OK);
        CHECK(strcmp(snapshot_employee.name, "Alice") == 0);
        sm_repo_value_free(snapshot_value);
        CHECK_REPO(sm_repo_scan_close(&snapshot_scan), repo);
    }

    {
        sm_product conflict = make_product("Conflict", "BC-1", "food", "S1", 1, 2);
        CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
        CHECK(sm_product_create(uow, &conflict) == SM_REPO_CONFLICT);
        CHECK(conflict.id[0] == '\0');
        CHECK_REPO(sm_repo_uow_rollback(&uow), repo);
    }
    {
        sm_member conflict = make_member("Conflict", "13800000001", 1);
        CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
        CHECK(sm_member_create(uow, &conflict) == SM_REPO_CONFLICT);
        CHECK(conflict.id == 0);
        CHECK_REPO(sm_repo_uow_rollback(&uow), repo);
    }

    employee1.status = 0;
    employee1.updated_at = 400;
    snprintf(employee1.role, sizeof(employee1.role), "manager");
    product1.stock = 10;
    product1.updated_at = 400;
    snprintf(product1.barcode, sizeof(product1.barcode), "BC-NEW");
    snprintf(product1.category_id, sizeof(product1.category_id), "drink");
    snprintf(product1.supplier_id, sizeof(product1.supplier_id), "S2");
    snprintf(supplier1.name, sizeof(supplier1.name), "Beta Co");
    member1.level = 2;
    member1.updated_at = 400;
    snprintf(member1.phone, sizeof(member1.phone), "13900000001");

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_employee_update(uow, &employee1), repo);
    CHECK_REPO(sm_product_update(uow, &product1), repo);
    CHECK_REPO(sm_supplier_update(uow, &supplier1), repo);
    CHECK_REPO(sm_member_update(uow, &member1), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);

    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow), repo);
    CHECK(sm_product_get_by_barcode(uow, "BC-1", &loaded_product) ==
          SM_REPO_NOT_FOUND);
    CHECK_REPO(sm_product_get_by_barcode(uow, "BC-NEW", &loaded_product), repo);
    CHECK(sm_member_get_by_phone(uow, "13800000001", &loaded_member) ==
          SM_REPO_NOT_FOUND);
    CHECK_REPO(sm_member_get_by_phone(uow, "13900000001", &loaded_member), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    CHECK_REPO(sm_employee_list_by_role(repo, "manager", &employees, &count), repo);
    CHECK(count == 1 && employees[0].id == employee1.id);
    sm_entity_array_free(employees);
    employees = NULL;
    CHECK_REPO(sm_employee_list_by_status(repo, 0, &employees, &count), repo);
    CHECK(count == 1 && employees[0].id == employee1.id);
    sm_entity_array_free(employees);
    employees = NULL;
    CHECK_REPO(sm_product_list_low_stock(repo, &products, &count), repo);
    CHECK(count == 0 && products == NULL);
    CHECK_REPO(sm_product_list_by_category(repo, "food", &products, &count), repo);
    CHECK(count == 1 && strcmp(products[0].id, product2.id) == 0);
    sm_entity_array_free(products);
    products = NULL;
    CHECK_REPO(sm_supplier_list_by_name(repo, "alpha co", &suppliers, &count), repo);
    CHECK(count == 1 && suppliers[0].id == supplier2.id);
    sm_entity_array_free(suppliers);
    suppliers = NULL;
    CHECK_REPO(sm_member_list_by_level(repo, 1, &members, &count), repo);
    CHECK(count == 1 && members[0].id == member2.id);
    sm_entity_array_free(members);
    members = NULL;

    CHECK_REPO(sm_repo_uow_begin(repo, 1, &uow), repo);
    CHECK_REPO(sm_employee_delete(uow, employee2.id), repo);
    CHECK_REPO(sm_product_delete(uow, product2.id), repo);
    CHECK_REPO(sm_supplier_delete(uow, supplier2.id), repo);
    CHECK_REPO(sm_member_delete(uow, member2.id), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);

    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow), repo);
    CHECK(sm_employee_get(uow, employee2.id, &loaded_employee) ==
          SM_REPO_NOT_FOUND);
    CHECK(sm_product_get(uow, product2.id, &loaded_product) ==
          SM_REPO_NOT_FOUND);
    CHECK(sm_supplier_get(uow, supplier2.id, &loaded_supplier) ==
          SM_REPO_NOT_FOUND);
    CHECK(sm_member_get(uow, member2.id, &loaded_member) ==
          SM_REPO_NOT_FOUND);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);

    CHECK_REPO(sm_repository_verify(repo, 1), repo);
    CHECK_REPO(sm_repository_close(&repo), repo);

    sm_repository_config_default(&repo_config, path);
    repo_config.store.read_only = 1;
    repo_config.store.create_if_missing = 0;
    CHECK_REPO(sm_repository_open(&repo_config, &repo), repo);
    CHECK_REPO(sm_repo_uow_begin(repo, 0, &uow), repo);
    CHECK_REPO(sm_employee_get(uow, employee1.id, &loaded_employee), repo);
    CHECK_REPO(sm_product_get(uow, product1.id, &loaded_product), repo);
    CHECK_REPO(sm_supplier_get(uow, supplier1.id, &loaded_supplier), repo);
    CHECK_REPO(sm_member_get(uow, member1.id, &loaded_member), repo);
    CHECK_REPO(sm_system_config_get(uow, &loaded_config), repo);
    CHECK_REPO(sm_repo_uow_commit(&uow), repo);
    CHECK(strcmp(loaded_employee.role, "manager") == 0);
    CHECK(strcmp(loaded_product.barcode, "BC-NEW") == 0);
    CHECK(strcmp(loaded_supplier.name, "Beta Co") == 0);
    CHECK(loaded_member.level == 2);
    CHECK_REPO(sm_repository_close(&repo), repo);

    remove_test_db(path);
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_codecs();
    failed |= test_repository();
    if (failed) {
        fprintf(stderr, "phase 4 base entity tests failed\n");
        return 1;
    }
    printf("phase 4 base entity tests passed\n");
    return 0;
}
