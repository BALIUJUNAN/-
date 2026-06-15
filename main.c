/**
 * @file main.c
 * @brief 超市管理系统主程序 - 入口函数、菜单控制、业务流程编排
 *
 * 本文件是整个系统的用户交互层，负责：
 *
 *   1. 系统启动初始化
 *      - init_system() 初始化哈希表、加载配置
 *      - 加载所有数据文件（员工/商品/会员/销售/采购/排班...）
 *      - 首次使用自动创建默认管理员（admin/admin123）
 *
 *   2. 登录/权限管理
 *      - login(): SHA-256(salt+password) 验证
 *      - logout(): 记录日志 + 清理该收银员的挂单
 *      - check_permission(): 管理员拥有全部权限，其他角色精确匹配
 *
 *   3. 主菜单及各子菜单
 *      - 数字选项 0-9 + 字母选项 A-F
 *      - 每个子菜单都是 while(1) 循环，输入 0 返回上级
 *
 *   4. 关键业务流程编排（以销售为例）
 *      show_sale_menu():
 *        扫码 → 创建挂单 → 显示明细 → 识别会员 → 计算折扣
 *        → 选择支付方式 → 现金/微信/支付宝/储值卡
 *        → 储值卡需验证密码 + 余额检查 + 失败回滚
 *        → complete_sale() → 扣库存 + 打小票 + 发积分
 *
 * 菜单结构：
 *   主菜单
 *   ├── 1. 登录/注销
 *   ├── 2. 人员管理（CRUD + 表格显示）
 *   ├── 3. 商品管理（添加/列表/条码查询/编辑/预警/盘点）
 *   ├── 4. 销售管理（扫码/挂单/支付/取消）
 *   ├── 5. 采购管理（创建/审批/收货/列表）
 *   ├── 6. 排班管理（创建/查看/统计）
 *   ├── 7. 报表管理（销售/库存/采购/盈亏/导出）
 *   ├── 8. 系统设置（备份/信息/初始化管理员）
 *   ├── 9. 小票打印（测试/设置端口/历史小票）
 *   ├── A. 套装管理
 *   ├── B. 库存调拨
 *   ├── C. 供应商结算
 *   ├── D. 促销管理
 *   ├── E. 储值卡管理
 *   ├── F. 会员管理
 *   └── 0. 退出系统
 */

#include "supermarket.h"
#include "ui.h"
#include <conio.h>
#include <ctype.h>
#include <limits.h>

#ifdef _WIN32
#include <direct.h>
#endif

// ==================== 全局状态 ====================
int g_current_user_id = 0;
char g_current_user_role[20] = {0};

// ==================== 函数声明 ====================
void show_main_menu(void);
void show_login_menu(void);
void show_employee_menu(void);
void show_product_menu(void);
void show_sale_menu(void);
void show_purchase_menu(void);
void show_schedule_menu(void);
void show_report_menu(void);
void show_system_menu(void);
void show_printer_menu(void);
void show_combo_menu(void);
void show_transfer_menu(void);
void show_supplier_settlement_menu(void);
void show_promotion_menu(void);
void show_vipcard_menu(void);
void show_member_menu(void);

int login(const char *username, const char *password);
void logout(void);

// ==================== 工具函数 ====================
// 注意：所有输入和UI函数已移至 ui.c/ui.h
// 此处保留业务相关的辅助函数

// ==================== 登录/权限 ====================

/**
 * 用户登录
 *
 * 登录流程：
 *   1. 获取所有在职员工列表
 *   2. 按用户名逐一匹配
 *   3. 对匹配到的员工，用其 salt 重新计算密码哈希并与存储值比对
 *   4. 验证通过则设置全局用户 ID 和角色
 *
 * @param username  用户名（对应员工姓名）
 * @param password  明文密码
 * @return 0 登录成功，-1 用户名或密码错误
 */
int login(const char *username, const char *password) {
    int count = 0;
    /* list_employees() 返回所有在职员工的动态数组，需要 free 释放 */
    Employee **employees = list_employees(&count);

    for (int i = 0; i < count; i++) {
        /* 按员工姓名匹配用户名 */
        if (strcmp(employees[i]->name, username) == 0) {
            char hash[65];
            /* hash_password(明文密码, 该员工的盐值, 输出哈希)
             * 计算 SHA-256(salt + password)，与存储的哈希比对 */
            hash_password(password, employees[i]->salt, hash);
            if (strcmp(hash, employees[i]->password_hash) == 0) {
                /* 密码匹配成功：设置全局登录状态 */
                g_current_user_id = employees[i]->id;
                strncpy(g_current_user_role, employees[i]->role, 19);
                free(employees);  /* 释放 list_employees 返回的数组 */
                return 0;
            }
        }
    }

    free(employees);  /* 遍历完毕未匹配，也要释放数组 */
    return -1;
}

/**
 * 用户登出
 *
 * 登出流程：
 *   1. 查找当前登录员工信息（用于日志显示）
 *   2. 记录登出事务日志到 transaction.log
 *   3. 清理该收银员名下的所有挂单（防止切换账号后看到别人的订单）
 *   4. 重置全局登录状态
 */
void logout(void) {
    if (g_current_user_id > 0) {
        /* find_employee_by_id: 按 ID 从哈希表中查找员工（O(1)） */
        Employee *emp = find_employee_by_id(g_current_user_id);
        if (emp) {
            char timestamp[32];
            /* get_timestamp: 获取当前时间的 "YYYY-MM-DD HH:MM:SS" 字符串 */
            get_timestamp(timestamp);
            printf("[%s] 用户 %s [%s] 登出\n", timestamp, emp->name, emp->role);

            /* write_transaction_log: 将操作记录追加到 data/transaction.log
             * 参数: 类型, 关联ID, 操作名, 描述, 操作员ID */
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "用户 %s 登出系统", emp->name);
            write_transaction_log("AUTH", g_current_user_id, "LOGOUT", log_msg, g_current_user_id);
        }

        /* cleanup_cashier_sales: 从 g_pending_sales 和 g_sale_items 中
         * 移除该收银员的所有挂单和明细，释放内存 */
        cleanup_cashier_sales(g_current_user_id);
    }

    /* 重置全局登录状态 */
    g_current_user_id = 0;
    g_current_user_role[0] = '\0';
    printf("已退出登录\n");
}

/**
 * 检查权限
 */
int check_permission(const char *required_role) {
    if (g_current_user_id == 0) return 0;
    if (strcmp(required_role, "ALL") == 0) return 1;
    if (strcmp(g_current_user_role, "管理员") == 0) return 1;
    if (strcmp(g_current_user_role, required_role) == 0) return 1;
    return 0;
}

// ==================== 主菜单 ====================

void show_main_menu(void) {
    while (1) {
        print_title_box("超市管理系统");
        
        if (g_current_user_id > 0) {
            Employee *emp = find_employee_by_id(g_current_user_id);
            print_info("当前用户: %s [%s]", 
                   emp ? emp->name : "未知", g_current_user_role);
        } else {
            print_warning("未登录");
        }
        
        printf("\n");
        printf("1. 登录/注销\n");
        printf("2. 人员管理\n");
        printf("3. 商品管理\n");
        printf("4. 销售管理\n");
        printf("5. 采购管理\n");
        printf("6. 排班管理\n");
        printf("7. 报表管理\n");
        printf("8. 系统设置\n");
        printf("9. 小票打印\n");
        printf("A. 套装管理\n");
        printf("B. 库存调拨\n");
        printf("C. 供应商结算\n");
        printf("D. 促销管理\n");
        printf("E. 储值卡管理\n");
        printf("F. 会员管理\n");
        printf("0. 退出系统\n");
        printf("\n请选择: ");
        
        char input[32];
        if (fgets(input, sizeof(input), stdin) == NULL) {
            continue;
        }
        input[strcspn(input, "\n")] = '\0';
        
        // 去除空白
        trim(input);
        
        if (strlen(input) == 0) {
            continue;
        }
        
        // 检查是否是字母选项
        if (strlen(input) == 1 && isalpha(input[0])) {
            char ch = toupper(input[0]);
            switch (ch) {
                case 'A': show_combo_menu(); break;
                case 'B': show_transfer_menu(); break;
                case 'C': show_supplier_settlement_menu(); break;
                case 'D': show_promotion_menu(); break;
                case 'E': show_vipcard_menu(); break;
                case 'F': show_member_menu(); break;
                default: print_warning("无效选择，请重试");
            }
            continue;
        }
        
        // 尝试解析为数字
        int choice = atoi(input);
        
        switch (choice) {
            case 1: show_login_menu(); break;
            case 2: show_employee_menu(); break;
            case 3: show_product_menu(); break;
            case 4: show_sale_menu(); break;
            case 5: show_purchase_menu(); break;
            case 6: show_schedule_menu(); break;
            case 7: show_report_menu(); break;
            case 8: show_system_menu(); break;
            case 9: show_printer_menu(); break;
            case 0: 
                print_success("感谢使用，再见！");
                cleanup_system();
                ui_cleanup();
                return;
            default: print_warning("无效选择，请重试");
        }
    }
}

/**
 * 登录菜单
 */
void show_login_menu(void) {
    print_title_box("登录/注销");
    
    if (g_current_user_id > 0) {
        print_info("当前已登录，是否注销？");
        printf("1. 注销\n");
        printf("0. 返回\n");
        
        int choice = get_safe_int("请选择: ", 0, 1);
        if (choice == 1) {
            logout();
        }
        return;
    }
    
    char username[MAX_NAME_LEN];
    char password[50];
    
    get_safe_string("用户名: ", username, sizeof(username));
    get_password_input("密码: ", password, sizeof(password));
    
    if (login(username, password) == 0) {
        Employee *emp = find_employee_by_id(g_current_user_id);
        if (emp) {
            print_success("登录成功！欢迎 %s [%s]", emp->name, emp->role);
        } else {
            print_success("登录成功！");
        }
    } else {
        print_error("用户名或密码错误");
    }
}

// ==================== 人员管理 ====================

void show_employee_menu(void) {
    while (1) {
        print_title_box("人员管理");
        printf("1. 添加员工\n");
        printf("2. 员工列表\n");
        printf("3. 查询员工\n");
        printf("4. 编辑员工\n");
        printf("5. 删除员工(离职)\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_safe_int("", 0, 5);
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                if (!check_permission("管理员") && !check_permission("店长")) {
                    print_error("权限不足，需要管理员或店长权限");
                    break;
                }
                
                Employee emp;
                memset(&emp, 0, sizeof(emp));
                
                get_safe_string("姓名: ", emp.name, sizeof(emp.name));
                get_safe_string("角色 (收银员/库管/店长/管理员): ", emp.role, sizeof(emp.role));
                
                char password[50];
                get_password_input("密码: ", password, sizeof(password));
                
                generate_salt(emp.salt);
                hash_password(password, emp.salt, emp.password_hash);
                
                int id = add_employee(&emp);
                if (id > 0) {
                    print_success("添加成功！员工ID: %d", id);
                    save_employees();
                }
                break;
            }
            
            case 2: {
                int count = 0;
                Employee **list = list_employees(&count);
                
                if (count == 0) {
                    print_warning("暂无员工记录");
                    break;
                }
                
                /* 表格列定义 */
                TableColumn cols[] = {
                    {"ID", 8, ALIGN_LEFT},
                    {"姓名", 12, ALIGN_LEFT},
                    {"角色", 12, ALIGN_LEFT},
                    {"状态", 10, ALIGN_CENTER}
                };
                
                table_begin(cols, 4);
                table_draw_header();
                
                char buf[4][64];
                for (int i = 0; i < count; i++) {
                    snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                    snprintf(buf[1], sizeof(buf[1]), "%s", list[i]->name);
                    snprintf(buf[2], sizeof(buf[2]), "%s", list[i]->role);
                    snprintf(buf[3], sizeof(buf[3]), "%s", 
                             list[i]->status == STATUS_ACTIVE ? "在职" : "离职");
                    const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                    table_draw_row(row);
                }
                
                table_end();
                print_info("共 %d 名员工", count);
                free(list);
                wait_for_key();
                break;
            }
            
            case 3: {
                int id = get_safe_int("员工ID: ", 1, 99999);
                
                Employee *emp = find_employee_by_id(id);
                if (emp) {
                    TableColumn cols[] = {
                        {"项目", 12, ALIGN_LEFT},
                        {"内容", 24, ALIGN_LEFT}
                    };
                    
                    table_begin(cols, 2);
                    
                    /* 模拟数据行 */
                    char id_buf[32], status_buf[32];
                    snprintf(id_buf, sizeof(id_buf), "%d", emp->id);
                    snprintf(status_buf, sizeof(status_buf), "%s", 
                             emp->status == STATUS_ACTIVE ? "在职" : "离职");
                    
                    const char *rows[][2] = {
                        {"ID", id_buf},
                        {"姓名", emp->name},
                        {"角色", emp->role},
                        {"状态", status_buf}
                    };
                    
                    table_draw_header();
                    for (int i = 0; i < 4; i++) {
                        table_draw_row(rows[i]);
                    }
                    table_end();
                } else {
                    print_error("未找到该员工");
                }
                break;
            }
            
            case 4: {
                int id = get_safe_int("员工ID: ", 1, 99999);
                
                Employee *emp = find_employee_by_id(id);
                if (emp) {
                    char input[MAX_NAME_LEN];
                    
                    print_info("当前姓名: %s (直接回车跳过)", emp->name);
                    get_safe_string("新姓名: ", input, sizeof(input));
                    if (strlen(input) > 0) strncpy(emp->name, input, MAX_NAME_LEN - 1);
                    
                    print_info("当前角色: %s (直接回车跳过)", emp->role);
                    get_safe_string("新角色: ", input, sizeof(input));
                    if (strlen(input) > 0) strncpy(emp->role, input, 19);
                    
                    update_employee(emp);
                    save_employees();
                    print_success("员工信息已更新");
                } else {
                    print_error("未找到该员工");
                }
                break;
            }
            
            case 5: {
                int id = get_safe_int("员工ID: ", 1, 99999);
                
                if (confirm_action("确定要将该员工标记为离职？")) {
                    if (delete_employee(id) == 0) {
                        save_employees();
                        print_success("已标记为离职");
                    } else {
                        print_error("操作失败");
                    }
                }
                break;
            }
            
            default:
                print_warning("无效选择");
        }
    }
}

// ==================== 商品管理 ====================

void show_product_menu(void) {
    while (1) {
        print_title_box("商品管理");
        printf("1. 添加商品\n");
        printf("2. 商品列表\n");
        printf("3. 条码查询\n");
        printf("4. 编辑商品\n");
        printf("5. 库存预警\n");
        printf("6. 库存盘点\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_safe_int("", 0, 6);
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                Product prod;
                memset(&prod, 0, sizeof(prod));

                /* 商品ID由系统自动生成，无需手动输入 */
                get_safe_string("商品名称: ", prod.name, sizeof(prod.name));
                get_safe_string("条码: ", prod.barcode, sizeof(prod.barcode));
                prod.price = get_safe_float("售价: ");
                prod.cost = get_safe_float("进价: ");
                prod.stock = get_safe_int("库存: ", 0, 999999);
                prod.min_stock = get_safe_int("最低库存预警: ", 0, 999999);
                get_safe_string("供应商ID: ", prod.supplier_id, sizeof(prod.supplier_id));

                if (add_product(&prod) == 0) {
                    save_products();
                    print_success("商品添加成功！系统分配ID: %s", prod.id);
                } else {
                    print_error("商品添加失败（条码可能已存在）");
                }
                break;
            }
            
            case 2: {
                /* 先统计商品数量 */
                int count = 0;
                Product **products = NULL;
                
                for (int i = 0; i < g_product_hash->size; i++) {
                    HashNode *node = g_product_hash->buckets[i];
                    while (node) {
                        Product *p = (Product*)node->data;
                        if (p->status == 1) {
                            count++;
                        }
                        node = node->next;
                    }
                }
                
                if (count == 0) {
                    print_warning("暂无商品记录");
                    break;
                }
                
                /* 收集商品数据 */
                products = malloc(count * sizeof(Product*));
                int idx = 0;
                for (int i = 0; i < g_product_hash->size; i++) {
                    HashNode *node = g_product_hash->buckets[i];
                    while (node) {
                        Product *p = (Product*)node->data;
                        if (p->status == 1) {
                            products[idx++] = p;
                        }
                        node = node->next;
                    }
                }
                
                /* 分页显示 */
                PageContext page = page_init(count, 10);
                
                while (1) {
                    TableColumn cols[] = {
                        {"商品ID", 12, ALIGN_LEFT},
                        {"名称", 16, ALIGN_LEFT},
                        {"条码", 12, ALIGN_LEFT},
                        {"库存", 8, ALIGN_RIGHT},
                        {"价格", 10, ALIGN_RIGHT}
                    };
                    
                    table_begin(cols, 5);
                    table_draw_header();
                    
                    int start = (page.current_page - 1) * page.page_size;
                    int end = start + page.page_size;
                    if (end > count) end = count;
                    
                    char buf[5][64];
                    for (int i = start; i < end; i++) {
                        snprintf(buf[0], sizeof(buf[0]), "%s", products[i]->id);
                        snprintf(buf[1], sizeof(buf[1]), "%s", products[i]->name);
                        snprintf(buf[2], sizeof(buf[2]), "%s", products[i]->barcode);
                        snprintf(buf[3], sizeof(buf[3]), "%d", products[i]->stock);
                        snprintf(buf[4], sizeof(buf[4]), "¥%.2f", products[i]->price);
                        const char *row[] = {buf[0], buf[1], buf[2], buf[3], buf[4]};
                        table_draw_row(row);
                    }
                    
                    table_end();
                    
                    if (page.total_pages == 1) {
                        break;
                    }
                    
                    page_show_navigation(&page);
                    int action = page_get_action(&page);
                    
                    if (action == -1) break;
                }
                
                free(products);
                break;
            }
            
            case 3: {
                char barcode[30];
                get_safe_string("请输入条码: ", barcode, sizeof(barcode));
                
                Product *p = find_product_by_barcode(barcode);
                if (p) {
                    TableColumn cols[] = {
                        {"项目", 12, ALIGN_LEFT},
                        {"内容", 24, ALIGN_LEFT}
                    };
                    
                    table_begin(cols, 2);
                    
                    char buf[6][2][32];
                    snprintf(buf[0][0], sizeof(buf[0][0]), "商品ID");
                    snprintf(buf[0][1], sizeof(buf[0][1]), "%s", p->id);
                    snprintf(buf[1][0], sizeof(buf[1][0]), "名称");
                    snprintf(buf[1][1], sizeof(buf[1][1]), "%s", p->name);
                    snprintf(buf[2][0], sizeof(buf[2][0]), "条码");
                    snprintf(buf[2][1], sizeof(buf[2][1]), "%s", p->barcode);
                    snprintf(buf[3][0], sizeof(buf[3][0]), "库存");
                    snprintf(buf[3][1], sizeof(buf[3][1]), "%d", p->stock);
                    snprintf(buf[4][0], sizeof(buf[4][0]), "售价");
                    snprintf(buf[4][1], sizeof(buf[4][1]), "¥%.2f", p->price);
                    snprintf(buf[5][0], sizeof(buf[5][0]), "进价");
                    snprintf(buf[5][1], sizeof(buf[5][1]), "¥%.2f", p->cost);
                    
                    table_draw_header();
                    for (int i = 0; i < 6; i++) {
                        const char *row_ptr[2] = {buf[i][0], buf[i][1]};
                        table_draw_row(row_ptr);
                    }
                    table_end();
                } else {
                    print_error("未找到该商品");
                }
                break;
            }
            
            case 4: {
                char id[MAX_ID_LEN];
                get_safe_string("商品ID: ", id, sizeof(id));

                Product *p = find_product_by_id(id);
                if (p) {
                    char input[50];

                    print_info("当前名称: %s (直接回车跳过)", p->name);
                    get_safe_string("新名称: ", input, sizeof(input));
                    if (strlen(input) > 0) strncpy(p->name, input, MAX_NAME_LEN - 1);

                    print_info("当前售价: ¥%.2f (直接回车跳过)", p->price);
                    float f = get_safe_float("新售价: ");
                    if (f > 0) p->price = f;

                    print_info("当前库存: %d (直接回车跳过)", p->stock);
                    char stock_input[32];
                    get_safe_string("新库存: ", stock_input, sizeof(stock_input));
                    if (strlen(stock_input) > 0) {
                        int n = atoi(stock_input);
                        if (n >= 0 && n <= 999999) {
                            p->stock = n;
                        } else {
                            print_warning("库存值无效，保持原值");
                        }
                    }

                    update_product(p);
                    save_products();
                    print_success("商品信息已更新");
                } else {
                    print_error("未找到该商品");
                }
                break;
            }
            
            case 5:
                check_stock_alert();
                break;
            
            case 6:
                inventory_check();
                break;
            
            default:
                print_warning("无效选择");
        }
    }
}

// ==================== 销售管理 ====================

void show_sale_menu(void) {
    while (1) {
        print_title_box("销售管理");
        printf("1. 扫描销售\n");
        printf("2. 挂单列表\n");
        printf("3. 完成支付\n");
        printf("4. 取消挂单\n");
        printf("5. 退款处理\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_safe_int("", 0, 5);
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                /* ===== 扫描销售 ===== */
                char barcode[30];
                get_safe_string("请输入条码: ", barcode, sizeof(barcode));
                int qty = get_safe_int("数量: ", 1, 9999);

                /* scan_and_sell(): 扫描条码并创建挂单
                 *   内部流程:
                 *   1. 先查 g_combo_barcode_hash 判断是否是套装条码
                 *   2. 如果是套装 → check_combo_stock 检查库存 → 创建挂单
                 *   3. 如果是普通商品 → 查 g_barcode_hash → 检查库存 → 创建挂单
                 *   4. 自动应用单品促销折扣（查 g_promotions 中该商品的有效促销）
                 *   返回: 新创建的销售订单 ID（挂单号），失败返回 -1 */
                int sale_id = scan_and_sell(g_current_user_id, barcode, qty);
                if (sale_id > 0) {
                    print_success("已添加到挂单，订单号: %d", sale_id);
                }
                break;
            }
            
            case 2: {
                Sale *sale = g_pending_sales;
                int count = 0;
                
                /* 统计挂单数量 */
                Sale *temp = sale;
                while (temp) { count++; temp = temp->next; }
                
                if (count == 0) {
                    print_warning("暂无挂单");
                    break;
                }
                
                TableColumn cols[] = {
                    {"订单号", 10, ALIGN_RIGHT},
                    {"收银员", 10, ALIGN_RIGHT},
                    {"时间", 18, ALIGN_LEFT},
                    {"状态", 10, ALIGN_CENTER}
                };
                
                table_begin(cols, 4);
                table_draw_header();
                
                char buf[4][64];
                while (sale) {
                    char time_str[32];
                    format_time(sale->created_at, time_str);
                    snprintf(buf[0], sizeof(buf[0]), "%d", sale->id);
                    snprintf(buf[1], sizeof(buf[1]), "%d", sale->cashier_id);
                    snprintf(buf[2], sizeof(buf[2]), "%s", time_str);
                    snprintf(buf[3], sizeof(buf[3]), "%s", 
                             sale->status == SALE_PENDING ? "挂单" : "已完成");
                    const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                    table_draw_row(row);
                    sale = sale->next;
                }
                table_end();
                break;
            }
            
            case 3: {
                /* ============================================================
                 * ===== 完成支付 — 核心业务流程（最复杂的一个 case） =====
                 * ============================================================
                 * 完整流程:
                 *   1. 验证挂单存在
                 *   2. 显示订单信息和商品明细
                 *   3. 识别会员（手机号/储值卡号 → 查找会员）
                 *   4. 计算综合折扣（单品促销 + 会员折扣 + 满减）
                 *   5. 选择支付方式（现金/微信/支付宝/储值卡）
                 *   6. 储值卡支付需验证密码 + 余额检查 + 失败回滚
                 *   7. 调用 complete_sale() 完成交易
                 *   8. complete_sale 内部: 扣库存 → 保存记录 → 打印小票 → 发放积分
                 * ============================================================ */
                int sale_id = get_safe_int("订单号: ", 1, 999999);

                /* find_pending_sale(): 在 g_pending_sales 链表中查找挂单 */
                Sale *sale_check = find_pending_sale(sale_id);
                if (!sale_check) {
                    print_error("未找到该挂单");
                    break;
                }

                /* calculate_sale_total(): 遍历 g_sale_items 中该订单的所有明细，
                 * 累加每个商品的 subtotal（折后价 × 数量） */
                float total = calculate_sale_total(sale_id);

                print_title_box_single("订单信息");
                print_info("订单号: %d", sale_check->id);
                print_info("收银员: %d", sale_check->cashier_id);
                print_info("商品总价: ¥%.2f", total);

                /* get_sale_items(): 从 g_sale_items 链表中提取该订单的所有明细，
                 * 返回动态数组（调用方需 free 释放） */
                int item_count = 0;
                SaleItem *items = get_sale_items(sale_id, &item_count);

                /* 用表格显示商品明细 */
                if (item_count > 0) {
                    TableColumn cols[] = {
                        {"商品", 20, ALIGN_LEFT},
                        {"数量", 8, ALIGN_RIGHT},
                        {"单价", 10, ALIGN_RIGHT},
                        {"小计", 10, ALIGN_RIGHT}
                    };
                    table_begin(cols, 4);
                    table_draw_header();

                    char buf[4][64];
                    for (int i = 0; i < item_count; i++) {
                        snprintf(buf[0], sizeof(buf[0]), "%s", items[i].product_name);
                        snprintf(buf[1], sizeof(buf[1]), "%d", (int)items[i].quantity);
                        snprintf(buf[2], sizeof(buf[2]), "¥%.2f", items[i].price);
                        snprintf(buf[3], sizeof(buf[3]), "¥%.2f", items[i].subtotal);
                        const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                        table_draw_row(row);
                    }
                    table_end();
                }

                /* ===== 第3步：识别会员 =====
                 * 优先从挂单中获取已关联的会员（扫码时可能已设置），
                 * 否则提示用户输入手机号或储值卡号 */
                Member *pay_member = sale_check->member_id > 0
                    ? find_member_by_id(sale_check->member_id) : NULL;

                if (!pay_member) {
                    char member_input[30] = {0};
                    print_hint("提示: 输入会员手机号可享受会员折扣，直接回车跳过");
                    get_safe_string("会员手机号（可选）: ", member_input, sizeof(member_input));
                    trim(member_input);

                    if (strlen(member_input) > 0) {
                        /* find_member_by_phone(): 在 g_member_phone_hash 中按手机号查找（O(1)） */
                        pay_member = find_member_by_phone(member_input);
                        if (!pay_member) {
                            /* 手机号未找到，尝试按储值卡号查找绑定的会员 */
                            VipCard *vc = g_vip_cards;
                            while (vc) {
                                if (strcmp(vc->card_no, member_input) == 0) break;
                                vc = vc->next;
                            }
                            if (vc && vc->member_id > 0)
                                /* find_member_by_id(): 在 g_members 链表中按 ID 查找 */
                                pay_member = find_member_by_id(vc->member_id);
                        }

                        if (pay_member) {
                            sale_check->member_id = pay_member->id;  /* 将会员关联到订单 */
                            /* get_member_level_name(): 返回等级中文名（普通/银卡/金卡/钻石） */
                            print_success("已识别会员: %s [%s]",
                                         pay_member->name,
                                         get_member_level_name(pay_member->level));
                        } else {
                            print_warning("未找到该会员，将按普通顾客结算");
                        }
                    }
                }

                /* ===== 第4步：计算综合折扣 =====
                 * 流程：创建临时购物车 → 填入商品 → 计算折扣 → 销毁购物车
                 *
                 * calculate_total_discount() 内部逻辑：
                 *   1. 遍历购物车每个商品，查找该商品的所有有效促销，取最优折扣
                 *   2. 计算会员折扣（银卡98折/金卡95折/钻石90折）
                 *   3. 计算满减（满 threshold 减 discount_amount）
                 *   4. 返回 DiscountInfo 结构（含各项折扣金额和标志位） */
                DiscountInfo discount_info = {0};

                if (item_count > 0) {
                    /* cart_create(): 分配 Cart 结构体，初始化为空购物车 */
                    Cart *temp_cart = cart_create();
                    for (int i = 0; i < item_count; i++) {
                        /* cart_add_item(): 将商品加入购物车，
                         * 自动查 g_promotions 应用单品折扣，设置 discounted_price */
                        cart_add_item(temp_cart, items[i].product_id, items[i].quantity);
                    }
                    /* calculate_total_discount(): 计算三层折扣叠加结果 */
                    discount_info = calculate_total_discount(temp_cart, pay_member);
                    /* cart_destroy(): 释放购物车及所有 CartItem 节点 */
                    cart_destroy(temp_cart);
                }

                /* 会员/VIP 百分比折扣和满减固定金额 */
                float member_pct = discount_info.has_member_discount
                    ? discount_info.member_discount_rate : 0;
                float override_amount  = discount_info.has_override
                    ? discount_info.override_discount   : 0;

                /* 计算会员折扣后、实付金额（所有支付方式通用） */
                float final_pay = total * (1 - member_pct / 100.0f) - override_amount;
                if (final_pay < 0) final_pay = 0;

                print_info("商品总价: ¥%.2f", total);
                /* 折扣信息 */
                if (discount_info.has_item_discount) {
                    /* 单品促销折扣金额 = 总折扣金额 - 会员折扣金额 - 满减金额 */
                    float member_discount_amt = pay_member
                        ? total * (member_pct / 100.0f) : 0;
                    float item_disc = discount_info.total_discount_amount
                                      - member_discount_amt - override_amount;
                    if (item_disc > 0) {
                        print_info("单品促销折扣: -¥%.2f", item_disc);
                    }
                }
                if (pay_member && discount_info.has_member_discount) {
                    print_info("会员折扣 [%s]: %.0f%%  ( -¥%.2f )",
                               get_member_level_name(pay_member->level),
                               member_pct, total * (member_pct / 100.0f));
                }
                if (discount_info.has_override) {
                    print_info("满减优惠: -¥%.2f", override_amount);
                }
                print_info("应付金额: ¥%.2f", final_pay);

                char method[20];
                printf("\n支付方式选择:\n");
                printf("1. 现金\n");
                printf("2. 微信\n");
                printf("3. 支付宝\n");
                printf("4. 储值卡（需有储值卡余额）\n");
                int pay_choice = get_safe_int("请选择支付方式: ", 1, 4);

                switch (pay_choice) {
                    case 1: strcpy(method, "现金"); break;
                    case 2: strcpy(method, "微信"); break;
                    case 3: strcpy(method, "支付宝"); break;
                    case 4: strcpy(method, "储值卡"); break;
                    default: strcpy(method, "现金"); break;
                }

                float cash_received = final_pay;

                /* =========== 储值卡支付（储值卡只是支付工具，不叠加额外折扣） =========== */
                if (strcmp(method, "储值卡") == 0) {
                    if (!pay_member) {
                        print_error("储值卡支付需先绑定会员，请输入会员手机号");
                        char search_input[30];
                        get_safe_string("会员手机号或储值卡号: ", search_input, sizeof(search_input));
                        pay_member = find_member_by_phone(search_input);
                        if (!pay_member) {
                            VipCard *vc = g_vip_cards;
                            while (vc) {
                                if (strcmp(vc->card_no, search_input) == 0) break;
                                vc = vc->next;
                            }
                            if (vc && vc->member_id > 0)
                                pay_member = find_member_by_id(vc->member_id);
                        }
                        if (!pay_member) {
                            print_error("未找到该会员，无法使用储值卡");
                            break;
                        }
                        /* 使用前面已计算好的折扣信息（此处 pay_member 刚确定，
                         * 需要基于已有 cart 重新计算会员折扣部分） */
                        if (item_count > 0) {
                            Cart *vip_cart = cart_create();
                            for (int ci = 0; ci < item_count; ci++) {
                                cart_add_item(vip_cart, items[ci].product_id, items[ci].quantity);
                            }
                            discount_info = calculate_total_discount(vip_cart, pay_member);
                            cart_destroy(vip_cart);
                            member_pct = discount_info.has_member_discount
                                ? discount_info.member_discount_rate : 0;
                            override_amount = discount_info.has_override
                                ? discount_info.override_discount : 0;
                        }
                        final_pay = total * (1 - member_pct / 100.0f) - override_amount;
                        if (final_pay < 0) final_pay = 0;
                        cash_received = final_pay;
                    }

                    VipCard *vip_card = NULL;
                    {
                        VipCard *t = g_vip_cards;
                        while (t) {
                            if (t->member_id == pay_member->id && t->status == 1) {
                                vip_card = t; break;
                            }
                            t = t->next;
                        }
                    }
                    if (!vip_card) {
                        print_error("该会员没有储值卡，请先办理储值卡");
                        break;
                    }

                    print_info("会员: %s [%s]",
                               pay_member->name,
                               get_member_level_name(pay_member->level));
                    print_info("储值卡余额: ¥%.2f", vip_card->balance);
                    if (pay_member && discount_info.has_member_discount) {
                        print_info("会员折扣: %.0f%% ( -¥%.2f )",
                                   member_pct, total * (member_pct / 100.0f));
                    }
                    if (discount_info.has_override) {
                        print_info("满减优惠: -¥%.2f", override_amount);
                    }
                    print_info("应付金额: ¥%.2f", final_pay);

                    /* 余额检查 */
                    if (vip_card->balance < final_pay) {
                        print_error("储值卡余额不足！余额: ¥%.2f，需支付: ¥%.2f",
                                    vip_card->balance, final_pay);
                        break;
                    }

                    /* verify_vip_card_password(): 用输入密码 + 卡的盐值计算哈希，
                     * 与卡内存储的 password_hash 比对（SHA-256 验证） */
                    char pay_password[50];
                    get_password_input("请输入储值卡支付密码: ", pay_password, sizeof(pay_password));
                    if (verify_vip_card_password(vip_card->card_no, pay_password) != 0) {
                        print_error("支付密码错误，支付已取消");
                        break;
                    }

                    char confirm_msg[100];
                    snprintf(confirm_msg, sizeof(confirm_msg),
                             "确认使用储值卡支付 ¥%.2f？", final_pay);
                    if (confirm_action(confirm_msg)) {
                        /* consume_vip_card(): 从储值卡余额中扣款
                         *   内部: 检查卡状态 → 检查余额 → 扣减 → 记录交易 → 保存文件
                         *   返回: 0 成功, -1 失败 */
                        if (consume_vip_card(vip_card->card_no, final_pay, sale_id,
                                             g_current_user_id, "购物消费") == 0) {
                            /* complete_sale(): 完成整个销售交易
                             *   参数: 订单ID, 支付方式, 会员折扣%, 满减金额, 实收金额
                             *   内部: 计算金额 → 扣库存 → 保存记录 → 打印小票 → 发放积分
                             *        → 从挂单链表移除 → 从明细链表移除
                             *   返回: 0 成功, -1 失败 */
                            if (complete_sale(sale_id, method,
                                              member_pct, override_amount,
                                              final_pay) == 0) {
                                print_success("储值卡支付完成！");
                                print_success("消费金额: ¥%.2f，余额: ¥%.2f",
                                              final_pay, vip_card->balance);
                            } else {
                                /* 事务回滚：complete_sale 失败，将已扣的储值卡金额退还 */
                                print_error("销售完成失败，正在回滚储值卡扣款...");
                                /* refund_vip_card(): 增加储值卡余额 + 记录退款交易 */
                                refund_vip_card(vip_card->card_no, final_pay, sale_id,
                                                g_current_user_id, "支付失败退款");
                                print_warning("储值卡已退款 ¥%.2f", final_pay);
                            }
                        } else {
                            print_error("储值卡扣款失败");
                        }
                    }
                    break;
                }

                /* =========== 现金/微信/支付宝支付 =========== */
                if (strcmp(method, "现金") == 0) {
                    /* 现金支付需要输入实收金额（用于计算找零） */
                    cash_received = get_safe_float("实收金额: ¥");
                }

                if (confirm_action("确认完成支付？")) {
                    /* complete_sale(): 完成销售交易（扣库存+保存+打印+发积分+清理挂单） */
                    if (complete_sale(sale_id, method,
                                      member_pct, override_amount, cash_received) == 0) {
                        if (strcmp(method, "现金") == 0) {
                            /* 现金支付显示找零金额 */
                            float change = cash_received - final_pay;
                            print_success("实收: ¥%.2f | 找零: ¥%.2f",
                                          cash_received, change > 0 ? change : 0);
                        }
                        print_success("支付完成！");
                    } else {
                        print_error("支付失败");
                    }
                }
                /* 释放 get_sale_items 返回的动态数组 */
                free(items);
                break;
            }

            case 4: {
                int sale_id = get_safe_int("订单号: ", 1, 999999);
                
                if (confirm_action("确定要取消该挂单？")) {
                    if (cancel_sale(sale_id) == 0) {
                        print_success("挂单已取消");
                    } else {
                        print_error("取消失败");
                    }
                }
                break;
            }
            
            default:
                print_warning("无效选择");
        }
    }
}

// ==================== 采购管理 ====================

void show_purchase_menu(void) {
    while (1) {
        print_title_box("采购管理");
        printf("1. 创建采购订单\n");
        printf("2. 待审核订单\n");
        printf("3. 审批订单\n");
        printf("4. 库管收货\n");
        printf("5. 订单列表\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_safe_int("", 0, 5);
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                /* ===== 创建采购订单 ===== */
                Purchase pur;
                memset(&pur, 0, sizeof(pur));

                get_safe_string("供应商ID: ", pur.supplier_id, sizeof(pur.supplier_id));
                get_safe_string("供应商名称: ", pur.supplier_name, sizeof(pur.supplier_name));
                pur.creator_id = g_current_user_id;

                /* create_purchase(): 创建采购订单
                 *   内部: 分配自增 ID → 设置状态=PURCHASE_PENDING → 加入 g_purchases 链表
                 *   → 写入事务日志（transaction.log）
                 *   返回: 新订单 ID */
                int purchase_id = create_purchase(&pur);
                print_success("采购订单已创建，订单号: %d", purchase_id);

                char ans = get_yes_no("是否添采购明细");

                if (ans == 'Y') {
                    /* 循环添加采购明细（输入 q 结束） */
                    while (1) {
                        char barcode[30];
                        get_safe_string("商品条码 (输入q结束): ", barcode, sizeof(barcode));
                        if (strcmp(barcode, "q") == 0) break;

                        /* find_product_by_barcode(): 在 g_barcode_hash 中按条码查找商品（O(1)） */
                        Product *prod = find_product_by_barcode(barcode);
                        if (!prod) {
                            print_error("商品不存在");
                            continue;
                        }
                        
                        print_info("商品: %s", prod->name);
                        float qty = get_safe_float("采购数量: ");
                        float price = get_safe_float("采购单价: ");
                        
                        PurchaseItem item;
                        memset(&item, 0, sizeof(item));
                        strncpy(item.product_id, prod->id, MAX_ID_LEN - 1);
                        strncpy(item.product_name, prod->name, MAX_NAME_LEN - 1);
                        item.quantity = qty;
                        item.price = price;
                        
                        /* add_purchase_item(): 分配自增 ID → 加入 g_purchase_items 链表
                         * save_purchase_item(): 将明细追加写入 data/purchase_item.txt */
                        add_purchase_item(purchase_id, &item);
                        save_purchase_item(&item);
                        print_success("已添加: %s x %.0f", prod->name, qty);
                    }
                }
                break;
            }
            
            case 2: {
                int count = 0;
                Purchase **list = list_purchases(PURCHASE_PENDING, &count);
                
                if (count == 0) {
                    print_warning("暂无待审核订单");
                    break;
                }
                
                TableColumn cols[] = {
                    {"订单号", 10, ALIGN_RIGHT},
                    {"供应商", 16, ALIGN_LEFT},
                    {"创建时间", 18, ALIGN_LEFT},
                    {"金额", 12, ALIGN_RIGHT}
                };
                
                table_begin(cols, 4);
                table_draw_header();
                
                char buf[4][64];
                for (int i = 0; i < count; i++) {
                    char time_str[32];
                    format_time(list[i]->created_at, time_str);
                    snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                    snprintf(buf[1], sizeof(buf[1]), "%s", list[i]->supplier_name);
                    snprintf(buf[2], sizeof(buf[2]), "%s", time_str);
                    snprintf(buf[3], sizeof(buf[3]), "¥%.2f", list[i]->total_amount);
                    const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                    table_draw_row(row);
                }
                table_end();
                print_info("待审核订单: %d个", count);
                free(list);
                break;
            }
            
            case 3: {
                /* ===== 审批采购订单 ===== */
                int id = get_safe_int("订单号: ", 1, 999999);
                printf("1. 审批通过  2. 拒绝: ");
                int op = get_safe_int("", 1, 2);

                if (op == 1) {
                    if (confirm_action("确认审批通过该订单？")) {
                        /* approve_purchase(): 校验状态=PENDING → 设为 APPROVED
                         *   → 计算采购总金额 → 写入事务日志 → 保存到 purchase.txt */
                        if (approve_purchase(id, g_current_user_id) == 0) {
                            print_success("审批通过");
                        }
                    }
                } else if (op == 2) {
                    if (confirm_action("确认拒绝该订单？")) {
                        /* reject_purchase(): 设状态为 REJECTED → 记录日志 → 保存 */
                        if (reject_purchase(id, g_current_user_id) == 0) {
                            print_success("已拒绝");
                        }
                    }
                }
                break;
            }

            case 4: {
                /* ===== 库管收货入库 ===== */
                int id = get_safe_int("订单号: ", 1, 999999);

                if (confirm_action("确认收货入库？")) {
                    /* receive_purchase(): 收货入库的核心函数
                     *   内部流程:
                     *   1. 校验订单状态 = APPROVED
                     *   2. 遍历采购明细（get_purchase_items）
                     *   3. 对每个明细商品: prod->stock += quantity（增加库存）
                     *   4. 记录库存变动日志（record_stock_log → stock_log.txt）
                     *   5. 更新订单状态为 COMPLETED → 保存到 purchase.txt
                     *   返回: 0 成功, -1 失败 */
                    if (receive_purchase(id, g_current_user_id) == 0) {
                        print_success("收货完成，已入库");
                    } else {
                        print_error("收货失败");
                    }
                }
                break;
            }
            
            case 5: {
                printf("1.全部 2.待审核 3.已审核 4.已完成 5.已拒绝: ");
                int status_type = get_safe_int("", 1, 5);
                int status = status_type - 2;
                
                int count = 0;
                Purchase **list = list_purchases(status, &count);
                
                if (count == 0) {
                    print_warning("暂无订单记录");
                    break;
                }
                
                TableColumn cols[] = {
                    {"订单号", 10, ALIGN_RIGHT},
                    {"供应商", 16, ALIGN_LEFT},
                    {"状态", 10, ALIGN_CENTER},
                    {"金额", 12, ALIGN_RIGHT}
                };
                
                table_begin(cols, 4);
                table_draw_header();
                
                char buf[4][64];
                for (int i = 0; i < count; i++) {
                    snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                    snprintf(buf[1], sizeof(buf[1]), "%s", list[i]->supplier_name);
                    snprintf(buf[2], sizeof(buf[2]), "%s", get_purchase_status_str(list[i]->status));
                    snprintf(buf[3], sizeof(buf[3]), "¥%.2f", list[i]->total_amount);
                    const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                    table_draw_row(row);
                }
                table_end();
                print_info("共 %d 条订单", count);
                free(list);
                break;
            }
            
            default:
                print_warning("无效选择");
        }
    }
}

// ==================== 排班管理 ====================

void show_schedule_menu(void) {
    while (1) {
        print_title_box("排班管理");
        printf("1. 创建周排班\n");
        printf("2. 周排班表\n");
        printf("3. 班次统计\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_safe_int("", 0, 3);
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                int year = get_safe_int("年份: ", 2020, 2100);
                int week = get_safe_int("周数: ", 1, 53);
                int emp_id = get_safe_int("员工ID: ", 1, 99999);
                
                if (!find_employee_by_id(emp_id)) {
                    print_error("员工不存在");
                    break;
                }
                
                print_info("输入周一到周日的班次 (早/晚/休):");
                char shifts[7][4];
                const char *day_names[] = {"一", "二", "三", "四", "五", "六", "日"};
                
                for (int i = 0; i < 7; i++) {
                    char prompt[32];
                    snprintf(prompt, sizeof(prompt), "周%s: ", day_names[i]);
                    get_safe_string(prompt, shifts[i], sizeof(shifts[i]));
                }
                
                if (batch_create_schedule(emp_id, year, week, shifts) > 0) {
                    print_success("排班创建成功");
                } else {
                    print_error("排班创建失败");
                }
                break;
            }
            
            case 2: {
                int year = get_safe_int("年份: ", 2020, 2100);
                int week = get_safe_int("周数: ", 1, 53);
                print_schedule_table(year, week);
                break;
            }
            
            case 3: {
                int year = get_safe_int("年份: ", 2020, 2100);
                int week = get_safe_int("周数: ", 1, 53);
                count_shifts_by_type(year, week);
                break;
            }
            
            default:
                print_warning("无效选择");
        }
    }
}

// ==================== 报表管理 ====================

void show_report_menu(void) {
    while (1) {
        print_title_box("报表管理");
        printf("1. 销售报表\n");
        printf("2. 库存报表\n");
        printf("3. 采购报表\n");
        printf("4. 盈亏报告\n");
        printf("5. 导出报表\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_safe_int("", 0, 5);
        
        if (choice == 0) break;
        
        TimeRange range;
        
        switch (choice) {
            case 1: {
                /* ===== 销售报表 ===== */
                int days = get_safe_int("统计天数 (1-30): ", 1, 30);
                if (days <= 0) days = 7;
                /* get_date_range(): 计算时间范围 range = [now - days*24h, now] */
                get_date_range(&range, days);
                /* generate_sales_report(): 从 sales.txt 读取数据
                 *   → 按时间区间筛选已完成订单 → 统计订单数/销售额/优惠/支付方式分布
                 *   → 打印到控制台 */
                generate_sales_report(range.start, range.end, "控制台");
                wait_for_key();
                break;
            }

            case 2:
                /* generate_inventory_report(): 遍历 g_product_hash 中所有上架商品
                 *   → 统计商品总数/库存不足数/成本总额/零售总额 → 打印到控制台 */
                generate_inventory_report("控制台");
                wait_for_key();
                break;

            case 3: {
                /* ===== 采购报表 ===== */
                int days = get_safe_int("统计天数: ", 1, 365);
                if (days <= 0) days = 30;
                get_date_range(&range, days);
                /* generate_purchase_report(): 遍历 g_purchases 链表
                 *   → 按时间区间筛选 → 按状态(待审核/已审核/已完成)分类统计 */
                generate_purchase_report(range.start, range.end, "控制台");
                wait_for_key();
                break;
            }

            case 4: {
                /* ===== 盈亏报告 ===== */
                int days = get_safe_int("统计天数: ", 1, 365);
                if (days <= 0) days = 30;
                get_date_range(&range, days);
                /* generate_profit_loss_report(): 最复杂的报表
                 *   1. 从 sales.txt 统计销售收入和优惠
                 *   2. 从 stock_log.txt 提取"出库"记录计算销售成本(COGS)
                 *   3. 从 g_config.monthly_fixed_cost 摊销固定成本
                 *   4. 计算毛利 = 收入 - COGS, 净利 = 毛利 - 固定成本 */
                generate_profit_loss_report(range.start, range.end);
                wait_for_key();
                break;
            }

            case 5: {
                /* ===== 导出报表（CSV/HTML） ===== */
                show_export_menu();
                int export_choice = get_safe_int("请选择导出类型: ", 1, 5);
                /* handle_export(): 根据选择调用 export_sales_csv/export_sales_html
                 *   或 export_inventory_csv/export_inventory_html
                 *   导出到 output/ 目录 */
                handle_export(export_choice);
                break;
            }
            
            default:
                print_warning("无效选择");
        }
    }
}

// ==================== 系统设置 ====================

void show_system_menu(void) {
    print_title_box("系统设置");
    printf("1. 数据备份\n");
    printf("2. 系统信息\n");
    printf("3. 初始化默认管理员\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_safe_int("", 0, 3);
    
    switch (choice) {
        case 1: {
            /* 数据备份：将 data/ 下所有文件复制到 output/backup_时间戳/ */
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char backup_dir[256];
            snprintf(backup_dir, sizeof(backup_dir),
                     "%s/backup_%04d%02d%02d_%02d%02d%02d",
                     OUTPUT_DIR,
                     t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                     t->tm_hour, t->tm_min, t->tm_sec);

            /* 确保 output 目录存在 */
#ifdef _WIN32
            _mkdir(OUTPUT_DIR);
            if (_mkdir(backup_dir) != 0) {
#else
            mkdir(OUTPUT_DIR, 0755);
            if (mkdir(backup_dir, 0755) != 0) {
#endif
                print_error("无法创建备份目录: %s", backup_dir);
                break;
            }

            /* 备份的文件列表 */
            const char *data_files[] = {
                "employee.txt", "product.txt", "supplier.txt", "member.txt",
                "sales.txt", "sale_item.txt", "pending_sales.txt",
                "purchase.txt", "purchase_item.txt", "schedule.txt",
                "stock_log.txt", "transaction.log", "config.txt",
                "daily_settlement.txt", "combo.txt", "combo_item.txt",
                "batch.txt", "promotion.txt", "store.txt", "store_stock.txt",
                "transfer.txt", "transfer_item.txt",
                "supplier_finance.txt", "payable.txt", "payment_record.txt",
                "vipcard.txt", "vipcard_trans.txt",
                NULL
            };

            int copied = 0, failed = 0;
            for (int i = 0; data_files[i] != NULL; i++) {
                char src_path[512], dst_path[512];
                snprintf(src_path, sizeof(src_path), "%s/%s", DATA_DIR, data_files[i]);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", backup_dir, data_files[i]);

                FILE *src = fopen(src_path, "r");
                if (!src) {
                    /* 文件不存在不是错误（可能尚未创建） */
                    continue;
                }

                FILE *dst = fopen(dst_path, "w");
                if (!dst) {
                    fclose(src);
                    failed++;
                    continue;
                }

                /* 逐块复制文件内容 */
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                    fwrite(buf, 1, n, dst);
                }

                fclose(src);
                fclose(dst);
                copied++;
            }

            if (copied > 0) {
                print_success("备份完成！目录: %s", backup_dir);
                print_info("已备份 %d 个文件", copied);
            } else {
                print_warning("没有需要备份的数据文件");
            }
            if (failed > 0) {
                print_warning("%d 个文件备份失败", failed);
            }
            wait_for_key();
            break;
        }
        
        case 2:
            print_title_box_single("系统信息");
            print_info("超市管理系统 v1.0");
            print_info("基于 ANSI C 开发");
            print_info("数据存储: 纯文本文件(.txt)");
            print_info("编译环境: GCC/MinGW");
            print_info("UI模块: 自定义美化界面");
            wait_for_key();
            break;
        
        case 3: {
            int count = 0;
            Employee **list = list_employees(&count);
            free(list);
            
            if (count > 0) {
                print_warning("系统中已有员工，无需初始化");
                break;
            }
            
            Employee admin;
            memset(&admin, 0, sizeof(admin));
            strcpy(admin.name, "admin");
            strcpy(admin.role, "管理员");
            generate_salt(admin.salt);
            hash_password("admin123", admin.salt, admin.password_hash);
            
            add_employee(&admin);
            save_employees();
            
            print_title_box_single("默认管理员已创建");
            print_success("用户名: admin");
            print_success("密码: admin123");
            print_warning("请首次登录后立即修改密码！");
            wait_for_key();
            break;
        }
        
        default:
            break;
    }
}

// ==================== 小票打印 ====================

void show_printer_menu(void) {
    print_title_box("小票打印");

    print_info("当前打印机: %s", printer_get_device_path());
    if (printer_is_available()) {
        print_success("打印机状态: 可用");
    } else {
        print_warning("打印机状态: 不可用");
    }
    printf("\n");

    printf("1. 打印测试页\n");
    printf("2. 设置打印机端口\n");
    printf("3. 打印历史小票\n");
    printf("0. 返回\n");
    printf("\n请选择: ");

    int choice = get_safe_int("", 0, 3);

    switch (choice) {
        case 1: {
            print_info("正在打印测试页...");
            if (printer_test_page() == 0) {
                print_success("测试页已发送到打印机");
            } else {
                print_warning("打印失败(打印机可能不可用，测试页已保存到文件)");
            }
            break;
        }

        case 2: {
            print_info("请输入打印机端口:");
            print_hint("Windows: LPT1, COM1, LPT2, COM2");
            print_hint("Linux: /dev/usb/lp0, /dev/lp0");
            print_hint("输入 0 返回上级菜单");
            printf("\n");

            char device[256];
            get_safe_string("端口: ", device, sizeof(device));

            /* 检查是否输入返回 */
            if (strcmp(device, "0") == 0) {
                break;
            }

            /* 验证端口格式 */
            if (strlen(device) == 0) {
                print_warning("端口不能为空！");
                break;
            }

            /* Windows 端口格式验证 (LPT1-LPT9, COM1-COM9) */
            /* Linux 端口格式验证 (/dev/usb/lp0, /dev/lp0 等) */
            int valid = 0;
            if (toupper(device[0]) == 'L' && toupper(device[1]) == 'P' &&
                toupper(device[2]) == 'T' && device[3] >= '1' && device[3] <= '9' &&
                device[4] == '\0') {
                valid = 1;  /* LPT1-LPT9 */
            }
            else if (toupper(device[0]) == 'C' && toupper(device[1]) == 'O' &&
                     toupper(device[2]) == 'M' && device[3] >= '1' && device[3] <= '9' &&
                     device[4] == '\0') {
                valid = 1;  /* COM1-COM9 */
            }
            else if (strncmp(device, "/dev/", 5) == 0 && strlen(device) > 5) {
                valid = 1;  /* Linux /dev/ 设备 */
            }

            if (!valid) {
                print_warning("端口格式不正确！");
                print_hint("正确的格式: LPT1, COM1, /dev/usb/lp0");
                break;
            }

            printer_set_device(device);
            print_success("已设置打印机端口: %s", device);
            break;
        }

        case 3: {
            print_info("当天小票文件:");
            print_hint("小票以 receipt_YYYYMMDD_XXXXXX.txt 格式保存");
            break;
        }

        default:
            break;
    }
}

// ==================== 主函数 ====================

/**
 * 程序入口
 *
 * 启动流程：
 *   1. ui_init()            — 初始化终端 UI（ANSI 颜色、清屏等）
 *   2. init_system()         — 创建目录、初始化哈希表、加载基础数据
 *   3. 加载扩展数据           — 排班/挂单/采购/调拨/储值卡等
 *   4. 首次使用检测           — 无员工时自动创建默认管理员
 *   5. show_main_menu()      — 进入主菜单循环
 *   6. ui_cleanup()          — 退出时清理 UI 资源
 */
int main(void) {
    /* ui_init(): 初始化终端环境（设置 ANSI 颜色支持、清屏等） */
    ui_init();

    printf("\n");
    printf(BOLD CYAN);
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║      超市管理系统 v1.0               ║\n");
    printf("  ║      Supermarket Management System   ║\n");
    printf("  ╚══════════════════════════════════════╝\n");
    printf(RESET);

    /* init_system(): 核心初始化
     *   → 创建 data/、tmp/、output/ 目录
     *   → 初始化 5 个哈希表（员工/商品/条码/供应商/会员手机号）
     *   → 加载 config.txt 系统配置
     *   → 加载基础数据（员工/商品/供应商/会员/日结单/批次/促销） */
    if (init_system() != 0) {
        print_error("系统初始化失败");
        return 1;
    }

    /* 加载扩展数据（init_system 中未加载的模块） */
    init_sale_id_counter();       /* 扫描 sales.txt + pending_sales.txt，初始化销售订单 ID 计数器 */
    load_schedules();             /* 加载排班数据 → g_schedules 链表 */
    load_pending_sales();         /* 加载挂单 → g_pending_sales 链表 */
    load_purchases();             /* 加载采购订单 → g_purchases 链表 */
    load_purchase_items();        /* 加载采购明细 → g_purchase_items 链表 */
    load_transaction_logs();      /* 加载事务日志 → g_transaction_logs 链表 */
    load_stock_logs();            /* 加载库存变动日志 → g_stock_logs 链表 */
    load_combos();                /* 加载套装 → g_combos 链表 + g_combo_barcode_hash */
    load_stores();                /* 加载门店 → g_stores 链表（无文件时自动创建"总店"） */
    load_store_stocks();          /* 加载门店库存 → g_store_stocks 链表 */
    load_transfers();             /* 加载调拨单 → g_transfer_orders 链表（含明细） */
    load_supplier_finances();     /* 加载供应商财务 → g_supplier_finances 链表 */
    load_payables();              /* 加载应付账款 → g_payables 链表 */
    load_payment_records();       /* 加载付款记录 → g_payment_records 链表 */
    load_vip_cards();             /* 加载储值卡 → g_vip_cards 链表 */
    load_vip_card_transactions(); /* 加载储值卡交易记录 → g_vip_card_transactions 链表 */

    /* 首次使用检测：如果系统中没有任何员工，自动创建默认管理员 */
    int emp_count = 0;
    /* list_employees(): 遍历 g_employee_hash，返回所有在职员工的数组 */
    Employee **emp_list = list_employees(&emp_count);
    free(emp_list);  /* 只需要数量，立即释放数组 */

    if (emp_count == 0) {
        printf("\n");
        print_title_box_single("首次使用");
        print_info("系统中暂无员工，正在创建默认管理员...");

        Employee admin;
        memset(&admin, 0, sizeof(admin));
        strcpy(admin.name, "admin");
        strcpy(admin.role, "管理员");
        /* generate_salt(): 生成 32 字节随机盐值 */
        generate_salt(admin.salt);
        /* hash_password(): 计算 SHA-256("admin123" + salt)，存入 password_hash */
        hash_password("admin123", admin.salt, admin.password_hash);
        /* add_employee(): 分配自增 ID，插入 g_employee_hash */
        add_employee(&admin);
        /* save_employees(): 将哈希表中所有员工全量写入 data/employee.txt */
        save_employees();
        
        print_success("默认管理员已创建!");
        print_info("用户名: admin");
        print_info("密码: admin123");
        print_warning("请首次登录后立即修改密码!");
        wait_for_key();
    }
    
    /* 显示主菜单 */
    show_main_menu();
    
    /* 清理UI资源 */
    ui_cleanup();
    
    return 0;
}

// ==================== 套装管理菜单 ====================

void show_combo_menu(void) {
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足，需要管理员或店长权限");
        return;
    }

    while (1) {
    print_title_box("套装管理");

    printf("1. 创建套装\n");
    printf("2. 套装列表\n");
    printf("3. 添加套装商品\n");
    printf("4. 套装详情\n");
    printf("5. 下架套装\n");
    printf("0. 返回\n");
    printf("\n请选择: ");

    int choice = get_safe_int("", 0, 5);
    
    switch (choice) {
        case 1: {
            ProductCombo combo;
            memset(&combo, 0, sizeof(combo));
            
            get_safe_string("套装名称: ", combo.name, sizeof(combo.name));
            get_safe_string("套装条码: ", combo.barcode, sizeof(combo.barcode));
            combo.price = get_safe_float("套装售价: ¥");
            combo.cost = 0;
            
            int combo_id = create_combo(&combo);
            if (combo_id > 0) {
                print_success("套装创建成功！ID: %d", combo_id);
                print_hint("请使用选项3添加子商品");
            }
            break;
        }
        
        case 2: {
            int count = 0;
            ProductCombo **list = list_combos(&count);
            
            if (count == 0) {
                print_warning("暂无套装");
                break;
            }
            
            TableColumn cols[] = {
                {"ID", 8, ALIGN_RIGHT},
                {"名称", 20, ALIGN_LEFT},
                {"条码", 15, ALIGN_LEFT},
                {"售价", 12, ALIGN_RIGHT}
            };
            
            table_begin(cols, 4);
            table_draw_header();
            
            char buf[4][64];
            for (int i = 0; i < count; i++) {
                snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                snprintf(buf[1], sizeof(buf[1]), "%s", list[i]->name);
                snprintf(buf[2], sizeof(buf[2]), "%s", list[i]->barcode);
                snprintf(buf[3], sizeof(buf[3]), "¥%.2f", list[i]->price);
                const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                table_draw_row(row);
            }
            table_end();
            print_info("共 %d 个套装", count);
            free(list);
            break;
        }
        
        case 3: {
            int combo_id = get_safe_int("套装ID: ", 1, 99999);
            ProductCombo *combo = find_combo_by_id(combo_id);
            if (!combo) {
                print_error("套装不存在");
                break;
            }
            
            char barcode[30];
            get_safe_string("商品条码: ", barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (!prod) {
                print_error("商品不存在");
                break;
            }
            
            int qty = get_safe_int("数量: ", 1, 9999);
            
            ComboItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.product_id, prod->id, MAX_ID_LEN - 1);
            strncpy(item.product_name, prod->name, MAX_NAME_LEN - 1);
            item.quantity = qty;
            
            if (add_combo_item(combo_id, &item) == 0) {
                print_success("已添加: %s x%d", prod->name, qty);
            }
            break;
        }

        case 4: {
            int combo_id = get_safe_int("套装ID: ", 1, 99999);
            ProductCombo *combo = find_combo_by_id(combo_id);
            if (!combo) {
                print_error("未找到该套装");
                break;
            }

            /* 套装基本信息 */
            print_title_box_single("套装详情");

            TableColumn info_cols[] = {
                {"项目", 14, ALIGN_LEFT},
                {"内容", 26, ALIGN_LEFT}
            };

            table_begin(info_cols, 2);

            char info_buf[5][2][64];
            snprintf(info_buf[0][0], sizeof(info_buf[0][0]), "ID");
            snprintf(info_buf[0][1], sizeof(info_buf[0][1]), "%d", combo->id);
            snprintf(info_buf[1][0], sizeof(info_buf[1][0]), "名称");
            snprintf(info_buf[1][1], sizeof(info_buf[1][1]), "%s", combo->name);
            snprintf(info_buf[2][0], sizeof(info_buf[2][0]), "条码");
            snprintf(info_buf[2][1], sizeof(info_buf[2][1]), "%s", combo->barcode);
            snprintf(info_buf[3][0], sizeof(info_buf[3][0]), "售价");
            snprintf(info_buf[3][1], sizeof(info_buf[3][1]), "¥%.2f", combo->price);
            snprintf(info_buf[4][0], sizeof(info_buf[4][0]), "状态");
            snprintf(info_buf[4][1], sizeof(info_buf[4][1]), "%s",
                     combo->status == COMBO_ACTIVE ? "在售" : "已下架");

            table_draw_header();
            for (int i = 0; i < 5; i++) {
                const char *row[] = {info_buf[i][0], info_buf[i][1]};
                table_draw_row(row);
            }
            table_end();

            /* 套装子商品列表 */
            ComboItem *ci = combo->items;
            if (!ci) {
                print_warning("该套装暂无子商品");
            } else {
                print_info("=== 套装子商品 ===");

                TableColumn item_cols[] = {
                    {"商品ID", 12, ALIGN_LEFT},
                    {"商品名称", 20, ALIGN_LEFT},
                    {"数量", 8, ALIGN_RIGHT}
                };

                table_begin(item_cols, 3);
                table_draw_header();

                char item_buf[3][64];
                int item_count = 0;
                while (ci) {
                    snprintf(item_buf[0], sizeof(item_buf[0]), "%s", ci->product_id);
                    snprintf(item_buf[1], sizeof(item_buf[1]), "%s", ci->product_name);
                    snprintf(item_buf[2], sizeof(item_buf[2]), "%d", ci->quantity);
                    const char *row[] = {item_buf[0], item_buf[1], item_buf[2]};
                    table_draw_row(row);
                    item_count++;
                    ci = ci->next;
                }

                table_end();
                print_info("共 %d 种子商品", item_count);
            }

            wait_for_key();
            break;
        }

        case 5: {
            int combo_id = get_safe_int("套装ID: ", 1, 99999);
            ProductCombo *combo = find_combo_by_id(combo_id);
            if (!combo) {
                print_error("未找到该套装");
                break;
            }

            if (combo->status == COMBO_INACTIVE) {
                print_warning("该套装已处于下架状态");
                break;
            }

            char confirm_msg[128];
            snprintf(confirm_msg, sizeof(confirm_msg),
                     "确定要下架套装 [%s]（ID: %d）？", combo->name, combo->id);

            if (confirm_action(confirm_msg)) {
                if (delete_combo(combo_id) == 0) {
                    print_success("套装 [%s] 已下架", combo->name);
                } else {
                    print_error("下架失败");
                }
            }
            break;
        }

        case 0:
            return;
        }
    }  // end while
}

// ==================== 库存调拨菜单 ====================

/**
 * 显示门店列表
 */
void show_store_list(void) {
    int count = 0;
    Store **stores = list_stores(&count);

    if (count == 0) {
        print_warning("暂无门店，请先添加门店");
        return;
    }

    TableColumn cols[] = {
        {"ID",     8, ALIGN_RIGHT},
        {"名称",  16, ALIGN_LEFT},
        {"地址",  16, ALIGN_LEFT},
        {"电话",  14, ALIGN_LEFT},
        {"店长",  10, ALIGN_LEFT},
        {"状态",  10, ALIGN_CENTER}
    };

    table_begin(cols, 6);
    table_draw_header();

    char buf[6][64];
    for (int i = 0; i < count; i++) {
        snprintf(buf[0], sizeof(buf[0]), "%d", stores[i]->id);
        snprintf(buf[1], sizeof(buf[1]), "%s", stores[i]->name);
        snprintf(buf[2], sizeof(buf[2]), "%s", stores[i]->address);
        snprintf(buf[3], sizeof(buf[3]), "%s", stores[i]->phone);
        snprintf(buf[4], sizeof(buf[4]), "%s", stores[i]->manager_name);
        snprintf(buf[5], sizeof(buf[5]), "%s",
                 stores[i]->status == STORE_ACTIVE ? "营业中" : "已停业");
        const char *row[] = {buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]};
        table_draw_row(row);
    }
    table_end();
    print_info("共 %d 家门店", count);
    free(stores);
}

/**
 * 添加门店（带电话验证）
 */
void add_new_store(void) {
    Store store;
    memset(&store, 0, sizeof(store));

    get_safe_string("门店名称: ", store.name, sizeof(store.name));
    get_safe_string("门店地址: ", store.address, sizeof(store.address));

    // 电话号码验证：必须为数字，长度7-11位
    while (1) {
        get_safe_string("联系电话: ", store.phone, sizeof(store.phone));
        trim(store.phone);
        if (strlen(store.phone) == 0) {
            break;  // 电话可以为空
        }
        if (!is_valid_phone(store.phone)) {
            print_warning("电话格式不正确，请输入7-11位数字");
            continue;
        }
        break;
    }

    int id = create_store(&store);
    if (id > 0) {
        print_success("门店添加成功！ID: %d", id);
    } else {
        print_error("添加失败");
    }
}

/**
 * 编辑门店
 */
void edit_store_info(void) {
    int id = get_safe_int("输入门店ID: ", 1, 99999);
    Store *store = find_store_by_id(id);
    if (!store) {
        print_error("未找到该门店");
        return;
    }

    char input[256];

    print_info("当前名称: %s (直接回车跳过)", store->name);
    get_safe_string("新名称: ", input, sizeof(input));
    if (strlen(input) > 0) strncpy(store->name, input, 99);

    print_info("当前地址: %s (直接回车跳过)", store->address);
    get_safe_string("新地址: ", input, sizeof(input));
    if (strlen(input) > 0) strncpy(store->address, input, 255);

    print_info("当前电话: %s (直接回车跳过)", store->phone);
    while (1) {
        get_safe_string("新电话: ", input, sizeof(input));
        if (strlen(input) == 0) break;  // 跳过
        if (is_valid_phone(input)) {
            strncpy(store->phone, input, 19);
            break;
        }
        print_warning("电话格式不正确，请输入7-11位数字");
    }

    print_info("当前店长: %s (直接回车跳过)", store->manager_name);
    get_safe_string("新店长: ", input, sizeof(input));
    if (strlen(input) > 0) strncpy(store->manager_name, input, 49);

    if (update_store(store) == 0) {
        print_success("门店信息已更新");
    } else {
        print_error("更新失败");
    }
}

/**
 * 选择门店（返回门店ID）
 */
int select_store(const char *prompt) {
    int count = 0;
    Store **stores = list_stores(&count);

    if (count == 0) {
        print_warning("暂无门店，请先添加门店");
        return -1;
    }

    printf("\n%s\n", prompt);

    TableColumn cols[] = {
        {"ID",   8, ALIGN_RIGHT},
        {"名称", 20, ALIGN_LEFT},
        {"地址", 20, ALIGN_LEFT}
    };

    table_begin(cols, 3);
    table_draw_header();

    char buf[3][64];
    for (int i = 0; i < count; i++) {
        snprintf(buf[0], sizeof(buf[0]), "%d", stores[i]->id);
        snprintf(buf[1], sizeof(buf[1]), "%s", stores[i]->name);
        snprintf(buf[2], sizeof(buf[2]), "%s", stores[i]->address);
        const char *row[] = {buf[0], buf[1], buf[2]};
        table_draw_row(row);
    }
    table_end();

    int id = get_safe_int("请输入门店ID (0取消): ", 0, 99999);
    free(stores);

    if (id == 0) return -1;

    // 验证门店是否存在
    if (find_store_by_id(id)) {
        return id;
    }

    print_error("门店ID %d 不存在", id);
    return -1;
}

void show_transfer_menu(void) {
    if (!check_permission("管理员") && !check_permission("店长") && !check_permission("库管")) {
        print_error("权限不足");
        return;
    }

    while (1) {
    print_title_box("库存调拨");

    print_info("=== 门店管理 ===");
    printf("1. 添加门店\n");
    printf("2. 门店列表\n");
    printf("3. 编辑门店\n");
    printf("\n");
    print_info("=== 调拨管理 ===");
    printf("4. 创建调拨单\n");
    printf("5. 添加调拨商品\n");
    printf("6. 审批调拨单\n");
    printf("7. 出库确认\n");
    printf("8. 入库确认\n");
    printf("9. 调拨单列表\n");
    printf("0. 返回\n");
    printf("\n请选择: ");

    int choice = get_safe_int("", 0, 9);

    switch (choice) {
        case 1: {
            add_new_store();
            break;
        }

        case 2: {
            show_store_list();
            break;
        }

        case 3: {
            edit_store_info();
            break;
        }
        
        case 4: {
            int from_id = select_store("=== 选择源门店 ===");
            if (from_id < 0) break;

            int to_id = select_store("=== 选择目标门店 ===");
            if (to_id < 0) break;

            if (from_id == to_id) {
                print_error("源门店和目标门店不能相同");
                break;
            }

            char remark[256];
            get_safe_string("备注: ", remark, sizeof(remark));

            int id = create_transfer_order(from_id, to_id, g_current_user_id, remark);
            if (id > 0) {
                print_success("调拨单创建成功！ID: %d", id);
            }
            break;
        }

        case 5: {
            int transfer_id = get_safe_int("调拨单ID: ", 1, 99999);
            char barcode[30];
            get_safe_string("商品条码: ", barcode, sizeof(barcode));

            Product *prod = find_product_by_barcode(barcode);
            if (!prod) {
                print_error("商品不存在");
                break;
            }

            int qty = get_safe_int("数量: ", 1, 9999);

            if (add_transfer_item(transfer_id, prod->id, qty) == 0) {
                print_success("已添加商品到调拨单");
            }
            break;
        }

        case 6: {
            int id = get_safe_int("调拨单ID: ", 1, 99999);
            if (confirm_action("确认审批通过该调拨单？")) {
                if (approve_transfer(id, g_current_user_id) == 0) {
                    print_success("审批通过");
                }
            }
            break;
        }

        case 7: {
            int id = get_safe_int("调拨单ID: ", 1, 99999);
            if (confirm_action("确认出库？")) {
                if (confirm_out_transfer(id, g_current_user_id) == 0) {
                    print_success("出库确认完成");
                }
            }
            break;
        }

        case 8: {
            int id = get_safe_int("调拨单ID: ", 1, 99999);
            if (confirm_action("确认入库？")) {
                if (confirm_in_transfer(id, g_current_user_id) == 0) {
                    print_success("入库确认完成");
                }
            }
            break;
        }

        case 9: {
            int count = 0;
            TransferOrder **list = list_transfers_by_status(-1, &count);
            
            if (count == 0) {
                print_warning("暂无调拨单");
                break;
            }
            
            TableColumn cols[] = {
                {"ID", 8, ALIGN_RIGHT},
                {"源门店", 14, ALIGN_LEFT},
                {"目标门店", 14, ALIGN_LEFT},
                {"状态", 10, ALIGN_CENTER}
            };
            
            table_begin(cols, 4);
            table_draw_header();
            
            const char *status_str[] = {"待出库", "已出库", "已完成", "已拒绝", "已取消"};
            char buf[4][64];
            for (int i = 0; i < count; i++) {
                snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                snprintf(buf[1], sizeof(buf[1]), "%s", list[i]->from_store_name);
                snprintf(buf[2], sizeof(buf[2]), "%s", list[i]->to_store_name);
                int st = list[i]->status;
                if (st < 0 || st > 4) st = 0;
                snprintf(buf[3], sizeof(buf[3]), "%s", status_str[st]);
                const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                table_draw_row(row);
            }
            table_end();
            free(list);
            break;
        }

        case 0:
            return;
        }
    }  // end while
}

// ==================== 供应商结算菜单 ====================

void show_supplier_settlement_menu(void) {
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }

    while (1) {
        print_title_box("供应商结算");

        printf("1. 应付款汇总\n");
        printf("2. 按供应商查看\n");
        printf("3. 付款\n");
        printf("4. 设置供应商账期/评级\n");
        printf("0. 返回\n");
        printf("\n请选择: ");

        int choice = get_safe_int("", 0, 4);

        switch (choice) {
            case 1:
                print_payables_summary();
                wait_for_key();
                break;

            case 2: {
                int supplier_id = get_safe_int("供应商ID: ", 1, 99999);
                time_t now = time(NULL);
                time_t start = now - 90 * 24 * 3600;
                generate_supplier_statement(supplier_id, start, now);
                wait_for_key();
                break;
            }

            case 3: {
                int payable_id = get_safe_int("应付单ID: ", 1, 99999);
                float amount = get_safe_float("付款金额: ¥");
                char method[20];
                get_safe_string("付款方式: ", method, sizeof(method));
                char ref[50];
                get_safe_string("参考号: ", ref, sizeof(ref));

                if (confirm_action("确认付款？")) {
                    if (record_payment(payable_id, amount, method, ref, g_current_user_id, "") == 0) {
                        print_success("付款成功");
                    } else {
                        print_error("付款失败");
                    }
                }
                break;
            }

            case 4: {
                int supplier_id = get_safe_int("供应商ID: ", 1, 99999);
                float days = get_safe_float("账期(天): ");
                char rating[4];
                get_safe_string("评级(A/B/C/D): ", rating, sizeof(rating));

                if (confirm_action("确认更新供应商信息？")) {
                    update_supplier_finance(supplier_id, days, rating[0]);
                    print_success("更新成功");
                }
                break;
            }

            case 0:
                return;
        }
    }
}

// ==================== 促销管理菜单 ====================

void show_promotion_menu(void) {
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }

    while (1) {
    print_title_box("促销管理");

    printf("1. 创建单品折扣\n");
    printf("2. 创建满减促销\n");
    printf("3. 创建第N件优惠\n");
    printf("4. 创建买M赠N\n");
    printf("5. 创建会员专属价\n");
    printf("6. 促销列表\n");
    printf("0. 返回\n");
    printf("\n请选择: ");

    int choice = get_safe_int("", 0, 6);
    
    switch (choice) {
        case 1: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            get_safe_string("促销名称: ", promo.name, sizeof(promo.name));
            
            char barcode[30];
            get_safe_string("商品条码: ", barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            
            promo.discount_rate = get_safe_float("折扣率(如80表示8折): ") / 100.0f;
            
            char date_str[20];
            get_safe_string("开始时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.start_time = parse_date_yyyymmdd(date_str);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.end_time = parse_date_yyyymmdd(date_str);
            
            promo.type = PROMOTION_TYPE_DISCOUNT;
            promo.priority = 1;
            
            create_promotion(&promo);
            print_success("促销创建成功");
            break;
        }
        
        case 2: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            get_safe_string("促销名称: ", promo.name, sizeof(promo.name));
            promo.threshold = get_safe_float("满减门槛: ¥");
            promo.discount_amount = get_safe_float("减免金额: ¥");
            
            char date_str[20];
            get_safe_string("开始时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.start_time = parse_date_yyyymmdd(date_str);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.end_time = parse_date_yyyymmdd(date_str);
            
            promo.type = PROMOTION_TYPE_OVERRIDE;
            promo.priority = 10;
            
            create_promotion(&promo);
            print_success("满减促销创建成功");
            break;
        }
        
        case 3: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            get_safe_string("促销名称: ", promo.name, sizeof(promo.name));
            
            char barcode[30];
            get_safe_string("商品条码: ", barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            
            promo.nth_item = get_safe_int("第几件优惠(如2): ", 2, 10);
            promo.nth_discount_rate = get_safe_float("折扣率(如50表示半价): ") / 100.0f;
            
            char date_str[20];
            get_safe_string("开始时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.start_time = parse_date_yyyymmdd(date_str);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.end_time = parse_date_yyyymmdd(date_str);
            
            promo.type = PROMOTION_TYPE_NTH_DISCOUNT;
            promo.priority = 2;
            
            create_promotion(&promo);
            print_success("第N件优惠创建成功");
            break;
        }
        
        case 4: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            get_safe_string("促销名称: ", promo.name, sizeof(promo.name));
            
            char barcode[30];
            get_safe_string("商品条码: ", barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            
            promo.buy_quantity = get_safe_int("买M件: ", 1, 100);
            promo.free_quantity = get_safe_int("送N件: ", 1, 100);
            
            char date_str[20];
            get_safe_string("开始时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.start_time = parse_date_yyyymmdd(date_str);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.end_time = parse_date_yyyymmdd(date_str);
            
            promo.type = PROMOTION_TYPE_BUY_M_GET_N;
            promo.priority = 2;
            
            create_promotion(&promo);
            print_success("买M赠N促销创建成功");
            break;
        }
        
        case 5: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            get_safe_string("促销名称: ", promo.name, sizeof(promo.name));
            
            char barcode[30];
            get_safe_string("商品条码: ", barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            
            promo.member_price = get_safe_float("会员专属价: ¥");
            promo.member_level = get_safe_int("适用会员等级(0=所有,1=银卡,2=金卡,3=钻卡): ", 0, 3);
            
            char date_str[20];
            get_safe_string("开始时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.start_time = parse_date_yyyymmdd(date_str);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            promo.end_time = parse_date_yyyymmdd(date_str);
            
            promo.type = PROMOTION_TYPE_MEMBER_PRICE;
            promo.priority = 3;
            
            create_promotion(&promo);
            print_success("会员专属价促销创建成功");
            break;
        }
        
        case 6: {
            int count = 0;
            Promotion **list = list_promotions(&count);
            
            if (count == 0) {
                print_warning("暂无促销活动");
                break;
            }
            
            TableColumn cols[] = {
                {"ID", 6, ALIGN_RIGHT},
                {"名称", 18, ALIGN_LEFT},
                {"类型", 8, ALIGN_CENTER},
                {"开始时间", 16, ALIGN_LEFT},
                {"结束时间", 16, ALIGN_LEFT}
            };
            
            table_begin(cols, 5);
            table_draw_header();
            
            const char *type_str[] = {"单品折扣", "满减", "第N件", "买M赠N", "会员价"};
            char buf[5][64];
            
            for (int i = 0; i < count; i++) {
                char start_str[32], end_str[32];
                format_time(list[i]->start_time, start_str);
                format_time(list[i]->end_time, end_str);
                
                snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                snprintf(buf[1], sizeof(buf[1]), "%s", list[i]->name);
                int t = list[i]->type;
                if (t < 0 || t > 4) t = 0;
                snprintf(buf[2], sizeof(buf[2]), "%s", type_str[t]);
                snprintf(buf[3], sizeof(buf[3]), "%s", start_str);
                snprintf(buf[4], sizeof(buf[4]), "%s", end_str);
                
                const char *row[] = {buf[0], buf[1], buf[2], buf[3], buf[4]};
                table_draw_row(row);
            }
            table_end();
            free(list);
            break;
        }

        case 0:
            return;
        }
    }  // end while
}

// ==================== 储值卡菜单 ====================

void show_vipcard_menu(void) {
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }

    while (1) {
    print_title_box("储值卡管理");

    printf("1. 创建储值卡\n");
    printf("2. 储值卡充值\n");
    printf("3. 储值卡消费\n");
    printf("4. 储值卡列表\n");
    printf("5. 绑定会员\n");
    printf("6. 交易记录查询\n");
    printf("7. 余额汇总\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_safe_int("", 0, 7);
    
    switch (choice) {
        case 1: {
            char password[20];
            get_password_input("设置支付密码: ", password, sizeof(password));
            
            VipCard *card = create_vip_card(password);
            if (card) {
                print_title_box_single("储值卡创建成功");
                print_success("卡号: %s", card->card_no);
                print_warning("请妥善保管卡号和密码");
                wait_for_key();
            }
            break;
        }
        
        case 2: {
            char card_no[30];
            get_safe_string("储值卡号: ", card_no, sizeof(card_no));
            float amount = get_safe_float("充值金额: ¥");
            
            if (amount > 0 && confirm_action("确认充值？")) {
                if (recharge_vip_card(card_no, amount, g_current_user_id, "") == 0) {
                    print_success("充值成功！");
                } else {
                    print_error("充值失败");
                }
            }
            break;
        }
        
        case 3: {
            char card_no[30];
            get_safe_string("储值卡号: ", card_no, sizeof(card_no));
            float amount = get_safe_float("消费金额: ¥");

            if (amount <= 0) {
                print_error("消费金额必须大于0");
                break;
            }

            // 验证支付密码
            char pay_pwd[50];
            get_password_input("支付密码: ", pay_pwd, sizeof(pay_pwd));
            if (verify_vip_card_password(card_no, pay_pwd) != 0) {
                print_error("支付密码错误，消费已取消");
                break;
            }

            if (confirm_action("确认消费？")) {
                if (consume_vip_card(card_no, amount, 0, g_current_user_id, "") == 0) {
                    print_success("消费成功！");
                } else {
                    print_error("消费失败");
                }
            }
            break;
        }
        
        case 4: {
            int count = 0;
            VipCard **list = list_vip_cards(&count);
            
            if (count == 0) {
                print_warning("暂无储值卡");
                break;
            }
            
            TableColumn cols[] = {
                {"卡号", 20, ALIGN_LEFT},
                {"会员ID", 10, ALIGN_RIGHT},
                {"余额", 12, ALIGN_RIGHT},
                {"状态", 10, ALIGN_CENTER}
            };
            
            table_begin(cols, 4);
            table_draw_header();
            
            const char *status_str[] = {"已注销", "正常", "冻结", "过期"};
            char buf[4][64];
            
            for (int i = 0; i < count; i++) {
                snprintf(buf[0], sizeof(buf[0]), "%s", list[i]->card_no);
                snprintf(buf[1], sizeof(buf[1]), "%d", list[i]->member_id);
                snprintf(buf[2], sizeof(buf[2]), "¥%.2f", list[i]->balance);
                int st = list[i]->status;
                if (st < 0 || st > 3) st = 0;
                snprintf(buf[3], sizeof(buf[3]), "%s", status_str[st]);
                
                const char *row[] = {buf[0], buf[1], buf[2], buf[3]};
                table_draw_row(row);
            }
            table_end();
            print_info("共 %d 张储值卡", count);
            free(list);
            break;
        }
        
        case 5: {
            // 绑定会员功能
            char card_no[30];
            get_safe_string("储值卡号: ", card_no, sizeof(card_no));
            
            VipCard *card = find_vip_card(card_no);
            if (!card) {
                print_error("未找到该储值卡");
                break;
            }
            
            print_info("当前绑定会员ID: %s", card->member_id > 0 ? "已绑定" : "未绑定");
            if (card->member_id > 0) {
                print_info("会员ID: %d", card->member_id);
            }
            
            printf("\n1. 绑定新会员\n");
            printf("2. 解除绑定\n");
            printf("0. 返回\n");
            
            int opt = get_safe_int("选择操作: ", 0, 2);
            
            if (opt == 1) {
                int member_id = get_safe_int("会员ID: ", 1, 999999);
                Member *m = find_member_by_id(member_id);
                if (!m) {
                    print_error("未找到该会员");
                    break;
                }
                if (confirm_action("确认绑定该会员？")) {
                    if (bind_vip_card_member(card_no, member_id) == 0) {
                        print_success("绑定成功！储值卡已绑定到会员 %s", m->name);
                    } else {
                        print_error("绑定失败");
                    }
                }
            } else if (opt == 2) {
                if (card->member_id <= 0) {
                    print_warning("该卡未绑定会员");
                    break;
                }
                if (confirm_action("确认解除绑定？")) {
                    if (bind_vip_card_member(card_no, 0) == 0) {
                        print_success("已解除会员绑定");
                    } else {
                        print_error("解除绑定失败");
                    }
                }
            }
            break;
        }
        
        case 6: {
            char card_no[30];
            get_safe_string("储值卡号: ", card_no, sizeof(card_no));
            
            int count = 0;
            VipCardTransaction **list = query_vip_card_transactions(card_no, &count);
            
            if (count == 0) {
                print_warning("暂无交易记录");
                break;
            }
            
            TableColumn cols[] = {
                {"ID", 8, ALIGN_RIGHT},
                {"类型", 8, ALIGN_CENTER},
                {"金额", 10, ALIGN_RIGHT},
                {"前余额", 10, ALIGN_RIGHT},
                {"后余额", 10, ALIGN_RIGHT}
            };
            
            table_begin(cols, 5);
            table_draw_header();
            
            const char *type_str[] = {"充值", "消费", "退款"};
            char buf[5][64];
            
            for (int i = 0; i < count; i++) {
                snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                int t = list[i]->type;
                if (t < 0 || t > 2) t = 0;
                snprintf(buf[1], sizeof(buf[1]), "%s", type_str[t]);
                snprintf(buf[2], sizeof(buf[2]), "¥%.2f", list[i]->amount);
                snprintf(buf[3], sizeof(buf[3]), "¥%.2f", list[i]->balance_before);
                snprintf(buf[4], sizeof(buf[4]), "¥%.2f", list[i]->balance_after);
                
                const char *row[] = {buf[0], buf[1], buf[2], buf[3], buf[4]};
                table_draw_row(row);
            }
            table_end();
            free(list);
            break;
        }
        
        case 7:
            print_vip_card_summary();
            wait_for_key();
            break;

        case 0:
            return;
        }
    }  // end while
}

// ==================== 会员管理 ====================

void show_member_menu(void) {
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }

    while (1) {
    print_title_box("会员管理");

    printf("1. 新增会员\n");
    printf("2. 会员列表\n");
    printf("3. 查询会员\n");
    printf("4. 编辑会员\n");
    printf("5. 删除会员\n");
    printf("6. 积分兑换\n");
    printf("7. 等级调整\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_safe_int("", 0, 7);
    
    switch (choice) {
        case 1: {
            char phone[20], name[50];
            
            // 验证手机号输入
            while (1) {
                get_safe_string("手机号: ", phone, sizeof(phone));
                trim_whitespace(phone);
                if (strlen(phone) == 0) {
                    print_warning("手机号不能为空，请重新输入");
                    continue;
                }
                if (!is_valid_phone(phone)) {
                    print_warning("手机号格式不正确，请重新输入");
                    continue;
                }
                break;
            }
            
            // 验证姓名输入
            while (1) {
                get_safe_string("姓名: ", name, sizeof(name));
                trim_whitespace(name);
                if (strlen(name) == 0) {
                    print_warning("姓名不能为空，请重新输入");
                    continue;
                }
                break;
            }
            
            int id = add_member(phone, name);
            if (id > 0) {
                print_success("会员创建成功！ID: %d", id);
                save_members();
            } else {
                print_error("创建失败！手机号可能已存在");
            }
            break;
        }
        
        case 2: {
            int count = 0;
            Member **list = list_members(&count);
            
            if (count == 0) {
                print_warning("暂无会员记录");
                break;
            }
            
            TableColumn cols[] = {
                {"ID", 6, ALIGN_RIGHT},
                {"姓名", 14, ALIGN_LEFT},
                {"手机", 12, ALIGN_LEFT},
                {"等级", 10, ALIGN_CENTER},
                {"积分", 10, ALIGN_RIGHT}
            };
            
            table_begin(cols, 5);
            table_draw_header();
            
            char buf[5][64];
            for (int i = 0; i < count; i++) {
                snprintf(buf[0], sizeof(buf[0]), "%d", list[i]->id);
                snprintf(buf[1], sizeof(buf[1]), "%s", list[i]->name);
                snprintf(buf[2], sizeof(buf[2]), "%s", list[i]->phone);
                snprintf(buf[3], sizeof(buf[3]), "%s", get_member_level_name(list[i]->level));
                snprintf(buf[4], sizeof(buf[4]), "%d", list[i]->points);
                const char *row[] = {buf[0], buf[1], buf[2], buf[3], buf[4]};
                table_draw_row(row);
            }
            table_end();
            print_info("共 %d 位会员", count);
            free(list);
            wait_for_key();
            break;
        }
        
        case 3: {
            char input[30];
            get_safe_string("输入手机号或会员ID: ", input, sizeof(input));
            
            // 检查输入是否为空
            if (strlen(input) == 0) {
                print_error("输入不能为空");
                break;
            }
            
            Member *m = NULL;
            
            // 检查哈希表是否已初始化
            if (g_member_phone_hash != NULL) {
                m = find_member_by_phone(input);
            }
            
            if (!m) {
                m = find_member_by_id(atoi(input));
            }
            
            if (m) {
                print_title_box_single("会员信息");
                
                TableColumn cols[] = {
                    {"项目", 14, ALIGN_LEFT},
                    {"内容", 26, ALIGN_LEFT}
                };
                
                table_begin(cols, 2);
                
                char info[6][2][64];
                snprintf(info[0][0], sizeof(info[0][0]), "ID");
                snprintf(info[0][1], sizeof(info[0][1]), "%d", m->id);
                snprintf(info[1][0], sizeof(info[1][0]), "姓名");
                snprintf(info[1][1], sizeof(info[1][1]), "%s", m->name);
                snprintf(info[2][0], sizeof(info[2][0]), "手机");
                snprintf(info[2][1], sizeof(info[2][1]), "%s", m->phone);
                snprintf(info[3][0], sizeof(info[3][0]), "等级");
                snprintf(info[3][1], sizeof(info[3][1]), "%s", get_member_level_name(m->level));
                snprintf(info[4][0], sizeof(info[4][0]), "积分");
                snprintf(info[4][1], sizeof(info[4][1]), "%d", m->points);
                snprintf(info[5][0], sizeof(info[5][0]), "累计消费");
                snprintf(info[5][1], sizeof(info[5][1]), "¥%.2f", m->total_consume);
                
                table_draw_header();
                for (int i = 0; i < 6; i++) {
                    const char *row[] = {info[i][0], info[i][1]};
                    table_draw_row(row);
                }
                table_end();
            } else {
                print_error("未找到该会员");
            }
            break;
        }
        
        case 4: {
            int id = get_safe_int("输入要编辑的会员ID: ", 1, 999999);
            Member *m = find_member_by_id(id);
            
            if (!m) {
                print_error("未找到该会员");
                break;
            }
            
            char name[50];
            print_info("当前姓名: %s (直接回车跳过)", m->name);
            get_safe_string("新姓名: ", name, sizeof(name));
            if (strlen(name) > 0) {
                strncpy(m->name, name, sizeof(m->name) - 1);
            }
            
            char phone[20];
            print_info("当前手机: %s (直接回车跳过)", m->phone);
            get_safe_string("新手机: ", phone, sizeof(phone));
            if (strlen(phone) > 0) {
                /* 如果手机号变更，需要同步更新哈希表 */
                char old_phone[20];
                strncpy(old_phone, m->phone, sizeof(old_phone) - 1);
                old_phone[sizeof(old_phone) - 1] = '\0';

                if (strcmp(old_phone, phone) != 0) {
                    /* 检查新手机号是否已被其他会员占用 */
                    if (find_member_by_phone(phone)) {
                        print_error("手机号 %s 已被其他会员使用", phone);
                        break;
                    }
                    /* 删除旧 key，插入新 key */
                    hash_delete(g_member_phone_hash, old_phone);
                    strncpy(m->phone, phone, sizeof(m->phone) - 1);
                    hash_insert(g_member_phone_hash, m->phone, m);
                }
            }
            
            update_member(m);
            save_members();
            print_success("会员信息已更新");
            break;
        }
        
        case 5: {
            int id = get_safe_int("输入要删除的会员ID: ", 1, 999999);
            
            if (confirm_action("确定要删除该会员？")) {
                if (delete_member(id) == 0) {
                    /* delete_member 内部已调用 save_members()，无需重复保存 */
                    print_success("会员已删除");
                } else {
                    print_error("删除失败，未找到该会员");
                }
            }
            break;
        }
        
        case 6: {
            int id = get_safe_int("输入会员ID: ", 1, 999999);
            Member *m = find_member_by_id(id);
            
            if (!m) {
                print_error("未找到该会员");
                break;
            }
            
            print_info("当前积分: %d", m->points);
            int points = get_safe_int("输入要兑换的积分: ", 0, m->points);
            
            if (points > m->points) {
                print_error("积分不足！");
                break;
            }
            
            float redeemed = redeem_points(m, points);
            save_members();
            print_success("已兑换 %.2f 元", redeemed);
            break;
        }
        
        case 7: {
            int id = get_safe_int("输入会员ID: ", 1, 999999);
            Member *m = find_member_by_id(id);
            
            if (!m) {
                print_error("未找到该会员");
                break;
            }
            
            print_info("当前等级: %s", get_member_level_name(m->level));
            int level = get_safe_int("选择新等级 (0=普通, 1=银卡, 2=金卡, 3=钻卡): ", 0, 3);
            
            if (confirm_action("确认调整会员等级？")) {
                m->level = level;
                update_member(m);
                save_members();
                print_success("等级已更新为: %s", get_member_level_name(level));
            }
            break;
        }

        case 0:
            return;
        }
    }  // end while
}
