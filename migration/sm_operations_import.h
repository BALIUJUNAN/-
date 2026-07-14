#ifndef SM_OPERATIONS_IMPORT_H
#define SM_OPERATIONS_IMPORT_H

#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

typedef struct sm_operations_import_report {
    int imported;
    int already_completed;
    size_t source_files;
    size_t stores;
    size_t store_stocks;
    size_t transfers;
    size_t transfer_items;
    size_t promotions;
    size_t combos;
    size_t combo_items;
    size_t schedules;
    size_t settlements;
    size_t audits;
    uint64_t source_fingerprint;
    char error_file[260];
    size_t error_line;
    char message[256];
} sm_operations_import_report;

sm_repo_status sm_store_transfer_import_if_needed(
    sm_repository *, const char *, sm_operations_import_report *);
sm_repo_status sm_control_import_if_needed(
    sm_repository *, const char *, sm_operations_import_report *);

#endif
