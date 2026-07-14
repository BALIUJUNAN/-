#ifndef SM_LEGACY_IMPORT_H
#define SM_LEGACY_IMPORT_H

#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

typedef struct sm_legacy_import_report {
    int imported;
    int already_completed;
    size_t source_files;
    size_t employees;
    size_t products;
    size_t suppliers;
    size_t members;
    int has_system_config;
    uint64_t source_fingerprint;
    char error_file[260];
    size_t error_line;
    char message[256];
} sm_legacy_import_report;

sm_repo_status sm_legacy_import_if_needed(
    sm_repository *repo, const char *legacy_data_dir,
    sm_legacy_import_report *report);

#endif
