#ifndef SM_FINANCE_SERVICE_H
#define SM_FINANCE_SERVICE_H

#include "domain/sm_finance_entities.h"
#include "repo/sm_repository.h"
#include "supermarket.h"

sm_repo_status sm_finance_payable_for_purchase_uow(
    sm_repo_uow *, uint64_t supplier_id, const char *supplier_name,
    uint64_t purchase_id, int64_t amount_cents, int64_t created_at);
sm_repo_status sm_vip_apply_uow(sm_repo_uow *, const char *card_no,
                                uint8_t type, int64_t amount_cents,
                                uint64_t sale_id, uint64_t operator_id,
                                const char *operator_name, const char *remark,
                                int64_t created_at,
                                sm_vip_transaction *out_transaction);

sm_repo_status sm_service_supplier_finance_update(uint64_t, uint32_t, uint8_t);
sm_repo_status sm_service_supplier_finance_get(uint64_t, SupplierFinance *);
sm_repo_status sm_service_supplier_finance_list(SupplierFinance **, size_t *);
sm_repo_status sm_service_payable_create(uint64_t, int64_t, const char *, Payable *);
sm_repo_status sm_service_payable_get(uint64_t, Payable *);
sm_repo_status sm_service_payable_list(uint64_t, int, Payable **, size_t *);
sm_repo_status sm_service_payment_record(uint64_t, int64_t, const char *,
                                         const char *, uint64_t, const char *,
                                         PaymentRecord *);
sm_repo_status sm_service_payment_list(uint64_t, uint64_t,
                                       PaymentRecord **, size_t *);
sm_repo_status sm_service_supplier_settle(uint64_t supplier_id,
                                          int64_t amount_cents,
                                          const char *method,
                                          const char *reference,
                                          uint64_t operator_id,
                                          const char *remark,
                                          int64_t *out_applied_cents);

sm_repo_status sm_service_vip_card_create(VipCard *);
sm_repo_status sm_service_vip_card_get(const char *, VipCard *);
sm_repo_status sm_service_vip_card_update(const VipCard *);
sm_repo_status sm_service_vip_card_list(uint64_t, int, VipCard **, size_t *);
sm_repo_status sm_service_vip_apply(const char *, int, int64_t, uint64_t,
                                    uint64_t, const char *,
                                    VipCardTransaction *);
sm_repo_status sm_service_vip_transaction_list(const char *,
                                               VipCardTransaction **, size_t *);
void sm_service_finance_array_free(void *);

#endif
