#include "sm_app_context.h"

#include <stdio.h>
#include <string.h>

static sm_repository *sm_current_repository = NULL;
static sm_legacy_import_report sm_current_import_report;
static sm_sales_import_report sm_current_sales_import_report;
static sm_inventory_import_report sm_current_inventory_import_report;
static sm_purchase_import_report sm_current_purchase_import_report;
static sm_finance_import_report sm_current_supplier_finance_import_report;
static sm_finance_import_report sm_current_vip_import_report;
static sm_operations_import_report sm_current_store_import_report;
static sm_operations_import_report sm_current_control_import_report;
static char sm_app_message[256];

void sm_app_context_config_default(sm_app_context_config *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->database_path = "data/supermarket.abdb";
    config->legacy_data_dir = "data";
    config->durability = SM_STORE_DURABILITY_FULL;
    config->checkpoint_threshold = 64u * 1024u;
    config->import_legacy = 1;
}

sm_repo_status sm_app_context_start(const sm_app_context_config *config) {
    sm_repository_config repo_config;
    sm_repo_status status;
    if (!config || !config->database_path || !config->legacy_data_dir ||
        sm_current_repository)
        return sm_current_repository ? SM_REPO_ERR_BUSY
                                     : SM_REPO_ERR_INVALID;
    memset(&sm_current_import_report, 0, sizeof(sm_current_import_report));
    memset(&sm_current_sales_import_report, 0,
           sizeof(sm_current_sales_import_report));
    memset(&sm_current_inventory_import_report, 0,
           sizeof(sm_current_inventory_import_report));
    memset(&sm_current_purchase_import_report, 0,
           sizeof(sm_current_purchase_import_report));
    memset(&sm_current_supplier_finance_import_report, 0,
           sizeof(sm_current_supplier_finance_import_report));
    memset(&sm_current_vip_import_report, 0,
           sizeof(sm_current_vip_import_report));
    memset(&sm_current_store_import_report, 0,
           sizeof(sm_current_store_import_report));
    memset(&sm_current_control_import_report, 0,
           sizeof(sm_current_control_import_report));
    sm_app_message[0] = '\0';
    sm_repository_config_default(&repo_config, config->database_path);
    repo_config.store.durability = config->durability;
    repo_config.store.checkpoint_threshold = config->checkpoint_threshold;
    status = sm_repository_open(&repo_config, &sm_current_repository);
    if (status != SM_REPO_OK) {
        snprintf(sm_app_message, sizeof(sm_app_message),
                 "database open failed: %s", sm_repo_status_name(status));
        return status;
    }
    if (config->import_legacy) {
        status = sm_legacy_import_if_needed(sm_current_repository,
                                            config->legacy_data_dir,
                                            &sm_current_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
        status = sm_purchase_import_if_needed(
            sm_current_repository, config->legacy_data_dir,
            &sm_current_purchase_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_purchase_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
        status = sm_sales_import_if_needed(sm_current_repository,
                                           config->legacy_data_dir,
                                           &sm_current_sales_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_sales_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
        status = sm_inventory_import_if_needed(
            sm_current_repository, config->legacy_data_dir,
            &sm_current_inventory_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_inventory_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
        status = sm_supplier_finance_import_if_needed(
            sm_current_repository, config->legacy_data_dir,
            &sm_current_supplier_finance_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_supplier_finance_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
        status = sm_vip_import_if_needed(
            sm_current_repository, config->legacy_data_dir,
            &sm_current_vip_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_vip_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
        status = sm_store_transfer_import_if_needed(
            sm_current_repository, config->legacy_data_dir,
            &sm_current_store_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_store_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
        status = sm_control_import_if_needed(
            sm_current_repository, config->legacy_data_dir,
            &sm_current_control_import_report);
        if (status != SM_REPO_OK) {
            snprintf(sm_app_message, sizeof(sm_app_message), "%s",
                     sm_current_control_import_report.message);
            (void)sm_repository_close(&sm_current_repository);
            return status;
        }
    }
    snprintf(sm_app_message, sizeof(sm_app_message), "application database ready");
    return SM_REPO_OK;
}

sm_repo_status sm_app_context_stop(void) {
    sm_repo_status status;
    if (!sm_current_repository) return SM_REPO_OK;
    status = sm_repository_close(&sm_current_repository);
    if (status == SM_REPO_OK)
        snprintf(sm_app_message, sizeof(sm_app_message),
                 "application database closed");
    else
        snprintf(sm_app_message, sizeof(sm_app_message),
                 "database close failed: %s", sm_repo_status_name(status));
    return status;
}

sm_repo_status sm_app_context_checkpoint(void) {
    sm_repo_status status;
    if (!sm_current_repository) return SM_REPO_ERR_INVALID;
    status = sm_repository_checkpoint(sm_current_repository);
    if (status == SM_REPO_OK)
        snprintf(sm_app_message, sizeof(sm_app_message),
                 "database checkpoint completed");
    else
        snprintf(sm_app_message, sizeof(sm_app_message),
                 "database checkpoint failed: %s",
                 sm_repository_last_message(sm_current_repository));
    return status;
}

sm_repository *sm_app_repository(void) {
    return sm_current_repository;
}

const sm_legacy_import_report *sm_app_import_report(void) {
    return &sm_current_import_report;
}

const sm_sales_import_report *sm_app_sales_import_report(void) {
    return &sm_current_sales_import_report;
}

const sm_inventory_import_report *sm_app_inventory_import_report(void) {
    return &sm_current_inventory_import_report;
}

const sm_purchase_import_report *sm_app_purchase_import_report(void) {
    return &sm_current_purchase_import_report;
}

const sm_finance_import_report *sm_app_supplier_finance_import_report(void) {
    return &sm_current_supplier_finance_import_report;
}

const sm_finance_import_report *sm_app_vip_import_report(void) {
    return &sm_current_vip_import_report;
}

const sm_operations_import_report *sm_app_store_import_report(void) {
    return &sm_current_store_import_report;
}

const sm_operations_import_report *sm_app_control_import_report(void) {
    return &sm_current_control_import_report;
}

const char *sm_app_last_message(void) {
    return sm_app_message;
}
