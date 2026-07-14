#ifndef SM_INVENTORY_CODEC_H
#define SM_INVENTORY_CODEC_H

#include "domain/sm_inventory_entities.h"
#include "storage/sm_codec.h"

#include <stddef.h>
#include <stdint.h>

#define SM_INVENTORY_CODEC_VERSION 1u
#define SM_INVENTORY_ENTITY_STOCK_LOG 0x30u

sm_codec_status sm_stock_log_encode(const sm_stock_log *entity,
                                    uint8_t **out, size_t *out_len);
sm_codec_status sm_stock_log_decode(const void *data, size_t len,
                                    sm_stock_log *out);

#endif
