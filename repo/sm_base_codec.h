#ifndef SM_BASE_CODEC_H
#define SM_BASE_CODEC_H

#include "domain/sm_base_entities.h"
#include "storage/sm_codec.h"

#include <stddef.h>
#include <stdint.h>

#define SM_BASE_CODEC_VERSION 1u

typedef enum sm_base_entity_type {
    SM_BASE_ENTITY_EMPLOYEE = 0x10,
    SM_BASE_ENTITY_PRODUCT = 0x11,
    SM_BASE_ENTITY_SUPPLIER = 0x12,
    SM_BASE_ENTITY_MEMBER = 0x13,
    SM_BASE_ENTITY_SYSTEM_CONFIG = 0x14
} sm_base_entity_type;

sm_codec_status sm_employee_encode(const sm_employee *entity,
                                   uint8_t **out, size_t *out_len);
sm_codec_status sm_employee_decode(const void *data, size_t len,
                                   sm_employee *out);

sm_codec_status sm_product_encode(const sm_product *entity,
                                  uint8_t **out, size_t *out_len);
sm_codec_status sm_product_decode(const void *data, size_t len,
                                  sm_product *out);

sm_codec_status sm_supplier_encode(const sm_supplier *entity,
                                   uint8_t **out, size_t *out_len);
sm_codec_status sm_supplier_decode(const void *data, size_t len,
                                   sm_supplier *out);

sm_codec_status sm_member_encode(const sm_member *entity,
                                 uint8_t **out, size_t *out_len);
sm_codec_status sm_member_decode(const void *data, size_t len,
                                 sm_member *out);

sm_codec_status sm_system_config_encode(const sm_system_config_entity *entity,
                                        uint8_t **out, size_t *out_len);
sm_codec_status sm_system_config_decode(const void *data, size_t len,
                                        sm_system_config_entity *out);

#endif
