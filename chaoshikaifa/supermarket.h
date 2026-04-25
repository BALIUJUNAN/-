/**
 * @file supermarket.h
 * @brief 超市管理系统 - 合并头文件
 * 
 * 包含所有模块的宏定义、结构体声明和函数原型
 * 模块: 核心(哈希表/文件IO)、会员、促销、套装、批次、财务、门店、调拨、供应商结算、储值卡、打印、数据看板
 */

#ifndef SUPERMARKET_H
#define SUPERMARKET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <process.h>
    #define LOCKFILE_LOCK          1
    #define LOCKFILE_UNLOCK        2
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/file.h>
    #include <sys/stat.h>
#endif

// ==================== 宏定义 ====================
#define MAX_NAME_LEN      50
#define MAX_ID_LEN        20
#define MAX_LINE_LEN      1024
#define DELIMITER         '|'
#define DATA_DIR          "data"
#define TMP_DIR           "tmp"
#define HASH_TABLE_SIZE   10007

// 员工状态
#define STATUS_ACTIVE     1   // 在职
#define STATUS_INACTIVE   0   // 离职

// 班次类型
#define SHIFT_MORNING     "早"   // 早班
#define SHIFT_EVENING     "晚"   // 晚班
#define SHIFT_OFF         "休"   // 休息

// 采购状态
#define PURCHASE_PENDING   0   // 待审核
#define PURCHASE_APPROVED  1   // 已审核
#define PURCHASE_COMPLETED 2  // 已完成
#define PURCHASE_REJECTED  3   // 已拒绝

// 销售状态
#define SALE_PENDING      0   // 挂单
#define SALE_COMPLETED    1   // 已完成
#define SALE_REFUNDED     2   // 已退款

// 会员等级
#define MEMBER_LEVEL_NORMAL   0   // 普通会员
#define MEMBER_LEVEL_SILVER  1   // 银卡会员
#define MEMBER_LEVEL_GOLD    2   // 金卡会员
#define MEMBER_LEVEL_DIAMOND 3   // 钻石会员

// 积分规则
#define POINTS_PER_YUAN      1    // 1元 = 1积分
#define POINTS_REDEEM_RATE   100  // 100积分抵1元

// 促销类型
#define PROMOTION_TYPE_DISCOUNT   0  // 单品折扣
#define PROMOTION_TYPE_OVERRIDE   1  // 整单满减
#define PROMOTION_TYPE_NTH_DISCOUNT 2 // 第N件优惠（如第二件半价）
#define PROMOTION_TYPE_BUY_M_GET_N  3 // 买M赠N
#define PROMOTION_TYPE_MEMBER_PRICE  4 // 会员专属价

// 促销状态
#define PROMOTION_ACTIVE    1   // 生效中
#define PROMOTION_INACTIVE  0   // 未生效

// 套装状态
#define COMBO_ACTIVE     1    // 生效中
#define COMBO_INACTIVE   0    // 已下架

// 批次常量
#define BATCH_EXPIRY_WARNING_DAYS 30  // 临期预警天数

// 日结单状态
#define SETTLEMENT_PENDING  0   // 待确认
#define SETTLEMENT_CONFIRMED 1  // 已确认
#define SETTLEMENT_DISPUTED  2  // 有差异

// 门店状态
#define STORE_ACTIVE    1
#define STORE_INACTIVE  0

// 调拨单状态
#define TRANSFER_PENDING      0  // 待出库
#define TRANSFER_OUT          1  // 已出库（待入库）
#define TRANSFER_IN           2  // 已入库（完成）
#define TRANSFER_REJECTED     3  // 已拒绝
#define TRANSFER_CANCELLED    4  // 已取消

// 供应商评级
#define SUPPLIER_RATING_A   'A'  // 优质
#define SUPPLIER_RATING_B   'B'  // 良好
#define SUPPLIER_RATING_C   'C'  // 一般
#define SUPPLIER_RATING_D   'D'  // 较差

// 付款状态
#define PAYMENT_PENDING     0  // 待付款
#define PAYMENT_PARTIAL     1  // 部分付款
#define PAYMENT_COMPLETED   2  // 已付清

// 储值卡状态
#define VIPCARD_ACTIVE      1  // 正常
#define VIPCARD_FROZEN      2  // 冻结
#define VIPCARD_EXPIRED     3  // 已过期
#define VIPCARD_CANCELLED   0  // 已注销

// 储值卡类型
#define VIPCARD_TYPE_NORMAL    0  // 普通卡
#define VIPCARD_TYPE_GOLD      1  // 金卡
#define VIPCARD_TYPE_DIAMOND   2  // 钻石卡

// ==================== 哈希表节点 ====================
typedef struct HashNode {
    char key[64];
    void *data;
    struct HashNode *next;
} HashNode;

// ==================== 员工结构 ====================
typedef struct Employee {
    int id;
    char name[MAX_NAME_LEN];
    char role[20];           // 收银员/库管/店长/管理员
    char password_hash[65];  // 密码哈希
    char salt[33];            // 盐值
    int status;              // 在职/离职
    time_t created_at;
    time_t updated_at;
} Employee;

// ==================== 商品分类 ====================
typedef struct Category {
    int id;
    char name[MAX_NAME_LEN];
    int parent_id;
    char path[256];          // 如 "1/3/5" 路径
} Category;

// ==================== 商品结构 ====================
typedef struct Product {
    char id[MAX_ID_LEN];
    char name[MAX_NAME_LEN];
    char barcode[30];
    float price;             // 零售价
    float cost;              // 进价
    int stock;               // 库存
    int min_stock;           // 最低库存预警
    char category_id[20];
    char supplier_id[20];
    int status;             // 0下架 1上架
    time_t created_at;
    time_t updated_at;
} Product;

// ==================== 排班结构 ====================
typedef struct Schedule {
    int id;
    int employee_id;
    int year;
    int week;               // 第几周
    char shifts[7][4];      // 周一到周日 ["早","晚","休",""]
    time_t created_at;
    struct Schedule *next;   // 链表指针
} Schedule;

// ==================== 采购结构 ====================
typedef struct Purchase {
    int id;
    char supplier_id[20];
    char supplier_name[MAX_NAME_LEN];
    int creator_id;         // 创建人
    int approver_id;        // 审批人
    int status;             // 状态
    float total_amount;
    time_t created_at;
    time_t approved_at;
    time_t completed_at;
    struct Purchase *next;
} Purchase;

// ==================== 采购明细 ====================
typedef struct PurchaseItem {
    int id;
    int purchase_id;
    char product_id[MAX_ID_LEN];
    char product_name[MAX_NAME_LEN];
    float quantity;
    float price;
    float received_qty;     // 已收货数量
    struct PurchaseItem *next;
} PurchaseItem;

// ==================== 销售结构 ====================
typedef struct Sale {
    int id;
    int cashier_id;
    int member_id;           // 会员ID
    float total_amount;
    float discount;
    float final_amount;
    float cash_received;     // 收款金额
    int points_used;         // 使用积分
    int status;             // 0挂单 1完成 2退款
    char payment_method[20]; // 现金/支付宝/微信
    time_t created_at;
    time_t completed_at;
    struct Sale *next;
} Sale;

// ==================== 销售明细 ====================
typedef struct SaleItem {
    int id;
    int sale_id;
    char product_id[MAX_ID_LEN];
    char product_name[MAX_NAME_LEN];
    float quantity;
    float price;
    float subtotal;
    float discount;          // 单品折扣
    int is_combo;            // 是否为套装: 0-普通商品, 1-套装
    int combo_id;            // 套装ID（如果是套装）
    void *next;              // 通用指针
} SaleItem;

// ==================== 库存记录 ====================
typedef struct StockLog {
    int id;
    char product_id[MAX_ID_LEN];
    char type[20];          // 入库/出库/盘点/调整
    float quantity;
    float before_stock;
    float after_stock;
    int operator_id;
    char remark[256];
    time_t created_at;
    struct StockLog *next;
} StockLog;

// ==================== 供应商结构 ====================
typedef struct Supplier {
    int id;
    char name[MAX_NAME_LEN];
    char contact[50];
    char phone[20];
    char address[256];
    int status;
} Supplier;

// ==================== 事务日志 ====================
typedef struct TransactionLog {
    int id;
    char type[20];          // SALE/PURCHASE/STOCK
    int ref_id;
    char operation[20];     // CREATE/UPDATE/DELETE
    char data[1024];
    int operator_id;
    time_t created_at;
    struct TransactionLog *next;
} TransactionLog;

// ==================== 系统配置 ====================
typedef struct SystemConfig {
    char shop_name[100];
    char shop_address[256];
    char shop_phone[20];
    float tax_rate;
    int auto_backup_interval; // 分钟
    float monthly_fixed_cost; // 月固定成本（用于净利计算）
} SystemConfig;

// ==================== 会员结构 ====================
typedef struct Member {
    int id;                     // 会员ID
    char phone[20];             // 手机号（唯一键）
    char name[50];              // 姓名
    int level;                  // 等级
    int points;                 // 积分余额
    float total_consume;        // 累计消费
    time_t created_at;          // 注册时间
    time_t updated_at;          // 更新时间
    time_t last_consume_at;     // 最后消费时间
    struct Member *next;        // 链表指针
} Member;

// 会员优惠记录
typedef struct MemberConsumeRecord {
    int member_id;
    char phone[20];
    char name[50];
    int points_earned;      // 本次获得积分
    int points_redeemed;    // 本次使用积分
    int total_points;      // 剩余总积分
    int level;
} MemberConsumeRecord;

// ==================== 促销结构 ====================
typedef struct Promotion {
    int id;                     // 促销ID
    char name[100];            // 促销名称
    int type;                  // 类型：折扣/满减/第N件/买M赠N/会员专属价
    char product_id[MAX_ID_LEN]; // 关联商品ID
    
    // 单品折扣参数
    float discount_rate;       // 折扣率（如0.8表示8折）
    
    // 满减参数
    float threshold;           // 满减门槛（如100）
    float discount_amount;     // 减免金额（如10）
    
    // 第N件优惠参数 (如第二件半价)
    int nth_item;             // 第几件开始优惠（如2）
    float nth_discount_rate; // 第N件折扣率（如0.5表示半价）
    
    // 买M赠N参数
    int buy_quantity;         // 购买数量M（如3）
    int free_quantity;       // 赠送数量N（如1）
    
    // 会员专属价参数
    int member_level;         // 会员等级（0=所有会员，1=银卡，2=金卡，3=钻卡等）
    float member_price;       // 会员专属价格
    
    time_t start_time;         // 开始时间
    time_t end_time;          // 结束时间
    int status;               // 状态
    int priority;             // 优先级（数字越大越优先）
    
    time_t created_at;
    struct Promotion *next;
} Promotion;

// 购物车项
typedef struct CartItem {
    char product_id[MAX_ID_LEN];
    char product_name[MAX_NAME_LEN];
    float price;
    float quantity;
    float subtotal;
    float discounted_price;   // 折后单价
    float discount_amount;     // 折扣金额
    char batch_no[20];         // 批次号
    struct CartItem *next;
} CartItem;

typedef struct Cart {
    CartItem *items;
    float subtotal;           // 小计
    float discount_amount;    // 总折扣金额
    float final_amount;      // 最终金额
    int member_id;           // 会员ID（可选）
} Cart;

// 促销应用结果
typedef struct PromotionResult {
    float original_amount;    // 原价
    float discount_amount;     // 折扣金额
    float final_amount;       // 最终金额
    int promotion_count;       // 应用的促销数量
    char detail[1024];        // 详细说明
} PromotionResult;

// ==================== 套装结构 ====================
typedef struct ComboItem {
    char product_id[MAX_ID_LEN];      // 子商品ID
    char product_name[MAX_NAME_LEN];  // 子商品名称
    int quantity;                     // 数量
    float ratio;                      // 分摊比例
    struct ComboItem *next;
} ComboItem;

typedef struct ProductCombo {
    int id;                           // 套装ID
    char name[100];                   // 套装名称
    char barcode[30];                 // 套装条码
    float price;                      // 套装售价
    float cost;                       // 套装成本
    ComboItem *items;                 // 子商品列表
    int status;                       // 状态
    time_t created_at;
    time_t updated_at;
    struct ProductCombo *next;
} ProductCombo;

// ==================== 批次结构 ====================
typedef struct Batch {
    char batch_no[20];          // 批号 (YYMMDD+序号)
    char product_id[MAX_ID_LEN]; // 商品ID
    char product_name[MAX_NAME_LEN];
    float quantity;             // 当前库存数量
    float initial_quantity;      // 初始数量
    float price;               // 采购单价
    time_t production_date;    // 生产日期
    time_t expiry_date;        // 保质期截止日期
    time_t received_date;      // 收货日期
    int supplier_id;           // 供应商ID
    time_t created_at;
    struct Batch *next;        // 链表指针
} Batch;

// ==================== 日结单结构 ====================
typedef struct DailySettlement {
    int id;                     // 日结单ID
    int cashier_id;            // 收银员ID
    char cashier_name[50];     // 收银员姓名
    time_t settlement_date;    // 结算日期
    time_t shift_start;       // 班次开始时间
    time_t shift_end;          // 班次结束时间
    
    // 系统统计
    int total_orders;          // 总订单数
    float system_cash;         // 系统现金收入
    float system_online;       // 系统线上收入
    float system_total;        // 系统总收入
    
    // 收银员录入
    float actual_cash;         // 实际现金
    float actual_online;       // 实际线上
    float actual_total;        // 实际总额
    
    // 差异
    float cash_diff;           // 现金差异
    float online_diff;         // 线上差异
    float total_diff;          // 总差异
    int status;                // 状态
    
    time_t created_at;         // 创建时间
    time_t confirmed_at;       // 确认时间
    char remark[512];          // 备注
    struct DailySettlement *next;
} DailySettlement;

// ==================== 门店结构 ====================
typedef struct Store {
    int id;                        // 门店ID
    char name[100];                // 门店名称
    char address[256];              // 地址
    char phone[20];                // 联系电话
    char manager_name[50];          // 店长姓名
    int manager_id;                 // 店长ID
    int status;                     // 状态
    time_t created_at;
    time_t updated_at;
    struct Store *next;
} Store;

// 门店库存
typedef struct StoreStock {
    int id;
    int store_id;                 // 门店ID
    char product_id[MAX_ID_LEN];  // 商品ID
    int quantity;                  // 库存数量
    int min_stock;                 // 最低库存预警
    time_t updated_at;
    struct StoreStock *next;
} StoreStock;

// ==================== 调拨结构 ====================
typedef struct TransferItem {
    int id;
    int transfer_id;              // 调拨单ID
    char product_id[MAX_ID_LEN];   // 商品ID
    char product_name[MAX_NAME_LEN]; // 商品名称
    int quantity;                  // 调拨数量
    struct TransferItem *next;
} TransferItem;

typedef struct TransferOrder {
    int id;                        // 调拨单ID
    int from_store_id;             // 源门店ID
    int to_store_id;               // 目标门店ID
    char from_store_name[100];     // 源门店名称
    char to_store_name[100];      // 目标门店名称
    TransferItem *items;           // 调拨明细
    int status;                    // 状态
    int creator_id;                // 创建人
    char creator_name[50];         // 创建人姓名
    int approver_id;               // 审批人
    char approver_name[50];        // 审批人姓名
    int out_operator_id;           // 出库操作员
    char out_operator_name[50];
    int in_operator_id;            // 入库操作员
    char in_operator_name[50];
    time_t created_at;             // 创建时间
    time_t approved_at;            // 审批时间
    time_t out_at;                 // 出库时间
    time_t in_at;                  // 入库时间
    char remark[256];              // 备注
    struct TransferOrder *next;
} TransferOrder;

// ==================== 供应商结算结构 ====================
typedef struct SupplierFinance {
    int supplier_id;              // 供应商ID
    float payment_days;           // 账期（天）
    char rating;                  // 评级
    float total_amount;           // 累计应付
    float paid_amount;            // 已付款
    float pending_amount;         // 待付款
    time_t last_payment_date;     // 最后付款日期
    time_t updated_at;
    struct SupplierFinance *next;
} SupplierFinance;

typedef struct Payable {
    int id;                       // 应付单ID
    int supplier_id;             // 供应商ID
    char supplier_name[100];      // 供应商名称
    int purchase_id;             // 关联采购单ID
    float amount;                 // 应付金额
    float paid_amount;           // 已付金额
    float pending_amount;         // 待付金额
    int status;                   // 状态
    time_t due_date;             // 到期日期
    time_t created_at;           // 创建日期
    time_t paid_at;              // 结清日期
    struct Payable *next;
} Payable;

typedef struct PaymentRecord {
    int id;                       // 付款ID
    int payable_id;               // 关联应付单ID
    int supplier_id;              // 供应商ID
    float amount;                 // 付款金额
    char method[20];              // 付款方式
    char reference[50];           // 付款参考号
    int operator_id;              // 操作员ID
    char operator_name[50];       // 操作员姓名
    char remark[256];             // 备注
    time_t paid_at;               // 付款时间
    struct PaymentRecord *next;
} PaymentRecord;

// ==================== 储值卡结构 ====================
typedef struct VipCard {
    char card_no[30];             // 卡号
    int member_id;               // 关联会员ID
    float balance;                // 余额
    float total_amount;           // 累计充值金额
    int card_type;                // 卡类型
    int status;                   // 状态
    time_t created_at;           // 创建时间
    time_t updated_at;           // 最后更新时间
    time_t expired_at;           // 过期时间
    char password_hash[65];       // 支付密码哈希
    char password_salt[33];       // 密码盐值
    struct VipCard *next;
} VipCard;

typedef struct VipCardTransaction {
    int id;                       // 交易ID
    char card_no[30];             // 卡号
    int type;                     // 交易类型: 0-充值, 1-消费, 2-退款
    float amount;                 // 金额
    float balance_before;          // 交易前余额
    float balance_after;          // 交易后余额
    int operator_id;              // 操作员ID
    char operator_name[50];       // 操作员姓名
    int sale_id;                 // 关联销售单ID
    char remark[256];             // 备注
    time_t created_at;            // 交易时间
    struct VipCardTransaction *next;
} VipCardTransaction;

// ==================== 打印结构 ====================
// ESC/POS commands
#define ESC 0x1B
#define GS 0x1D
#define LF 0x0A

typedef struct {
    char device_path[256];
    int paper_width;
    int codepage;
} PrinterConfig;

typedef struct ReceiptItem {
    char name[50];
    float price;
    float quantity;
    float subtotal;
    float discount;
} ReceiptItem;

typedef struct Receipt {
    char shop_name[100];
    char shop_address[256];
    char shop_phone[20];
    char cashier[50];
    int receipt_no;
    time_t print_time;
    char member_phone[20];
    int member_points;
    int item_count;
    ReceiptItem items[100];
    float subtotal;
    float discount;
    float total;
    int points_earned;
    int points_used;
    float cash;
    float change;
} Receipt;

// ==================== 数据看板结构 ====================
typedef struct DashboardMetrics {
    float today_sales;
    int today_orders;
    float today_avg_order;
    float yesterday_sales;
    int low_stock_count;
    int zero_stock_count;
    struct {
        char product_id[MAX_ID_LEN];
        char product_name[MAX_NAME_LEN];
        int quantity;
    } top_products[5];
    struct {
        char product_id[MAX_ID_LEN];
        char product_name[MAX_NAME_LEN];
        int stock;
        int min_stock;
    } low_stock_products[5];
    int total_members;
    int new_members_today;
    time_t generated_at;
} DashboardMetrics;

// ==================== 全局变量声明 ====================
// 核心全局变量
extern HashNode **g_employee_hash;
extern HashNode **g_product_hash;
extern HashNode **g_barcode_hash;
extern HashNode **g_supplier_hash;
extern int g_auto_id_counter;
extern int g_current_user_id;

// 会员全局变量
extern HashNode **g_member_phone_hash;
extern Member *g_members;

// 促销全局变量
extern Promotion *g_promotions;

// 套装全局变量
extern ProductCombo *g_combos;
extern HashNode **g_combo_barcode_hash;

// 批次全局变量
extern Batch *g_batches;

// 日结全局变量
extern DailySettlement *g_settlements;

// 门店全局变量
extern Store *g_stores;
extern StoreStock *g_store_stocks;
extern int g_default_store_id;

// 调拨全局变量
extern TransferOrder *g_transfer_orders;
extern TransferItem *g_transfer_items;

// 供应商结算全局变量
extern SupplierFinance *g_supplier_finances;
extern Payable *g_payables;
extern PaymentRecord *g_payment_records;

// 储值卡全局变量
extern VipCard *g_vip_cards;
extern VipCardTransaction *g_vip_card_transactions;

// 销售全局变量
extern Sale *g_pending_sales;
extern SaleItem *g_sale_items;

// 采购全局变量
extern Purchase *g_purchases;
extern PurchaseItem *g_purchase_items;
extern TransactionLog *g_transaction_logs;

// 排班全局变量
extern Schedule *g_schedules;

// 库存日志全局变量
extern StockLog *g_stock_logs;

// 系统配置
extern SystemConfig g_config;

// ==================== 哈希表操作 ====================
HashNode** hash_create(int size);
int hash_insert(HashNode **table, int size, const char *key, void *data);
void* hash_search(HashNode **table, int size, const char *key);
int hash_delete(HashNode **table, int size, const char *key);
void hash_destroy(HashNode **table, int size, void (*free_data)(void*));

// ==================== 文件IO操作 ====================
int file_lock(FILE *fp, int operation);
int file_unlock(FILE *fp);
int safe_write(const char *filepath, const char *content);
int atomic_write(const char *filepath, const char *content);
int atomic_append(const char *filepath, const char *content);
char** read_lines(const char *filepath, int *count);
void free_lines(char **lines, int count);

// ==================== 工具函数 ====================
char* trim(char *str);
char* hash_password(const char *password, const char *salt);
char* generate_salt(char *salt);
void get_timestamp(char *buffer);
int get_year_week(int *year, int *week);
int generate_id(void);
void format_time(time_t t, char *buffer);
typedef struct { time_t start; time_t end; } TimeRange;
void get_date_range(TimeRange *range, int days_ago);

// ==================== 系统配置 ====================
int load_config(void);
int save_config(void);

// ==================== 员工操作 ====================
int add_employee(Employee *emp);
Employee* find_employee_by_id(int id);
int update_employee(Employee *emp);
int delete_employee(int id);
int load_employees(void);
int save_employees(void);
Employee** list_employees(int *count);

// ==================== 商品操作 ====================
int add_product(Product *prod);
Product* find_product_by_id(const char *id);
Product* find_product_by_barcode(const char *barcode);
int update_product(Product *prod);
int delete_product(const char *id);
int load_products(void);
int save_products(void);

// ==================== 供应商操作 ====================
int add_supplier(Supplier *sup);
Supplier* find_supplier_by_id(int id);
int load_suppliers(void);
int save_suppliers(void);

// ==================== 库存操作 ====================
int record_stock_log(const char *product_id, const char *type, float quantity,
                     float before_stock, float after_stock, int operator_id, const char *remark);
int save_stock_log(StockLog *log);
int load_stock_logs(void);
StockLog* query_stock_logs(const char *product_id, time_t start, time_t end, int *count);
void check_stock_alert(void);
void inventory_check(void);

// ==================== 事务日志 ====================
int write_transaction_log(const char *type, int ref_id, const char *operation,
                         const char *data, int operator_id);
int load_transaction_logs(void);
TransactionLog* query_transaction_logs(const char *type, time_t start, time_t end, int *count);

// ==================== 会员操作 ====================
int add_member(const char *phone, const char *name);
Member* find_member_by_phone(const char *phone);
Member* find_member_by_id(int id);
int update_member(Member *member);
int delete_member(int id);
float redeem_points(Member *member, int points_to_redeem);
int add_points(Member *member, float amount);
void member_consume(Member *member, float amount);
const char* get_member_level_name(int level);
void upgrade_member_level(Member *member);
Member** list_members(int *count);
void check_expiring_points(void);
float get_member_discount(Member *member);
int get_member_level(Member *member);
int load_members(void);
int save_members(void);
int save_member_record(Member *member);

// ==================== 促销操作 ====================
int create_promotion(Promotion *promo);
int create_discount_promotion(const char *name, const char *product_id,
                             float discount_rate, time_t start, time_t end);
int create_override_promotion(const char *name, float threshold, 
                             float discount_amount, time_t start, time_t end);
Promotion* find_promotion(int id);
Promotion* get_product_promotion(const char *product_id);
Promotion* get_order_override_promotion(void);
int delete_promotion(int id);
Promotion** list_promotions(int *count);
Promotion** list_active_promotions(int *count);

// 购物车
Cart* cart_create(void);
int cart_add_item(Cart *cart, const char *product_id, float quantity);
int cart_remove_item(Cart *cart, const char *product_id);
void cart_clear(Cart *cart);
void cart_destroy(Cart *cart);
float cart_calculate_subtotal(Cart *cart);

// 促销应用
PromotionResult apply_promotions(Cart *cart, Member *member);
float calculate_item_discount(CartItem *item, Promotion *promo);
float calculate_order_discount(Cart *cart, Promotion *promo);
float calculate_nth_discount(int quantity, Promotion *promo);
float calculate_buy_m_get_n(int quantity, Promotion *promo, float unit_price);
float calculate_member_price(Promotion *promo, Member *member, float original_price);

int load_promotions(void);
int save_promotion(Promotion *p);
int save_all_promotions(void);

// ==================== 套装操作 ====================
int create_combo(ProductCombo *combo);
int add_combo_item(int combo_id, ComboItem *item);
ProductCombo* find_combo_by_id(int id);
ProductCombo* find_combo_by_barcode(const char *barcode);
int update_combo(ProductCombo *combo);
int delete_combo(int id);
ProductCombo** list_combos(int *count);
ProductCombo** list_active_combos(int *count);
int check_combo_stock(ProductCombo *combo, int store_id, int quantity);
int deduct_combo_stock(ProductCombo *combo, int store_id, int quantity, int operator_id);
int rollback_combo_stock(ProductCombo *combo, int store_id, int quantity, int operator_id);
int load_combos(void);
int save_combo(ProductCombo *combo);
int save_all_combos(void);
int load_combo_items(void);
int save_combo_item(ComboItem *item);
int load_all_combo_items(void);

// ==================== 批次操作 ====================
char* create_batch(const char *product_id, const char *product_name,
                  float quantity, float price,
                  time_t production_date, int expiry_days, int supplier_id);
Batch* find_batch(const char *batch_no);
Batch** list_product_batches(const char *product_id, int *count);
float deduct_batch_stock(const char *product_id, float quantity, int operator_id);
int add_batch_stock(const char *batch_no, float quantity);
float get_batch_total_stock(const char *product_id);
void check_expiring_batches(void);
Batch** list_expiring_batches(int days, int *count);
int load_batches(void);
int save_batch(Batch *batch);
int save_all_batches(void);
char* generate_batch_no(const char *product_id);
const char* get_expiry_category(int days);

// ==================== 销售操作 ====================
int create_sale(Sale *sale);
int add_sale_item(int sale_id, SaleItem *item);
SaleItem* get_sale_items(int sale_id, int *count);
Sale* find_pending_sale(int sale_id);
float calculate_sale_total(int sale_id);
int complete_sale(int sale_id, const char *payment_method, float discount, float cash_received);
int hang_sale(int sale_id);
int cancel_sale(int sale_id);
int load_pending_sales(void);
int save_pending_sale(Sale *sale);
int save_sale_record(Sale *sale);
int scan_and_sell(int cashier_id, const char *barcode, int quantity);

// ==================== 采购操作 ====================
int create_purchase(Purchase *purchase);
int add_purchase_item(int purchase_id, PurchaseItem *item);
Purchase* find_purchase(int id);
PurchaseItem* get_purchase_items(int purchase_id, int *count);
float calculate_purchase_total(int purchase_id);
int approve_purchase(int purchase_id, int approver_id);
int reject_purchase(int purchase_id, int approver_id);
int receive_purchase(int purchase_id, int operator_id);
Purchase** list_purchases(int status, int *count);
const char* get_purchase_status_str(int status);
void print_purchase_detail(int purchase_id);
int save_purchase(Purchase *pur);
int save_purchase_item(PurchaseItem *item);
int load_purchases(void);
int load_purchase_items(void);

// ==================== 排班操作 ====================
int create_schedule(Schedule *schedule);
int update_schedule(int schedule_id, char shifts[7][4]);
Schedule* find_schedule(int schedule_id);
Schedule* find_schedule_by_employee_week(int employee_id, int year, int week);
int batch_create_schedule(int employee_id, int year, int week, char shifts[7][4]);
Schedule** list_employee_schedules(int employee_id, int *count);
Schedule** list_week_schedules(int year, int week, int *count);
const char* get_shift_name(const char *shift);
void print_schedule_table(int year, int week);
void count_shifts_by_type(int year, int week);
int load_schedules(void);
int save_schedule(Schedule *sch);
int delete_schedule(int schedule_id);

// ==================== 日结/财务操作 ====================
int create_settlement(int cashier_id, float actual_cash, float actual_online, 
                     const char *remark);
DailySettlement* find_settlement(int id);
DailySettlement* find_settlement_by_cashier_date(int cashier_id, time_t date);
DailySettlement** list_settlements_by_date(time_t date, int *count);
DailySettlement** list_cashier_settlements(int cashier_id, int *count);
int confirm_settlement(int settlement_id, const char *remark);
void calculate_cashier_sales(int cashier_id, time_t start, time_t end,
                            int *order_count, float *cash_total, 
                            float *online_total, float *grand_total);
const char* get_settlement_status_name(int status);
void print_settlement_detail(int settlement_id);
int load_settlements(void);
int save_settlement(DailySettlement *sett);
int save_all_settlements(void);

// ==================== 门店操作 ====================
int create_store(Store *store);
Store* find_store_by_id(int id);
Store* find_store_by_name(const char *name);
int update_store(Store *store);
int delete_store(int id);
Store** list_stores(int *count);
Store** list_active_stores(int *count);
int get_store_stock(int store_id, const char *product_id);
int set_store_stock(int store_id, const char *product_id, int quantity, int min_stock);
int add_store_stock(int store_id, const char *product_id, int quantity, int operator_id, const char *remark);
int reduce_store_stock(int store_id, const char *product_id, int quantity, int operator_id, const char *remark);
StoreStock** get_product_all_stores(const char *product_id, int *count);
int load_stores(void);
int save_store(Store *store);
int save_all_stores(void);
int load_store_stocks(void);
int save_store_stock(StoreStock *stock);
int save_all_store_stocks(void);

// ==================== 调拨操作 ====================
int create_transfer_order(int from_store_id, int to_store_id, int creator_id, const char *remark);
int add_transfer_item(int transfer_id, const char *product_id, int quantity);
TransferOrder* find_transfer_order(int id);
int approve_transfer(int transfer_id, int approver_id);
int reject_transfer(int transfer_id, int approver_id, const char *reason);
int confirm_out_transfer(int transfer_id, int operator_id);
int confirm_in_transfer(int transfer_id, int operator_id);
int cancel_transfer(int transfer_id, int operator_id);
TransferOrder** list_transfers_by_status(int status, int *count);
TransferOrder** list_transfers_by_store(int store_id, int *count);
int load_transfers(void);
int load_transfer_items(void);
int save_transfer_order(TransferOrder *order);
int save_all_transfers(void);
int save_transfer_item(TransferItem *item);

// ==================== 供应商结算操作 ====================
SupplierFinance* get_supplier_finance(int supplier_id);
int update_supplier_finance(int supplier_id, float payment_days, char rating);
SupplierFinance** list_supplier_finances(int *count);
int generate_payable(int purchase_id);
int create_payable(int supplier_id, float amount, const char *remark);
Payable* find_payable(int id);
Payable** list_payables_by_supplier(int supplier_id, int *count);
Payable** list_pending_payables(int *count);
int record_payment(int payable_id, float amount, const char *method, 
                  const char *reference, int operator_id, const char *remark);
int settle_supplier(int supplier_id, float amount, const char *method,
                    const char *reference, int operator_id, const char *remark);
PaymentRecord** list_payment_records(int *count);
PaymentRecord** list_supplier_payment_records(int supplier_id, int *count);
void generate_supplier_statement(int supplier_id, time_t start, time_t end);
void print_payables_summary(void);
int load_supplier_finances(void);
int save_supplier_finance(SupplierFinance *fin);
int load_payables(void);
int save_payable(Payable *payable);
int load_payment_records(void);
int save_payment_record(PaymentRecord *record);

// ==================== 储值卡操作 ====================
VipCard* create_vip_card(const char *password);
VipCard* find_vip_card(const char *card_no);
VipCard* find_vip_card_by_member(int member_id);
int verify_vip_card_password(const char *card_no, const char *password);
int change_vip_card_password(const char *card_no, const char *old_password, const char *new_password);
int bind_vip_card_member(const char *card_no, int member_id);
int freeze_vip_card(const char *card_no);
int unfreeze_vip_card(const char *card_no);
int cancel_vip_card(const char *card_no);
int set_vip_card_expired(const char *card_no, time_t expired_at);
VipCard** list_vip_cards(int *count);
VipCard** list_member_vip_cards(int member_id, int *count);
int recharge_vip_card(const char *card_no, float amount, int operator_id, const char *remark);
int consume_vip_card(const char *card_no, float amount, int sale_id, int operator_id, const char *remark);
int refund_vip_card(const char *card_no, float amount, int sale_id, int operator_id, const char *remark);
VipCardTransaction** query_vip_card_transactions(const char *card_no, int *count);
VipCardTransaction** query_member_vip_transactions(int member_id, int *count);
void print_vip_card_summary(void);
void generate_vip_card_statement(const char *card_no, time_t start, time_t end);
int load_vip_cards(void);
int save_vip_card(VipCard *card);
int save_all_vip_cards(void);
int load_vip_card_transactions(void);
int save_vip_card_transaction(VipCardTransaction *trans);

// ==================== 打印操作 ====================
PrinterConfig printer_get_default_config(void);
int printer_set_device(const char *device_path);
int printer_init(const PrinterConfig *config);
int printer_close(void);
int printer_print_receipt(const Receipt *receipt);
int printer_test_page(void);
const char* printer_get_device_path(void);
int printer_is_available(void);

// ==================== 数据看板 ====================
DashboardMetrics generate_dashboard_metrics(void);
void print_dashboard(void);
void print_bar_chart(const char *label, float value, float max_value, int width);
float get_today_sales(void);
int get_today_orders(void);
void get_low_stock_alerts(int top_n);
void get_top_selling_products(int top_n);

// ==================== 报表操作 ====================
void generate_sales_report(time_t start, time_t end, const char *format);
int export_sales_csv(time_t start, time_t end, const char *filename);
int export_sales_html(time_t start, time_t end, const char *filename);
void generate_inventory_report(const char *format);
void generate_purchase_report(time_t start, time_t end, const char *format);
void generate_profit_loss_report(time_t start, time_t end);
void show_export_menu(void);
void handle_export(int choice);

// ==================== 初始化/清理 ====================
int init_system(void);
void cleanup_system(void);

#endif // SUPERMARKET_H
