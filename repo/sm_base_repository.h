#ifndef SM_BASE_REPOSITORY_H
#define SM_BASE_REPOSITORY_H

#include "domain/sm_base_entities.h"
#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

sm_repo_status sm_employee_create(sm_repo_uow *uow, sm_employee *entity);
sm_repo_status sm_employee_import(sm_repo_uow *uow,
                                  const sm_employee *entity);
sm_repo_status sm_employee_get(sm_repo_uow *uow, uint64_t id,
                               sm_employee *out);
sm_repo_status sm_employee_update(sm_repo_uow *uow,
                                  const sm_employee *entity);
sm_repo_status sm_employee_delete(sm_repo_uow *uow, uint64_t id);
sm_repo_status sm_employee_list_by_role(sm_repository *repo,
                                        const char *role,
                                        sm_employee **out,
                                        size_t *out_count);
sm_repo_status sm_employee_list_by_status(sm_repository *repo,
                                          uint8_t status,
                                          sm_employee **out,
                                          size_t *out_count);
sm_repo_status sm_employee_list_all(sm_repository *repo,
                                    sm_employee **out,
                                    size_t *out_count);

sm_repo_status sm_product_create(sm_repo_uow *uow, sm_product *entity);
sm_repo_status sm_product_import(sm_repo_uow *uow,
                                 const sm_product *entity);
sm_repo_status sm_product_get(sm_repo_uow *uow, const char *id,
                              sm_product *out);
sm_repo_status sm_product_get_by_barcode(sm_repo_uow *uow,
                                         const char *barcode,
                                         sm_product *out);
sm_repo_status sm_product_update(sm_repo_uow *uow,
                                 const sm_product *entity);
sm_repo_status sm_product_delete(sm_repo_uow *uow, const char *id);
sm_repo_status sm_product_list_by_category(sm_repository *repo,
                                           const char *category_id,
                                           sm_product **out,
                                           size_t *out_count);
sm_repo_status sm_product_list_by_supplier(sm_repository *repo,
                                           const char *supplier_id,
                                           sm_product **out,
                                           size_t *out_count);
sm_repo_status sm_product_list_low_stock(sm_repository *repo,
                                         sm_product **out,
                                         size_t *out_count);
sm_repo_status sm_product_list_all(sm_repository *repo,
                                   sm_product **out,
                                   size_t *out_count);

sm_repo_status sm_supplier_create(sm_repo_uow *uow, sm_supplier *entity);
sm_repo_status sm_supplier_import(sm_repo_uow *uow,
                                  const sm_supplier *entity);
sm_repo_status sm_supplier_get(sm_repo_uow *uow, uint64_t id,
                               sm_supplier *out);
sm_repo_status sm_supplier_update(sm_repo_uow *uow,
                                  const sm_supplier *entity);
sm_repo_status sm_supplier_delete(sm_repo_uow *uow, uint64_t id);
sm_repo_status sm_supplier_list_by_name(sm_repository *repo,
                                        const char *name,
                                        sm_supplier **out,
                                        size_t *out_count);
sm_repo_status sm_supplier_list_all(sm_repository *repo,
                                    sm_supplier **out,
                                    size_t *out_count);

sm_repo_status sm_member_create(sm_repo_uow *uow, sm_member *entity);
sm_repo_status sm_member_import(sm_repo_uow *uow,
                                const sm_member *entity);
sm_repo_status sm_member_get(sm_repo_uow *uow, uint64_t id,
                             sm_member *out);
sm_repo_status sm_member_get_by_phone(sm_repo_uow *uow,
                                      const char *phone,
                                      sm_member *out);
sm_repo_status sm_member_update(sm_repo_uow *uow,
                                const sm_member *entity);
sm_repo_status sm_member_delete(sm_repo_uow *uow, uint64_t id);
sm_repo_status sm_member_list_by_level(sm_repository *repo,
                                       uint32_t level,
                                       sm_member **out,
                                       size_t *out_count);
sm_repo_status sm_member_list_all(sm_repository *repo,
                                  sm_member **out,
                                  size_t *out_count);

sm_repo_status sm_system_config_get(sm_repo_uow *uow,
                                    sm_system_config_entity *out);
sm_repo_status sm_system_config_put(sm_repo_uow *uow,
                                    const sm_system_config_entity *entity);

void sm_entity_array_free(void *array);

#endif
