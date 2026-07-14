#ifndef SM_FINANCE_IMPORT_H
#define SM_FINANCE_IMPORT_H

#include "repo/sm_repository.h"

#include <stddef.h>
#include <stdint.h>

typedef struct sm_finance_import_report {
    int imported;
    int already_completed;
    size_t source_files;
    size_t supplier_finances;
    size_t payables;
    size_t payments;
    size_t vip_cards;
    size_t vip_transactions;
    uint64_t source_fingerprint;
    char error_file[260];
    size_t error_line;
    char message[256];
} sm_finance_import_report;

sm_repo_status sm_supplier_finance_import_if_needed(
    sm_repository *, const char *, sm_finance_import_report *);
sm_repo_status sm_vip_import_if_needed(
    sm_repository *, const char *, sm_finance_import_report *);

#endif
