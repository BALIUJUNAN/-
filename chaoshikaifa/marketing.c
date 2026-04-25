/**
 * @file marketing.c
 * @brief 营销模块合并文件
 * 
 * 合并了以下模块：
 * - 会员管理 (member)
 * - 促销管理 (promotion)
 * - 套装管理 (combo)
 * - 批次管理 (batch)
 * - 储值卡管理 (vipcard)
 */

#include "supermarket.h"

// ==================== 会员模块 ====================

/**
 * 添加会员
 */
int add_member(const char *phone, const char *name) {
    if (find_member_by_phone(phone)) {
        printf("错误: 手机号 %s 已注册\n", phone);
        return -1;
    }
    
    Member *member = (Member*)malloc(sizeof(Member));
    if (!member) return -1;
    
    member->id = generate_id();
    strncpy(member->phone, phone, 19);
    strncpy(member->name, name, 49);
    member->level = MEMBER_LEVEL_NORMAL;
    member->points = 0;
    member->total_consume = 0;
    member->created_at = time(NULL);
    member->updated_at = member->created_at;
    member->last_consume_at = 0;
    
    hash_insert(g_member_phone_hash, HASH_TABLE_SIZE, phone, member);
    
    member->next = g_members;
    g_members = member;
    
    save_member_record(member);
    
    printf("会员注册成功! 会员ID: %d\n", member->id);
    return member->id;
}

/**
 * 查找会员（按手机号）
 */
Member* find_member_by_phone(const char *phone) {
    return (Member*)hash_search(g_member_phone_hash, HASH_TABLE_SIZE, phone);
}

/**
 * 查找会员（按ID）
 */
Member* find_member_by_id(int id) {
    Member *m = g_members;
    while (m) {
        if (m->id == id) return m;
        m = m->next;
    }
    return NULL;
}

/**
 * 更新会员信息
 */
int update_member(Member *member) {
    member->updated_at = time(NULL);
    return save_members();
}

/**
 * 删除会员（注销）
 */
int delete_member(int id) {
    Member **prev = &g_members;
    while (*prev) {
        if ((*prev)->id == id) {
            Member *to_free = *prev;
            hash_delete(g_member_phone_hash, HASH_TABLE_SIZE, to_free->phone);
            *prev = (*prev)->next;
            free(to_free);
            return save_members();
        }
        prev = &(*prev)->next;
    }
    return -1;
}

/**
 * 获取会员等级名称
 */
const char* get_member_level_name(int level) {
    switch (level) {
        case MEMBER_LEVEL_NORMAL:  return "普通会员";
        case MEMBER_LEVEL_SILVER:  return "银卡会员";
        case MEMBER_LEVEL_GOLD:    return "金卡会员";
        case MEMBER_LEVEL_DIAMOND: return "钻石会员";
        default: return "未知";
    }
}

/**
 * 获取会员折扣
 */
float get_member_discount(Member *member) {
    if (!member) return 1.0f;
    switch (member->level) {
        case MEMBER_LEVEL_NORMAL:  return 1.00f;
        case MEMBER_LEVEL_SILVER:  return 0.98f;
        case MEMBER_LEVEL_GOLD:    return 0.95f;
        case MEMBER_LEVEL_DIAMOND: return 0.90f;
        default: return 1.0f;
    }
}

/**
 * 获取会员等级
 */
int get_member_level(Member *member) {
    if (!member) return 0;
    if (member->total_consume >= 20000) return MEMBER_LEVEL_DIAMOND;
    if (member->total_consume >= 5000) return MEMBER_LEVEL_GOLD;
    if (member->total_consume >= 1000) return MEMBER_LEVEL_SILVER;
    return MEMBER_LEVEL_NORMAL;
}

/**
 * 升级会员等级
 */
void upgrade_member_level(Member *member) {
    int old_level = member->level;
    
    if (member->total_consume >= 20000) {
        member->level = MEMBER_LEVEL_DIAMOND;
    } else if (member->total_consume >= 5000) {
        member->level = MEMBER_LEVEL_GOLD;
    } else if (member->total_consume >= 1000) {
        member->level = MEMBER_LEVEL_SILVER;
    } else {
        member->level = MEMBER_LEVEL_NORMAL;
    }
    
    if (old_level != member->level) {
        printf("[升级] %s 升级为 %s！\n", member->name, get_member_level_name(member->level));
        member->updated_at = time(NULL);
    }
}

/**
 * 增加积分
 */
int add_points(Member *member, float amount) {
    int earned = (int)(amount * POINTS_PER_YUAN);
    member->points += earned;
    member->total_consume += amount;
    member->last_consume_at = time(NULL);
    upgrade_member_level(member);
    member->updated_at = time(NULL);
    return earned;
}

/**
 * 积分兑换
 */
float redeem_points(Member *member, int points_to_redeem) {
    if (points_to_redeem < POINTS_REDEEM_RATE) {
        printf("错误: 最少需要 %d 积分\n", POINTS_REDEEM_RATE);
        return 0;
    }
    
    int max_redeem = (member->points / POINTS_REDEEM_RATE) * POINTS_REDEEM_RATE;
    if (points_to_redeem > max_redeem) {
        points_to_redeem = max_redeem;
    }
    
    member->points -= points_to_redeem;
    float cash = points_to_redeem / (float)POINTS_REDEEM_RATE;
    member->updated_at = time(NULL);
    
    return cash;
}

/**
 * 会员消费处理
 */
void member_consume(Member *member, float amount) {
    add_points(member, amount);
    printf("会员 %s 消费 ¥%.2f，获得 %d 积分，当前等级: %s\n",
           member->name, amount, (int)(amount * POINTS_PER_YUAN),
           get_member_level_name(member->level));
}

/**
 * 列出所有会员
 */
Member** list_members(int *count) {
    Member **list = NULL;
    *count = 0;
    
    Member *m = g_members;
    while (m) {
        list = (Member**)realloc(list, (*count + 1) * sizeof(Member*));
        list[*count] = m;
        (*count)++;
        m = m->next;
    }
    
    return list;
}

/**
 * 检查即将过期的积分
 */
void check_expiring_points(void) {
    time_t now = time(NULL);
    time_t one_year = 365 * 24 * 3600;
    int count = 0;
    
    printf("\n========== 积分即将过期提醒 ==========\n");
    
    Member *m = g_members;
    while (m) {
        if (m->points > 0 && m->last_consume_at > 0) {
            time_t diff = now - m->last_consume_at;
            if (diff > one_year * 2) {
                printf("[警告] %s (手机: %s) 积分即将过期: %d 积分\n",
                       m->name, m->phone, m->points);
                count++;
            }
        }
        m = m->next;
    }
    
    if (count == 0) {
        printf("无即将过期的积分\n");
    }
    printf("=====================================\n\n");
}

/**
 * 加载会员数据
 */
int load_members(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/member.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Member *m = (Member*)malloc(sizeof(Member));
        m->id = atoi(strtok(line, "|"));
        strcpy(m->phone, strtok(NULL, "|"));
        strcpy(m->name, strtok(NULL, "|"));
        m->level = atoi(strtok(NULL, "|"));
        m->points = atoi(strtok(NULL, "|"));
        m->total_consume = atof(strtok(NULL, "|"));
        m->created_at = (time_t)atoi(strtok(NULL, "|"));
        m->updated_at = (time_t)atoi(strtok(NULL, "|"));
        m->last_consume_at = (time_t)atoi(strtok(NULL, "|"));
        
        hash_insert(g_member_phone_hash, HASH_TABLE_SIZE, m->phone, m);
        m->next = g_members;
        g_members = m;
        
        if (m->id > g_auto_id_counter) {
            g_auto_id_counter = m->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存会员数据 - 原子写入
 */
int save_members(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/member.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    Member *m = g_members;
    while (m) {
        char line[512];
        snprintf(line, sizeof(line), "%d|%s|%s|%d|%d|%.2f|%ld|%ld|%ld\n",
            m->id, m->phone, m->name, m->level, m->points,
            m->total_consume, (long)m->created_at,
            (long)m->updated_at, (long)m->last_consume_at);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        m = m->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

/**
 * 保存单个会员记录 - 原子追加
 */
int save_member_record(Member *member) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/member.txt", DATA_DIR);
    snprintf(content, sizeof(content), "%d|%s|%s|%d|%d|%.2f|%ld|%ld|%ld",
        member->id, member->phone, member->name, member->level, member->points,
        member->total_consume, (long)member->created_at,
        (long)member->updated_at, (long)member->last_consume_at);
    
    return atomic_append(filepath, content);
}

// ==================== 促销模块 ====================

/**
 * 创建促销
 */
int create_promotion(Promotion *promo) {
    promo->id = generate_id();
    promo->created_at = time(NULL);
    promo->status = PROMOTION_ACTIVE;
    
    Promotion *new_promo = (Promotion*)malloc(sizeof(Promotion));
    *new_promo = *promo;
    
    Promotion **prev = &g_promotions;
    while (*prev && (*prev)->priority >= promo->priority) {
        prev = &(*prev)->next;
    }
    new_promo->next = *prev;
    *prev = new_promo;
    
    save_promotion(new_promo);
    return promo->id;
}

/**
 * 创建单品折扣促销
 */
int create_discount_promotion(const char *name, const char *product_id,
                             float discount_rate, time_t start, time_t end) {
    Promotion promo;
    memset(&promo, 0, sizeof(promo));
    
    strncpy(promo.name, name, 99);
    promo.type = PROMOTION_TYPE_DISCOUNT;
    strncpy(promo.product_id, product_id, MAX_ID_LEN - 1);
    promo.discount_rate = discount_rate;
    promo.start_time = start;
    promo.end_time = end;
    promo.priority = 1;
    
    return create_promotion(&promo);
}

/**
 * 创建满减促销
 */
int create_override_promotion(const char *name, float threshold,
                            float discount_amount, time_t start, time_t end) {
    Promotion promo;
    memset(&promo, 0, sizeof(Promotion));
    
    strncpy(promo.name, name, 99);
    promo.type = PROMOTION_TYPE_OVERRIDE;
    promo.threshold = threshold;
    promo.discount_amount = discount_amount;
    promo.start_time = start;
    promo.end_time = end;
    promo.priority = 10;
    
    return create_promotion(&promo);
}

/**
 * 查找促销
 */
Promotion* find_promotion(int id) {
    Promotion *p = g_promotions;
    while (p) {
        if (p->id == id) return p;
        p = p->next;
    }
    return NULL;
}

/**
 * 检查促销是否有效
 */
static int is_promotion_valid(Promotion *p) {
    time_t now = time(NULL);
    return p->status == PROMOTION_ACTIVE &&
           now >= p->start_time && now <= p->end_time;
}

/**
 * 获取商品当前有效促销
 */
Promotion* get_product_promotion(const char *product_id) {
    Promotion *p = g_promotions;
    while (p) {
        if (p->type == PROMOTION_TYPE_DISCOUNT &&
            strcmp(p->product_id, product_id) == 0 &&
            is_promotion_valid(p)) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

/**
 * 获取当前有效的满减促销
 */
Promotion* get_order_override_promotion(void) {
    Promotion *best = NULL;
    Promotion *p = g_promotions;
    
    while (p) {
        if (p->type == PROMOTION_TYPE_OVERRIDE && is_promotion_valid(p)) {
            if (!best || p->discount_amount > best->discount_amount) {
                best = p;
            }
        }
        p = p->next;
    }
    return best;
}

/**
 * 删除促销
 */
int delete_promotion(int id) {
    Promotion **prev = &g_promotions;
    while (*prev) {
        if ((*prev)->id == id) {
            Promotion *to_free = *prev;
            *prev = (*prev)->next;
            free(to_free);
            return save_all_promotions();
        }
        prev = &(*prev)->next;
    }
    return -1;
}

/**
 * 列出所有促销
 */
Promotion** list_promotions(int *count) {
    Promotion **list = NULL;
    *count = 0;
    
    Promotion *p = g_promotions;
    while (p) {
        list = (Promotion**)realloc(list, (*count + 1) * sizeof(Promotion*));
        list[*count] = p;
        (*count)++;
        p = p->next;
    }
    
    return list;
}

/**
 * 列出生效中的促销
 */
Promotion** list_active_promotions(int *count) {
    Promotion **list = NULL;
    *count = 0;
    
    Promotion *p = g_promotions;
    while (p) {
        if (is_promotion_valid(p)) {
            list = (Promotion**)realloc(list, (*count + 1) * sizeof(Promotion*));
            list[*count] = p;
            (*count)++;
        }
        p = p->next;
    }
    
    return list;
}

// ==================== 购物车 ====================

/**
 * 创建购物车
 */
Cart* cart_create(void) {
    Cart *cart = (Cart*)malloc(sizeof(Cart));
    cart->items = NULL;
    cart->subtotal = 0;
    cart->discount_amount = 0;
    cart->final_amount = 0;
    cart->member_id = 0;
    return cart;
}

/**
 * 添加商品到购物车
 */
int cart_add_item(Cart *cart, const char *product_id, float quantity) {
    Product *prod = find_product_by_id(product_id);
    if (!prod) return -1;
    
    CartItem **prev = &cart->items;
    while (*prev) {
        if (strcmp((*prev)->product_id, product_id) == 0) {
            (*prev)->quantity += quantity;
            (*prev)->subtotal = (*prev)->quantity * (*prev)->discounted_price;
            return 0;
        }
        prev = &(*prev)->next;
    }
    
    CartItem *item = (CartItem*)malloc(sizeof(CartItem));
    strncpy(item->product_id, product_id, MAX_ID_LEN - 1);
    strncpy(item->product_name, prod->name, MAX_NAME_LEN - 1);
    item->price = prod->price;
    item->quantity = quantity;
    item->discounted_price = prod->price;
    item->discount_amount = 0;
    item->batch_no[0] = '\0';
    item->next = NULL;
    
    Promotion *promo = get_product_promotion(product_id);
    if (promo) {
        item->discounted_price = item->price * promo->discount_rate;
        item->discount_amount = item->price - item->discounted_price;
    }
    
    item->subtotal = item->quantity * item->discounted_price;
    
    item->next = cart->items;
    cart->items = item;
    
    return 0;
}

/**
 * 从购物车移除商品
 */
int cart_remove_item(Cart *cart, const char *product_id) {
    CartItem **prev = &cart->items;
    while (*prev) {
        if (strcmp((*prev)->product_id, product_id) == 0) {
            CartItem *to_free = *prev;
            *prev = (*prev)->next;
            free(to_free);
            return 0;
        }
        prev = &(*prev)->next;
    }
    return -1;
}

/**
 * 清空购物车
 */
void cart_clear(Cart *cart) {
    CartItem *item = cart->items;
    while (item) {
        CartItem *next = item->next;
        free(item);
        item = next;
    }
    cart->items = NULL;
    cart->subtotal = 0;
    cart->discount_amount = 0;
    cart->final_amount = 0;
}

/**
 * 销毁购物车
 */
void cart_destroy(Cart *cart) {
    cart_clear(cart);
    free(cart);
}

/**
 * 计算购物车小计
 */
float cart_calculate_subtotal(Cart *cart) {
    float subtotal = 0;
    CartItem *item = cart->items;
    
    while (item) {
        subtotal += item->quantity * item->discounted_price;
        item = item->next;
    }
    
    cart->subtotal = subtotal;
    return subtotal;
}

// ==================== 促销应用 ====================

/**
 * 计算单品折扣
 */
float calculate_item_discount(CartItem *item, Promotion *promo) {
    if (!promo || promo->type != PROMOTION_TYPE_DISCOUNT) return 0;
    if (strcmp(promo->product_id, item->product_id) != 0) return 0;
    if (!is_promotion_valid(promo)) return 0;
    
    float discount = item->price * (1 - promo->discount_rate);
    return discount * item->quantity;
}

/**
 * 计算满减优惠
 */
float calculate_order_discount(Cart *cart, Promotion *promo) {
    if (!promo || promo->type != PROMOTION_TYPE_OVERRIDE) return 0;
    if (!is_promotion_valid(promo)) return 0;
    
    if (cart->subtotal >= promo->threshold) {
        return promo->discount_amount;
    }
    return 0;
}

/**
 * 计算第N件优惠
 */
float calculate_nth_discount(int quantity, Promotion *promo) {
    if (!promo || promo->type != PROMOTION_TYPE_NTH_DISCOUNT) return 0;
    if (!is_promotion_valid(promo)) return 0;
    if (quantity < promo->nth_item) return 0;
    
    int eligible_count = quantity / promo->nth_item;
    return eligible_count * promo->nth_discount_rate;
}

/**
 * 计算买M赠N优惠
 */
float calculate_buy_m_get_n(int quantity, Promotion *promo, float unit_price) {
    if (!promo || promo->type != PROMOTION_TYPE_BUY_M_GET_N) return 0;
    if (!is_promotion_valid(promo)) return 0;
    
    int total_required = promo->buy_quantity + promo->free_quantity;
    if (quantity < total_required) return 0;
    
    int groups = quantity / total_required;
    return groups * promo->free_quantity * unit_price;
}

/**
 * 检查会员专属价是否适用
 */
float calculate_member_price(Promotion *promo, Member *member, float original_price) {
    if (!promo || promo->type != PROMOTION_TYPE_MEMBER_PRICE) return 0;
    if (!is_promotion_valid(promo)) return 0;
    
    if (promo->member_level > 0 && member) {
        int member_level = get_member_level(member);
        if (member_level < promo->member_level) return 0;
    }
    
    if (promo->member_price >= original_price) return 0;
    return original_price - promo->member_price;
}

/**
 * 应用促销规则
 */
PromotionResult apply_promotions(Cart *cart, Member *member) {
    PromotionResult result;
    memset(&result, 0, sizeof(result));
    result.detail[0] = '\0';
    
    int member_level = get_member_level(member);
    
    CartItem *item = cart->items;
    while (item) {
        result.original_amount += item->price * item->quantity;
        item = item->next;
    }
    
    item = cart->items;
    while (item) {
        float item_original = item->price * item->quantity;
        float item_discount = 0;
        float final_price = item->price;
        
        Promotion *p = g_promotions;
        while (p) {
            if (is_promotion_valid(p) && strcmp(p->product_id, item->product_id) == 0) {
                switch (p->type) {
                    case PROMOTION_TYPE_DISCOUNT: {
                        float save = item->price * (1 - p->discount_rate) * item->quantity;
                        if (save > item_discount) {
                            item_discount = save;
                            final_price = item->price * p->discount_rate;
                            snprintf(result.detail + strlen(result.detail),
                                    sizeof(result.detail) - strlen(result.detail),
                                    "%s: %.0f折 -¥%.2f\n", 
                                    p->name, p->discount_rate * 10, save);
                            result.promotion_count++;
                        }
                        break;
                    }
                    case PROMOTION_TYPE_NTH_DISCOUNT: {
                        int eligible = item->quantity / p->nth_item;
                        if (eligible > 0) {
                            float save2 = eligible * item->price * (1 - p->nth_discount_rate);
                            if (save2 > item_discount) {
                                item_discount = save2;
                                snprintf(result.detail + strlen(result.detail),
                                        sizeof(result.detail) - strlen(result.detail),
                                        "%s: 第%d件享%.0f折 -¥%.2f\n", 
                                        p->name, p->nth_item, p->nth_discount_rate * 10, save2);
                                result.promotion_count++;
                            }
                        }
                        break;
                    }
                    case PROMOTION_TYPE_BUY_M_GET_N: {
                        int groups = item->quantity / (p->buy_quantity + p->free_quantity);
                        if (groups > 0) {
                            float save = groups * p->free_quantity * item->price;
                            if (save > item_discount) {
                                item_discount = save;
                                snprintf(result.detail + strlen(result.detail),
                                        sizeof(result.detail) - strlen(result.detail),
                                        "%s: 买%d送%d -¥%.2f\n", 
                                        p->name, p->buy_quantity, p->free_quantity, save);
                                result.promotion_count++;
                            }
                        }
                        break;
                    }
                    case PROMOTION_TYPE_MEMBER_PRICE: {
                        if (p->member_level == 0 || member_level >= p->member_level) {
                            float save = (item->price - p->member_price) * item->quantity;
                            if (save > 0 && save > item_discount) {
                                item_discount = save;
                                final_price = p->member_price;
                                snprintf(result.detail + strlen(result.detail),
                                        sizeof(result.detail) - strlen(result.detail),
                                        "%s: 会员价¥%.2f -¥%.2f\n", 
                                        p->name, p->member_price, save);
                                result.promotion_count++;
                            }
                        }
                        break;
                    }
                }
            }
            p = p->next;
        }
        
        result.discount_amount += item_discount;
        item = item->next;
    }
    
    float after_item_discount = result.original_amount - result.discount_amount;
    float member_discount = 0;
    if (member) {
        float discount_rate = get_member_discount(member);
        if (discount_rate < 1.0f) {
            member_discount = after_item_discount * (1 - discount_rate);
            result.discount_amount += member_discount;
            snprintf(result.detail + strlen(result.detail), 
                    sizeof(result.detail) - strlen(result.detail),
                    "会员折扣(%.0f折): -¥%.2f\n", 
                    discount_rate * 10, member_discount);
        }
    }
    
    cart->subtotal = result.original_amount - result.discount_amount;
    Promotion *override = get_order_override_promotion();
    if (override && cart->subtotal >= override->threshold) {
        float override_amount = override->discount_amount;
        if (override_amount > 0) {
            result.discount_amount += override_amount;
            cart->subtotal -= override_amount;
            snprintf(result.detail + strlen(result.detail),
                    sizeof(result.detail) - strlen(result.detail),
                    "%s: -¥%.2f\n", override->name, override_amount);
            result.promotion_count++;
        }
    }
    
    result.final_amount = result.original_amount - result.discount_amount;
    if (result.final_amount < 0) result.final_amount = 0;
    
    return result;
}

/**
 * 加载促销数据
 */
int load_promotions(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/promotion.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Promotion *p = (Promotion*)malloc(sizeof(Promotion));
        memset(p, 0, sizeof(Promotion));
        
        char *tokens[18];
        int field_count = 0;
        char *token = strtok(line, "|");
        while (token && field_count < 18) {
            tokens[field_count++] = token;
            token = strtok(NULL, "|");
        }
        
        p->id = atoi(tokens[0]);
        strncpy(p->name, tokens[1] ? tokens[1] : "", 99);
        p->type = tokens[2] ? atoi(tokens[2]) : 0;
        strncpy(p->product_id, tokens[3] ? tokens[3] : "", MAX_ID_LEN - 1);
        p->discount_rate = tokens[4] ? atof(tokens[4]) : 0;
        p->threshold = tokens[5] ? atof(tokens[5]) : 0;
        p->discount_amount = tokens[6] ? atof(tokens[6]) : 0;
        p->start_time = tokens[7] ? (time_t)atoi(tokens[7]) : 0;
        p->end_time = tokens[8] ? (time_t)atoi(tokens[8]) : 0;
        p->status = tokens[9] ? atoi(tokens[9]) : 0;
        p->priority = tokens[10] ? atoi(tokens[10]) : 0;
        p->created_at = tokens[11] ? (time_t)atoi(tokens[11]) : 0;
        if (field_count > 12) p->nth_item = atoi(tokens[12]);
        if (field_count > 13) p->nth_discount_rate = atof(tokens[13]);
        if (field_count > 14) p->buy_quantity = atoi(tokens[14]);
        if (field_count > 15) p->free_quantity = atoi(tokens[15]);
        if (field_count > 16) p->member_level = atoi(tokens[16]);
        if (field_count > 17) p->member_price = atof(tokens[17]);
        
        Promotion **prev = &g_promotions;
        while (*prev && (*prev)->priority >= p->priority) {
            prev = &(*prev)->next;
        }
        p->next = *prev;
        *prev = p;
        
        if (p->id > g_auto_id_counter) {
            g_auto_id_counter = p->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存促销 - 原子追加
 */
int save_promotion(Promotion *p) {
    char filepath[256];
    char content[2048];
    
    snprintf(filepath, sizeof(filepath), "%s/promotion.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%s|%d|%s|%.2f|%.2f|%.2f|%ld|%ld|%d|%d|%ld|%d|%.2f|%d|%d|%d|%.2f",
        p->id, p->name, p->type, p->product_id,
        p->discount_rate, p->threshold, p->discount_amount,
        (long)p->start_time, (long)p->end_time,
        p->status, p->priority, (long)p->created_at,
        p->nth_item, p->nth_discount_rate,
        p->buy_quantity, p->free_quantity,
        p->member_level, p->member_price);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有促销 - 原子写入
 */
int save_all_promotions(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/promotion.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    Promotion *p = g_promotions;
    while (p) {
        char line[2048];
        snprintf(line, sizeof(line),
            "%d|%s|%d|%s|%.2f|%.2f|%.2f|%ld|%ld|%d|%d|%ld|%d|%.2f|%d|%d|%d|%.2f\n",
            p->id, p->name, p->type, p->product_id,
            p->discount_rate, p->threshold, p->discount_amount,
            (long)p->start_time, (long)p->end_time,
            p->status, p->priority, (long)p->created_at,
            p->nth_item, p->nth_discount_rate,
            p->buy_quantity, p->free_quantity,
            p->member_level, p->member_price);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        p = p->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

// ==================== 套装模块 ====================

/**
 * 创建套装
 */
int create_combo(ProductCombo *combo) {
    combo->id = generate_id();
    combo->created_at = time(NULL);
    combo->updated_at = combo->created_at;
    combo->status = COMBO_ACTIVE;
    
    ProductCombo *new_combo = (ProductCombo*)malloc(sizeof(ProductCombo));
    *new_combo = *combo;
    new_combo->items = NULL;
    
    new_combo->next = g_combos;
    g_combos = new_combo;
    
    if (strlen(combo->barcode) > 0 && g_combo_barcode_hash) {
        hash_insert(g_combo_barcode_hash, HASH_TABLE_SIZE, combo->barcode, new_combo);
    }
    
    save_combo(new_combo);
    
    return combo->id;
}

/**
 * 添加套装子商品
 */
int add_combo_item(int combo_id, ComboItem *item) {
    ProductCombo *combo = find_combo_by_id(combo_id);
    if (!combo) return -1;
    
    ComboItem *new_item = (ComboItem*)malloc(sizeof(ComboItem));
    *new_item = *item;
    new_item->next = combo->items;
    combo->items = new_item;
    
    Product *prod = find_product_by_id(item->product_id);
    if (prod) {
        combo->cost += prod->cost * item->quantity;
    }
    
    return 0;
}

/**
 * 查找套装（按ID）
 */
ProductCombo* find_combo_by_id(int id) {
    ProductCombo *combo = g_combos;
    while (combo) {
        if (combo->id == id) return combo;
        combo = combo->next;
    }
    return NULL;
}

/**
 * 查找套装（按条码）
 */
ProductCombo* find_combo_by_barcode(const char *barcode) {
    if (!barcode || strlen(barcode) == 0) return NULL;
    if (g_combo_barcode_hash) {
        return (ProductCombo*)hash_search(g_combo_barcode_hash, HASH_TABLE_SIZE, barcode);
    }
    
    ProductCombo *combo = g_combos;
    while (combo) {
        if (strcmp(combo->barcode, barcode) == 0) return combo;
        combo = combo->next;
    }
    return NULL;
}

/**
 * 更新套装
 */
int update_combo(ProductCombo *combo) {
    combo->updated_at = time(NULL);
    
    ProductCombo *existing = find_combo_by_id(combo->id);
    if (existing) {
        *existing = *combo;
        return save_all_combos();
    }
    return -1;
}

/**
 * 删除套装（下架）
 */
int delete_combo(int id) {
    ProductCombo **prev = &g_combos;
    while (*prev) {
        if ((*prev)->id == id) {
            (*prev)->status = COMBO_INACTIVE;
            (*prev)->updated_at = time(NULL);
            return save_all_combos();
        }
        prev = &(*prev)->next;
    }
    return -1;
}

/**
 * 列出所有套装
 */
ProductCombo** list_combos(int *count) {
    ProductCombo **list = NULL;
    *count = 0;
    
    ProductCombo *combo = g_combos;
    while (combo) {
        list = (ProductCombo**)realloc(list, (*count + 1) * sizeof(ProductCombo*));
        list[*count] = combo;
        (*count)++;
        combo = combo->next;
    }
    
    return list;
}

/**
 * 列出生效中的套装
 */
ProductCombo** list_active_combos(int *count) {
    ProductCombo **list = NULL;
    *count = 0;
    
    ProductCombo *combo = g_combos;
    while (combo) {
        if (combo->status == COMBO_ACTIVE) {
            list = (ProductCombo**)realloc(list, (*count + 1) * sizeof(ProductCombo*));
            list[*count] = combo;
            (*count)++;
        }
        combo = combo->next;
    }
    
    return list;
}

/**
 * 检查套装子商品库存
 */
int check_combo_stock(ProductCombo *combo, int store_id, int quantity) {
    (void)store_id;
    
    ComboItem *item = combo->items;
    while (item) {
        Product *prod = find_product_by_id(item->product_id);
        if (!prod) {
            printf("错误: 套装子商品 %s 不存在\n", item->product_name);
            return -1;
        }
        
        int required = item->quantity * quantity;
        if (prod->stock < required) {
            printf("错误: %s 库存不足，需要 %d，当前库存 %d\n", 
                   prod->name, required, prod->stock);
            return -1;
        }
        item = item->next;
    }
    return 0;
}

/**
 * 扣减套装子商品库存
 */
int deduct_combo_stock(ProductCombo *combo, int store_id, int quantity, int operator_id) {
    (void)store_id;
    
    ComboItem *item = combo->items;
    while (item) {
        Product *prod = find_product_by_id(item->product_id);
        if (prod) {
            int deduct_qty = item->quantity * quantity;
            int before_stock = prod->stock;
            prod->stock -= deduct_qty;
            prod->updated_at = time(NULL);
            
            record_stock_log(prod->id, "出库", deduct_qty, 
                           before_stock, prod->stock,
                           operator_id, "套装销售出库");
            
            update_product(prod);
        }
        item = item->next;
    }
    
    return 0;
}

/**
 * 回滚套装子商品库存
 */
int rollback_combo_stock(ProductCombo *combo, int store_id, int quantity, int operator_id) {
    (void)store_id;
    
    ComboItem *item = combo->items;
    while (item) {
        Product *prod = find_product_by_id(item->product_id);
        if (prod) {
            int return_qty = item->quantity * quantity;
            int before_stock = prod->stock;
            prod->stock += return_qty;
            prod->updated_at = time(NULL);
            
            record_stock_log(prod->id, "入库", return_qty, 
                           before_stock, prod->stock,
                           operator_id, "套装退款入库");
            
            update_product(prod);
        }
        item = item->next;
    }
    
    return 0;
}

/**
 * 加载套装数据
 */
int load_combos(void) {
    g_combo_barcode_hash = hash_create(HASH_TABLE_SIZE);
    
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/combo.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        ProductCombo *combo = (ProductCombo*)malloc(sizeof(ProductCombo));
        memset(combo, 0, sizeof(ProductCombo));
        
        char *token = strtok(line, "|");
        combo->id = atoi(token);
        strncpy(combo->name, strtok(NULL, "|"), 99);
        strncpy(combo->barcode, strtok(NULL, "|"), 29);
        combo->price = atof(strtok(NULL, "|"));
        combo->cost = atof(strtok(NULL, "|"));
        combo->status = atoi(strtok(NULL, "|"));
        combo->created_at = (time_t)atoi(strtok(NULL, "|"));
        combo->updated_at = (time_t)atoi(strtok(NULL, "|"));
        
        combo->next = g_combos;
        g_combos = combo;
        
        if (strlen(combo->barcode) > 0) {
            hash_insert(g_combo_barcode_hash, HASH_TABLE_SIZE, combo->barcode, combo);
        }
        
        if (combo->id > g_auto_id_counter) {
            g_auto_id_counter = combo->id;
        }
    }
    
    fclose(fp);
    load_all_combo_items();
    
    return 0;
}

/**
 * 保存套装
 */
int save_combo(ProductCombo *combo) {
    char filepath[256];
    char content[1024];
    
    snprintf(filepath, sizeof(filepath), "%s/combo.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%s|%s|%.2f|%.2f|%d|%ld|%ld",
        combo->id, combo->name, combo->barcode, combo->price, combo->cost,
        combo->status, (long)combo->created_at, (long)combo->updated_at);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有套装
 */
int save_all_combos(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/combo.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    ProductCombo *combo = g_combos;
    while (combo) {
        char line[1024];
        snprintf(line, sizeof(line),
            "%d|%s|%s|%.2f|%.2f|%d|%ld|%ld\n",
            combo->id, combo->name, combo->barcode, combo->price, combo->cost,
            combo->status, (long)combo->created_at, (long)combo->updated_at);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        combo = combo->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

/**
 * 加载所有套装子商品
 */
int load_all_combo_items(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/combo_item.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        int combo_id = atoi(strtok(line, "|"));
        ProductCombo *combo = find_combo_by_id(combo_id);
        if (combo) {
            ComboItem *new_item = (ComboItem*)malloc(sizeof(ComboItem));
            strncpy(new_item->product_id, strtok(NULL, "|"), MAX_ID_LEN - 1);
            strncpy(new_item->product_name, strtok(NULL, "|"), MAX_NAME_LEN - 1);
            new_item->quantity = atoi(strtok(NULL, "|"));
            new_item->ratio = atof(strtok(NULL, "|"));
            new_item->next = combo->items;
            combo->items = new_item;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存套装子商品
 */
int save_combo_item(ComboItem *item) {
    char filepath[256];
    char content[512];
    
    snprintf(filepath, sizeof(filepath), "%s/combo_item.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%s|%s|%d|%.2f",
        generate_id(), item->product_id, item->product_name, 
        item->quantity, item->ratio);
    
    return atomic_append(filepath, content);
}

// ==================== 批次模块 ====================

/**
 * 生成批号
 */
char* generate_batch_no(const char *product_id) {
    static char batch_no[20];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    (void)product_id;
    
    snprintf(batch_no, sizeof(batch_no), "%02d%02d%02d",
             tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday);
    
    int max_seq = 0;
    Batch *b = g_batches;
    while (b) {
        if (strncmp(b->batch_no, batch_no, 6) == 0) {
            int seq = atoi(b->batch_no + 6);
            if (seq > max_seq) max_seq = seq;
        }
        b = b->next;
    }
    
    snprintf(batch_no, sizeof(batch_no), "%02d%02d%02d%03d",
             tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
             max_seq + 1);
    
    return batch_no;
}

/**
 * 获取保质期类别名称
 */
const char* get_expiry_category(int days) {
    if (days <= 0) return "无保质期";
    if (days <= 7) return "短保质期(≤7天)";
    if (days <= 30) return "中等保质期(8-30天)";
    if (days <= 90) return "常规保质期(31-90天)";
    return "长保质期(>90天)";
}

/**
 * 创建批次记录
 */
char* create_batch(const char *product_id, const char *product_name,
                  float quantity, float price,
                  time_t production_date, int expiry_days, int supplier_id) {
    
    Batch *batch = (Batch*)malloc(sizeof(Batch));
    if (!batch) return NULL;
    
    char *batch_no = generate_batch_no(product_id);
    strncpy(batch->batch_no, batch_no, 19);
    strncpy(batch->product_id, product_id, MAX_ID_LEN - 1);
    strncpy(batch->product_name, product_name, MAX_NAME_LEN - 1);
    batch->quantity = quantity;
    batch->initial_quantity = quantity;
    batch->price = price;
    batch->production_date = production_date;
    batch->received_date = time(NULL);
    
    if (expiry_days > 0) {
        batch->expiry_date = production_date + expiry_days * 24 * 3600;
    } else {
        batch->expiry_date = 0;
    }
    
    batch->supplier_id = supplier_id;
    batch->created_at = batch->received_date;
    
    Batch **prev = &g_batches;
    while (*prev && (*prev)->received_date <= batch->received_date) {
        prev = &(*prev)->next;
    }
    batch->next = *prev;
    *prev = batch;
    
    save_batch(batch);
    
    printf("批次创建成功: %s, 商品: %s, 数量: %.2f, 单价: ¥%.2f\n",
           batch_no, product_name, quantity, price);
    
    return batch->batch_no;
}

/**
 * 查找批次
 */
Batch* find_batch(const char *batch_no) {
    Batch *b = g_batches;
    while (b) {
        if (strcmp(b->batch_no, batch_no) == 0) return b;
        b = b->next;
    }
    return NULL;
}

/**
 * 获取商品的批次列表
 */
Batch** list_product_batches(const char *product_id, int *count) {
    Batch **list = NULL;
    *count = 0;
    
    Batch *b = g_batches;
    while (b) {
        if (strcmp(b->product_id, product_id) == 0 && b->quantity > 0) {
            list = (Batch**)realloc(list, (*count + 1) * sizeof(Batch*));
            list[*count] = b;
            (*count)++;
        }
        b = b->next;
    }
    
    return list;
}

/**
 * 先进先出扣库存
 */
float deduct_batch_stock(const char *product_id, float quantity, int operator_id) {
    float remaining = quantity;
    float deducted = 0;
    
    Batch *b = g_batches;
    while (b && remaining > 0) {
        if (strcmp(b->product_id, product_id) == 0 && b->quantity > 0) {
            float deduct = (b->quantity >= remaining) ? remaining : b->quantity;
            b->quantity -= deduct;
            remaining -= deduct;
            deducted += deduct;
            
            Product *prod = find_product_by_id(product_id);
            if (prod) {
                record_stock_log(product_id, "批次出库", deduct,
                               (int)(prod->stock - deducted + remaining),
                               (int)(prod->stock - deducted),
                               operator_id, b->batch_no);
            }
        }
        b = b->next;
    }
    
    if (remaining > 0) {
        printf("[警告] 批次库存不足，需要 %.2f，实际扣减 %.2f\n", quantity, deducted);
    }
    
    return deducted;
}

/**
 * 增加批次库存
 */
int add_batch_stock(const char *batch_no, float quantity) {
    Batch *b = find_batch(batch_no);
    if (!b) return -1;
    
    b->quantity += quantity;
    b->initial_quantity += quantity;
    
    return 0;
}

/**
 * 获取商品总库存
 */
float get_batch_total_stock(const char *product_id) {
    float total = 0;
    Batch *b = g_batches;
    while (b) {
        if (strcmp(b->product_id, product_id) == 0) {
            total += b->quantity;
        }
        b = b->next;
    }
    return total;
}

/**
 * 检查临期批次
 */
void check_expiring_batches(void) {
    list_expiring_batches(BATCH_EXPIRY_WARNING_DAYS, &(int){0});
}

/**
 * 获取即将过期的批次
 */
Batch** list_expiring_batches(int days, int *count) {
    Batch **list = NULL;
    *count = 0;
    time_t now = time(NULL);
    time_t warning_time = now + days * 24 * 3600;
    
    printf("\n========== 临期批次预警 (≤%d天) ==========\n", days);
    
    Batch *b = g_batches;
    while (b) {
        if (b->expiry_date > 0 && b->quantity > 0 && b->expiry_date <= warning_time) {
            int days_left = (b->expiry_date - now) / (24 * 3600);
            
            if (days_left >= 0) {
                printf("[预警] %s | %s | 剩余: %.2f | 剩余天数: %d\n",
                       b->batch_no, b->product_name, b->quantity, days_left);
                list = (Batch**)realloc(list, (*count + 1) * sizeof(Batch*));
                list[*count] = b;
                (*count)++;
            }
        }
        b = b->next;
    }
    
    if (*count == 0) {
        printf("无临期批次\n");
    }
    printf("=========================================\n\n");
    
    return list;
}

/**
 * 加载批次数据
 */
int load_batches(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/batch.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        Batch *b = (Batch*)malloc(sizeof(Batch));
        
        strcpy(b->batch_no, strtok(line, "|"));
        strcpy(b->product_id, strtok(NULL, "|"));
        strcpy(b->product_name, strtok(NULL, "|"));
        b->quantity = atof(strtok(NULL, "|"));
        b->initial_quantity = atof(strtok(NULL, "|"));
        b->price = atof(strtok(NULL, "|"));
        b->production_date = (time_t)atoi(strtok(NULL, "|"));
        b->expiry_date = (time_t)atoi(strtok(NULL, "|"));
        b->received_date = (time_t)atoi(strtok(NULL, "|"));
        b->supplier_id = atoi(strtok(NULL, "|"));
        b->created_at = (time_t)atoi(strtok(NULL, "|"));
        
        Batch **prev = &g_batches;
        while (*prev && (*prev)->received_date <= b->received_date) {
            prev = &(*prev)->next;
        }
        b->next = *prev;
        *prev = b;
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存批次 - 原子追加
 */
int save_batch(Batch *batch) {
    char filepath[256];
    char content[1024];
    
    snprintf(filepath, sizeof(filepath), "%s/batch.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%s|%s|%s|%.2f|%.2f|%.2f|%ld|%ld|%ld|%d|%ld",
        batch->batch_no, batch->product_id, batch->product_name,
        batch->quantity, batch->initial_quantity, batch->price,
        (long)batch->production_date, (long)batch->expiry_date,
        (long)batch->received_date, batch->supplier_id,
        (long)batch->created_at);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有批次 - 原子写入
 */
int save_all_batches(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/batch.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    Batch *b = g_batches;
    while (b) {
        char line[1024];
        snprintf(line, sizeof(line),
            "%s|%s|%s|%.2f|%.2f|%.2f|%ld|%ld|%ld|%d|%ld\n",
            b->batch_no, b->product_id, b->product_name,
            b->quantity, b->initial_quantity, b->price,
            (long)b->production_date, (long)b->expiry_date,
            (long)b->received_date, b->supplier_id,
            (long)b->created_at);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        b = b->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

// ==================== 储值卡模块 ====================

/**
 * 创建储值卡
 */
VipCard* create_vip_card(const char *password) {
    VipCard *card = (VipCard*)malloc(sizeof(VipCard));
    memset(card, 0, sizeof(VipCard));
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(card->card_no, sizeof(card->card_no), 
             "VC%04d%02d%02d%06d", 
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             rand() % 1000000);
    
    card->balance = 0;
    card->total_amount = 0;
    card->card_type = VIPCARD_TYPE_NORMAL;
    card->status = VIPCARD_ACTIVE;
    card->created_at = now;
    card->updated_at = now;
    card->member_id = 0;
    card->expired_at = now + 365 * 24 * 3600;
    
    if (password && strlen(password) > 0) {
        generate_salt(card->password_salt);
        strcpy(card->password_hash, hash_password(password, card->password_salt));
    }
    
    card->next = g_vip_cards;
    g_vip_cards = card;
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "创建储值卡: %s", card->card_no);
    write_transaction_log("VIPCARD", 0, "CREATE", log_msg, 0);
    
    save_vip_card(card);
    
    return card;
}

/**
 * 查找储值卡（按卡号）
 */
VipCard* find_vip_card(const char *card_no) {
    VipCard *card = g_vip_cards;
    while (card) {
        if (strcmp(card->card_no, card_no) == 0) return card;
        card = card->next;
    }
    return NULL;
}

/**
 * 查找储值卡（按会员ID）
 */
VipCard* find_vip_card_by_member(int member_id) {
    VipCard *card = g_vip_cards;
    while (card) {
        if (card->member_id == member_id && card->status == VIPCARD_ACTIVE) {
            return card;
        }
        card = card->next;
    }
    return NULL;
}

/**
 * 验证储值卡支付密码
 */
int verify_vip_card_password(const char *card_no, const char *password) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    char *hash = hash_password(password, card->password_salt);
    return strcmp(hash, card->password_hash) == 0 ? 0 : -1;
}

/**
 * 修改储值卡支付密码
 */
int change_vip_card_password(const char *card_no, const char *old_password, const char *new_password) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    if (verify_vip_card_password(card_no, old_password) != 0) {
        printf("错误: 原密码错误\n");
        return -1;
    }
    
    generate_salt(card->password_salt);
    strcpy(card->password_hash, hash_password(new_password, card->password_salt));
    card->updated_at = time(NULL);
    
    save_all_vip_cards();
    write_transaction_log("VIPCARD", 0, "CHANGE_PASSWORD", card_no, 0);
    
    return 0;
}

/**
 * 绑定会员
 */
int bind_vip_card_member(const char *card_no, int member_id) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    card->member_id = member_id;
    card->updated_at = time(NULL);
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "储值卡%s绑定会员#%d", card_no, member_id);
    write_transaction_log("VIPCARD", member_id, "BIND", log_msg, 0);
    
    return save_all_vip_cards();
}

/**
 * 冻结储值卡
 */
int freeze_vip_card(const char *card_no) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    card->status = VIPCARD_FROZEN;
    card->updated_at = time(NULL);
    
    write_transaction_log("VIPCARD", 0, "FREEZE", card_no, 0);
    
    return save_all_vip_cards();
}

/**
 * 解冻储值卡
 */
int unfreeze_vip_card(const char *card_no) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    card->status = VIPCARD_ACTIVE;
    card->updated_at = time(NULL);
    
    write_transaction_log("VIPCARD", 0, "UNFREEZE", card_no, 0);
    
    return save_all_vip_cards();
}

/**
 * 注销储值卡
 */
int cancel_vip_card(const char *card_no) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    if (card->balance > 0) {
        printf("警告: 储值卡还有余额 %.2f，请先退还\n", card->balance);
        return -1;
    }
    
    card->status = VIPCARD_CANCELLED;
    card->updated_at = time(NULL);
    
    write_transaction_log("VIPCARD", 0, "CANCEL", card_no, 0);
    
    return save_all_vip_cards();
}

/**
 * 设置储值卡过期时间
 */
int set_vip_card_expired(const char *card_no, time_t expired_at) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    card->expired_at = expired_at;
    card->updated_at = time(NULL);
    
    return save_all_vip_cards();
}

/**
 * 列出所有储值卡
 */
VipCard** list_vip_cards(int *count) {
    VipCard **list = NULL;
    *count = 0;
    
    VipCard *card = g_vip_cards;
    while (card) {
        list = (VipCard**)realloc(list, (*count + 1) * sizeof(VipCard*));
        list[*count] = card;
        (*count)++;
        card = card->next;
    }
    
    return list;
}

/**
 * 列出某会员的储值卡
 */
VipCard** list_member_vip_cards(int member_id, int *count) {
    VipCard **list = NULL;
    *count = 0;
    
    VipCard *card = g_vip_cards;
    while (card) {
        if (card->member_id == member_id) {
            list = (VipCard**)realloc(list, (*count + 1) * sizeof(VipCard*));
            list[*count] = card;
            (*count)++;
        }
        card = card->next;
    }
    
    return list;
}

/**
 * 储值卡充值
 */
int recharge_vip_card(const char *card_no, float amount, int operator_id, const char *remark) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    if (card->status != VIPCARD_ACTIVE) {
        printf("错误: 储值卡状态异常\n");
        return -1;
    }
    
    time_t now = time(NULL);
    if (now > card->expired_at) {
        printf("错误: 储值卡已过期\n");
        return -1;
    }
    
    if (amount <= 0) {
        printf("错误: 充值金额必须大于0\n");
        return -1;
    }
    
    VipCardTransaction *trans = (VipCardTransaction*)malloc(sizeof(VipCardTransaction));
    memset(trans, 0, sizeof(VipCardTransaction));
    
    trans->id = generate_id();
    strncpy(trans->card_no, card_no, 29);
    trans->type = 0;
    trans->amount = amount;
    trans->balance_before = card->balance;
    card->balance += amount;
    card->total_amount += amount;
    trans->balance_after = card->balance;
    trans->operator_id = operator_id;
    strncpy(trans->remark, remark ? remark : "", 255);
    trans->created_at = now;
    
    Employee *emp = find_employee_by_id(operator_id);
    if (emp) {
        strncpy(trans->operator_name, emp->name, 49);
    }
    
    card->updated_at = now;
    
    trans->next = g_vip_card_transactions;
    g_vip_card_transactions = trans;
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "储值卡%s充值¥%.2f，余额¥%.2f",
            card_no, amount, card->balance);
    write_transaction_log("VIPCARD", trans->id, "RECHARGE", log_msg, operator_id);
    
    save_vip_card(card);
    save_vip_card_transaction(trans);
    
    return 0;
}

/**
 * 储值卡消费（扣款）
 */
int consume_vip_card(const char *card_no, float amount, int sale_id, int operator_id, const char *remark) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    if (card->status != VIPCARD_ACTIVE) {
        printf("错误: 储值卡状态异常\n");
        return -1;
    }
    
    time_t now = time(NULL);
    if (now > card->expired_at) {
        printf("错误: 储值卡已过期\n");
        return -1;
    }
    
    if (amount <= 0) {
        printf("错误: 消费金额必须大于0\n");
        return -1;
    }
    
    if (card->balance < amount) {
        printf("错误: 余额不足，当前余额 ¥%.2f\n", card->balance);
        return -1;
    }
    
    VipCardTransaction *trans = (VipCardTransaction*)malloc(sizeof(VipCardTransaction));
    memset(trans, 0, sizeof(VipCardTransaction));
    
    trans->id = generate_id();
    strncpy(trans->card_no, card_no, 29);
    trans->type = 1;
    trans->amount = amount;
    trans->balance_before = card->balance;
    card->balance -= amount;
    trans->balance_after = card->balance;
    trans->sale_id = sale_id;
    trans->operator_id = operator_id;
    strncpy(trans->remark, remark ? remark : "", 255);
    trans->created_at = now;
    
    Employee *emp = find_employee_by_id(operator_id);
    if (emp) {
        strncpy(trans->operator_name, emp->name, 49);
    }
    
    card->updated_at = now;
    
    trans->next = g_vip_card_transactions;
    g_vip_card_transactions = trans;
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "储值卡%s消费¥%.2f，余额¥%.2f",
            card_no, amount, card->balance);
    write_transaction_log("VIPCARD", trans->id, "CONSUME", log_msg, operator_id);
    
    save_vip_card(card);
    save_vip_card_transaction(trans);
    
    return 0;
}

/**
 * 储值卡退款
 */
int refund_vip_card(const char *card_no, float amount, int sale_id, int operator_id, const char *remark) {
    VipCard *card = find_vip_card(card_no);
    if (!card) return -1;
    
    if (card->status == VIPCARD_CANCELLED) {
        printf("错误: 储值卡已注销\n");
        return -1;
    }
    
    VipCardTransaction *trans = (VipCardTransaction*)malloc(sizeof(VipCardTransaction));
    memset(trans, 0, sizeof(VipCardTransaction));
    
    trans->id = generate_id();
    strncpy(trans->card_no, card_no, 29);
    trans->type = 2;
    trans->amount = amount;
    trans->balance_before = card->balance;
    card->balance += amount;
    trans->balance_after = card->balance;
    trans->sale_id = sale_id;
    trans->operator_id = operator_id;
    strncpy(trans->remark, remark ? remark : "", 255);
    trans->created_at = time(NULL);
    
    Employee *emp = find_employee_by_id(operator_id);
    if (emp) {
        strncpy(trans->operator_name, emp->name, 49);
    }
    
    card->updated_at = time(NULL);
    
    trans->next = g_vip_card_transactions;
    g_vip_card_transactions = trans;
    
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "储值卡%s退款¥%.2f，余额¥%.2f",
            card_no, amount, card->balance);
    write_transaction_log("VIPCARD", trans->id, "REFUND", log_msg, operator_id);
    
    save_vip_card(card);
    save_vip_card_transaction(trans);
    
    return 0;
}

/**
 * 查询储值卡交易记录
 */
VipCardTransaction** query_vip_card_transactions(const char *card_no, int *count) {
    VipCardTransaction **list = NULL;
    *count = 0;
    
    VipCardTransaction *trans = g_vip_card_transactions;
    while (trans) {
        if (strcmp(trans->card_no, card_no) == 0) {
            list = (VipCardTransaction**)realloc(list, (*count + 1) * sizeof(VipCardTransaction*));
            list[*count] = trans;
            (*count)++;
        }
        trans = trans->next;
    }
    
    return list;
}

/**
 * 查询会员储值卡交易记录
 */
VipCardTransaction** query_member_vip_transactions(int member_id, int *count) {
    VipCardTransaction **list = NULL;
    *count = 0;
    
    VipCard *card = find_vip_card_by_member(member_id);
    if (!card) return list;
    
    return query_vip_card_transactions(card->card_no, count);
}

/**
 * 打印储值卡余额汇总
 */
void print_vip_card_summary(void) {
    printf("\n========== 储值卡余额汇总 ==========\n");
    printf("%-20s %-10s %-10s %-10s %-8s\n", 
           "卡号", "会员ID", "余额", "累计充值", "状态");
    printf("--------------------------------------------------------------\n");
    
    float total_balance = 0;
    int active_count = 0;
    
    VipCard *card = g_vip_cards;
    while (card) {
        const char *status_str;
        switch (card->status) {
            case VIPCARD_ACTIVE: status_str = "正常"; active_count++; break;
            case VIPCARD_FROZEN: status_str = "冻结"; break;
            case VIPCARD_EXPIRED: status_str = "过期"; break;
            case VIPCARD_CANCELLED: status_str = "注销"; break;
            default: status_str = "未知";
        }
        
        printf("%-20s %-10d %-10.2f %-10.2f %-8s\n",
               card->card_no, card->member_id, card->balance, 
               card->total_amount, status_str);
        
        total_balance += card->balance;
        card = card->next;
    }
    
    printf("--------------------------------------------------------------\n");
    printf("合计: 活跃卡数=%d, 总余额=¥%.2f\n", active_count, total_balance);
    printf("================================\n\n");
}

/**
 * 生成储值卡对账单
 */
void generate_vip_card_statement(const char *card_no, time_t start, time_t end) {
    VipCard *card = find_vip_card(card_no);
    if (!card) {
        printf("储值卡不存在\n");
        return;
    }
    
    printf("\n========== 储值卡对账单 ==========\n");
    printf("卡号: %s\n", card_no);
    printf("会员ID: %d\n", card->member_id);
    
    char date_str[32];
    format_time(start, date_str);
    printf("起始日期: %s\n", date_str);
    format_time(end, date_str);
    printf("结束日期: %s\n", date_str);
    
    printf("\n当前余额: ¥%.2f\n", card->balance);
    
    printf("\n【交易记录】\n");
    printf("%-10s %-10s %-10s %-10s %-10s %-20s\n", 
           "交易ID", "类型", "金额", "前余额", "后余额", "时间");
    printf("-----------------------------------------------------------------------------\n");
    
    float total_in = 0, total_out = 0;
    
    VipCardTransaction *trans = g_vip_card_transactions;
    while (trans) {
        if (strcmp(trans->card_no, card_no) == 0 &&
            trans->created_at >= start && trans->created_at <= end) {
            
            const char *type_str;
            switch (trans->type) {
                case 0: type_str = "充值"; total_in += trans->amount; break;
                case 1: type_str = "消费"; total_out += trans->amount; break;
                case 2: type_str = "退款"; total_in += trans->amount; break;
                default: type_str = "未知";
            }
            
            char time_str[32];
            format_time(trans->created_at, time_str);
            
            printf("%-10d %-10s %-10.2f %-10.2f %-10.2f %-20s\n",
                   trans->id, type_str, trans->amount, 
                   trans->balance_before, trans->balance_after, time_str);
        }
        trans = trans->next;
    }
    
    printf("-----------------------------------------------------------------------------\n");
    printf("期间充值: ¥%.2f | 期间消费: ¥%.2f | 净变化: ¥%.2f\n", 
           total_in, total_out, total_in - total_out);
    printf("================================\n\n");
}

/**
 * 加载储值卡
 */
int load_vip_cards(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/vipcard.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        VipCard *card = (VipCard*)malloc(sizeof(VipCard));
        memset(card, 0, sizeof(VipCard));
        
        char *tokens[11];
        int field_count = 0;
        char *token = strtok(line, "|");
        while (token && field_count < 11) {
            tokens[field_count++] = token;
            token = strtok(NULL, "|");
        }
        
        strncpy(card->card_no, tokens[0] ? tokens[0] : "", 29);
        card->member_id = tokens[1] ? atoi(tokens[1]) : 0;
        card->balance = tokens[2] ? atof(tokens[2]) : 0;
        card->total_amount = tokens[3] ? atof(tokens[3]) : 0;
        card->card_type = tokens[4] ? atoi(tokens[4]) : 0;
        card->status = tokens[5] ? atoi(tokens[5]) : 1;
        card->created_at = tokens[6] ? (time_t)atoi(tokens[6]) : 0;
        card->updated_at = tokens[7] ? (time_t)atoi(tokens[7]) : 0;
        card->expired_at = tokens[8] ? (time_t)atoi(tokens[8]) : 0;
        strncpy(card->password_hash, tokens[9] ? tokens[9] : "", 64);
        strncpy(card->password_salt, tokens[10] ? tokens[10] : "", 32);
        
        card->next = g_vip_cards;
        g_vip_cards = card;
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存储值卡
 */
int save_vip_card(VipCard *card) {
    char filepath[256];
    char content[1024];
    
    snprintf(filepath, sizeof(filepath), "%s/vipcard.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%s|%d|%.2f|%.2f|%d|%d|%ld|%ld|%ld|%s|%s",
        card->card_no, card->member_id, card->balance, card->total_amount,
        card->card_type, card->status,
        (long)card->created_at, (long)card->updated_at, (long)card->expired_at,
        card->password_hash, card->password_salt);
    
    return atomic_append(filepath, content);
}

/**
 * 保存所有储值卡
 */
int save_all_vip_cards(void) {
    char filepath[256];
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/vipcard.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    VipCard *card = g_vip_cards;
    while (card) {
        char line[1024];
        snprintf(line, sizeof(line),
            "%s|%d|%.2f|%.2f|%d|%d|%ld|%ld|%ld|%s|%s\n",
            card->card_no, card->member_id, card->balance, card->total_amount,
            card->card_type, card->status,
            (long)card->created_at, (long)card->updated_at, (long)card->expired_at,
            card->password_hash, card->password_salt);
        
        if (strlen(buffer) + strlen(line) + 1 > bufsize) {
            bufsize *= 2;
            buffer = (char*)realloc(buffer, bufsize);
        }
        strcat(buffer, line);
        card = card->next;
    }
    
    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

/**
 * 加载储值卡交易记录
 */
int load_vip_card_transactions(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/vipcard_trans.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;
        
        VipCardTransaction *trans = (VipCardTransaction*)malloc(sizeof(VipCardTransaction));
        memset(trans, 0, sizeof(VipCardTransaction));
        
        char *token = strtok(line, "|");
        trans->id = atoi(token ? token : "0");
        strncpy(trans->card_no, strtok(NULL, "|"), 29);
        trans->type = atoi(strtok(NULL, "|"));
        trans->amount = atof(strtok(NULL, "|"));
        trans->balance_before = atof(strtok(NULL, "|"));
        trans->balance_after = atof(strtok(NULL, "|"));
        trans->operator_id = atoi(strtok(NULL, "|"));
        strncpy(trans->operator_name, strtok(NULL, "|"), 49);
        trans->sale_id = atoi(strtok(NULL, "|"));
        strncpy(trans->remark, strtok(NULL, "|"), 255);
        trans->created_at = (time_t)atoi(strtok(NULL, "|"));
        
        trans->next = g_vip_card_transactions;
        g_vip_card_transactions = trans;
        
        if (trans->id > g_auto_id_counter) {
            g_auto_id_counter = trans->id;
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存交易记录
 */
int save_vip_card_transaction(VipCardTransaction *trans) {
    char filepath[256];
    char content[1024];
    
    snprintf(filepath, sizeof(filepath), "%s/vipcard_trans.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "%d|%s|%d|%.2f|%.2f|%.2f|%d|%s|%d|%s|%ld",
        trans->id, trans->card_no, trans->type, trans->amount,
        trans->balance_before, trans->balance_after,
        trans->operator_id, trans->operator_name,
        trans->sale_id, trans->remark, (long)trans->created_at);
    
    return atomic_append(filepath, content);
}
