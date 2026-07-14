#include "supermarket.h"

#include "app/sm_app_context.h"
#include "app/sm_operations_service.h"
#include "repo/sm_operations_repository.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Schedule *schedule_cache;
static DailySettlement *settlement_cache;

static int64_t cents(float value) { return (int64_t)floor((double)value * 100.0 + (value >= 0 ? 0.5 : -0.5)); }
static float amount(int64_t value) { return (float)((double)value / 100.0); }
static uint32_t bps(float value) { double v = value * 10000.0; return v <= 0 ? 0u : v >= UINT32_MAX ? UINT32_MAX : (uint32_t)floor(v + 0.5); }

static void free_promotions(void) { while (g_promotions) { Promotion *n = g_promotions->next; free(g_promotions); g_promotions = n; } }

static void promotion_to_legacy(const sm_promotion_record *src, Promotion *dst) {
    memset(dst, 0, sizeof(*dst)); dst->id = (int)src->id;
    snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    dst->type = src->type; snprintf(dst->product_id, sizeof(dst->product_id), "%s", src->product_id);
    dst->discount_rate = (float)src->discount_bps / 10000.0f;
    dst->threshold = amount(src->threshold_cents); dst->discount_amount = amount(src->discount_cents);
    dst->nth_item = (int)src->nth_item; dst->nth_discount_rate = (float)src->nth_discount_bps / 10000.0f;
    dst->buy_quantity = (int)src->buy_quantity; dst->free_quantity = (int)src->free_quantity;
    dst->member_level = (int)src->member_level; dst->member_price = amount(src->member_price_cents);
    dst->start_time = (time_t)src->start_at; dst->end_time = (time_t)src->end_at;
    dst->status = src->status; dst->priority = (int)src->priority; dst->created_at = (time_t)src->created_at;
}

static int promotion_from_legacy(const Promotion *src, sm_promotion_record *dst) {
    if (!src || src->id < 0 || src->type < 0 || src->status < 0 || src->priority < 0 ||
        src->nth_item < 0 || src->buy_quantity < 0 || src->free_quantity < 0 || src->member_level < 0)
        return 0;
    memset(dst, 0, sizeof(*dst)); dst->id = (uint64_t)src->id;
    snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    dst->type = (uint8_t)src->type; snprintf(dst->product_id, sizeof(dst->product_id), "%s", src->product_id);
    dst->discount_bps = bps(src->discount_rate); dst->threshold_cents = cents(src->threshold);
    dst->discount_cents = cents(src->discount_amount); dst->nth_item = (uint32_t)src->nth_item;
    dst->nth_discount_bps = bps(src->nth_discount_rate); dst->buy_quantity = (uint32_t)src->buy_quantity;
    dst->free_quantity = (uint32_t)src->free_quantity; dst->member_level = (uint32_t)src->member_level;
    dst->member_price_cents = cents(src->member_price); dst->start_at = (int64_t)src->start_time;
    dst->end_at = (int64_t)src->end_time; dst->status = (uint8_t)src->status;
    dst->priority = (uint32_t)src->priority; dst->created_at = (int64_t)src->created_at;
    return 1;
}

static int refresh_promotions(void) {
    sm_promotion_record *values = NULL; size_t count = 0, i;
    if (sm_service_promotion_list(NULL, 0, &values, &count) != SM_REPO_OK) return -1;
    free_promotions();
    for (i = 0; i < count; ++i) {
        Promotion *node = malloc(sizeof(*node)); Promotion **at = &g_promotions;
        if (!node) { sm_operations_array_free(values); return -1; }
        promotion_to_legacy(&values[i], node);
        while (*at && (*at)->priority >= node->priority) at = &(*at)->next;
        node->next = *at; *at = node;
    }
    sm_operations_array_free(values); return 0;
}

int create_promotion(Promotion *promotion) {
    sm_promotion_record value;
    if (!promotion || !promotion_from_legacy(promotion, &value)) return -1;
    value.id = 0; if (!value.created_at) value.created_at = (int64_t)time(NULL);
    if (sm_service_promotion_create(&value) != SM_REPO_OK || value.id > INT_MAX) return -1;
    promotion->id = (int)value.id; promotion->created_at = (time_t)value.created_at;
    return refresh_promotions() == 0 ? promotion->id : -1;
}

int create_discount_promotion(const char *name, const char *product_id,
                              float rate, time_t start, time_t end) {
    Promotion p; memset(&p, 0, sizeof(p)); snprintf(p.name, sizeof(p.name), "%s", name ? name : "");
    snprintf(p.product_id, sizeof(p.product_id), "%s", product_id ? product_id : "");
    p.type = PROMOTION_TYPE_DISCOUNT; p.discount_rate = rate; p.start_time = start;
    p.end_time = end; p.status = PROMOTION_ACTIVE; return create_promotion(&p);
}

int create_override_promotion(const char *name, float threshold,
                              float discount, time_t start, time_t end) {
    Promotion p; memset(&p, 0, sizeof(p)); snprintf(p.name, sizeof(p.name), "%s", name ? name : "");
    p.type = PROMOTION_TYPE_OVERRIDE; p.threshold = threshold; p.discount_amount = discount;
    p.start_time = start; p.end_time = end; p.status = PROMOTION_ACTIVE;
    return create_promotion(&p);
}

Promotion *find_promotion(int id) { Promotion *p; for (p = g_promotions; p; p = p->next) if (p->id == id) return p; return NULL; }
int is_promotion_valid(Promotion *p) { time_t now = time(NULL); return p && p->status == PROMOTION_ACTIVE && now >= p->start_time && now <= p->end_time; }
Promotion *get_product_promotion(const char *product_id) { Promotion *p; if (!product_id) return NULL; for (p = g_promotions; p; p = p->next) if (!strcmp(p->product_id, product_id) && is_promotion_valid(p)) return p; return NULL; }
Promotion *get_order_override_promotion(void) { Promotion *p; for (p = g_promotions; p; p = p->next) if (p->type == PROMOTION_TYPE_OVERRIDE && is_promotion_valid(p)) return p; return NULL; }

int delete_promotion(int id) { return sm_service_promotion_delete((uint64_t)id) == SM_REPO_OK && refresh_promotions() == 0 ? 0 : -1; }

static Promotion **promotion_list(int active, int *count) {
    Promotion **out = NULL; Promotion *p; int n = 0, i = 0;
    if (!count) return NULL;
    for (p = g_promotions; p; p = p->next) if (!active || is_promotion_valid(p)) ++n;
    if (n) { out = malloc((size_t)n * sizeof(*out)); if (!out) { *count = 0; return NULL; } }
    for (p = g_promotions; p; p = p->next) if (!active || is_promotion_valid(p)) out[i++] = p;
    *count = n; return out;
}
Promotion **list_promotions(int *count) { return promotion_list(0, count); }
Promotion **list_active_promotions(int *count) { return promotion_list(1, count); }
int load_promotions(void) { return sm_app_repository() ? refresh_promotions() : -1; }
int save_promotion(Promotion *p) { sm_promotion_record v; return promotion_from_legacy(p, &v) && sm_service_promotion_update(&v) == SM_REPO_OK && refresh_promotions() == 0 ? 0 : -1; }
int save_all_promotions(void) { return sm_app_repository() ? 0 : -1; }

static void free_combos(void) {
    while (g_combos) {
        ProductCombo *next = g_combos->next; ComboItem *item = g_combos->items;
        while (item) { ComboItem *item_next = item->next; free(item); item = item_next; }
        free(g_combos); g_combos = next;
    }
    if (g_combo_barcode_hash) { hash_destroy(g_combo_barcode_hash, NULL); g_combo_barcode_hash = NULL; }
}

static void combo_to_legacy(const sm_combo_record *src, ProductCombo *dst) {
    memset(dst, 0, sizeof(*dst)); dst->id = (int)src->id;
    snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    snprintf(dst->barcode, sizeof(dst->barcode), "%s", src->barcode);
    dst->price = amount(src->price_cents); dst->cost = amount(src->cost_cents);
    dst->status = src->status; dst->created_at = (time_t)src->created_at;
    dst->updated_at = (time_t)src->updated_at;
}

static int combo_from_legacy(const ProductCombo *src, sm_combo_record *dst) {
    if (!src || src->id < 0 || src->status < 0) return 0;
    memset(dst, 0, sizeof(*dst)); dst->id = (uint64_t)src->id;
    snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    snprintf(dst->barcode, sizeof(dst->barcode), "%s", src->barcode);
    dst->price_cents = cents(src->price); dst->cost_cents = cents(src->cost);
    dst->status = (uint8_t)src->status; dst->created_at = (int64_t)src->created_at;
    dst->updated_at = (int64_t)src->updated_at; return 1;
}

static int refresh_combos(void) {
    sm_combo_record *values = NULL; sm_combo_item_record *item_values = NULL;
    size_t count = 0, item_count = 0, i, j;
    if (sm_service_combo_list(-1, &values, &count) != SM_REPO_OK) return -1;
    free_combos(); g_combo_barcode_hash = hash_create(HASH_TABLE_SIZE);
    if (!g_combo_barcode_hash) { sm_operations_array_free(values); return -1; }
    for (i = count; i > 0; --i) {
        ProductCombo *node = malloc(sizeof(*node));
        if (!node) { sm_operations_array_free(values); return -1; }
        combo_to_legacy(&values[i - 1], node); node->next = g_combos; g_combos = node;
        (void)hash_insert(g_combo_barcode_hash, node->barcode, node);
    }
    for (ProductCombo *combo = g_combos; combo; combo = combo->next) {
        if (sm_service_combo_item_list((uint64_t)combo->id, &item_values,
                                       &item_count) != SM_REPO_OK) {
            sm_operations_array_free(values); return -1;
        }
        for (j = item_count; j > 0; --j) {
            ComboItem *node = calloc(1, sizeof(*node));
            if (!node) { sm_operations_array_free(item_values); sm_operations_array_free(values); return -1; }
            snprintf(node->product_id, sizeof(node->product_id), "%s", item_values[j - 1].product_id);
            snprintf(node->product_name, sizeof(node->product_name), "%s", item_values[j - 1].product_name);
            node->quantity = (int)item_values[j - 1].quantity;
            node->ratio = (float)item_values[j - 1].ratio_bps / 10000.0f;
            node->next = combo->items; combo->items = node;
        }
        sm_operations_array_free(item_values); item_values = NULL; item_count = 0;
    }
    sm_operations_array_free(values); return 0;
}

int create_combo(ProductCombo *combo) {
    sm_combo_record value;
    if (!combo || !combo_from_legacy(combo, &value)) return -1;
    value.id = 0; value.status = COMBO_ACTIVE;
    if (!value.created_at) value.created_at = (int64_t)time(NULL);
    value.updated_at = value.created_at;
    if (sm_service_combo_create(&value) != SM_REPO_OK || value.id > INT_MAX) return -1;
    combo->id = (int)value.id; combo->created_at = (time_t)value.created_at;
    combo->updated_at = (time_t)value.updated_at;
    return refresh_combos() == 0 ? combo->id : -1;
}

int add_combo_item(int combo_id, ComboItem *item) {
    sm_combo_item_record value;
    if (combo_id <= 0 || !item || item->quantity <= 0) return -1;
    memset(&value, 0, sizeof(value));
    snprintf(value.product_id, sizeof(value.product_id), "%s", item->product_id);
    snprintf(value.product_name, sizeof(value.product_name), "%s", item->product_name);
    value.quantity = (uint32_t)item->quantity; value.ratio_bps = bps(item->ratio);
    return sm_service_combo_add_item((uint64_t)combo_id, &value) == SM_REPO_OK &&
           refresh_combos() == 0 ? 0 : -1;
}

ProductCombo *find_combo_by_id(int id) { ProductCombo *p; for (p = g_combos; p; p = p->next) if (p->id == id) return p; return NULL; }
ProductCombo *find_combo_by_barcode(const char *barcode) { ProductCombo *p; if (!barcode) return NULL; for (p = g_combos; p; p = p->next) if (!strcmp(p->barcode, barcode)) return p; return NULL; }
int update_combo(ProductCombo *combo) { sm_combo_record value; if (!combo_from_legacy(combo, &value)) return -1; value.updated_at = (int64_t)time(NULL); return sm_service_combo_update(&value) == SM_REPO_OK && refresh_combos() == 0 ? 0 : -1; }
int delete_combo(int id) { ProductCombo *combo = find_combo_by_id(id); if (!combo) return -1; combo->status = COMBO_INACTIVE; return update_combo(combo); }
static ProductCombo **combo_list(int active, int *count) { ProductCombo **out = NULL, *p; int n = 0, i = 0; if (!count) return NULL; for (p = g_combos; p; p = p->next) if (!active || p->status == COMBO_ACTIVE) ++n; if (n) { out = malloc((size_t)n * sizeof(*out)); if (!out) { *count = 0; return NULL; } } for (p = g_combos; p; p = p->next) if (!active || p->status == COMBO_ACTIVE) out[i++] = p; *count = n; return out; }
ProductCombo **list_combos(int *count) { return combo_list(0, count); }
ProductCombo **list_active_combos(int *count) { return combo_list(1, count); }
int load_combos(void) { return sm_app_repository() ? refresh_combos() : -1; }
int save_combo(ProductCombo *combo) { return update_combo(combo); }
int save_all_combos(void) { return sm_app_repository() ? 0 : -1; }
int load_combo_items(void) { return sm_app_repository() ? refresh_combos() : -1; }
int load_all_combo_items(void) { return sm_app_repository() ? refresh_combos() : -1; }
int save_combo_item(int combo_id, ComboItem *item) { (void)combo_id; (void)item; return sm_app_repository() ? 0 : -1; }

static void free_schedules(void) { while (schedule_cache) { Schedule *n = schedule_cache->next; free(schedule_cache); schedule_cache = n; } }
static void schedule_to_legacy(const sm_schedule_record *src, Schedule *dst) {
    memset(dst, 0, sizeof(*dst)); dst->id = (int)src->id; dst->employee_id = (int)src->employee_id;
    dst->year = (int)src->year; dst->week = src->week; memcpy(dst->shifts, src->shifts, sizeof(dst->shifts));
    dst->created_at = (time_t)src->created_at;
}
static int schedule_from_legacy(const Schedule *src, sm_schedule_record *dst) {
    if (!src || src->id < 0 || src->employee_id <= 0 || src->year < 0 || src->week < 0) return 0;
    memset(dst, 0, sizeof(*dst)); dst->id = (uint64_t)src->id; dst->employee_id = (uint64_t)src->employee_id;
    dst->year = (uint32_t)src->year; dst->week = (uint8_t)src->week;
    memcpy(dst->shifts, src->shifts, sizeof(dst->shifts)); dst->created_at = (int64_t)src->created_at; return 1;
}
static int refresh_schedules(void) {
    sm_schedule_record *values = NULL; size_t count = 0, i;
    if (sm_service_schedule_list(0, 0, 0, &values, &count) != SM_REPO_OK) return -1;
    free_schedules(); for (i = count; i > 0; --i) { Schedule *n = malloc(sizeof(*n)); if (!n) { sm_operations_array_free(values); return -1; } schedule_to_legacy(&values[i - 1], n); n->next = schedule_cache; schedule_cache = n; }
    sm_operations_array_free(values); return 0;
}
int create_schedule(Schedule *schedule) { sm_schedule_record v; if (!schedule_from_legacy(schedule, &v)) return -1; v.id = 0; if (!v.created_at) v.created_at = (int64_t)time(NULL); if (sm_service_schedule_create(&v) != SM_REPO_OK || v.id > INT_MAX) return -1; schedule->id = (int)v.id; schedule->created_at = (time_t)v.created_at; return refresh_schedules() == 0 ? schedule->id : -1; }
int update_schedule(int id, char shifts[7][4]) { sm_schedule_record v; if (sm_service_schedule_get((uint64_t)id, &v) != SM_REPO_OK) return -1; memcpy(v.shifts, shifts, sizeof(v.shifts)); return sm_service_schedule_update(&v) == SM_REPO_OK && refresh_schedules() == 0 ? 0 : -1; }
Schedule *find_schedule(int id) { Schedule *p; for (p = schedule_cache; p; p = p->next) if (p->id == id) return p; return NULL; }
Schedule *find_schedule_by_employee_week(int employee, int year, int week) { Schedule *p; for (p = schedule_cache; p; p = p->next) if (p->employee_id == employee && p->year == year && p->week == week) return p; return NULL; }
int batch_create_schedule(int employee, int year, int week, char shifts[7][4]) { Schedule v; if (find_schedule_by_employee_week(employee, year, week)) return -1; memset(&v, 0, sizeof(v)); v.employee_id = employee; v.year = year; v.week = week; memcpy(v.shifts, shifts, sizeof(v.shifts)); return create_schedule(&v); }
static Schedule **schedule_list(int employee, int year, int week, int *count) { Schedule **out = NULL, *p; int n = 0, i = 0; if (!count) return NULL; for (p = schedule_cache; p; p = p->next) if ((!employee || p->employee_id == employee) && (!year || p->year == year) && (!week || p->week == week)) ++n; if (n) { out = malloc((size_t)n * sizeof(*out)); if (!out) { *count = 0; return NULL; } } for (p = schedule_cache; p; p = p->next) if ((!employee || p->employee_id == employee) && (!year || p->year == year) && (!week || p->week == week)) out[i++] = p; *count = n; return out; }
Schedule **list_employee_schedules(int employee, int *count) { return schedule_list(employee, 0, 0, count); }
Schedule **list_week_schedules(int year, int week, int *count) { return schedule_list(0, year, week, count); }
void print_schedule_table(int year, int week) { int count = 0, i, d; Schedule **list = list_week_schedules(year, week, &count); printf("\nSchedule %d-W%02d\n", year, week); for (i = 0; i < count; ++i) { Employee *e = find_employee_by_id(list[i]->employee_id); printf("%-20s", e ? e->name : "Unknown"); for (d = 0; d < 7; ++d) printf(" %-3s", list[i]->shifts[d]); printf("\n"); } free(list); }
void count_shifts_by_type(int year, int week) { int count = 0, am = 0, pm = 0, rest = 0, i, d; Schedule **list = list_week_schedules(year, week, &count); for (i = 0; i < count; ++i) for (d = 0; d < 7; ++d) { if (!strcmp(list[i]->shifts[d], "AM")) ++am; else if (!strcmp(list[i]->shifts[d], "PM")) ++pm; else ++rest; } printf("AM: %d, PM: %d, rest: %d\n", am, pm, rest); free(list); }
int load_schedules(void) { return sm_app_repository() ? refresh_schedules() : -1; }
int save_schedule(Schedule *s) { sm_schedule_record v; return schedule_from_legacy(s, &v) && sm_service_schedule_update(&v) == SM_REPO_OK && refresh_schedules() == 0 ? 0 : -1; }
int delete_schedule(int id) { return sm_service_schedule_delete((uint64_t)id) == SM_REPO_OK && refresh_schedules() == 0 ? 0 : -1; }

static void free_settlements(void) { while (settlement_cache) { DailySettlement *n = settlement_cache->next; free(settlement_cache); settlement_cache = n; } }
static void settlement_to_legacy(const sm_settlement_record *s, DailySettlement *d) { memset(d, 0, sizeof(*d)); d->id = (int)s->id; d->cashier_id = (int)s->cashier_id; snprintf(d->cashier_name, sizeof(d->cashier_name), "%s", s->cashier_name); d->settlement_date = (time_t)s->business_date; d->shift_start = (time_t)s->shift_start; d->shift_end = (time_t)s->shift_end; d->total_orders = (int)s->total_orders; d->system_cash = amount(s->system_cash_cents); d->system_online = amount(s->system_online_cents); d->system_total = amount(s->system_total_cents); d->actual_cash = amount(s->actual_cash_cents); d->actual_online = amount(s->actual_online_cents); d->actual_total = amount(s->actual_total_cents); d->cash_diff = amount(s->cash_diff_cents); d->online_diff = amount(s->online_diff_cents); d->total_diff = amount(s->total_diff_cents); d->status = s->status; d->created_at = (time_t)s->created_at; d->confirmed_at = (time_t)s->confirmed_at; snprintf(d->remark, sizeof(d->remark), "%s", s->remark); }
static int settlement_from_legacy(const DailySettlement *d, sm_settlement_record *s) { if (!d || d->id < 0 || d->cashier_id <= 0 || d->total_orders < 0 || d->status < 0) return 0; memset(s, 0, sizeof(*s)); s->id = (uint64_t)d->id; s->cashier_id = (uint64_t)d->cashier_id; snprintf(s->cashier_name, sizeof(s->cashier_name), "%s", d->cashier_name); s->business_date = (int64_t)d->settlement_date; s->shift_start = (int64_t)d->shift_start; s->shift_end = (int64_t)d->shift_end; s->total_orders = (uint32_t)d->total_orders; s->system_cash_cents = cents(d->system_cash); s->system_online_cents = cents(d->system_online); s->system_total_cents = s->system_cash_cents + s->system_online_cents; s->actual_cash_cents = cents(d->actual_cash); s->actual_online_cents = cents(d->actual_online); s->actual_total_cents = s->actual_cash_cents + s->actual_online_cents; s->cash_diff_cents = s->actual_cash_cents - s->system_cash_cents; s->online_diff_cents = s->actual_online_cents - s->system_online_cents; s->total_diff_cents = s->actual_total_cents - s->system_total_cents; s->status = (uint8_t)d->status; s->created_at = (int64_t)d->created_at; s->confirmed_at = (int64_t)d->confirmed_at; snprintf(s->remark, sizeof(s->remark), "%s", d->remark); return 1; }
static int refresh_settlements(void) { sm_settlement_record *values = NULL; size_t count = 0, i; if (sm_service_settlement_list(0, 0, &values, &count) != SM_REPO_OK) return -1; free_settlements(); for (i = count; i > 0; --i) { DailySettlement *n = malloc(sizeof(*n)); if (!n) { sm_operations_array_free(values); return -1; } settlement_to_legacy(&values[i - 1], n); n->next = settlement_cache; settlement_cache = n; } sm_operations_array_free(values); return 0; }
int create_settlement(int cashier, float actual_cash, float actual_online, const char *remark) { DailySettlement d; sm_settlement_record s; Employee *e = find_employee_by_id(cashier); time_t now = time(NULL); struct tm *tmv; if (!e) return -1; memset(&d, 0, sizeof(d)); d.cashier_id = cashier; snprintf(d.cashier_name, sizeof(d.cashier_name), "%s", e->name); tmv = localtime(&now); if (!tmv) return -1; tmv->tm_hour = tmv->tm_min = tmv->tm_sec = 0; d.settlement_date = mktime(tmv); d.shift_start = d.settlement_date; d.shift_end = now; calculate_cashier_sales(cashier, d.shift_start, d.shift_end, &d.total_orders, &d.system_cash, &d.system_online, &d.system_total); d.actual_cash = actual_cash; d.actual_online = actual_online; d.actual_total = actual_cash + actual_online; d.cash_diff = actual_cash - d.system_cash; d.online_diff = actual_online - d.system_online; d.total_diff = d.actual_total - d.system_total; d.status = fabsf(d.total_diff) < 0.01f ? SETTLEMENT_CONFIRMED : SETTLEMENT_PENDING; d.created_at = now; snprintf(d.remark, sizeof(d.remark), "%s", remark ? remark : ""); if (!settlement_from_legacy(&d, &s) || sm_service_settlement_create(&s) != SM_REPO_OK || s.id > INT_MAX) return -1; return refresh_settlements() == 0 ? (int)s.id : -1; }
DailySettlement *find_settlement(int id) { DailySettlement *p; for (p = settlement_cache; p; p = p->next) if (p->id == id) return p; return NULL; }
DailySettlement *find_settlement_by_cashier_date(int cashier, time_t date) { DailySettlement *p; for (p = settlement_cache; p; p = p->next) if (p->cashier_id == cashier && p->settlement_date == date) return p; return NULL; }
static DailySettlement **settlement_list(int cashier, time_t date, int *count) { DailySettlement **out = NULL, *p; int n = 0, i = 0; if (!count) return NULL; for (p = settlement_cache; p; p = p->next) if ((!cashier || p->cashier_id == cashier) && (!date || p->settlement_date == date)) ++n; if (n) { out = malloc((size_t)n * sizeof(*out)); if (!out) { *count = 0; return NULL; } } for (p = settlement_cache; p; p = p->next) if ((!cashier || p->cashier_id == cashier) && (!date || p->settlement_date == date)) out[i++] = p; *count = n; return out; }
DailySettlement **list_settlements_by_date(time_t date, int *count) { return settlement_list(0, date, count); }
DailySettlement **list_cashier_settlements(int cashier, int *count) { return settlement_list(cashier, 0, count); }
int confirm_settlement(int id, const char *remark) { return sm_service_settlement_confirm((uint64_t)id, remark, (int64_t)time(NULL)) == SM_REPO_OK && refresh_settlements() == 0 ? 0 : -1; }
void print_settlement_detail(int id) { DailySettlement *d = find_settlement(id); if (!d) { printf("Settlement not found\n"); return; } printf("Settlement #%d cashier=%s orders=%d system=%.2f actual=%.2f diff=%.2f status=%s\n", d->id, d->cashier_name, d->total_orders, d->system_total, d->actual_total, d->total_diff, get_settlement_status_name(d->status)); }
int load_settlements(void) { return sm_app_repository() ? refresh_settlements() : -1; }
int save_settlement(DailySettlement *d) { sm_settlement_record s; return settlement_from_legacy(d, &s) && sm_service_settlement_update(&s) == SM_REPO_OK && refresh_settlements() == 0 ? 0 : -1; }
int save_all_settlements(void) { return sm_app_repository() ? 0 : -1; }

int write_transaction_log(const char *type, int ref_id, const char *operation, const char *data, int operator_id) { uint64_t id = 0; if (ref_id < 0 || operator_id < 0 || sm_service_audit_write(type, (uint64_t)ref_id, operation, data, (uint64_t)operator_id, (int64_t)time(NULL), &id) != SM_REPO_OK || id > INT_MAX) return -1; return (int)id; }
int load_transaction_logs(void) { return sm_app_repository() ? 0 : -1; }
TransactionLog *query_transaction_logs(const char *type, time_t start, time_t end, int *count) { sm_audit_record *values = NULL; TransactionLog *out = NULL; size_t n = 0, i; if (!count || sm_service_audit_list(type, 0, (int64_t)start, (int64_t)end, &values, &n) != SM_REPO_OK || n > INT_MAX) { if (count) *count = 0; return NULL; } if (n) { out = calloc(n, sizeof(*out)); if (!out) { sm_operations_array_free(values); *count = 0; return NULL; } } for (i = 0; i < n; ++i) { out[i].id = (int)values[i].id; snprintf(out[i].type, sizeof(out[i].type), "%s", values[i].type); out[i].ref_id = (int)values[i].ref_id; snprintf(out[i].operation, sizeof(out[i].operation), "%s", values[i].operation); snprintf(out[i].data, sizeof(out[i].data), "%s", values[i].data); out[i].operator_id = (int)values[i].operator_id; out[i].created_at = (time_t)values[i].created_at; } sm_operations_array_free(values); *count = (int)n; return out; }
