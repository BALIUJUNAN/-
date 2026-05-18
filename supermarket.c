/**
 * @file supermarket.c
 * @brief 超市管理系统核心实现 - 数据访问层、哈希表、文件IO
 */

#include "supermarket.h"
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>
#endif

// ==================== 全局变量定义 ====================
int g_auto_id_counter = 1;  // 从1开始自增，会根据已加载数据动态更新
int g_sale_order_counter = 1;  // 销售订单专用计数器，从1开始，每次+1
int g_member_id_counter = 1;    // 会员ID专用计数器，从1开始，每次+1
static HashTable *g_employee_hash = NULL;
HashTable *g_product_hash = NULL;
static HashTable *g_barcode_hash = NULL;
static HashTable *g_supplier_hash = NULL;
HashTable *g_member_phone_hash = NULL;  // 会员手机号哈希表
Member *g_members = NULL;  // 会员链表头指针

// 营销模块全局变量
ProductCombo *g_combos = NULL;
HashTable *g_combo_barcode_hash = NULL;
Batch *g_batches = NULL;
Promotion *g_promotions = NULL;
VipCard *g_vip_cards = NULL;

// ==================== 哈希表实现 ====================

/**
 * 创建哈希表
 */
HashTable* hash_create(int size) {
    HashTable *ht = (HashTable*)malloc(sizeof(HashTable));
    if (!ht) {
        fprintf(stderr, "分配哈希表结构失败\n");
        return NULL;
    }
    ht->buckets = (HashNode**)calloc(size, sizeof(HashNode*));
    if (!ht->buckets) {
        fprintf(stderr, "分配哈希表桶失败\n");
        free(ht);
        return NULL;
    }
    ht->size = size;
    return ht;
}

/**
 * 简单哈希函数（使用 djb2 算法）
 */
static unsigned int simple_hash(const char *key, int size) {
    unsigned int hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    return hash % size;
}

/**
 * 插入哈希表（检测重复key，已存在则更新data）
 */
int hash_insert(HashTable *table, const char *key, void *data) {
    unsigned int index = simple_hash(key, table->size);

    // 检查是否已存在相同key
    HashNode *node = table->buckets[index];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            node->data = data;  // 更新已有节点
            return 0;
        }
        node = node->next;
    }

    HashNode *new_node = (HashNode*)malloc(sizeof(HashNode));
    if (!new_node) return -1;

    strncpy(new_node->key, key, 63);
    new_node->key[63] = '\0';
    new_node->data = data;
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;

    return 0;
}

/**
 * 查找哈希表
 */
void* hash_search(HashTable *table, const char *key) {
    if (!table || !key) {
        return NULL;
    }

    unsigned int index = simple_hash(key, table->size);
    HashNode *node = table->buckets[index];

    while (node) {
        if (strcmp(node->key, key) == 0) {
            return node->data;
        }
        node = node->next;
    }
    return NULL;
}

/**
 * 删除哈希表项
 */
int hash_delete(HashTable *table, const char *key) {
    unsigned int index = simple_hash(key, table->size);
    HashNode *node = table->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                table->buckets[index] = node->next;
            }
            free(node);
            return 0;
        }
        prev = node;
        node = node->next;
    }
    return -1;
}

/**
 * 销毁哈希表
 */
void hash_destroy(HashTable *table, void (*free_data)(void*)) {
    if (!table) return;
    for (int i = 0; i < table->size; i++) {
        HashNode *node = table->buckets[i];
        while (node) {
            HashNode *next = node->next;
            if (free_data && node->data) {
                free_data(node->data);
            }
            free(node);
            node = next;
        }
    }
    free(table->buckets);
    free(table);
}

// ==================== 文件IO操作 ====================

/**
 * 确保目录存在
 */
static int ensure_dir(const char *dir) {
#ifdef _WIN32
    return _mkdir(dir);
#else
    return mkdir(dir, 0755);
#endif
}

/**
 * 文件加锁（Windows 实现）
 */
#ifdef _WIN32
int file_lock(FILE *fp, int operation) {
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(fp));
    OVERLAPPED ovlp = {0};
    
    if (operation == LOCKFILE_LOCK) {
        return LockFileEx(hFile, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ovlp) ? 0 : -1;
    } else {
        return UnlockFileEx(hFile, 0, 1, 0, &ovlp) ? 0 : -1;
    }
}
#else
int file_lock(FILE *fp, int operation) {
    if (operation == LOCKFILE_LOCK) {
        return flock(fileno(fp), LOCK_EX);
    } else {
        return flock(fileno(fp), LOCK_UN);
    }
}
#endif

int file_unlock(FILE *fp) {
    return file_lock(fp, LOCKFILE_UNLOCK);
}

/**
 * 安全写入（带锁）
 */
int safe_write(const char *filepath, const char *content) {
    FILE *fp = fopen(filepath, "a");
    if (!fp) return -1;
    
    if (file_lock(fp, LOCKFILE_LOCK) != 0) {
        fclose(fp);
        return -1;
    }
    
    fprintf(fp, "%s\n", content);
    file_unlock(fp);
    fclose(fp);
    return 0;
}

// Windows 辅助函数 - 检查文件是否存在（需要在atomic_write之前声明）
#ifdef _WIN32
static int FileExists(const char *path) {
    return access(path, 0) == 0;
}
#endif

/**
 * 原子写入（临时文件 + rename）
 * 流程：创建.tmp → 写入 → fflush → fsync → rename → 失败时删除.tmp
 * 适用于全量覆盖写入
 */
int atomic_write(const char *filepath, const char *content) {
    char tmppath[512];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", filepath);
    
    FILE *fp = fopen(tmppath, "w");
    if (!fp) {
        fprintf(stderr, "无法创建临时文件: %s\n", tmppath);
        return -1;
    }
    
    // 写入数据
    fputs(content, fp);
    
    // 确保数据写入磁盘
    fflush(fp);
    
#ifdef _WIN32
    // Windows: 使用 _commit 强制同步
    _commit(_fileno(fp));
#else
    // Linux: 使用 fsync
    fsync(fileno(fp));
#endif
    
    fclose(fp);
    
    // Windows 需要先删除原文件
#ifdef _WIN32
    if (FileExists(filepath)) {
        if (remove(filepath) != 0) {
            fprintf(stderr, "无法删除原文件: %s\n", filepath);
            remove(tmppath);
            return -1;
        }
    }
#endif
    
    // 原子替换
    if (rename(tmppath, filepath) != 0) {
        fprintf(stderr, "rename失败: %s -> %s\n", tmppath, filepath);
        remove(tmppath);
        return -1;
    }
    
    return 0;
}

/**
 * 原子追加写入（追加模式）
 * 读取原文件 → 追加内容 → 原子替换
 */
int atomic_append(const char *filepath, const char *content) {
    // 读取原文件内容
    char *existing = NULL;
    long fsize = 0;
    
    FILE *fpr = fopen(filepath, "rb");
    if (fpr) {
        fseek(fpr, 0, SEEK_END);
        fsize = ftell(fpr);
        fseek(fpr, 0, SEEK_SET);
        
        if (fsize > 0) {
            existing = (char*)malloc(fsize + 1);
            fread(existing, 1, fsize, fpr);
            existing[fsize] = '\0';
        }
        fclose(fpr);
    }
    
    // 构建新内容
    char *new_content;
    if (existing) {
        new_content = (char*)malloc(fsize + strlen(content) + 2);
        snprintf(new_content, fsize + strlen(content) + 2, "%s%s\n", existing, content);
        free(existing);
    } else {
        new_content = (char*)malloc(strlen(content) + 2);
        snprintf(new_content, strlen(content) + 2, "%s\n", content);
    }
    
    // 使用原子写入
    int result = atomic_write(filepath, new_content);
    free(new_content);
    
    return result;
}

/**
 * 读取文件所有行到动态数组
 */
char** read_lines(const char *filepath, int *count) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        *count = 0;
        return NULL;
    }
    
    char **lines = NULL;
    char buffer[MAX_LINE_LEN];
    int capacity = 100;
    int size = 0;
    
    lines = (char**)malloc(capacity * sizeof(char*));
    
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (size >= capacity) {
            capacity *= 2;
            lines = (char**)realloc(lines, capacity * sizeof(char*));
        }
        lines[size] = strdup(buffer);
        size++;
    }
    
    fclose(fp);
    *count = size;
    return lines;
}

/**
 * 释放行数组
 */
void free_lines(char **lines, int count) {
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
}

// ==================== 工具函数 ====================

/**
 * 去除字符串首尾空白
 */
char* trim(char *str) {
    if (!str) return str;
    
    // 去除首部空白
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // 去除尾部空白
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    
    return str;
}

/**
 * 生成随机盐值
 */
char* generate_salt(char *salt) {
    static const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < 32; i++) {
        salt[i] = charset[rand() % 36];
    }
    salt[32] = '\0';
    return salt;
}

// ==================== SHA-256 实现 ====================

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j+1] << 16) | ((uint32_t)data[j+2] << 8) | (uint32_t)data[j+3];
    for ( ; i < 64; ++i)
        m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void sha256_final(SHA256_CTX *ctx, uint8_t hash[SHA256_BLOCK_SIZE]) {
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

void sha256_hash_to_hex(const uint8_t hash[SHA256_BLOCK_SIZE], char hex[65]) {
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    hex[64] = '\0';
}

/**
 * 密码哈希 - 使用 SHA-256 (salt + password)
 */
char* hash_password(const char *password, const char *salt) {
    static char hash_hex[65];
    uint8_t hash[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)salt, strlen(salt));
    sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    sha256_final(&ctx, hash);
    sha256_hash_to_hex(hash, hash_hex);

    return hash_hex;
}

/**
 * 获取时间戳字符串
 */
void get_timestamp(char *buffer) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", t);
}

/**
 * 获取当前年份和周数
 */
int get_year_week(int *year, int *week) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    *year = t->tm_year + 1900;
    
    // 计算第几周（从每年第一周开始）
    struct tm first_day = {0};
    first_day.tm_year = *year - 1900;
    first_day.tm_mon = 0;
    first_day.tm_mday = 1;
    time_t first_time = mktime(&first_day);
    
    // 第一天是星期几（0=周日）
    int first_wday = (first_time - now) / (7 * 24 * 3600);
    *week = ((t->tm_yday + first_wday) / 7) + 1;
    
    return 0;
}

/**
 * 生成唯一ID（简单的自增序号）
 * 格式: 基于 g_auto_id_counter，确保连续不跳跃
 * 注意: 需要在启动时调用 init_sale_id_counter() 来初始化计数器
 */
int generate_id(void) {
    return ++g_auto_id_counter;
}

/**
 * 初始化销售订单ID计数器
 * 从 sales.txt 和 pending_sales.txt 中扫描最大订单号，确保新订单从最大值继续
 */
void init_sale_id_counter(void) {
    char filepath[256];
    int max_sale_id = 0;
    
    // 扫描 sales.txt 获取最大订单号
    snprintf(filepath, sizeof(filepath), "%s/sales.txt", DATA_DIR);
    FILE *fp = fopen(filepath, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            char *token = strtok(line, "|");
            if (token) {
                int id = atoi(token);
                if (id > max_sale_id) {
                    max_sale_id = id;
                }
            }
        }
        fclose(fp);
    }
    
    // 也检查 pending_sales.txt
    snprintf(filepath, sizeof(filepath), "%s/pending_sales.txt", DATA_DIR);
    fp = fopen(filepath, "r");
    if (fp) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            trim(line);
            if (strlen(line) == 0) continue;
            
            char *token = strtok(line, "|");
            if (token) {
                int id = atoi(token);
                if (id > max_sale_id) {
                    max_sale_id = id;
                }
            }
        }
        fclose(fp);
    }
    
    // 设置销售订单计数器 = 最大ID，下次会+1
    g_sale_order_counter = max_sale_id;
}

/**
 * 生成销售订单ID
 * 每次调用返回上一个值+1
 */
int generate_sale_order_id(void) {
    return ++g_sale_order_counter;
}

// ==================== 初始化/清理 ====================

/**
 * 初始化系统
 */
int init_system(void) {
    srand((unsigned int)time(NULL));

    // 创建必要目录
    ensure_dir(DATA_DIR);
    ensure_dir(TMP_DIR);
    ensure_dir(OUTPUT_DIR);
    
    // 初始化哈希表
    g_employee_hash = hash_create(HASH_TABLE_SIZE);
    g_product_hash = hash_create(HASH_TABLE_SIZE);
    g_barcode_hash = hash_create(HASH_TABLE_SIZE);
    g_supplier_hash = hash_create(HASH_TABLE_SIZE);
    g_member_phone_hash = hash_create(HASH_TABLE_SIZE);  // 会员手机号哈希表
    
    if (!g_employee_hash || !g_product_hash || !g_barcode_hash || 
        !g_supplier_hash || !g_member_phone_hash) {
        fprintf(stderr, "初始化哈希表失败\n");
        return -1;
    }
    
    // 加载系统配置
    load_config();
    
    // 从文件加载数据
    load_employees();
    load_products();
    load_suppliers();
    load_members();        // 加载会员数据
    load_settlements();    // 加载日结单数据
    load_batches();        // 加载批次数据
    load_promotions();     // 加载促销数据
    
    printf("[%s] 系统初始化完成\n", __TIME__);
    return 0;
}

/**
 * 清理系统资源
 */
void cleanup_system(void) {
    // 保存数据到文件
    save_employees();
    save_products();
    save_suppliers();
    save_members();      // 保存会员数据
    save_all_settlements(); // 保存日结单
    save_all_batches();  // 保存批次数据
    save_all_promotions(); // 保存促销数据
    save_config();       // 保存系统配置
    
    // 释放哈希表
    if (g_employee_hash) {
        hash_destroy(g_employee_hash, free);
    }
    if (g_product_hash) {
        hash_destroy(g_product_hash, free);
    }
    if (g_barcode_hash) {
        hash_destroy(g_barcode_hash, NULL);
    }
    if (g_supplier_hash) {
        hash_destroy(g_supplier_hash, free);
    }
    if (g_member_phone_hash) {
        hash_destroy(g_member_phone_hash, free);
    }
    if (g_combo_barcode_hash) {
        hash_destroy(g_combo_barcode_hash, NULL);
    }

    printf("系统资源已清理\n");
}

// ==================== 员工数据操作 ====================

/**
 * 添加员工
 */
int add_employee(Employee *emp) {
    emp->id = generate_id();
    emp->created_at = time(NULL);
    emp->updated_at = emp->created_at;
    emp->status = STATUS_ACTIVE;
    
    Employee *new_emp = (Employee*)malloc(sizeof(Employee));
    *new_emp = *emp;

    char key[32];
    sprintf(key, "%d", emp->id);
    hash_insert(g_employee_hash, key, new_emp);

    return emp->id;
}

/**
 * 查找员工（按ID）
 */
Employee* find_employee_by_id(int id) {
    char key[32];
    sprintf(key, "%d", id);
    return (Employee*)hash_search(g_employee_hash, key);
}

/**
 * 更新员工
 */
int update_employee(Employee *emp) {
    emp->updated_at = time(NULL);
    
    Employee *existing = find_employee_by_id(emp->id);
    if (existing) {
        *existing = *emp;
        return 0;
    }
    return -1;
}

/**
 * 删除员工（标记离职）
 */
int delete_employee(int id) {
    Employee *emp = find_employee_by_id(id);
    if (emp) {
        emp->status = STATUS_INACTIVE;
        emp->updated_at = time(NULL);
        return 0;
    }
    return -1;
}

/**
 * 加载员工数据
 */
int load_employees(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/employee.txt", DATA_DIR);

    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;  // 文件不存在不算错误

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;

        Employee *emp = (Employee*)malloc(sizeof(Employee));
        memset(emp, 0, sizeof(Employee));
        char *token;
        token = strtok(line, "|"); emp->id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); if (token) strncpy(emp->name, token, MAX_NAME_LEN - 1);
        token = strtok(NULL, "|"); if (token) strncpy(emp->role, token, 19);
        token = strtok(NULL, "|"); if (token) strncpy(emp->password_hash, token, 64);
        token = strtok(NULL, "|"); if (token) strncpy(emp->salt, token, 32);
        token = strtok(NULL, "|"); emp->status = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); emp->created_at = token ? (time_t)atoll(token) : 0;
        token = strtok(NULL, "|"); emp->updated_at = token ? (time_t)atoll(token) : 0;

        char key[32];
        sprintf(key, "%d", emp->id);
        hash_insert(g_employee_hash, key, emp);

        // 更新自增ID
        if (emp->id > g_auto_id_counter) {
            g_auto_id_counter = emp->id;
        }
    }

    fclose(fp);
    return 0;
}

/**
 * 保存员工数据 - 原子写入版本
 * 构建内容 → atomic_write → rename
 */
int save_employees(void) {
    char filepath[256];
    size_t bufsize = 65536;
    char *buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    char *pos = buffer;
    size_t remaining = bufsize;

    snprintf(filepath, sizeof(filepath), "%s/employee.txt", DATA_DIR);

    for (int i = 0; i < g_employee_hash->size; i++) {
        HashNode *node = g_employee_hash->buckets[i];
        while (node) {
            Employee *emp = (Employee*)node->data;
            int written = snprintf(pos, remaining, "%d|%s|%s|%s|%s|%d|%lld|%lld\n",
                emp->id, emp->name, emp->role, emp->password_hash,
                emp->salt, emp->status, (long long)emp->created_at, (long long)emp->updated_at);

            if (written >= (int)remaining) {
                size_t offset = pos - buffer;
                bufsize *= 2;
                buffer = (char*)realloc(buffer, bufsize);
                if (!buffer) return -1;
                pos = buffer + offset;
                remaining = bufsize - offset;
                written = snprintf(pos, remaining, "%d|%s|%s|%s|%s|%d|%lld|%lld\n",
                    emp->id, emp->name, emp->role, emp->password_hash,
                    emp->salt, emp->status, (long long)emp->created_at, (long long)emp->updated_at);
            }
            pos += written;
            remaining -= written;
            node = node->next;
        }
    }

    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

/**
 * 列出所有员工
 */
Employee** list_employees(int *count) {
    Employee **list = NULL;
    int capacity = 0;
    *count = 0;

    for (int i = 0; i < g_employee_hash->size; i++) {
        HashNode *node = g_employee_hash->buckets[i];
        while (node) {
            Employee *emp = (Employee*)node->data;
            if (emp->status == STATUS_ACTIVE) {
                if (*count >= capacity) {
                    capacity = capacity == 0 ? 16 : capacity * 2;
                    list = (Employee**)realloc(list, capacity * sizeof(Employee*));
                }
                list[*count] = emp;
                (*count)++;
            }
            node = node->next;
        }
    }

    return list;
}

// ==================== 商品数据操作 ====================

/**
 * 添加商品
 */
int add_product(Product *prod) {
    prod->created_at = time(NULL);
    prod->updated_at = prod->created_at;
    prod->status = 1;
    
    Product *new_prod = (Product*)malloc(sizeof(Product));
    *new_prod = *prod;
    
    hash_insert(g_product_hash, prod->id, new_prod);
    if (strlen(prod->barcode) > 0) {
        hash_insert(g_barcode_hash, prod->barcode, new_prod);
    }
    
    return 0;
}

/**
 * 查找商品（按ID）
 */
Product* find_product_by_id(const char *id) {
    return (Product*)hash_search(g_product_hash, id);
}

/**
 * 查找商品（按条码）
 */
Product* find_product_by_barcode(const char *barcode) {
    return (Product*)hash_search(g_barcode_hash, barcode);
}

/**
 * 更新商品
 */
int update_product(Product *prod) {
    prod->updated_at = time(NULL);
    
    Product *existing = find_product_by_id(prod->id);
    if (existing) {
        *existing = *prod;
        return 0;
    }
    return -1;
}

/**
 * 删除商品（标记下架）
 */
int delete_product(const char *id) {
    Product *prod = find_product_by_id(id);
    if (prod) {
        prod->status = 0;
        prod->updated_at = time(NULL);
        return 0;
    }
    return -1;
}

/**
 * 加载商品数据
 */
int load_products(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/product.txt", DATA_DIR);

    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;

        Product *prod = (Product*)malloc(sizeof(Product));
        memset(prod, 0, sizeof(Product));
        char *token;
        token = strtok(line, "|"); if (token) strncpy(prod->id, token, MAX_ID_LEN - 1);
        token = strtok(NULL, "|"); if (token) strncpy(prod->name, token, MAX_NAME_LEN - 1);
        token = strtok(NULL, "|"); if (token) strncpy(prod->barcode, token, 29);
        token = strtok(NULL, "|"); prod->price = token ? atof(token) : 0.0f;
        token = strtok(NULL, "|"); prod->cost = token ? atof(token) : 0.0f;
        token = strtok(NULL, "|"); prod->stock = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); prod->min_stock = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); if (token) strncpy(prod->category_id, token, 19);
        token = strtok(NULL, "|"); if (token) strncpy(prod->supplier_id, token, 19);
        token = strtok(NULL, "|"); prod->status = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); prod->created_at = token ? (time_t)atoll(token) : 0;
        token = strtok(NULL, "|"); prod->updated_at = token ? (time_t)atoll(token) : 0;

        hash_insert(g_product_hash, prod->id, prod);
        if (strlen(prod->barcode) > 0) {
            hash_insert(g_barcode_hash, prod->barcode, prod);
        }
    }

    fclose(fp);
    return 0;
}

/**
 * 保存商品数据 - 原子写入版本
 */
int save_products(void) {
    char filepath[256];
    size_t bufsize = 65536;
    char *buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    char *pos = buffer;
    size_t remaining = bufsize;

    snprintf(filepath, sizeof(filepath), "%s/product.txt", DATA_DIR);

    for (int i = 0; i < g_product_hash->size; i++) {
        HashNode *node = g_product_hash->buckets[i];
        while (node) {
            Product *prod = (Product*)node->data;
            int written = snprintf(pos, remaining, "%s|%s|%s|%.2f|%.2f|%d|%d|%s|%s|%d|%lld|%lld\n",
                prod->id, prod->name, prod->barcode, prod->price, prod->cost,
                prod->stock, prod->min_stock, prod->category_id, prod->supplier_id,
                prod->status, (long long)prod->created_at, (long long)prod->updated_at);

            if (written >= (int)remaining) {
                size_t offset = pos - buffer;
                bufsize *= 2;
                buffer = (char*)realloc(buffer, bufsize);
                if (!buffer) return -1;
                pos = buffer + offset;
                remaining = bufsize - offset;
                written = snprintf(pos, remaining, "%s|%s|%s|%.2f|%.2f|%d|%d|%s|%s|%d|%lld|%lld\n",
                    prod->id, prod->name, prod->barcode, prod->price, prod->cost,
                    prod->stock, prod->min_stock, prod->category_id, prod->supplier_id,
                    prod->status, (long long)prod->created_at, (long long)prod->updated_at);
            }
            pos += written;
            remaining -= written;
            node = node->next;
        }
    }

    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

// ==================== 供应商数据操作 ====================

/**
 * 添加供应商
 */
int add_supplier(Supplier *sup) {
    sup->id = generate_id();
    sup->status = 1;
    
    Supplier *new_sup = (Supplier*)malloc(sizeof(Supplier));
    *new_sup = *sup;
    
    char key[32];
    sprintf(key, "%d", sup->id);
    hash_insert(g_supplier_hash, key, new_sup);
    
    return sup->id;
}

/**
 * 查找供应商
 */
Supplier* find_supplier_by_id(int id) {
    char key[32];
    sprintf(key, "%d", id);
    return (Supplier*)hash_search(g_supplier_hash, key);
}

/**
 * 加载供应商
 */
int load_suppliers(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/supplier.txt", DATA_DIR);

    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0) continue;

        Supplier *sup = (Supplier*)malloc(sizeof(Supplier));
        memset(sup, 0, sizeof(Supplier));
        char *token;
        token = strtok(line, "|"); if (token) strncpy(sup->name, token, MAX_NAME_LEN-1);
        token = strtok(NULL, "|"); if (token) strncpy(sup->contact, token, 49);
        token = strtok(NULL, "|"); if (token) strncpy(sup->phone, token, 19);
        token = strtok(NULL, "|"); if (token) strncpy(sup->address, token, 255);
        token = strtok(NULL, "|"); sup->id = token ? atoi(token) : 0;
        token = strtok(NULL, "|"); sup->status = token ? atoi(token) : 0;

        char key[32];
        sprintf(key, "%d", sup->id);
        hash_insert(g_supplier_hash, key, sup);

        if (sup->id > g_auto_id_counter) {
            g_auto_id_counter = sup->id;
        }
    }

    fclose(fp);
    return 0;
}

/**
 * 保存供应商 - 原子写入版本
 */
int save_suppliers(void) {
    char filepath[256];
    size_t bufsize = 65536;
    char *buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    char *pos = buffer;
    size_t remaining = bufsize;

    snprintf(filepath, sizeof(filepath), "%s/supplier.txt", DATA_DIR);

    for (int i = 0; i < g_supplier_hash->size; i++) {
        HashNode *node = g_supplier_hash->buckets[i];
        while (node) {
            Supplier *sup = (Supplier*)node->data;
            int written = snprintf(pos, remaining, "%s|%s|%s|%s|%d|%d\n",
                sup->name, sup->contact, sup->phone, sup->address, sup->id, sup->status);

            if (written >= (int)remaining) {
                size_t offset = pos - buffer;
                bufsize *= 2;
                buffer = (char*)realloc(buffer, bufsize);
                if (!buffer) return -1;
                pos = buffer + offset;
                remaining = bufsize - offset;
                written = snprintf(pos, remaining, "%s|%s|%s|%s|%d|%d\n",
                    sup->name, sup->contact, sup->phone, sup->address, sup->id, sup->status);
            }
            pos += written;
            remaining -= written;
            node = node->next;
        }
    }

    int result = atomic_write(filepath, buffer);
    free(buffer);
    return result;
}

// ==================== 系统配置 ====================
SystemConfig g_config = {
    .shop_name = "超市管理系统",
    .shop_address = "",
    .shop_phone = "",
    .tax_rate = 0.0f,
    .auto_backup_interval = 30,
    .monthly_fixed_cost = 10000.0f  // 默认月固定成本
};

/**
 * 加载系统配置
 */
int load_config(void) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/config.txt", DATA_DIR);
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (strlen(line) == 0 || line[0] == '#') continue;
        
        char key[64], value[256];
        if (sscanf(line, "%63[^=]=%255[^\n]", key, value) == 2) {
            trim(key);
            trim(value);
            
            if (strcmp(key, "shop_name") == 0) strncpy(g_config.shop_name, value, 99);
            else if (strcmp(key, "shop_address") == 0) strncpy(g_config.shop_address, value, 255);
            else if (strcmp(key, "shop_phone") == 0) strncpy(g_config.shop_phone, value, 19);
            else if (strcmp(key, "tax_rate") == 0) g_config.tax_rate = atof(value);
            else if (strcmp(key, "auto_backup_interval") == 0) g_config.auto_backup_interval = atoi(value);
            else if (strcmp(key, "monthly_fixed_cost") == 0) g_config.monthly_fixed_cost = atof(value);
        }
    }
    
    fclose(fp);
    return 0;
}

/**
 * 保存系统配置
 */
int save_config(void) {
    char filepath[256];
    char content[2048];
    
    snprintf(filepath, sizeof(filepath), "%s/config.txt", DATA_DIR);
    
    snprintf(content, sizeof(content),
        "# 超市管理系统配置\n"
        "shop_name=%s\n"
        "shop_address=%s\n"
        "shop_phone=%s\n"
        "tax_rate=%.2f\n"
        "auto_backup_interval=%d\n"
        "monthly_fixed_cost=%.2f\n",
        g_config.shop_name,
        g_config.shop_address,
        g_config.shop_phone,
        g_config.tax_rate,
        g_config.auto_backup_interval,
        g_config.monthly_fixed_cost
    );
    
    return atomic_write(filepath, content);
}
