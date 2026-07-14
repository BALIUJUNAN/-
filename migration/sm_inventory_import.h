#ifndef SM_INVENTORY_IMPORT_H
#define SM_INVENTORY_IMPORT_H

#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

typedef struct sm_inventory_import_report {
    int imported;
    int already_completed;
    size_t source_files;
    size_t stock_logs;
    uint64_t source_fingerprint;
    char error_file[260];
    size_t error_line;
    char message[256];
} sm_inventory_import_report;

sm_repo_status sm_inventory_import_if_needed(
    sm_repository *repo, const char *legacy_data_dir,
    sm_inventory_import_report *report);

#endif
