#ifndef SM_FINANCE_REPOSITORY_H
#define SM_FINANCE_REPOSITORY_H

#include "domain/sm_finance_entities.h"
#include "repo/sm_repository.h"

sm_repo_status sm_supplier_finance_get(sm_repo_uow *, uint64_t, sm_supplier_finance *);
sm_repo_status sm_supplier_finance_put(sm_repo_uow *, const sm_supplier_finance *);
sm_repo_status sm_supplier_finance_list(sm_repository *, sm_supplier_finance **, size_t *);

sm_repo_status sm_payable_create(sm_repo_uow *, sm_payable *);
sm_repo_status sm_payable_import(sm_repo_uow *, const sm_payable *);
sm_repo_status sm_payable_get(sm_repo_uow *, uint64_t, sm_payable *);
sm_repo_status sm_payable_update(sm_repo_uow *, const sm_payable *);
sm_repo_status sm_payable_list(sm_repository *, uint64_t supplier_id, int status,
                               sm_payable **, size_t *);
sm_repo_status sm_payable_find_purchase(sm_repo_uow *, uint64_t purchase_id,
                                        sm_payable *);

sm_repo_status sm_payment_record_create(sm_repo_uow *, sm_payment_record *);
sm_repo_status sm_payment_record_import(sm_repo_uow *, const sm_payment_record *);
sm_repo_status sm_payment_record_list(sm_repository *, uint64_t payable_id,
                                      uint64_t supplier_id,
                                      sm_payment_record **, size_t *);

sm_repo_status sm_vip_card_create(sm_repo_uow *, const sm_vip_card *);
sm_repo_status sm_vip_card_import(sm_repo_uow *, const sm_vip_card *);
sm_repo_status sm_vip_card_get(sm_repo_uow *, const char *, sm_vip_card *);
sm_repo_status sm_vip_card_update(sm_repo_uow *, const sm_vip_card *);
sm_repo_status sm_vip_card_list(sm_repository *, uint64_t member_id, int status,
                                sm_vip_card **, size_t *);

sm_repo_status sm_vip_transaction_create(sm_repo_uow *, sm_vip_transaction *);
sm_repo_status sm_vip_transaction_import(sm_repo_uow *, const sm_vip_transaction *);
sm_repo_status sm_vip_transaction_list(sm_repository *, const char *,
                                       sm_vip_transaction **, size_t *);

void sm_finance_array_free(void *);

#endif
