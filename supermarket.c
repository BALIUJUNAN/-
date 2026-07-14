/**
 * @file supermarket.c
 * @brief 超市管理系统核心实现 - 数据访问层、哈希表、文件IO
 *
 * 本文件是整个系统的基础设施层，负责：
 *   1. 哈希表数据结构实现（djb2 算法 + 链表法解决冲突）
 *   2. 文件IO操作（原子写入、文件锁、行读取）
 *   3. 工具函数（trim、SHA-256密码哈希、时间戳、ID生成）
 *   4. 员工/商品/供应商 的 CRUD 操作和持久化
 *   5. 系统初始化和资源清理
 *
 * 数据持久化策略：
 *   - 全量覆盖：atomic_write（临时文件 + rename，用于 save_all_xxx）
 *   - 原子追加：atomic_append（带锁的 append 模式，用于单条记录写入）
 *   - 所有数据文件存储在 data/ 目录下，以 | 分隔的纯文本格式
 */

#include "supermarket.h"
#include "app/sm_app_context.h"
#include "app/sm_base_service.h"
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>
#endif

// ==================== 全局变量定义 ====================

/*
 * ID 计数器设计说明：
 *   每类实体有独立的自增计数器，确保 ID 在各自范围内唯一。
 *   初始值均为 0，首次 ++ 后变为 1（从 1 开始）。
 *   启动时通过 load_xxx() 从文件加载数据，自动将计数器更新为历史最大值，
 *   保证重启后新 ID 不会与已有数据冲突。
 */
int g_auto_id_counter = 0;       // 通用自增计数器（供应商等辅助实体使用）
int g_member_id_counter = 0;     // 会员ID专用计数器（格式：纯数字）
int g_employee_id_counter = 0;   // 员工ID专用计数器（格式：纯数字）
int g_product_id_counter = 0;    // 商品ID专用计数器（格式：P0001, P0002...）
int g_transfer_id_counter = 0;   // 调拨单专用计数器（格式：纯数字 1,2,3...）

/*
 * 哈希表全局变量：
 *   g_employee_hash   — 员工表（key=员工ID字符串，value=Employee*）
 *   g_product_hash    — 商品表（key=商品ID字符串，value=Product*）
 *   g_barcode_hash    — 条码索引表（key=条码字符串，value=Product*，与 g_product_hash 共享同一对象）
 *   g_supplier_hash   — 供应商表（key=供应商ID字符串，value=Supplier*）
 *   g_member_phone_hash — 会员手机号索引表（key=手机号，value=Member*，与 g_members 链表共享对象）
 */
static HashTable *g_employee_hash = NULL;
static HashTable *g_product_hash = NULL;
static HashTable *g_barcode_hash = NULL;
static HashTable *g_supplier_hash = NULL;
HashTable *g_member_phone_hash = NULL;
Member *g_members = NULL;  // 会员链表头指针（与 g_member_phone_hash 共享 Member 对象）

// 营销模块全局变量（套装/批次/促销/储值卡均为链表结构）
ProductCombo *g_combos = NULL;           // 套装链表
HashTable *g_combo_barcode_hash = NULL;  // 套装条码索引表
Batch *g_batches = NULL;                 // 批次链表（按收货日期排序）
Promotion *g_promotions = NULL;          // 促销链表（按优先级排序）
VipCard *g_vip_cards = NULL;             // 储值卡链表

// ==================== 哈希表实现 ====================

/**
 * 创建哈希表
 *
 * 分配 HashTable 结构体和桶数组（calloc 初始化为 NULL）。
 * 使用质数作为桶数量（如 10007）可减少哈希冲突。
 *
 * @param size  桶的数量（建议使用质数，如 HASH_TABLE_SIZE=10007）
 * @return 哈希表指针，失败返回 NULL
 */
HashTable* hash_create(int size) {
    HashTable *ht = (HashTable*)malloc(sizeof(HashTable));
    if (!ht) {
        fprintf(stderr, "分配哈希表结构失败\n");
        return NULL;
    }
    /* calloc 将所有桶初始化为 NULL（空链表头） */
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
 * djb2 哈希函数
 *
 * 经典的字符串哈希算法，由 Daniel J. Bernstein 提出。
 * 核心公式：hash = hash * 33 + c，对每个字符迭代计算。
 * 初始种子 5381 是经验值，能产生良好的分布效果。
 *
 * @param key   待哈希的字符串
 * @param size  桶的数量（取模用）
 * @return 哈希值（桶索引，范围 [0, size)）
 */
static unsigned int simple_hash(const char *key, int size) {
    unsigned int hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;  /* 等价于 hash * 33 + c，位运算更快 */
    }
    return hash % size;
}

/**
 * 向哈希表插入键值对
 *
 * 使用链表法处理冲突：新节点头插到对应桶的链表头部（O(1)）。
 * 如果 key 已存在，则更新其关联的 data 指针（不创建重复节点）。
 *
 * @param table  哈希表指针
 * @param key    键（字符串，最长 63 字节）
 * @param data   值（void*，通常是指向结构体的指针）
 * @return 0 成功，-1 内存分配失败
 */
int hash_insert(HashTable *table, const char *key, void *data) {
    unsigned int index = simple_hash(key, table->size);

    /* 检查是否已存在相同 key（遍历桶内链表） */
    HashNode *node = table->buckets[index];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            node->data = data;  /* key 已存在，仅更新 data */
            return 0;
        }
        node = node->next;
    }

    /* key 不存在，创建新节点并头插到链表 */
    HashNode *new_node = (HashNode*)malloc(sizeof(HashNode));
    if (!new_node) return -1;

    strncpy(new_node->key, key, 63);
    new_node->key[63] = '\0';
    new_node->data = data;
    new_node->next = table->buckets[index];  /* 新节点指向原链表头 */
    table->buckets[index] = new_node;        /* 桶头指向新节点 */

    return 0;
}

/**
 * 在哈希表中查找指定 key
 *
 * 计算哈希值定位桶，然后遍历桶内链表逐一比较 key。
 * 平均时间复杂度 O(1)，最坏 O(n)（所有 key 冲突到同一桶）。
 *
 * @param table  哈希表指针
 * @param key    待查找的键
 * @return 关联的 data 指针，未找到返回 NULL
 */
void* hash_search(HashTable *table, const char *key) {
    if (!table || !key) return NULL;

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
 * 从哈希表中删除指定 key
 *
 * 遍历桶内链表，找到目标节点后将其从链表中摘除并释放内存。
 * 使用 prev 指针维护前驱节点，实现单链表的删除操作。
 *
 * @param table  哈希表指针
 * @param key    待删除的键
 * @return 0 成功，-1 未找到该 key
 */
int hash_delete(HashTable *table, const char *key) {
    unsigned int index = simple_hash(key, table->size);
    HashNode *node = table->buckets[index];
    HashNode *prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev) {
                prev->next = node->next;   /* 删除中间/末尾节点 */
            } else {
                table->buckets[index] = node->next;  /* 删除链表头节点 */
            }
            free(node);
            return 0;
        }
        prev = node;
        node = node->next;
    }
    return -1;  /* 未找到 */
}

/**
 * 销毁整个哈希表，释放所有内存
 *
 * 遍历每个桶的链表，依次释放节点和关联数据（如果提供了 free_data 回调），
 * 最后释放桶数组和哈希表结构体本身。
 *
 * @param table      哈希表指针
 * @param free_data  释放 data 的回调函数（传 NULL 表示不释放 data）
 */
void hash_destroy(HashTable *table, void (*free_data)(void*)) {
    if (!table) return;
    for (int i = 0; i < table->size; i++) {
        HashNode *node = table->buckets[i];
        while (node) {
            HashNode *next = node->next;
            if (free_data && node->data) {
                free_data(node->data);  /* 释放 data 指向的对象 */
            }
            free(node);  /* 释放节点本身 */
            node = next;
        }
    }
    free(table->buckets);  /* 释放桶数组 */
    free(table);           /* 释放哈希表结构体 */
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
 * 文件加锁/解锁（跨平台实现）
 *
 * 通过文件锁防止多进程同时写入同一文件导致数据损坏。
 * Windows: 使用 LockFileEx/UnlockFileEx（排他锁）
 * Linux:   使用 flock（LOCK_EX 排他锁 / LOCK_UN 解锁）
 *
 * @param fp        文件指针
 * @param operation LOCKFILE_LOCK=加锁, LOCKFILE_UNLOCK=解锁
 * @return 0 成功，-1 失败
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
 * 原子写入（全量覆盖）
 *
 * 核心思路：先写入临时文件，确认数据落盘后再用 rename 原子替换原文件。
 * 这样即使在写入过程中断电/崩溃，原文件也不会损坏（要么是旧数据，要么是新数据）。
 *
 * 流程：
 *   1. 创建 filepath.tmp 临时文件
 *   2. 写入全部数据
 *   3. fflush + fsync 确保数据从内核缓冲区写入物理磁盘
 *   4. 关闭文件
 *   5. Windows: 先删除原文件，再 rename；Linux: 直接 rename（原子操作）
 *   6. 任何步骤失败都删除 .tmp 文件，不留下垃圾
 *
 * 适用场景：save_employees()、save_products() 等全量覆盖写入
 *
 * @param filepath  目标文件路径
 * @param content   要写入的全部内容（字符串）
 * @return 0 成功，-1 失败
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
 * 原子追加写入（单条记录）
 *
 * 与 atomic_write（全量覆盖）不同，此函数以 append 模式追加一行数据。
 * 使用文件锁防止多进程同时追加导致数据交错。
 * 适用于仍待迁移的单条文本记录写入场景。
 *
 * @param filepath  目标文件路径
 * @param content   要追加的一行内容（自动添加换行符）
 * @return 0 成功，-1 失败
 */
int atomic_append(const char *filepath, const char *content) {
    FILE *fp = fopen(filepath, "a");
    if (!fp) return -1;

    if (file_lock(fp, LOCKFILE_LOCK) != 0) {
        fclose(fp);
        return -1;
    }

    fprintf(fp, "%s\n", content);
    fflush(fp);

    file_unlock(fp);
    fclose(fp);
    return 0;
}

/**
 * 读取文件所有行到动态数组
 *
 * 逐行读取文件内容，每行复制到独立的堆内存中（strdup）。
 * 使用动态扩容策略（初始 100 行，不够时容量翻倍）。
 * 调用方使用完后必须调用 free_lines() 释放内存。
 *
 * @param filepath  文件路径
 * @param count     [out] 读取到的行数
 * @return 行字符串数组（每行一个 char*），文件不存在返回 NULL
 */
char** read_lines(const char *filepath, int *count) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        *count = 0;
        return NULL;
    }

    char **lines = NULL;
    char buffer[MAX_LINE_LEN];
    int capacity = 100;   /* 初始容量 */
    int size = 0;

    lines = (char**)malloc(capacity * sizeof(char*));

    while (fgets(buffer, sizeof(buffer), fp)) {
        /* 容量不足时翻倍扩容 */
        if (size >= capacity) {
            capacity *= 2;
            lines = (char**)realloc(lines, capacity * sizeof(char*));
        }
        lines[size] = strdup(buffer);  /* 复制一行到堆内存 */
        size++;
    }

    fclose(fp);
    *count = size;
    return lines;
}

/**
 * 释放 read_lines 返回的行数组
 *
 * 逐行释放 strdup 分配的内存，最后释放数组本身。
 *
 * @param lines  read_lines 返回的数组
 * @param count  行数
 */
void free_lines(char **lines, int count) {
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
}

// ==================== 工具函数 ====================

/**
 * 去除字符串首尾空白字符（原地修改）
 *
 * 处理步骤：
 *   1. 跳过开头的空白字符（空格、制表符、换行等）
 *   2. 用 memmove 将有效内容移到缓冲区开头（memmove 处理重叠区域）
 *   3. 从末尾向前扫描，将尾部空白替换为 '\0'
 *
 * @param str  待处理的字符串（会被原地修改）
 * @return 处理后的字符串指针（与输入相同）
 */
char* trim(char *str) {
    if (!str) return str;

    /* 去除首部空白：找到第一个非空白字符的位置 */
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    if (start != str) {
        size_t len = strlen(start);
        memmove(str, start, len + 1);  /* +1 包含 '\0' 终止符 */
    }

    if (*str == '\0') return str;

    /* 去除尾部空白：从末尾向前找到最后一个非空白字符 */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    return str;
}

/**
 * 生成 32 字节随机盐值
 *
 * 盐值用于密码哈希：SHA-256(salt + password)。
 * 即使两个用户使用相同密码，由于盐值不同，哈希结果也不同，
 * 从而防止彩虹表攻击。
 *
 * @param salt  [out] 输出缓冲区（至少 33 字节）
 * @return salt 指针
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
/*
 * SHA-256 安全哈希算法
 *
 * 将任意长度的输入映射为 256 位（32 字节）的哈希值。
 * 用于密码存储：不保存明文密码，只保存 SHA-256(salt + password) 的十六进制字符串。
 *
 * 算法核心：
 *   1. 消息分组（512 位 = 64 字节一组）
 *   2. 每组进行 64 轮压缩运算
 *   3. 使用 8 个 32 位寄存器 (a-h) 作为中间状态
 *   4. 最终输出 256 位哈希值
 *
 * 安全性：单向不可逆，抗碰撞，适合密码存储场景。
 */

/* 位运算辅助宏 */
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))   /* 循环右移 */
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))           /* 条件函数 */
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z))) /* 多数函数 */
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))  /* 扩展函数 Σ0 */
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))  /* 扩展函数 Σ1 */
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))     /* 消息调度 σ0 */
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))   /* 消息调度 σ1 */

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
 * 密码哈希计算
 *
 * 将盐值和密码拼接后计算 SHA-256 哈希，输出 64 字节十六进制字符串。
 * 验证密码时，用相同的盐值重新计算哈希并与存储值比较。
 *
 * 存储格式：用户表中保存 password_hash（64字符十六进制）和 salt（32字符随机串）
 * 验证流程：hash_password(input_password, stored_salt) == stored_hash ?
 *
 * @param password  明文密码
 * @param salt      盐值（32字节随机串）
 * @param out_hex   [out] 64字节十六进制哈希字符串
 */
void hash_password(const char *password, const char *salt, char *out_hex) {
    uint8_t hash[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)salt, strlen(salt));
    sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    sha256_final(&ctx, hash);
    sha256_hash_to_hex(hash, out_hex);
}

/**
 * 获取当前时间戳字符串
 *
 * @param buffer [out] 至少 32 字节，输出格式 "YYYY-MM-DD HH:MM:SS"
 */
void get_timestamp(char *buffer) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", t);
}

/**
 * 计算当前日期属于哪一年的第几周
 *
 * 以周一作为每周起始日，1月1日所在的周为第1周。
 * 用于排班系统中按周查询排班记录。
 *
 * @param year  [out] 年份（如 2026）
 * @param week  [out] 周数（1-53）
 * @return 固定返回 0
 */
int get_year_week(int *year, int *week) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    *year = t->tm_year + 1900;

    /* 计算 1月1日是星期几（让系统填充 tm_wday） */
    struct tm first_day = {0};
    first_day.tm_year = *year - 1900;
    first_day.tm_mon = 0;
    first_day.tm_mday = 1;
    mktime(&first_day);

    int first_wday = first_day.tm_wday;  /* 0=周日, 1=周一, ... */
    int yday = t->tm_yday + 1;           /* 当前是今年第几天（从1开始） */

    /* 将周日映射为 6，周一映射为 0（以周一为起始） */
    int offset = (first_wday == 0) ? 6 : first_wday - 1;

    /* 周数 = (天数 + 偏移 - 1) / 7 + 1 */
    *week = (yday + offset - 1) / 7 + 1;
    return 0;
}

/**
 * 通用 ID 生成器（简单自增）
 *
 * 用于供应商、排班、调拨单等不需要特殊格式的实体。
 * 返回 ++g_auto_id_counter，从 1 开始连续递增。
 *
 * @return 新的唯一 ID
 */
int generate_id(void) {
    return ++g_auto_id_counter;
}

// ==================== 初始化/清理 ====================

/**
 * 系统初始化（main 函数启动时调用）
 *
 * 初始化流程：
 *   1. 初始化随机数种子（用于 generate_salt 等）
 *   2. 创建必要目录（data/、tmp/、output/）
 *   3. 初始化 5 个哈希表（员工、商品、条码、供应商、会员手机号）
 *   4. 打开 AbyssDB 并执行首次旧数据迁移
 *   5. 从数据库加载配置，扩展聚合暂沿用文本加载
 *
 * 注意：此函数只加载基础数据。main.c 中还会额外调用：
 *   load_schedules()、load_pending_sales() 等。
 *
 * @return 0 成功，-1 哈希表分配失败
 */
int init_system(void) {
    sm_app_context_config app_config;
    sm_repo_status repo_status;
    srand((unsigned int)time(NULL));

    /* 1. 创建必要目录（已存在则忽略） */
    ensure_dir(DATA_DIR);    /* data/  — 数据文件目录 */
    ensure_dir(TMP_DIR);     /* tmp/   — 临时文件目录（atomic_write 的 .tmp 文件） */
    ensure_dir(OUTPUT_DIR);  /* output/ — 输出目录（报表、小票、备份） */

    /* 2. 初始化哈希表（桶数量使用质数 10007 以减少冲突） */
    g_employee_hash    = hash_create(HASH_TABLE_SIZE);
    g_product_hash     = hash_create(HASH_TABLE_SIZE);
    g_barcode_hash     = hash_create(HASH_TABLE_SIZE);
    g_supplier_hash    = hash_create(HASH_TABLE_SIZE);
    g_member_phone_hash = hash_create(HASH_TABLE_SIZE);

    if (!g_employee_hash || !g_product_hash || !g_barcode_hash ||
        !g_supplier_hash || !g_member_phone_hash) {
        fprintf(stderr, "初始化哈希表失败\n");
        return -1;
    }

    /* 3. 打开数据库，并在首次启动时原子导入旧基础数据。 */
    sm_app_context_config_default(&app_config);
    repo_status = sm_app_context_start(&app_config);
    if (repo_status != SM_REPO_OK) {
        fprintf(stderr, "数据库初始化失败: %s\n", sm_app_last_message());
        hash_destroy(g_employee_hash, free); g_employee_hash = NULL;
        hash_destroy(g_product_hash, free); g_product_hash = NULL;
        hash_destroy(g_barcode_hash, NULL); g_barcode_hash = NULL;
        hash_destroy(g_supplier_hash, free); g_supplier_hash = NULL;
        hash_destroy(g_member_phone_hash, NULL); g_member_phone_hash = NULL;
        return -1;
    }

    /* 4. 配置是单例缓存；其他基础实体按需加载。 */
    if (load_config() != 0) {
        fprintf(stderr, "加载系统配置失败\n");
        (void)sm_app_context_stop();
        hash_destroy(g_employee_hash, free); g_employee_hash = NULL;
        hash_destroy(g_product_hash, free); g_product_hash = NULL;
        hash_destroy(g_barcode_hash, NULL); g_barcode_hash = NULL;
        hash_destroy(g_supplier_hash, free); g_supplier_hash = NULL;
        hash_destroy(g_member_phone_hash, NULL); g_member_phone_hash = NULL;
        return -1;
    }

    /* 5. 批次从数据库重建缓存；其他扩展聚合暂沿用原加载路径。 */
    load_settlements();   /* 日结单 → g_settlements 链表 */
    load_batches();       /* 批次 → g_batches 链表 */
    load_promotions();    /* 促销 → g_promotions 链表（按优先级排序） */

    printf("[%s] 系统初始化完成\n", __TIME__);
    return 0;
}

/**
 * 清理系统资源（main 函数退出前调用）
 *
 * 清理流程：
 *   1. 将内存中的数据全量保存到文件（atomic_write 覆盖模式）
 *   2. 释放所有哈希表及其节点
 *   3. 释放所有链表及其节点（会员、套装、批次、促销、储值卡）
 *
 * 注意：g_barcode_hash 和 g_combo_barcode_hash 不需要 free_data，
 *       因为它们的 data 指针与 g_product_hash / g_combos 共享，
 *       由后者负责释放实际对象，避免重复释放。
 */
void cleanup_system(void) {
    /* 1. 基础实体同步提交；这里只刷新可变会员缓存和配置。 */
    save_members();
    save_all_settlements();
    save_all_promotions();
    save_config();

    /* 2. 释放哈希表（free_data=free 表示释放 data 指向的堆内存） */
    if (g_employee_hash)     hash_destroy(g_employee_hash, free);
    if (g_product_hash)      hash_destroy(g_product_hash, free);
    if (g_barcode_hash)      hash_destroy(g_barcode_hash, NULL);    /* 共享指针，不释放 data */
    if (g_supplier_hash)     hash_destroy(g_supplier_hash, free);
    if (g_member_phone_hash) hash_destroy(g_member_phone_hash, NULL);
    if (g_combo_barcode_hash) hash_destroy(g_combo_barcode_hash, NULL); /* 共享指针 */

    /* 3. 释放链表内存 */

    /* 释放会员链表 */
    Member *m = g_members;
    while (m) {
        Member *next = m->next;
        free(m);
        m = next;
    }
    g_members = NULL;

    /* 释放套装链表（含子商品链表） */
    ProductCombo *combo = g_combos;
    while (combo) {
        ProductCombo *next = combo->next;
        ComboItem *ci = combo->items;
        while (ci) {
            ComboItem *ci_next = ci->next;
            free(ci);
            ci = ci_next;
        }
        free(combo);
        combo = next;
    }
    g_combos = NULL;

    // 释放批次链表
    Batch *batch = g_batches;
    while (batch) {
        Batch *next = batch->next;
        free(batch);
        batch = next;
    }
    g_batches = NULL;

    // 释放促销链表
    Promotion *promo = g_promotions;
    while (promo) {
        Promotion *next = promo->next;
        free(promo);
        promo = next;
    }
    g_promotions = NULL;

    // 释放储值卡链表
    VipCard *vc = g_vip_cards;
    while (vc) {
        VipCard *next = vc->next;
        free(vc);
        vc = next;
    }
    g_vip_cards = NULL;

    if (sm_app_context_stop() != SM_REPO_OK)
        fprintf(stderr, "关闭数据库失败: %s\n", sm_app_last_message());

    printf("系统资源已清理\n");
}

// ==================== 员工数据操作 ====================

/**
 * 添加员工
 */
static Employee *cache_employee(const Employee *value) {
    char key[32];
    Employee *cached;
    Employee *copy;
    if (!value || value->id <= 0) return NULL;
    snprintf(key, sizeof(key), "%d", value->id);
    cached = (Employee *)hash_search(g_employee_hash, key);
    if (cached) {
        *cached = *value;
        return cached;
    }
    copy = (Employee *)malloc(sizeof(*copy));
    if (!copy) return NULL;
    *copy = *value;
    if (hash_insert(g_employee_hash, key, copy) != 0) {
        free(copy);
        return NULL;
    }
    return copy;
}

int add_employee(Employee *emp) {
    if (!emp) return -1;
    emp->id = 0;
    emp->created_at = time(NULL);
    emp->updated_at = emp->created_at;
    emp->status = STATUS_ACTIVE;
    if (sm_service_employee_create(emp) != SM_REPO_OK) return -1;
    (void)cache_employee(emp);
    if (emp->id > g_employee_id_counter) g_employee_id_counter = emp->id;
    return emp->id;
}

/**
 * 查找员工（按ID）
 */
Employee* find_employee_by_id(int id) {
    char key[32];
    Employee value;
    Employee *cached;
    if (id <= 0) return NULL;
    snprintf(key, sizeof(key), "%d", id);
    cached = (Employee *)hash_search(g_employee_hash, key);
    if (cached) return cached;
    if (sm_service_employee_get(id, &value) != SM_REPO_OK) return NULL;
    return cache_employee(&value);
}

/**
 * 更新员工
 */
int update_employee(Employee *emp) {
    Employee persisted;
    if (!emp || emp->id <= 0) return -1;
    emp->updated_at = time(NULL);
    if (sm_service_employee_update(emp) != SM_REPO_OK) {
        if (sm_service_employee_get(emp->id, &persisted) == SM_REPO_OK)
            (void)cache_employee(&persisted);
        return -1;
    }
    (void)cache_employee(emp);
    return 0;
}

/**
 * 删除员工（标记离职）
 */
int delete_employee(int id) {
    Employee *emp = find_employee_by_id(id);
    if (!emp) return -1;
    emp->status = STATUS_INACTIVE;
    return update_employee(emp);
}

/**
 * 加载员工数据
 */
int load_employees(void) {
    return 0;
}

/**
 * 保存员工数据 - 原子写入版本
 * 构建内容 → atomic_write → rename
 */
int save_employees(void) {
    return sm_app_repository() ? 0 : -1;
}

/**
 * 列出所有员工
 */
Employee** list_employees(int *count) {
    Employee *values = NULL;
    Employee **list = NULL;
    size_t value_count = 0;
    size_t i;
    if (!count) return NULL;
    *count = 0;
    if (sm_service_employee_list(1, &values, &value_count) != SM_REPO_OK ||
        value_count > 2147483647u)
        return NULL;
    if (value_count != 0) {
        list = (Employee **)calloc(value_count, sizeof(*list));
        if (!list) {
            sm_service_array_free(values);
            return NULL;
        }
    }
    for (i = 0; i < value_count; ++i) {
        list[i] = cache_employee(&values[i]);
        if (!list[i]) {
            free(list);
            sm_service_array_free(values);
            return NULL;
        }
    }
    sm_service_array_free(values);
    *count = (int)value_count;
    return list;
}

// ==================== 商品数据操作 ====================

/**
 * 生成商品ID
 * 格式: P + 4位数字（如 P0001, P0002 ...）
 */
static Product *cache_product(const Product *value) {
    Product *cached;
    Product *copy;
    char old_barcode[sizeof(value->barcode)];
    if (!value || value->id[0] == '\0') return NULL;
    cached = (Product *)hash_search(g_product_hash, value->id);
    if (cached) {
        memcpy(old_barcode, cached->barcode, sizeof(old_barcode));
        old_barcode[sizeof(old_barcode) - 1] = '\0';
        if (old_barcode[0] != '\0' && strcmp(old_barcode, value->barcode) != 0)
            (void)hash_delete(g_barcode_hash, old_barcode);
        *cached = *value;
    } else {
        copy = (Product *)malloc(sizeof(*copy));
        if (!copy) return NULL;
        *copy = *value;
        if (hash_insert(g_product_hash, copy->id, copy) != 0) {
            free(copy);
            return NULL;
        }
        cached = copy;
    }
    if (cached->barcode[0] != '\0')
        (void)hash_insert(g_barcode_hash, cached->barcode, cached);
    return cached;
}

/**
 * 添加商品（自动分配ID）
 * @param prod 商品信息（id 字段会被自动生成覆盖）
 * @return 0 成功，-1 失败（条码重复）
 */
int add_product(Product *prod) {
    if (!prod) return -1;
    prod->id[0] = '\0';
    prod->created_at = time(NULL);
    prod->updated_at = prod->created_at;
    prod->status = STATUS_ACTIVE;
    if (sm_service_product_create(prod) != SM_REPO_OK) return -1;
    (void)cache_product(prod);
    if (prod->id[0] == 'P' || prod->id[0] == 'p') {
        int number = atoi(prod->id + 1);
        if (number > g_product_id_counter) g_product_id_counter = number;
    }
    return 0;
}

/**
 * 查找商品（按ID）
 */
Product* find_product_by_id(const char *id) {
    Product value;
    Product *cached;
    if (!id || id[0] == '\0') return NULL;
    cached = (Product *)hash_search(g_product_hash, id);
    if (cached) return cached;
    if (sm_service_product_get(id, &value) != SM_REPO_OK) return NULL;
    return cache_product(&value);
}

Product* refresh_product_by_id(const char *id) {
    Product value;
    if (!id || id[0] == '\0' ||
        sm_service_product_get(id, &value) != SM_REPO_OK)
        return NULL;
    return cache_product(&value);
}

/**
 * 查找商品（按条码）
 */
Product* find_product_by_barcode(const char *barcode) {
    Product value;
    Product *cached;
    if (!barcode || barcode[0] == '\0') return NULL;
    cached = (Product *)hash_search(g_barcode_hash, barcode);
    if (cached) return cached;
    if (sm_service_product_get_by_barcode(barcode, &value) != SM_REPO_OK)
        return NULL;
    return cache_product(&value);
}

/**
 * 更新商品
 */
int update_product(Product *prod) {
    Product persisted;
    if (!prod || prod->id[0] == '\0') return -1;
    prod->updated_at = time(NULL);
    if (sm_service_product_update(prod) != SM_REPO_OK) {
        if (sm_service_product_get(prod->id, &persisted) == SM_REPO_OK)
            (void)cache_product(&persisted);
        return -1;
    }
    (void)cache_product(prod);
    return 0;
}

/**
 * 删除商品（标记下架）
 */
int delete_product(const char *id) {
    Product *prod = find_product_by_id(id);
    if (!prod) return -1;
    prod->status = STATUS_INACTIVE;
    return update_product(prod);
}

/**
 * 加载商品数据
 */
int load_products(void) {
    return 0;
}

/**
 * 保存商品数据 - 原子写入版本
 */
int save_products(void) {
    return sm_app_repository() ? 0 : -1;
}

static Product **list_products_from_service(int low_stock, int *count) {
    Product *values = NULL;
    Product **list = NULL;
    size_t value_count = 0;
    size_t i;
    sm_repo_status status;
    if (!count) return NULL;
    *count = 0;
    status = low_stock
                 ? sm_service_product_list_low_stock(&values, &value_count)
                 : sm_service_product_list(&values, &value_count);
    if (status != SM_REPO_OK || value_count > 2147483647u) return NULL;
    if (value_count != 0) {
        list = (Product **)calloc(value_count, sizeof(*list));
        if (!list) {
            sm_service_array_free(values);
            return NULL;
        }
    }
    for (i = 0; i < value_count; ++i) {
        list[i] = cache_product(&values[i]);
        if (!list[i]) {
            free(list);
            sm_service_array_free(values);
            return NULL;
        }
    }
    sm_service_array_free(values);
    *count = (int)value_count;
    return list;
}

Product **list_products(int *count) {
    return list_products_from_service(0, count);
}

Product **list_low_stock_products(int *count) {
    return list_products_from_service(1, count);
}

// ==================== 供应商数据操作 ====================

/**
 * 添加供应商
 */
static Supplier *cache_supplier(const Supplier *value) {
    char key[32];
    Supplier *cached;
    Supplier *copy;
    if (!value || value->id <= 0) return NULL;
    snprintf(key, sizeof(key), "%d", value->id);
    cached = (Supplier *)hash_search(g_supplier_hash, key);
    if (cached) {
        *cached = *value;
        return cached;
    }
    copy = (Supplier *)malloc(sizeof(*copy));
    if (!copy) return NULL;
    *copy = *value;
    if (hash_insert(g_supplier_hash, key, copy) != 0) {
        free(copy);
        return NULL;
    }
    return copy;
}

int add_supplier(Supplier *sup) {
    if (!sup) return -1;
    sup->id = 0;
    sup->status = STATUS_ACTIVE;
    if (sm_service_supplier_create(sup) != SM_REPO_OK) return -1;
    (void)cache_supplier(sup);
    if (sup->id > g_auto_id_counter) g_auto_id_counter = sup->id;
    return sup->id;
}

/**
 * 查找供应商
 */
Supplier* find_supplier_by_id(int id) {
    char key[32];
    Supplier value;
    Supplier *cached;
    if (id <= 0) return NULL;
    snprintf(key, sizeof(key), "%d", id);
    cached = (Supplier *)hash_search(g_supplier_hash, key);
    if (cached) return cached;
    if (sm_service_supplier_get(id, &value) != SM_REPO_OK) return NULL;
    return cache_supplier(&value);
}

/**
 * 加载供应商
 */
int load_suppliers(void) {
    return 0;
}

/**
 * 保存供应商 - 原子写入版本
 */
int save_suppliers(void) {
    return sm_app_repository() ? 0 : -1;
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
    sm_repo_status status = sm_service_config_get(&g_config);
    if (status == SM_REPO_OK) return 0;
    if (status != SM_REPO_NOT_FOUND) return -1;
    return sm_service_config_put(&g_config) == SM_REPO_OK ? 0 : -1;
}

/**
 * 保存系统配置
 */
int save_config(void) {
    return sm_service_config_put(&g_config) == SM_REPO_OK ? 0 : -1;
}
