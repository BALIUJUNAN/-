#include "app/sm_finance_service.h"

#include "app/sm_app_context.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static VipCardTransaction *transaction_cache;

static int to_cents(float amount, int64_t *out) {
    double scaled;
    if (!out || !isfinite(amount) || amount <= 0.0f) return 0;
    scaled = (double)amount * 100.0;
    if (scaled > (double)INT64_MAX) return 0;
    *out = (int64_t)floor(scaled + 0.5); return 1;
}

static void clear_cards(void) {
    while (g_vip_cards) { VipCard *next = g_vip_cards->next; free(g_vip_cards); g_vip_cards = next; }
}

static void clear_transactions(void) {
    while (transaction_cache) {
        VipCardTransaction *next = transaction_cache->next;
        free(transaction_cache); transaction_cache = next;
    }
}

int load_vip_cards(void) {
    VipCard *values = NULL; size_t count = 0, i;
    clear_cards();
    if (sm_service_vip_card_list(0, -1, &values, &count) != SM_REPO_OK) return -1;
    for (i = count; i > 0; --i) {
        VipCard *node = malloc(sizeof(*node));
        if (!node) { free(values); clear_cards(); return -1; }
        *node = values[i - 1u]; node->next = g_vip_cards; g_vip_cards = node;
    }
    free(values); return 0;
}

int load_vip_card_transactions(void) {
    VipCardTransaction *values = NULL; size_t count = 0, i;
    clear_transactions();
    if (sm_service_vip_transaction_list(NULL, &values, &count) != SM_REPO_OK)
        return -1;
    for (i = count; i > 0; --i) {
        VipCardTransaction *node = malloc(sizeof(*node));
        if (!node) { free(values); clear_transactions(); return -1; }
        *node = values[i - 1u]; node->next = transaction_cache;
        transaction_cache = node;
    }
    free(values); return 0;
}

VipCard *find_vip_card(const char *card_no) {
    VipCard *card = g_vip_cards;
    while (card && (!card_no || strcmp(card->card_no, card_no) != 0)) card = card->next;
    return card;
}

VipCard *find_vip_card_by_member(int member_id) {
    VipCard *card = g_vip_cards;
    while (card && card->member_id != member_id) card = card->next;
    return card;
}

VipCard *create_vip_card(const char *password) {
    VipCard card; time_t now = time(NULL); struct tm *parts = localtime(&now);
    int attempt;
    if (!password || !*password || !parts) return NULL;
    memset(&card, 0, sizeof(card)); card.card_type = VIPCARD_TYPE_NORMAL;
    card.status = VIPCARD_ACTIVE; card.created_at = card.updated_at = now;
    card.expired_at = now + (time_t)10 * 365 * 24 * 3600;
    generate_salt(card.password_salt);
    hash_password(password, card.password_salt, card.password_hash);
    for (attempt = 0; attempt < 32; ++attempt) {
        snprintf(card.card_no, sizeof(card.card_no), "VC%04d%02d%02d%06d",
                 parts->tm_year + 1900, parts->tm_mon + 1, parts->tm_mday,
                 rand() % 1000000);
        if (sm_service_vip_card_create(&card) == SM_REPO_OK) {
            if (load_vip_cards() != 0) return NULL;
            return find_vip_card(card.card_no);
        }
    }
    return NULL;
}

int verify_vip_card_password(const char *card_no, const char *password) {
    VipCard *card = find_vip_card(card_no); char hash[65];
    if (!card || !password) return -1;
    hash_password(password, card->password_salt, hash);
    return strcmp(hash, card->password_hash) == 0 ? 0 : -1;
}

int save_vip_card(VipCard *card) {
    return card && sm_service_vip_card_update(card) == SM_REPO_OK ? 0 : -1;
}

int save_all_vip_cards(void) {
    VipCard *card;
    for (card = g_vip_cards; card; card = card->next)
        if (sm_service_vip_card_update(card) != SM_REPO_OK) return -1;
    return sm_app_repository() ? 0 : -1;
}

int change_vip_card_password(const char *card_no, const char *old_password,
                             const char *new_password) {
    VipCard *card = find_vip_card(card_no);
    if (!card || !new_password || !*new_password ||
        verify_vip_card_password(card_no, old_password) != 0) return -1;
    generate_salt(card->password_salt);
    hash_password(new_password, card->password_salt, card->password_hash);
    card->updated_at = time(NULL); return save_vip_card(card);
}

int bind_vip_card_member(const char *card_no, int member_id) {
    VipCard *card = find_vip_card(card_no);
    if (!card || member_id < 0) return -1;
    card->member_id = member_id; card->updated_at = time(NULL);
    return save_vip_card(card);
}

static int set_status(const char *card_no, int status) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    card->status = status; card->updated_at = time(NULL);
    return save_vip_card(card);
}

int freeze_vip_card(const char *card_no) { return set_status(card_no, VIPCARD_FROZEN); }
int unfreeze_vip_card(const char *card_no) { return set_status(card_no, VIPCARD_ACTIVE); }

int cancel_vip_card(const char *card_no) {
    VipCard *card = find_vip_card(card_no);
    return !card || card->balance > 0.0f ? -1 : set_status(card_no, VIPCARD_CANCELLED);
}

int set_vip_card_expired(const char *card_no, time_t expired_at) {
    VipCard *card = find_vip_card(card_no);
    if (!card || expired_at < 0) return -1;
    card->expired_at = expired_at; card->updated_at = time(NULL);
    return save_vip_card(card);
}

static VipCard **list_cards(int member_id, int *count) {
    VipCard **list = NULL; VipCard *card; int n = 0, cap = 0;
    if (!count) return NULL;
    *count = 0;
    for (card = g_vip_cards; card; card = card->next) {
        if (member_id && card->member_id != member_id) continue;
        if (n == cap) {
            int next = cap ? cap * 2 : 16; VipCard **grown = realloc(list, (size_t)next * sizeof(*list));
            if (!grown) { free(list); return NULL; } list = grown; cap = next;
        }
        list[n++] = card;
    }
    *count = n; return list;
}

VipCard **list_vip_cards(int *count) { return list_cards(0, count); }
VipCard **list_member_vip_cards(int member_id, int *count) {
    return list_cards(member_id, count);
}

static int apply(const char *card_no, int type, float amount, int sale_id,
                 int operator_id, const char *remark) {
    VipCardTransaction transaction; VipCard *cached; int64_t cents;
    if (!to_cents(amount, &cents) || sale_id < 0 || operator_id < 0 ||
        sm_service_vip_apply(card_no, type, cents, (uint64_t)sale_id,
                             (uint64_t)operator_id, remark ? remark : "",
                             &transaction) != SM_REPO_OK)
        return -1;
    cached = find_vip_card(card_no);
    if (cached && sm_service_vip_card_get(card_no, cached) != SM_REPO_OK) return -1;
    return load_vip_card_transactions();
}

int recharge_vip_card(const char *card_no, float amount, int operator_id,
                      const char *remark) {
    return apply(card_no, 0, amount, 0, operator_id, remark);
}

int consume_vip_card(const char *card_no, float amount, int sale_id,
                     int operator_id, const char *remark) {
    return apply(card_no, 1, amount, sale_id, operator_id, remark);
}

int refund_vip_card(const char *card_no, float amount, int sale_id,
                    int operator_id, const char *remark) {
    return apply(card_no, 2, amount, sale_id, operator_id, remark);
}

static VipCardTransaction **list_transactions(const char *card_no, int *count) {
    VipCardTransaction **list = NULL, *tx; int n = 0, cap = 0;
    if (!count) return NULL;
    *count = 0;
    for (tx = transaction_cache; tx; tx = tx->next) {
        if (card_no && strcmp(tx->card_no, card_no) != 0) continue;
        if (n == cap) {
            int next = cap ? cap * 2 : 16;
            VipCardTransaction **grown = realloc(list, (size_t)next * sizeof(*list));
            if (!grown) { free(list); return NULL; } list = grown; cap = next;
        }
        list[n++] = tx;
    }
    *count = n; return list;
}

VipCardTransaction **query_vip_card_transactions(const char *card_no, int *count) {
    return list_transactions(card_no, count);
}

VipCardTransaction **query_member_vip_transactions(int member_id, int *count) {
    VipCard *card = find_vip_card_by_member(member_id);
    return card ? list_transactions(card->card_no, count) : list_transactions("", count);
}

void print_vip_card_summary(void) {
    VipCard *card; int active = 0; double total = 0;
    printf("\nStored-value card summary\n");
    for (card = g_vip_cards; card; card = card->next) {
        printf("%s member %d balance %.2f status %d\n", card->card_no,
               card->member_id, card->balance, card->status);
        if (card->status == VIPCARD_ACTIVE) ++active;
        total += card->balance;
    }
    printf("Active cards %d, total balance %.2f\n", active, total);
}

void generate_vip_card_statement(const char *card_no, time_t start, time_t end) {
    VipCardTransaction *tx; VipCard *card = find_vip_card(card_no);
    if (!card) return;
    printf("\nCard %s balance %.2f\n", card_no, card->balance);
    for (tx = transaction_cache; tx; tx = tx->next)
        if (strcmp(tx->card_no, card_no) == 0 && tx->created_at >= start && tx->created_at <= end)
            printf("Transaction #%d type %d amount %.2f balance %.2f\n",
                   tx->id, tx->type, tx->amount, tx->balance_after);
}

int save_vip_card_transaction(VipCardTransaction *transaction) {
    return transaction && sm_app_repository() ? 0 : -1;
}
