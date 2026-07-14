#ifndef SM_FINANCE_CODEC_H
#define SM_FINANCE_CODEC_H

#include "domain/sm_finance_entities.h"
#include "storage/sm_codec.h"

#define SM_FINANCE_CODEC_VERSION 1u
#define SM_FINANCE_ENTITY_SUPPLIER 0x50u
#define SM_FINANCE_ENTITY_PAYABLE 0x51u
#define SM_FINANCE_ENTITY_PAYMENT 0x52u
#define SM_FINANCE_ENTITY_VIP_CARD 0x53u
#define SM_FINANCE_ENTITY_VIP_TRANSACTION 0x54u

sm_codec_status sm_supplier_finance_encode(const sm_supplier_finance *, uint8_t **, size_t *);
sm_codec_status sm_supplier_finance_decode(const void *, size_t, sm_supplier_finance *);
sm_codec_status sm_payable_encode(const sm_payable *, uint8_t **, size_t *);
sm_codec_status sm_payable_decode(const void *, size_t, sm_payable *);
sm_codec_status sm_payment_record_encode(const sm_payment_record *, uint8_t **, size_t *);
sm_codec_status sm_payment_record_decode(const void *, size_t, sm_payment_record *);
sm_codec_status sm_vip_card_encode(const sm_vip_card *, uint8_t **, size_t *);
sm_codec_status sm_vip_card_decode(const void *, size_t, sm_vip_card *);
sm_codec_status sm_vip_transaction_encode(const sm_vip_transaction *, uint8_t **, size_t *);
sm_codec_status sm_vip_transaction_decode(const void *, size_t, sm_vip_transaction *);

#endif
