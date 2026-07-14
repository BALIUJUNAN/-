#ifndef SM_SALES_SERVICE_H
#define SM_SALES_SERVICE_H

#include "repo/sm_repository.h"
#include "supermarket.h"

#include <stddef.h>

typedef struct sm_stock_adjustment {
    char product_id[MAX_ID_LEN];
    int quantity;
} sm_stock_adjustment;

sm_repo_status sm_service_sale_create(Sale *sale);
sm_repo_status sm_service_sale_update_pending(const Sale *sale);
sm_repo_status sm_service_sale_get(int id, Sale *out);
sm_repo_status sm_service_sale_list_pending(Sale **out, size_t *out_count);
sm_repo_status sm_service_sale_list_completed(Sale **out, size_t *out_count);

sm_repo_status sm_service_sale_item_create(int sale_id, SaleItem *item);
sm_repo_status sm_service_sale_item_list(int sale_id, SaleItem **out,
                                         size_t *out_count);

sm_repo_status sm_service_sale_cancel(int sale_id);
sm_repo_status sm_service_sale_complete(
    const Sale *completed_sale, const sm_stock_adjustment *adjustments,
    size_t adjustment_count, int earned_points, Member *out_member);
sm_repo_status sm_service_sale_complete_with_vip(
    const Sale *completed_sale, const sm_stock_adjustment *adjustments,
    size_t adjustment_count, int earned_points, const char *vip_card_no,
    Member *out_member);

void sm_service_sales_array_free(void *array);

#endif
