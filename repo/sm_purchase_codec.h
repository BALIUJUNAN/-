#ifndef SM_PURCHASE_CODEC_H
#define SM_PURCHASE_CODEC_H

#include "domain/sm_purchase_entities.h"
#include "storage/sm_codec.h"

#define SM_PURCHASE_CODEC_VERSION 1u
#define SM_PURCHASE_ENTITY_ORDER 0x40u
#define SM_PURCHASE_ENTITY_ITEM 0x41u
#define SM_PURCHASE_ENTITY_BATCH 0x42u

sm_codec_status sm_purchase_encode(const sm_purchase *value,
                                   uint8_t **out, size_t *out_len);
sm_codec_status sm_purchase_decode(const void *data, size_t len,
                                   sm_purchase *out);
sm_codec_status sm_purchase_item_encode(const sm_purchase_item *value,
                                        uint8_t **out, size_t *out_len);
sm_codec_status sm_purchase_item_decode(const void *data, size_t len,
                                        sm_purchase_item *out);
sm_codec_status sm_batch_encode(const sm_batch *value,
                                uint8_t **out, size_t *out_len);
sm_codec_status sm_batch_decode(const void *data, size_t len,
                                sm_batch *out);

#endif
