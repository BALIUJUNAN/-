/**
 * @file supermarket.c
 * @brief 超市管理系统核心实现 - 数据访问层、哈希表、文件IO
 */

#include "supermarket.h"
#include <errno.h>
#include <ctype.h>

// ==================== 全局变量定义 ====================
int g_auto_id_counter = 1000;
HashNode **g_employee_hash = NULL;
HashNode **g_product_hash = NULL;
HashNode **g_barcode_hash = NULL;
HashNode **g_supplier_hash = NULL;
HashNode **g_member_phone_hash = NULL;  // 会员手机号哈希表
Member *g_members = NULL;  // 会员链表头指针

// 营销模块全局变量
ProductCombo *g_combos = NULL;
HashNode **g_combo_barcode_hash = NULL;
Batch *g_batches = NULL;
Promotion *g_promotions = NULL;
VipCard *g_vip_cards = NULL;
VipCardTransaction *g_vip_card_transactions = NULL;

// ==================== 哈希表实现 ====================

/**
 * 创建哈希表
 */
HashNode** hash_create(int size) {
    HashNode **table = (HashNode**)calloc(size, sizeof(HashNode*));
    if (!table) {
        fprintf(stderr, "分配哈希表失败\n");
        return NULL;
    }
    return table;
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
 * 插入哈希表
 */
int hash_insert(HashNode **table, int size, const char *key, void *data) {
    unsigned int index = simple_hash(key, size);
    
    HashNode *new_node = (HashNode*)malloc(sizeof(HashNode));
    if (!new_node) return -1;
    
    strncpy(new_node->key, key, 63);
    new_node->key[63] = '\0';
    new_node->data = data;
    new_node->next = table[index];
    table[index] = new_node;
    
    return 0;
}

/**
 * 查找哈希表
 */
void* hash_search(HashNode **table, int size, const char *key) {
    unsigned int index = simple_hash(key, size);
    HashNode *node = table[index];
    
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
int hash_delete(HashNode **table, int size, const char *key) {
    unsigned int index = simple_hash(key, size);
    HashNode *node = table[index];
    HashNode *prev = NULL;
    
    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                table[index] = node->next;
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
void hash_destroy(HashNode **table, int size, void (*free_data)(void*)) {
    for (int i = 0; i < size; i++) {
        HashNode *node = table[i];
        while (node) {
            HashNode *next = node->next;
            if (free_data && node->data) {
                free_data(node->data);
            }
            free(node);
            node = next;
        }
    }
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
    static const char charset[] = "0123456789abcdef";
    srand((unsigned int)time(NULL));
    
    for (int i = 0; i < 32; i++) {
        salt[i] = charset[rand() % 36];
    }
    salt[32] = '\0';
    return salt;
}

/**
 * 简单密码哈希（实际应用中应使用 SHA-256）
 * 这里用简单的 salt + password 拼接后求和作为演示
 */
char* hash_password(const char *password, const char *salt) {
    static char hash[65];
    unsigned int h = 0;
    
    // 组合 salt 和 password
    char combined[128];
    snprintf(combined, sizeof(combined), "%s%s", salt, password);
    
    // 简单哈希（实际应使用 SHA-256 库如 OpenSSL）
    for (int i = 0; combined[i]; i++) {
        h = h * 31 + combined[i];
    }
    
    // 转换为 16 进制
    for (int i = 0; i < 32; i++) {
        sprintf(hash + i * 2, "%02x", (h >> (i % 4) * 8) & 0xFF);
    }
    hash[64] = '\0';
    
    return hash;
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
 * 生成唯一ID（基于日期时间 + 序号，确保唯一性）
 * 格式: YYYYMMDDHHMMSS + 3位序号 (共17位)
 */
int generate_id(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    // 构建基于时间的基本ID
    // YYYYMMDDHHMMSS = 14位
    int time_based = (t->tm_year + 1900) * 1000000000000LL +
                     (t->tm_mon + 1) * 10000000000LL +
                     t->tm_mday * 100000000LL +
                     t->tm_hour * 1000000LL +
                     t->tm_min * 10000LL +
                     t->tm_sec * 100LL;
    
    // 如果基于时间的ID <= 当前计数器，说明同秒内有多个订单
    // 在计数器基础上加序号
    if (time_based <= g_auto_id_counter) {
        return ++g_auto_id_counter;
    }
    
    // 更新计数器为当前时间戳基准
    g_auto_id_counter = time_based;
    return g_auto_id_counter;
}

// ==================== 初始化/清理 ====================

/**
 * 初始化系统
 */
int init_system(void) {
    // 创建必要目录
    ensure_dir(DATA_DIR);
    ensure_dir(TMP_DIR);
    
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
        hash_destroy(g_employee_hash, HASH_TABLE_SIZE, free);
    }
    if (g_product_hash) {
        hash_destroy(g_product_hash, HASH_TABLE_SIZE, free);
    }
    if (g_barcode_hash) {
        hash_destroy(g_barcode_hash, HASH_TABLE_SIZE, NULL);
    }
    if (g_supplier_hash) {
        hash_destroy(g_supplier_hash, HASH_TABLE_SIZE, free);
    }
    if (g_member_phone_hash) {
        hash_destroy(g_member_phone_hash, HASH_TABLE_SIZE, free);
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
    hash_insert(g_employee_hash, HASH_TABLE_SIZE, key, new_emp);
    
    // 添加到链表头部
    new_emp->updated_at = (time_t)((Employee*)0);  // 占位
    new_emp->created_at = (time_t)NULL;  // 复用字段作链表指针
    // 注意：简化版不维护员工链表
    
    return emp->id;
}

/**
 * 查找员工（按ID）
 */
Employee* find_employee_by_id(int id) {
    char key[32];
    sprintf(key, "%d", id);
    return (Employee*)hash_search(g_employee_hash, HASH_TABLE_SIZE, key);
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
        char *token = strtok(line, "|");
        emp->id = atoi(token);
        strncpy(emp->name, strtok(NULL, "|"), MAX_NAME_LEN - 1);
        strncpy(emp->role, strtok(NULL, "|"), 19);
        strncpy(emp->password_hash, strtok(NULL, "|"), 64);
        strncpy(emp->salt, strtok(NULL, "|"), 32);
        emp->status = atoi(strtok(NULL, "|"));
        emp->created_at = (time_t)atoi(strtok(NULL, "|"));
        emp->updated_at = (time_t)atoi(strtok(NULL, "|"));
        
        char key[32];
        sprintf(key, "%d", emp->id);
        hash_insert(g_employee_hash, HASH_TABLE_SIZE, key, emp);
        
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
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/employee.txt", DATA_DIR);
    
    // 分配初始缓冲区
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    // 遍历哈希表构建内容
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_employee_hash[i];
        while (node) {
            Employee *emp = (Employee*)node->data;
            char line[512];
            snprintf(line, sizeof(line), "%d|%s|%s|%s|%s|%d|%ld|%ld\n",
                emp->id, emp->name, emp->role, emp->password_hash,
                emp->salt, emp->status, (long)emp->created_at, (long)emp->updated_at);
            
            // 扩展缓冲区如果需要
            if (strlen(buffer) + strlen(line) + 1 > bufsize) {
                bufsize *= 2;
                buffer = (char*)realloc(buffer, bufsize);
                if (!buffer) return -1;
            }
            strcat(buffer, line);
            node = node->next;
        }
    }
    
    // 原子写入
    int result = atomic_write(filepath, buffer);
    free(buffer);
    
    return result;
}

/**
 * 列出所有员工
 */
Employee** list_employees(int *count) {
    Employee **list = NULL;
    *count = 0;
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_employee_hash[i];
        while (node) {
            Employee *emp = (Employee*)node->data;
            if (emp->status == STATUS_ACTIVE) {
                list = (Employee**)realloc(list, (*count + 1) * sizeof(Employee*));
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
    
    hash_insert(g_product_hash, HASH_TABLE_SIZE, prod->id, new_prod);
    if (strlen(prod->barcode) > 0) {
        hash_insert(g_barcode_hash, HASH_TABLE_SIZE, prod->barcode, new_prod);
    }
    
    return 0;
}

/**
 * 查找商品（按ID）
 */
Product* find_product_by_id(const char *id) {
    return (Product*)hash_search(g_product_hash, HASH_TABLE_SIZE, id);
}

/**
 * 查找商品（按条码）
 */
Product* find_product_by_barcode(const char *barcode) {
    return (Product*)hash_search(g_barcode_hash, HASH_TABLE_SIZE, barcode);
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
        char *p = line;
        
        strncpy(prod->id, strtok(p, "|"), MAX_ID_LEN - 1);
        strncpy(prod->name, strtok(NULL, "|"), MAX_NAME_LEN - 1);
        strncpy(prod->barcode, strtok(NULL, "|"), 29);
        prod->price = atof(strtok(NULL, "|"));
        prod->cost = atof(strtok(NULL, "|"));
        prod->stock = atoi(strtok(NULL, "|"));
        prod->min_stock = atoi(strtok(NULL, "|"));
        strncpy(prod->category_id, strtok(NULL, "|"), 19);
        strncpy(prod->supplier_id, strtok(NULL, "|"), 19);
        prod->status = atoi(strtok(NULL, "|"));
        prod->created_at = (time_t)atoi(strtok(NULL, "|"));
        prod->updated_at = (time_t)atoi(strtok(NULL, "|"));
        
        hash_insert(g_product_hash, HASH_TABLE_SIZE, prod->id, prod);
        if (strlen(prod->barcode) > 0) {
            hash_insert(g_barcode_hash, HASH_TABLE_SIZE, prod->barcode, prod);
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
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/product.txt", DATA_DIR);
    
    // 分配初始缓冲区
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    // 遍历哈希表构建内容
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_product_hash[i];
        while (node) {
            Product *prod = (Product*)node->data;
            char line[512];
            snprintf(line, sizeof(line), "%s|%s|%s|%.2f|%.2f|%d|%d|%s|%s|%d|%ld|%ld\n",
                prod->id, prod->name, prod->barcode, prod->price, prod->cost,
                prod->stock, prod->min_stock, prod->category_id, prod->supplier_id,
                prod->status, (long)prod->created_at, (long)prod->updated_at);
            
            if (strlen(buffer) + strlen(line) + 1 > bufsize) {
                bufsize *= 2;
                buffer = (char*)realloc(buffer, bufsize);
                if (!buffer) return -1;
            }
            strcat(buffer, line);
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
    hash_insert(g_supplier_hash, HASH_TABLE_SIZE, key, new_sup);
    
    return sup->id;
}

/**
 * 查找供应商
 */
Supplier* find_supplier_by_id(int id) {
    char key[32];
    sprintf(key, "%d", id);
    return (Supplier*)hash_search(g_supplier_hash, HASH_TABLE_SIZE, key);
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
        strcpy(sup->name, strtok(line, "|"));
        strcpy(sup->contact, strtok(NULL, "|"));
        strcpy(sup->phone, strtok(NULL, "|"));
        strcpy(sup->address, strtok(NULL, "|"));
        sup->id = atoi(strtok(NULL, "|"));
        sup->status = atoi(strtok(NULL, "|"));
        
        char key[32];
        sprintf(key, "%d", sup->id);
        hash_insert(g_supplier_hash, HASH_TABLE_SIZE, key, sup);
        
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
    char *buffer = NULL;
    size_t bufsize = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/supplier.txt", DATA_DIR);
    
    bufsize = 65536;
    buffer = (char*)malloc(bufsize);
    if (!buffer) return -1;
    buffer[0] = '\0';
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = g_supplier_hash[i];
        while (node) {
            Supplier *sup = (Supplier*)node->data;
            char line[512];
            snprintf(line, sizeof(line), "%s|%s|%s|%s|%d|%d\n",
                sup->name, sup->contact, sup->phone, sup->address, sup->id, sup->status);
            
            if (strlen(buffer) + strlen(line) + 1 > bufsize) {
                bufsize *= 2;
                buffer = (char*)realloc(buffer, bufsize);
            }
            strcat(buffer, line);
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
