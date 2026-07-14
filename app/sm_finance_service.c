#include "app/sm_finance_service.h"

#include "app/sm_app_context.h"
#include "repo/sm_base_repository.h"
#include "repo/sm_finance_repository.h"
#include "repo/sm_operations_repository.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static sm_repo_status finish(sm_repo_uow **uow, sm_repo_status s) {
    if (s == SM_REPO_OK) return sm_repo_uow_commit(uow);
    (void)sm_repo_uow_rollback(uow); return s;
}

static sm_repo_status finance_audit(sm_repo_uow *uow, const char *type,
                                    uint64_t ref, const char *operation,
                                    uint64_t operator_id, int64_t at) {
    sm_audit_record record;
    memset(&record, 0, sizeof(record));
    snprintf(record.type, sizeof(record.type), "%s", type);
    snprintf(record.operation, sizeof(record.operation), "%s", operation);
    record.ref_id = ref; record.operator_id = operator_id; record.created_at = at;
    return sm_audit_record_create(uow, &record);
}

static int add_ok(int64_t a, int64_t b, int64_t *out) {
    if (b > 0 && a > INT64_MAX - b) return 0;
    if (b < 0 && a < INT64_MIN - b) return 0;
    *out = a + b; return 1;
}

sm_repo_status sm_finance_payable_for_purchase_uow(
    sm_repo_uow *uow, uint64_t supplier_id, const char *supplier_name,
    uint64_t purchase_id, int64_t amount_cents, int64_t created_at) {
    sm_supplier_finance finance; sm_payable payable, existing;
    sm_repo_status s; int64_t due_delta;
    if (!uow || !supplier_id || !supplier_name || !*supplier_name ||
        !purchase_id || amount_cents <= 0 || created_at < 0)
        return SM_REPO_ERR_INVALID;
    s = sm_payable_find_purchase(uow, purchase_id, &existing);
    if (s == SM_REPO_OK) return SM_REPO_CONFLICT;
    if (s != SM_REPO_NOT_FOUND) return s;
    s = sm_supplier_finance_get(uow, supplier_id, &finance);
    if (s == SM_REPO_NOT_FOUND) {
        memset(&finance, 0, sizeof(finance));
        finance.supplier_id = supplier_id; finance.rating = 'C';
        finance.updated_at = created_at; s = SM_REPO_OK;
    }
    if (s != SM_REPO_OK) return s;
    memset(&payable, 0, sizeof(payable));
    payable.supplier_id = supplier_id; payable.purchase_id = purchase_id;
    snprintf(payable.supplier_name, sizeof(payable.supplier_name), "%s",
             supplier_name);
    payable.amount_cents = payable.pending_cents = amount_cents;
    payable.status = PAYMENT_PENDING; payable.created_at = created_at;
    if (finance.payment_days > (uint32_t)(INT64_MAX / 86400))
        return SM_REPO_ERR_FULL;
    due_delta = (int64_t)finance.payment_days * 86400;
    if (!add_ok(created_at, due_delta, &payable.due_at)) return SM_REPO_ERR_FULL;
    s = sm_payable_create(uow, &payable);
    if (s == SM_REPO_OK &&
        (!add_ok(finance.total_cents, amount_cents, &finance.total_cents) ||
         !add_ok(finance.pending_cents, amount_cents, &finance.pending_cents)))
        s = SM_REPO_ERR_FULL;
    if (s == SM_REPO_OK) {
        finance.updated_at = created_at;
        s = sm_supplier_finance_put(uow, &finance);
    }
    return s;
}

sm_repo_status sm_vip_apply_uow(sm_repo_uow *uow, const char *card_no,
                                uint8_t type, int64_t amount_cents,
                                uint64_t sale_id, uint64_t operator_id,
                                const char *operator_name, const char *remark,
                                int64_t created_at,
                                sm_vip_transaction *out_transaction) {
    sm_vip_card card; sm_vip_transaction tx; sm_repo_status s;
    int64_t after;
    if (!uow || !card_no || !*card_no || type > 2u || amount_cents <= 0 ||
        !operator_name || !remark || created_at < 0)
        return SM_REPO_ERR_INVALID;
    s = sm_vip_card_get(uow, card_no, &card);
    if (s != SM_REPO_OK) return s;
    if ((type == 0u || type == 1u) && card.status != VIPCARD_ACTIVE)
        return SM_REPO_CONFLICT;
    if (type == 2u && card.status == VIPCARD_CANCELLED)
        return SM_REPO_CONFLICT;
    if ((type == 0u || type == 1u) && card.expired_at && created_at > card.expired_at)
        return SM_REPO_CONFLICT;
    if (type == 1u) {
        if (card.balance_cents < amount_cents) return SM_REPO_CONFLICT;
        after = card.balance_cents - amount_cents;
    } else {
        if (!add_ok(card.balance_cents, amount_cents, &after))
            return SM_REPO_ERR_FULL;
    }
    memset(&tx, 0, sizeof(tx));
    snprintf(tx.card_no, sizeof(tx.card_no), "%s", card_no);
    tx.type = type; tx.amount_cents = amount_cents;
    tx.balance_before_cents = card.balance_cents;
    tx.balance_after_cents = after; tx.operator_id = operator_id;
    snprintf(tx.operator_name, sizeof(tx.operator_name), "%s", operator_name);
    tx.sale_id = sale_id; snprintf(tx.remark, sizeof(tx.remark), "%s", remark);
    tx.created_at = created_at;
    card.balance_cents = after; card.updated_at = created_at;
    if (type == 0u &&
        !add_ok(card.total_recharged_cents, amount_cents,
                &card.total_recharged_cents)) return SM_REPO_ERR_FULL;
    s = sm_vip_card_update(uow, &card);
    if (s == SM_REPO_OK) s = sm_vip_transaction_create(uow, &tx);
    if (s == SM_REPO_OK && out_transaction) *out_transaction = tx;
    return s;
}

static int finance_from_entity(const sm_supplier_finance *v,
                               SupplierFinance *out) {
    if (!v || !out || v->supplier_id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out)); out->supplier_id = (int)v->supplier_id;
    out->payment_days = (float)v->payment_days; out->rating = (char)v->rating;
    out->total_amount = (float)((double)v->total_cents / 100.0);
    out->paid_amount = (float)((double)v->paid_cents / 100.0);
    out->pending_amount = (float)((double)v->pending_cents / 100.0);
    out->last_payment_date = (time_t)v->last_payment_at;
    out->updated_at = (time_t)v->updated_at; return 1;
}

static int payable_from_entity(const sm_payable *v, Payable *out) {
    if (!v || !out || v->id > INT_MAX || v->supplier_id > INT_MAX ||
        v->purchase_id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out)); out->id = (int)v->id;
    out->supplier_id = (int)v->supplier_id;
    snprintf(out->supplier_name, sizeof(out->supplier_name), "%s", v->supplier_name);
    out->purchase_id = (int)v->purchase_id;
    out->amount = (float)((double)v->amount_cents / 100.0);
    out->paid_amount = (float)((double)v->paid_cents / 100.0);
    out->pending_amount = (float)((double)v->pending_cents / 100.0);
    out->status = v->status; out->due_date = (time_t)v->due_at;
    out->created_at = (time_t)v->created_at; out->paid_at = (time_t)v->paid_at;
    return 1;
}

static int payment_from_entity(const sm_payment_record *v, PaymentRecord *out) {
    if (!v || !out || v->id > INT_MAX || v->payable_id > INT_MAX ||
        v->supplier_id > INT_MAX || v->operator_id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out)); out->id = (int)v->id;
    out->payable_id = (int)v->payable_id; out->supplier_id = (int)v->supplier_id;
    out->amount = (float)((double)v->amount_cents / 100.0);
    snprintf(out->method, sizeof(out->method), "%s", v->method);
    snprintf(out->reference, sizeof(out->reference), "%s", v->reference);
    out->operator_id = (int)v->operator_id;
    snprintf(out->operator_name, sizeof(out->operator_name), "%s", v->operator_name);
    snprintf(out->remark, sizeof(out->remark), "%s", v->remark);
    out->paid_at = (time_t)v->paid_at; return 1;
}

static int card_to_entity(const VipCard *v, sm_vip_card *out) {
    if (!v || !out || v->member_id < 0 || !memchr(v->card_no, '\0', sizeof(v->card_no)) ||
        !v->card_no[0] || v->balance < 0 || v->total_amount < 0) return 0;
    memset(out, 0, sizeof(*out)); snprintf(out->card_no, sizeof(out->card_no), "%s", v->card_no);
    out->member_id = (uint64_t)v->member_id;
    out->balance_cents = (int64_t)((double)v->balance * 100.0 + 0.5);
    out->total_recharged_cents = (int64_t)((double)v->total_amount * 100.0 + 0.5);
    out->card_type = (uint8_t)v->card_type; out->status = (uint8_t)v->status;
    out->created_at = v->created_at; out->updated_at = v->updated_at;
    out->expired_at = v->expired_at;
    snprintf(out->password_hash, sizeof(out->password_hash), "%s", v->password_hash);
    snprintf(out->password_salt, sizeof(out->password_salt), "%s", v->password_salt);
    return 1;
}

static int card_from_entity(const sm_vip_card *v, VipCard *out) {
    if (!v || !out || v->member_id > INT_MAX) return 0;
    memset(out, 0, sizeof(*out)); snprintf(out->card_no, sizeof(out->card_no), "%s", v->card_no);
    out->member_id = (int)v->member_id;
    out->balance = (float)((double)v->balance_cents / 100.0);
    out->total_amount = (float)((double)v->total_recharged_cents / 100.0);
    out->card_type = v->card_type; out->status = v->status;
    out->created_at = (time_t)v->created_at; out->updated_at = (time_t)v->updated_at;
    out->expired_at = (time_t)v->expired_at;
    snprintf(out->password_hash, sizeof(out->password_hash), "%s", v->password_hash);
    snprintf(out->password_salt, sizeof(out->password_salt), "%s", v->password_salt);
    return 1;
}

static int tx_from_entity(const sm_vip_transaction *v, VipCardTransaction *out) {
    if (!v || !out || v->id > INT_MAX || v->operator_id > INT_MAX || v->sale_id > INT_MAX)
        return 0;
    memset(out, 0, sizeof(*out)); out->id = (int)v->id;
    snprintf(out->card_no, sizeof(out->card_no), "%s", v->card_no);
    out->type = v->type; out->amount = (float)((double)v->amount_cents / 100.0);
    out->balance_before = (float)((double)v->balance_before_cents / 100.0);
    out->balance_after = (float)((double)v->balance_after_cents / 100.0);
    out->operator_id = (int)v->operator_id;
    snprintf(out->operator_name, sizeof(out->operator_name), "%s", v->operator_name);
    out->sale_id = (int)v->sale_id; snprintf(out->remark, sizeof(out->remark), "%s", v->remark);
    out->created_at = (time_t)v->created_at; return 1;
}

sm_repo_status sm_service_supplier_finance_update(uint64_t id, uint32_t days,
                                                  uint8_t rating) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_supplier_finance v; sm_repo_status s;
    if (!repo || !id || !rating) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_supplier_finance_get(uow, id, &v);
    if (s == SM_REPO_NOT_FOUND) { memset(&v, 0, sizeof(v)); v.supplier_id = id; s = SM_REPO_OK; }
    if (s == SM_REPO_OK) { v.payment_days = days; v.rating = rating; v.updated_at = time(NULL); s = sm_supplier_finance_put(uow, &v); }
    return finish(&uow, s);
}

sm_repo_status sm_service_supplier_finance_get(uint64_t id, SupplierFinance *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_supplier_finance v; sm_repo_status s;
    if (!repo || !id || !out) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 0, &uow);
    if (s == SM_REPO_OK) s = sm_supplier_finance_get(uow, id, &v);
    s = finish(&uow, s);
    return s == SM_REPO_OK && !finance_from_entity(&v, out) ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_supplier_finance_list(SupplierFinance **out, size_t *count) {
    sm_supplier_finance *src = NULL; SupplierFinance *dst = NULL;
    size_t n = 0, i; sm_repo_status s; sm_repository *repo = sm_app_repository();
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    *out = NULL; *count = 0; s = sm_supplier_finance_list(repo, &src, &n);
    if (s == SM_REPO_OK && n && !(dst = calloc(n, sizeof(*dst)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < n; ++i)
        if (!finance_from_entity(&src[i], &dst[i])) s = SM_REPO_ERR_CORRUPT;
    free(src); if (s != SM_REPO_OK) { free(dst); return s; }
    *out = dst; *count = n; return s;
}

sm_repo_status sm_service_payable_create(uint64_t supplier_id, int64_t cents,
                                         const char *name, Payable *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_supplier_finance fin; sm_payable v; sm_repo_status s;
    if (!repo || !supplier_id || cents <= 0 || !name || !*name || !out)
        return SM_REPO_ERR_INVALID;
    memset(&v, 0, sizeof(v)); v.supplier_id = supplier_id;
    snprintf(v.supplier_name, sizeof(v.supplier_name), "%s", name);
    v.amount_cents = v.pending_cents = cents; v.status = PAYMENT_PENDING;
    v.created_at = time(NULL);
    s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK) s = sm_supplier_finance_get(uow, supplier_id, &fin);
    if (s == SM_REPO_NOT_FOUND) { memset(&fin, 0, sizeof(fin)); fin.supplier_id = supplier_id; fin.rating = 'C'; s = SM_REPO_OK; }
    if (s == SM_REPO_OK) v.due_at = v.created_at + (int64_t)fin.payment_days * 86400;
    if (s == SM_REPO_OK) s = sm_payable_create(uow, &v);
    if (s == SM_REPO_OK && (!add_ok(fin.total_cents, cents, &fin.total_cents) ||
                            !add_ok(fin.pending_cents, cents, &fin.pending_cents))) s = SM_REPO_ERR_FULL;
    if (s == SM_REPO_OK) { fin.updated_at = v.created_at; s = sm_supplier_finance_put(uow, &fin); }
    if (s == SM_REPO_OK)
        s = finance_audit(uow, "PAYABLE", v.id, "CREATE", 0,
                          v.created_at);
    s = finish(&uow, s);
    return s == SM_REPO_OK && !payable_from_entity(&v, out) ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_payable_get(uint64_t id, Payable *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_payable v; sm_repo_status s;
    if (!repo || !id || !out) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 0, &uow); if (s == SM_REPO_OK) s = sm_payable_get(uow, id, &v);
    s = finish(&uow, s); return s == SM_REPO_OK && !payable_from_entity(&v, out) ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_payable_list(uint64_t supplier_id, int status,
                                       Payable **out, size_t *count) {
    sm_payable *src = NULL; Payable *dst = NULL; size_t n = 0, i;
    sm_repo_status s; sm_repository *repo = sm_app_repository();
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    *out = NULL; *count = 0; s = sm_payable_list(repo, supplier_id, status, &src, &n);
    if (s == SM_REPO_OK && n && !(dst = calloc(n, sizeof(*dst)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < n; ++i)
        if (!payable_from_entity(&src[i], &dst[i])) s = SM_REPO_ERR_CORRUPT;
    free(src); if (s != SM_REPO_OK) { free(dst); return s; }
    *out = dst; *count = n; return s;
}

sm_repo_status sm_service_payment_record(uint64_t payable_id, int64_t cents,
                                         const char *method, const char *reference,
                                         uint64_t operator_id, const char *remark,
                                         PaymentRecord *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_payable payable; sm_supplier_finance fin; sm_payment_record record;
    sm_employee employee; sm_repo_status s; int64_t now = time(NULL);
    if (!repo || !payable_id || cents <= 0 || !method || !*method || !reference || !remark || !out)
        return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 1, &uow); if (s == SM_REPO_OK) s = sm_payable_get(uow, payable_id, &payable);
    if (s == SM_REPO_OK && (payable.status == PAYMENT_COMPLETED || cents > payable.pending_cents)) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) s = sm_supplier_finance_get(uow, payable.supplier_id, &fin);
    memset(&record, 0, sizeof(record));
    if (s == SM_REPO_OK) {
        record.payable_id = payable.id; record.supplier_id = payable.supplier_id;
        record.amount_cents = cents; snprintf(record.method, sizeof(record.method), "%s", method);
        snprintf(record.reference, sizeof(record.reference), "%s", reference);
        record.operator_id = operator_id; snprintf(record.remark, sizeof(record.remark), "%s", remark);
        record.paid_at = now;
        if (operator_id && sm_employee_get(uow, operator_id, &employee) == SM_REPO_OK)
            snprintf(record.operator_name, sizeof(record.operator_name), "%s", employee.name);
        payable.paid_cents += cents; payable.pending_cents -= cents;
        payable.status = payable.pending_cents ? PAYMENT_PARTIAL : PAYMENT_COMPLETED;
        if (!payable.pending_cents) payable.paid_at = now;
        fin.paid_cents += cents; fin.pending_cents -= cents;
        fin.last_payment_at = fin.updated_at = now;
        s = sm_payment_record_create(uow, &record);
    }
    if (s == SM_REPO_OK) s = sm_payable_update(uow, &payable);
    if (s == SM_REPO_OK) s = sm_supplier_finance_put(uow, &fin);
    if (s == SM_REPO_OK)
        s = finance_audit(uow, "PAYMENT", record.id, "PAY", operator_id,
                          record.paid_at);
    s = finish(&uow, s);
    return s == SM_REPO_OK && !payment_from_entity(&record, out) ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_payment_list(uint64_t payable_id, uint64_t supplier_id,
                                       PaymentRecord **out, size_t *count) {
    sm_payment_record *src = NULL; PaymentRecord *dst = NULL; size_t n = 0, i;
    sm_repo_status s; sm_repository *repo = sm_app_repository();
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    *out = NULL; *count = 0; s = sm_payment_record_list(repo, payable_id, supplier_id, &src, &n);
    if (s == SM_REPO_OK && n && !(dst = calloc(n, sizeof(*dst)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < n; ++i)
        if (!payment_from_entity(&src[i], &dst[i])) s = SM_REPO_ERR_CORRUPT;
    free(src); if (s != SM_REPO_OK) { free(dst); return s; }
    *out = dst; *count = n; return s;
}

static int payable_due_compare(const void *left, const void *right) {
    const sm_payable *a = left, *b = right;
    if (a->due_at < b->due_at) return -1;
    if (a->due_at > b->due_at) return 1;
    return a->id < b->id ? -1 : a->id > b->id;
}

sm_repo_status sm_service_supplier_settle(uint64_t supplier_id,
                                          int64_t amount_cents,
                                          const char *method,
                                          const char *reference,
                                          uint64_t operator_id,
                                          const char *remark,
                                          int64_t *out_applied_cents) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_payable *payables = NULL; sm_supplier_finance finance;
    sm_employee employee; const char *operator_name = "";
    size_t count = 0, i; int64_t remaining = amount_cents, applied = 0;
    sm_repo_status s;
    if (out_applied_cents) *out_applied_cents = 0;
    if (!repo || !supplier_id || amount_cents <= 0 || !method || !*method ||
        !reference || !remark) return SM_REPO_ERR_INVALID;
    s = sm_payable_list(repo, supplier_id, -1, &payables, &count);
    if (s == SM_REPO_OK)
        qsort(payables, count, sizeof(*payables), payable_due_compare);
    if (s == SM_REPO_OK) s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK)
        s = sm_supplier_finance_get(uow, supplier_id, &finance);
    if (s == SM_REPO_OK && operator_id &&
        sm_employee_get(uow, operator_id, &employee) == SM_REPO_OK)
        operator_name = employee.name;
    for (i = 0; s == SM_REPO_OK && i < count && remaining > 0; ++i) {
        sm_payable current; sm_payment_record record;
        int64_t payment;
        if (payables[i].status == PAYMENT_COMPLETED) continue;
        s = sm_payable_get(uow, payables[i].id, &current);
        if (s != SM_REPO_OK) break;
        payment = current.pending_cents < remaining
                      ? current.pending_cents : remaining;
        if (payment <= 0) continue;
        memset(&record, 0, sizeof(record));
        record.payable_id = current.id;
        record.supplier_id = current.supplier_id;
        record.amount_cents = payment;
        snprintf(record.method, sizeof(record.method), "%s", method);
        snprintf(record.reference, sizeof(record.reference), "%s", reference);
        record.operator_id = operator_id;
        snprintf(record.operator_name, sizeof(record.operator_name), "%s",
                 operator_name);
        snprintf(record.remark, sizeof(record.remark), "%s", remark);
        record.paid_at = time(NULL);
        current.paid_cents += payment;
        current.pending_cents -= payment;
        current.status = current.pending_cents ? PAYMENT_PARTIAL
                                               : PAYMENT_COMPLETED;
        if (!current.pending_cents) current.paid_at = record.paid_at;
        s = sm_payment_record_create(uow, &record);
        if (s == SM_REPO_OK) s = sm_payable_update(uow, &current);
        if (s == SM_REPO_OK) {
            remaining -= payment;
            applied += payment;
        }
    }
    if (s == SM_REPO_OK && applied == 0) s = SM_REPO_CONFLICT;
    if (s == SM_REPO_OK) {
        if (finance.pending_cents < applied ||
            finance.paid_cents > INT64_MAX - applied)
            s = SM_REPO_ERR_CORRUPT;
        else {
            finance.pending_cents -= applied;
            finance.paid_cents += applied;
            finance.last_payment_at = finance.updated_at = time(NULL);
            s = sm_supplier_finance_put(uow, &finance);
        }
    }
    if (s == SM_REPO_OK)
        s = finance_audit(uow, "SUPPLIER", supplier_id, "SETTLE",
                          operator_id, finance.updated_at);
    free(payables); s = finish(&uow, s);
    if (s == SM_REPO_OK && out_applied_cents) *out_applied_cents = applied;
    return s;
}

sm_repo_status sm_service_vip_card_create(VipCard *v) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_vip_card entity; sm_repo_status s;
    if (!repo || !card_to_entity(v, &entity)) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 1, &uow); if (s == SM_REPO_OK) s = sm_vip_card_create(uow, &entity);
    if (s == SM_REPO_OK)
        s = finance_audit(uow, "VIPCARD", 0, "CREATE", 0,
                          entity.created_at);
    return finish(&uow, s);
}

sm_repo_status sm_service_vip_card_get(const char *no, VipCard *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_vip_card v; sm_repo_status s;
    if (!repo || !no || !out) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 0, &uow); if (s == SM_REPO_OK) s = sm_vip_card_get(uow, no, &v);
    s = finish(&uow, s); return s == SM_REPO_OK && !card_from_entity(&v, out) ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_vip_card_update(const VipCard *v) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_vip_card entity; sm_repo_status s;
    if (!repo || !card_to_entity(v, &entity)) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 1, &uow); if (s == SM_REPO_OK) s = sm_vip_card_update(uow, &entity);
    if (s == SM_REPO_OK)
        s = finance_audit(uow, "VIPCARD", 0, "UPDATE", 0,
                          entity.updated_at);
    return finish(&uow, s);
}

sm_repo_status sm_service_vip_card_list(uint64_t member_id, int status,
                                        VipCard **out, size_t *count) {
    sm_vip_card *src = NULL; VipCard *dst = NULL; size_t n = 0, i;
    sm_repo_status s; sm_repository *repo = sm_app_repository();
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    *out = NULL; *count = 0; s = sm_vip_card_list(repo, member_id, status, &src, &n);
    if (s == SM_REPO_OK && n && !(dst = calloc(n, sizeof(*dst)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < n; ++i)
        if (!card_from_entity(&src[i], &dst[i])) s = SM_REPO_ERR_CORRUPT;
    free(src); if (s != SM_REPO_OK) { free(dst); return s; }
    *out = dst; *count = n; return s;
}

sm_repo_status sm_service_vip_apply(const char *no, int type, int64_t cents,
                                    uint64_t sale_id, uint64_t operator_id,
                                    const char *remark, VipCardTransaction *out) {
    sm_repository *repo = sm_app_repository(); sm_repo_uow *uow = NULL;
    sm_vip_transaction tx; sm_employee employee; const char *name = "";
    sm_repo_status s;
    if (!repo || !out || type < 0 || type > 2) return SM_REPO_ERR_INVALID;
    s = sm_repo_uow_begin(repo, 1, &uow);
    if (s == SM_REPO_OK && operator_id && sm_employee_get(uow, operator_id, &employee) == SM_REPO_OK)
        name = employee.name;
    if (s == SM_REPO_OK) s = sm_vip_apply_uow(uow, no, (uint8_t)type, cents,
                                              sale_id, operator_id, name,
                                              remark ? remark : "", time(NULL), &tx);
    if (s == SM_REPO_OK)
        s = finance_audit(uow, "VIPCARD", tx.id,
                          type == 0 ? "RECHARGE" : type == 1 ? "CONSUME" : "REFUND",
                          operator_id, tx.created_at);
    s = finish(&uow, s); return s == SM_REPO_OK && !tx_from_entity(&tx, out) ? SM_REPO_ERR_CORRUPT : s;
}

sm_repo_status sm_service_vip_transaction_list(const char *no,
                                               VipCardTransaction **out,
                                               size_t *count) {
    sm_vip_transaction *src = NULL; VipCardTransaction *dst = NULL;
    size_t n = 0, i; sm_repo_status s; sm_repository *repo = sm_app_repository();
    if (!repo || !out || !count) return SM_REPO_ERR_INVALID;
    *out = NULL; *count = 0; s = sm_vip_transaction_list(repo, no, &src, &n);
    if (s == SM_REPO_OK && n && !(dst = calloc(n, sizeof(*dst)))) s = SM_REPO_ERR_NOMEM;
    for (i = 0; s == SM_REPO_OK && i < n; ++i)
        if (!tx_from_entity(&src[i], &dst[i])) s = SM_REPO_ERR_CORRUPT;
    free(src); if (s != SM_REPO_OK) { free(dst); return s; }
    *out = dst; *count = n; return s;
}

void sm_service_finance_array_free(void *array) { free(array); }
