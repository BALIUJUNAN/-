#include "app/sm_finance_service.h"

#include "app/sm_app_context.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SupplierFinance *finance_cache;
static Payable *payable_cache;
static PaymentRecord *payment_cache;

static int to_cents(float amount, int64_t *out) {
    double scaled;
    if (!out || !isfinite(amount) || amount <= 0.0f) return 0;
    scaled = (double)amount * 100.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5); return 1;
}

static void clear_finances(void) {
    while (finance_cache) { SupplierFinance *next = finance_cache->next; free(finance_cache); finance_cache = next; }
}

static void clear_payables(void) {
    while (payable_cache) { Payable *next = payable_cache->next; free(payable_cache); payable_cache = next; }
}

static void clear_payments(void) {
    while (payment_cache) { PaymentRecord *next = payment_cache->next; free(payment_cache); payment_cache = next; }
}

int load_supplier_finances(void) {
    SupplierFinance *values = NULL; size_t count = 0, i;
    clear_finances();
    if (sm_service_supplier_finance_list(&values, &count) != SM_REPO_OK) return -1;
    for (i = count; i > 0; --i) {
        SupplierFinance *node = malloc(sizeof(*node));
        if (!node) { free(values); clear_finances(); return -1; }
        *node = values[i - 1u]; node->next = finance_cache; finance_cache = node;
    }
    free(values); return 0;
}

int load_payables(void) {
    Payable *values = NULL; size_t count = 0, i;
    clear_payables();
    if (sm_service_payable_list(0, -1, &values, &count) != SM_REPO_OK) return -1;
    for (i = count; i > 0; --i) {
        Payable *node = malloc(sizeof(*node));
        if (!node) { free(values); clear_payables(); return -1; }
        *node = values[i - 1u]; node->next = payable_cache; payable_cache = node;
    }
    free(values); return 0;
}

int load_payment_records(void) {
    PaymentRecord *values = NULL; size_t count = 0, i;
    clear_payments();
    if (sm_service_payment_list(0, 0, &values, &count) != SM_REPO_OK) return -1;
    for (i = count; i > 0; --i) {
        PaymentRecord *node = malloc(sizeof(*node));
        if (!node) { free(values); clear_payments(); return -1; }
        *node = values[i - 1u]; node->next = payment_cache; payment_cache = node;
    }
    free(values); return 0;
}

SupplierFinance *get_supplier_finance(int supplier_id) {
    SupplierFinance *item = finance_cache;
    while (item && item->supplier_id != supplier_id) item = item->next;
    return item;
}

int update_supplier_finance(int supplier_id, float payment_days, char rating) {
    uint32_t days;
    if (supplier_id <= 0 || !isfinite(payment_days) || payment_days < 0 ||
        payment_days > UINT32_MAX || floor(payment_days) != payment_days)
        return -1;
    days = (uint32_t)payment_days;
    if (sm_service_supplier_finance_update((uint64_t)supplier_id, days,
                                           (uint8_t)rating) != SM_REPO_OK)
        return -1;
    return load_supplier_finances();
}

SupplierFinance **list_supplier_finances(int *count) {
    SupplierFinance **list = NULL; SupplierFinance *item; int n = 0;
    if (!count) return NULL;
    for (item = finance_cache; item; item = item->next) ++n;
    if (n && !(list = malloc((size_t)n * sizeof(*list)))) { *count = 0; return NULL; }
    n = 0; for (item = finance_cache; item; item = item->next) list[n++] = item;
    *count = n; return list;
}

Payable *find_payable(int id) {
    Payable *item = payable_cache;
    while (item && item->id != id) item = item->next;
    return item;
}

static Payable **list_payables(int supplier_id, int pending_only, int *count) {
    Payable **list = NULL; Payable *item; int n = 0, cap = 0;
    if (!count) return NULL;
    *count = 0;
    for (item = payable_cache; item; item = item->next) {
        if ((supplier_id && item->supplier_id != supplier_id) ||
            (pending_only && item->status == PAYMENT_COMPLETED)) continue;
        if (n == cap) {
            int next = cap ? cap * 2 : 16; Payable **grown = realloc(list, (size_t)next * sizeof(*list));
            if (!grown) { free(list); return NULL; } list = grown; cap = next;
        }
        list[n++] = item;
    }
    *count = n; return list;
}

Payable **list_payables_by_supplier(int supplier_id, int *count) {
    return list_payables(supplier_id, 0, count);
}

Payable **list_pending_payables(int *count) {
    return list_payables(0, 1, count);
}

int generate_payable(int purchase_id) {
    Payable *item;
    for (item = payable_cache; item; item = item->next)
        if (item->purchase_id == purchase_id) return item->id;
    return -1;
}

int create_payable(int supplier_id, float amount, const char *remark) {
    Supplier *supplier; Payable created; int64_t cents;
    (void)remark;
    if (!to_cents(amount, &cents) || !(supplier = find_supplier_by_id(supplier_id)))
        return -1;
    if (sm_service_payable_create((uint64_t)supplier_id, cents, supplier->name,
                                  &created) != SM_REPO_OK)
        return -1;
    if (load_payables() || load_supplier_finances()) return -1;
    return created.id;
}

int record_payment(int payable_id, float amount, const char *method,
                   const char *reference, int operator_id, const char *remark) {
    PaymentRecord record; int64_t cents;
    if (!to_cents(amount, &cents) ||
        sm_service_payment_record((uint64_t)payable_id, cents,
                                  method ? method : "bank transfer",
                                  reference ? reference : "",
                                  operator_id > 0 ? (uint64_t)operator_id : 0,
                                  remark ? remark : "", &record) != SM_REPO_OK)
        return -1;
    return load_payables() || load_supplier_finances() || load_payment_records()
               ? -1 : 0;
}

int settle_supplier(int supplier_id, float amount, const char *method,
                    const char *reference, int operator_id, const char *remark) {
    int64_t cents, applied;
    if (!to_cents(amount, &cents) ||
        sm_service_supplier_settle(
            (uint64_t)supplier_id, cents, method ? method : "bank transfer",
            reference ? reference : "",
            operator_id > 0 ? (uint64_t)operator_id : 0,
            remark ? remark : "", &applied) != SM_REPO_OK)
        return -1;
    return load_payables() || load_supplier_finances() || load_payment_records()
               ? -1 : 0;
}

static PaymentRecord **list_payments(int supplier_id, int *count) {
    PaymentRecord **list = NULL; PaymentRecord *item; int n = 0, cap = 0;
    if (!count) return NULL;
    *count = 0;
    for (item = payment_cache; item; item = item->next) {
        if (supplier_id && item->supplier_id != supplier_id) continue;
        if (n == cap) {
            int next = cap ? cap * 2 : 16;
            PaymentRecord **grown = realloc(list, (size_t)next * sizeof(*list));
            if (!grown) { free(list); return NULL; } list = grown; cap = next;
        }
        list[n++] = item;
    }
    *count = n; return list;
}

PaymentRecord **list_payment_records(int *count) { return list_payments(0, count); }
PaymentRecord **list_supplier_payment_records(int supplier_id, int *count) {
    return list_payments(supplier_id, count);
}

void generate_supplier_statement(int supplier_id, time_t start, time_t end) {
    Payable *p; PaymentRecord *r;
    printf("\nSupplier statement #%d\n", supplier_id);
    for (p = payable_cache; p; p = p->next)
        if (p->supplier_id == supplier_id && p->created_at >= start && p->created_at <= end)
            printf("Payable #%d amount %.2f paid %.2f pending %.2f\n",
                   p->id, p->amount, p->paid_amount, p->pending_amount);
    for (r = payment_cache; r; r = r->next)
        if (r->supplier_id == supplier_id && r->paid_at >= start && r->paid_at <= end)
            printf("Payment #%d %.2f %s\n", r->id, r->amount, r->method);
}

void print_payables_summary(void) {
    SupplierFinance *f;
    printf("\nSupplier payables summary\n");
    for (f = finance_cache; f; f = f->next)
        printf("Supplier %d total %.2f paid %.2f pending %.2f rating %c\n",
               f->supplier_id, f->total_amount, f->paid_amount,
               f->pending_amount, f->rating);
}

int save_supplier_finance(SupplierFinance *fin) {
    return fin && update_supplier_finance(fin->supplier_id, fin->payment_days,
                                          fin->rating) == 0 ? 0 : -1;
}

int save_payable(Payable *payable) {
    return payable && sm_app_repository() ? 0 : -1;
}

int save_payment_record(PaymentRecord *record) {
    return record && sm_app_repository() ? 0 : -1;
}
