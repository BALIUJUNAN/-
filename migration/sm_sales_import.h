#ifndef SM_SALES_IMPORT_H
#define SM_SALES_IMPORT_H

#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

typedef struct sm_sales_import_report {
    int imported;
    int already_completed;
    size_t source_files;
    size_t sales;
    size_t sale_items;
    uint64_t source_fingerprint;
    char error_file[260];
    size_t error_line;
    char message[256];
} sm_sales_import_report;

sm_repo_status sm_sales_import_if_needed(sm_repository *repo,
                                         const char *legacy_data_dir,
                                         sm_sales_import_report *report);

#endif
