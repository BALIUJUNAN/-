#ifndef SM_PURCHASE_IMPORT_H
#define SM_PURCHASE_IMPORT_H

#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

typedef struct sm_purchase_import_report {
    int imported;
    int already_completed;
    size_t source_files;
    size_t purchases;
    size_t purchase_items;
    size_t batches;
    uint64_t source_fingerprint;
    char error_file[260];
    size_t error_line;
    char message[256];
} sm_purchase_import_report;

sm_repo_status sm_purchase_import_if_needed(
    sm_repository *repo, const char *legacy_data_dir,
    sm_purchase_import_report *report);

#endif
