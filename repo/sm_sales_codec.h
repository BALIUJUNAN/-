#ifndef SM_SALES_CODEC_H
#define SM_SALES_CODEC_H

#include "domain/sm_sales_entities.h"
#include "storage/sm_codec.h"

#include <stddef.h>
#include <stdint.h>

#define SM_SALES_CODEC_VERSION 1u

typedef enum sm_sales_entity_type {
    SM_SALES_ENTITY_SALE = 0x20,
    SM_SALES_ENTITY_ITEM = 0x21
} sm_sales_entity_type;

sm_codec_status sm_sale_encode(const sm_sale *entity,
                               uint8_t **out, size_t *out_len);
sm_codec_status sm_sale_decode(const void *data, size_t len,
                               sm_sale *out);
sm_codec_status sm_sale_item_encode(const sm_sale_item *entity,
                                    uint8_t **out, size_t *out_len);
sm_codec_status sm_sale_item_decode(const void *data, size_t len,
                                    sm_sale_item *out);

#endif
