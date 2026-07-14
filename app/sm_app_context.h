#ifndef SM_APP_CONTEXT_H
#define SM_APP_CONTEXT_H

#include "migration/sm_legacy_import.h"
#include "migration/sm_sales_import.h"
#include "migration/sm_inventory_import.h"
#include "migration/sm_purchase_import.h"
#include "migration/sm_finance_import.h"
#include "migration/sm_operations_import.h"
#include "repo/sm_repository.h"

#include <stdint.h>

typedef struct sm_app_context_config {
    const char *database_path;
    const char *legacy_data_dir;
    sm_store_durability durability;
    uint32_t checkpoint_threshold;
    int import_legacy;
} sm_app_context_config;

void sm_app_context_config_default(sm_app_context_config *config);
sm_repo_status sm_app_context_start(const sm_app_context_config *config);
sm_repo_status sm_app_context_stop(void);
sm_repo_status sm_app_context_checkpoint(void);
sm_repository *sm_app_repository(void);
const sm_legacy_import_report *sm_app_import_report(void);
const sm_sales_import_report *sm_app_sales_import_report(void);
const sm_inventory_import_report *sm_app_inventory_import_report(void);
const sm_purchase_import_report *sm_app_purchase_import_report(void);
const sm_finance_import_report *sm_app_supplier_finance_import_report(void);
const sm_finance_import_report *sm_app_vip_import_report(void);
const sm_operations_import_report *sm_app_store_import_report(void);
const sm_operations_import_report *sm_app_control_import_report(void);
const char *sm_app_last_message(void);

#endif
