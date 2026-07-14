#ifndef SM_BASE_SERVICE_H
#define SM_BASE_SERVICE_H

#include "repo/sm_repository.h"
#include "supermarket.h"

#include <stddef.h>

sm_repo_status sm_service_employee_create(Employee *employee);
sm_repo_status sm_service_employee_get(int id, Employee *out);
sm_repo_status sm_service_employee_update(const Employee *employee);
sm_repo_status sm_service_employee_list(int active_only,
                                        Employee **out, size_t *out_count);

sm_repo_status sm_service_product_create(Product *product);
sm_repo_status sm_service_product_get(const char *id, Product *out);
sm_repo_status sm_service_product_get_by_barcode(const char *barcode,
                                                 Product *out);
sm_repo_status sm_service_product_update(const Product *product);
sm_repo_status sm_service_product_list(Product **out, size_t *out_count);
sm_repo_status sm_service_product_list_low_stock(Product **out,
                                                 size_t *out_count);

sm_repo_status sm_service_supplier_create(Supplier *supplier);
sm_repo_status sm_service_supplier_get(int id, Supplier *out);
sm_repo_status sm_service_supplier_list(Supplier **out, size_t *out_count);

sm_repo_status sm_service_member_create(Member *member);
sm_repo_status sm_service_member_get(int id, Member *out);
sm_repo_status sm_service_member_get_by_phone(const char *phone, Member *out);
sm_repo_status sm_service_member_update(const Member *member);
sm_repo_status sm_service_member_delete(int id);
sm_repo_status sm_service_member_list(Member **out, size_t *out_count);

sm_repo_status sm_service_config_get(SystemConfig *out);
sm_repo_status sm_service_config_put(const SystemConfig *config);

void sm_service_array_free(void *array);

#endif
