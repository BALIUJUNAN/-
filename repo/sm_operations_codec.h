#ifndef SM_OPERATIONS_CODEC_H
#define SM_OPERATIONS_CODEC_H

#include "domain/sm_operations_entities.h"
#include "storage/sm_codec.h"

#define SM_OPERATIONS_CODEC_VERSION 1u

sm_codec_status sm_store_record_encode(const sm_store_record *, uint8_t **, size_t *);
sm_codec_status sm_store_record_decode(const void *, size_t, sm_store_record *);
sm_codec_status sm_store_stock_record_encode(const sm_store_stock_record *, uint8_t **, size_t *);
sm_codec_status sm_store_stock_record_decode(const void *, size_t, sm_store_stock_record *);
sm_codec_status sm_transfer_order_record_encode(const sm_transfer_order_record *, uint8_t **, size_t *);
sm_codec_status sm_transfer_order_record_decode(const void *, size_t, sm_transfer_order_record *);
sm_codec_status sm_transfer_item_record_encode(const sm_transfer_item_record *, uint8_t **, size_t *);
sm_codec_status sm_transfer_item_record_decode(const void *, size_t, sm_transfer_item_record *);
sm_codec_status sm_promotion_record_encode(const sm_promotion_record *, uint8_t **, size_t *);
sm_codec_status sm_promotion_record_decode(const void *, size_t, sm_promotion_record *);
sm_codec_status sm_schedule_record_encode(const sm_schedule_record *, uint8_t **, size_t *);
sm_codec_status sm_schedule_record_decode(const void *, size_t, sm_schedule_record *);
sm_codec_status sm_combo_record_encode(const sm_combo_record *, uint8_t **, size_t *);
sm_codec_status sm_combo_record_decode(const void *, size_t, sm_combo_record *);
sm_codec_status sm_combo_item_record_encode(const sm_combo_item_record *, uint8_t **, size_t *);
sm_codec_status sm_combo_item_record_decode(const void *, size_t, sm_combo_item_record *);
sm_codec_status sm_settlement_record_encode(const sm_settlement_record *, uint8_t **, size_t *);
sm_codec_status sm_settlement_record_decode(const void *, size_t, sm_settlement_record *);
sm_codec_status sm_audit_record_encode(const sm_audit_record *, uint8_t **, size_t *);
sm_codec_status sm_audit_record_decode(const void *, size_t, sm_audit_record *);

#endif
