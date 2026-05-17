/**
 * @file main.c
 * @brief 超市管理系统主程序 - 业务流程控制
 * 
 * 所有用户界面逻辑已移至 ui.c/ui.h
 */

#include "supermarket.h"
#include "ui.h"
#include <conio.h>
#include <limits.h>

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
 */
int login(const char *username, const char *password) {
    // 简化版：根据用户名查找员工
    int count = 0;
    Employee **employees = list_employees(&count);
    
    for (int i = 0; i < count; i++) {
        if (strcmp(employees[i]->name, username) == 0) {
            // 验证密码（简化版）
            char *hash = hash_password(password, employees[i]->salt);
            if (strcmp(hash, employees[i]->password_hash) == 0) {
                g_current_user_id = employees[i]->id;
                strncpy(g_current_user_role, employees[i]->role, 19);
                free(employees);
                return 0;
            }
        }
    }
    
    free(employees);
    return -1;
}

/**
 * 用户登出
 */
void logout(void) {
    if (g_current_user_id > 0) {
        Employee *emp = find_employee_by_id(g_current_user_id);
        if (emp) {
            char timestamp[32];
            get_timestamp(timestamp);
            printf("[%s] 用户 %s [%s] 登出\n", timestamp, emp->name, emp->role);
            
            // 记录登出日志
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), "用户 %s 登出系统", emp->name);
            write_transaction_log("AUTH", g_current_user_id, "LOGOUT", log_msg, g_current_user_id);
        }
        
        // 清理当前用户的挂单（防止切换账号后看到上一个人订单）
        Sale **prev = &g_pending_sales;
        while (*prev) {
            if ((*prev)->cashier_id == g_current_user_id) {
                Sale *to_free = *prev;
                *prev = (*prev)->next;
                free(to_free);
            } else {
                prev = &(*prev)->next;
            }
        }
    }
    
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
        print_success("登录成功！欢迎 %s [%s]", emp->name, emp->role);
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
                strcpy(emp.password_hash, hash_password(password, emp.salt));
                
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
                
                get_safe_string("商品ID: ", prod.id, sizeof(prod.id));
                get_safe_string("商品名称: ", prod.name, sizeof(prod.name));
                get_safe_string("条码: ", prod.barcode, sizeof(prod.barcode));
                prod.price = get_safe_float("售价: ");
                prod.cost = get_safe_float("进价: ");
                prod.stock = get_safe_int("库存: ", 0, 999999);
                prod.min_stock = get_safe_int("最低库存预警: ", 0, 999999);
                get_safe_string("供应商ID: ", prod.supplier_id, sizeof(prod.supplier_id));
                
                if (add_product(&prod) == 0) {
                    save_products();
                    print_success("商品添加成功！");
                } else {
                    print_error("商品添加失败，ID可能已存在");
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
                    int n = get_safe_int("新库存: ", 0, 999999);
                    if (n >= 0) p->stock = n;
                    
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
                char barcode[30];
                get_safe_string("请输入条码: ", barcode, sizeof(barcode));
                int qty = get_safe_int("数量: ", 1, 9999);
                
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
                int sale_id = get_safe_int("订单号: ", 1, 999999);
                
                Sale *sale_check = find_pending_sale(sale_id);
                if (!sale_check) {
                    print_error("未找到该挂单");
                    break;
                }

                float total = calculate_sale_total(sale_id);
                
                print_title_box_single("订单信息");
                print_info("订单号: %d", sale_check->id);
                print_info("收银员: %d", sale_check->cashier_id);
                print_info("商品总价: ¥%.2f", total);
                
                /* 显示商品明细 */
                int item_count = 0;
                SaleItem *items = get_sale_items(sale_id, &item_count);
                
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
                
                /* 计算折扣 */
                DiscountInfo discount_info = {0};
                
                if (item_count > 0) {
                    Cart *temp_cart = cart_create();
                    
                    for (int i = 0; i < item_count; i++) {
                        cart_add_item(temp_cart, items[i].product_id, items[i].quantity);
                    }
                    
                    Member *member = sale_check->member_id > 0 ? find_member_by_id(sale_check->member_id) : NULL;

                    discount_info = calculate_total_discount(temp_cart, member,
                        items[0].product_id, (int)items[0].quantity);

                    cart_destroy(temp_cart);
                }
                
                print_info("应付金额: ¥%.2f", total);
                
                /* 折扣信息 */
                print_title_box_single("折扣详情");
                if (discount_info.has_item_discount) {
                    print_info("单品促销折扣: %.0f%%", discount_info.item_discount_rate);
                }
                if (discount_info.has_member_discount) {
                    print_info("会员折扣: %.0f%%", discount_info.member_discount_rate);
                }
                if (discount_info.has_override) {
                    print_info("满减优惠: ¥%.2f", discount_info.override_discount);
                }
                print_info("综合折扣率: %.1f%%", discount_info.total_discount_rate);
                print_info("预计优惠: ¥%.2f", discount_info.total_discount_amount);
                
                char method[20];
                printf("\n支付方式选择:\n");
                printf("1. 现金\n");
                printf("2. 微信\n");
                printf("3. 支付宝\n");
                printf("4. 储值卡（需绑定会员且有储值卡）\n");
                int pay_choice = get_safe_int("请选择支付方式: ", 1, 4);
                
                switch (pay_choice) {
                    case 1: strcpy(method, "现金"); break;
                    case 2: strcpy(method, "微信"); break;
                    case 3: strcpy(method, "支付宝"); break;
                    case 4: strcpy(method, "储值卡"); break;
                    default: strcpy(method, "现金"); break;
                }
                
                float discount_input = get_safe_float("额外折扣(%%，0表示无额外折扣): ");
                float discount = (discount_input > 0) ? discount_input : discount_info.total_discount_rate;
                
                float cash_received = total;
                
                // 储值卡支付处理
                if (strcmp(method, "储值卡") == 0) {
                    // 检查是否有会员
                    Member *pay_member = sale_check->member_id > 0 ? find_member_by_id(sale_check->member_id) : NULL;
                    
                    if (!pay_member) {
                        // 没有绑定会员，尝试通过手机号或卡号查找
                        char search_input[30];
                        get_safe_string("请输入会员手机号或储值卡号: ", search_input, sizeof(search_input));
                        
                        pay_member = find_member_by_phone(search_input);
                        
                        if (!pay_member) {
                            // 尝试作为储值卡号查找
                            VipCard *vip_card = NULL;
                            VipCard *temp = g_vip_cards;
                            while (temp) {
                                if (strcmp(temp->card_no, search_input) == 0) {
                                    vip_card = temp;
                                    break;
                                }
                                temp = temp->next;
                            }
                            
                            if (vip_card && vip_card->member_id > 0) {
                                pay_member = find_member_by_id(vip_card->member_id);
                            }
                        }
                    }
                    
                    if (!pay_member) {
                        print_error("未找到该会员，请确认手机号或卡号正确");
                        break;
                    }
                    
                    // 检查会员是否有储值卡
                    VipCard *vip_card = NULL;
                    VipCard *temp = g_vip_cards;
                    while (temp) {
                        if (temp->member_id == pay_member->id && temp->status == 1) {
                            vip_card = temp;
                            break;
                        }
                        temp = temp->next;
                    }
                    
                    if (!vip_card) {
                        print_error("该会员没有储值卡，请先办理储值卡");
                        break;
                    }
                    
                    // 检查储值卡余额
                    float vip_discount_pct = get_vip_card_discount_percent(pay_member);
                    float vip_price = total * (1 - vip_discount_pct / 100);
                    
                    print_info("会员: %s (%s)", pay_member->name, get_member_level_name(pay_member->level));
                    print_info("储值卡余额: ¥%.2f", vip_card->balance);
                    print_info("储值卡折扣: %.0f%%", vip_discount_pct);
                    print_info("折后金额: ¥%.2f", vip_price);
                    
                    if (vip_card->balance < vip_price) {
                        print_error("储值卡余额不足！余额: ¥%.2f", vip_card->balance);
                        break;
                    }
                    
                    // 储值卡支付不需要找零
                    cash_received = vip_price;
                    
                    char confirm_msg[100];
                    snprintf(confirm_msg, sizeof(confirm_msg), "确认使用储值卡支付 ¥%.2f？", vip_price);
                    if (confirm_action(confirm_msg)) {
                        // 扣除储值卡余额
                        if (consume_vip_card(vip_card->card_no, vip_price, sale_id, g_current_user_id, "购物消费") == 0) {
                            // 储值卡支付完成后，更新折扣为包含储值卡折扣
                            discount = discount_info.total_discount_rate + vip_discount_pct;
                            if (complete_sale(sale_id, method, discount, cash_received) == 0) {
                                print_success("储值卡支付完成！");
                                print_success("消费金额: ¥%.2f，余额: ¥%.2f", vip_price, vip_card->balance);
                            } else {
                                print_error("支付失败");
                            }
                        } else {
                            print_error("储值卡扣款失败");
                        }
                        break;
                    } else {
                        break;
                    }
                }
                
                if (strcmp(method, "现金") == 0) {
                    cash_received = get_safe_float("实收金额: ¥");
                }
                
                if (confirm_action("确认完成支付？")) {
                    if (complete_sale(sale_id, method, discount, cash_received) == 0) {
                        if (strcmp(method, "现金") == 0) {
                            float final_amount = total * (1 - discount/100);
                            float change = cash_received - final_amount;
                            print_success("实收: ¥%.2f | 找零: ¥%.2f", 
                                         cash_received, change > 0 ? change : 0);
                        }
                        print_success("支付完成！");
                    } else {
                        print_error("支付失败");
                    }
                }
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
                Purchase pur;
                memset(&pur, 0, sizeof(pur));
                
                get_safe_string("供应商ID: ", pur.supplier_id, sizeof(pur.supplier_id));
                get_safe_string("供应商名称: ", pur.supplier_name, sizeof(pur.supplier_name));
                pur.creator_id = g_current_user_id;
                
                int purchase_id = create_purchase(&pur);
                print_success("采购订单已创建，订单号: %d", purchase_id);
                
                char ans = get_yes_no("是否添采购明细");
                
                if (ans == 'Y') {
                    while (1) {
                        char barcode[30];
                        get_safe_string("商品条码 (输入q结束): ", barcode, sizeof(barcode));
                        
                        if (strcmp(barcode, "q") == 0) break;
                        
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
                int id = get_safe_int("订单号: ", 1, 999999);
                printf("1. 审批通过  2. 拒绝: ");
                int op = get_safe_int("", 1, 2);
                
                if (op == 1) {
                    if (confirm_action("确认审批通过该订单？")) {
                        if (approve_purchase(id, g_current_user_id) == 0) {
                            print_success("审批通过");
                        }
                    }
                } else if (op == 2) {
                    if (confirm_action("确认拒绝该订单？")) {
                        if (reject_purchase(id, g_current_user_id) == 0) {
                            print_success("已拒绝");
                        }
                    }
                }
                break;
            }
            
            case 4: {
                int id = get_safe_int("订单号: ", 1, 999999);
                
                if (confirm_action("确认收货入库？")) {
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
                int days = get_safe_int("统计天数 (1-30): ", 1, 30);
                if (days <= 0) days = 7;
                get_date_range(&range, days);
                generate_sales_report(range.start, range.end, "控制台");
                wait_for_key();
                break;
            }
            
            case 2:
                generate_inventory_report("控制台");
                wait_for_key();
                break;
            
            case 3: {
                int days = get_safe_int("统计天数: ", 1, 365);
                if (days <= 0) days = 30;
                get_date_range(&range, days);
                generate_purchase_report(range.start, range.end, "控制台");
                wait_for_key();
                break;
            }
            
            case 4: {
                int days = get_safe_int("统计天数: ", 1, 365);
                if (days <= 0) days = 30;
                get_date_range(&range, days);
                generate_profit_loss_report(range.start, range.end);
                wait_for_key();
                break;
            }
            
            case 5:
                show_export_menu();
                int export_choice = get_safe_int("请选择导出类型: ", 1, 5);
                handle_export(export_choice);
                break;
            
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
        case 1:
            print_warning("数据备份功能（待实现）");
            break;
        
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
            strcpy(admin.password_hash, hash_password("admin123", admin.salt));
            
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
            print_hint("Windows: LPT1, COM1");
            print_hint("Linux: /dev/usb/lp0, /dev/lp0");
            
            char device[256];
            get_safe_string("端口: ", device, sizeof(device));
            if (strlen(device) > 0) {
                printer_set_device(device);
                print_success("已设置打印机端口: %s", device);
            }
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

int main(void) {
    /* 初始化UI模块 */
    ui_init();
    
    printf("\n");
    printf(BOLD CYAN);
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║      超市管理系统 v1.0               ║\n");
    printf("  ║      Supermarket Management System   ║\n");
    printf("  ╚══════════════════════════════════════╝\n");
    printf(RESET);
    
    /* 初始化系统 */
    if (init_system() != 0) {
        print_error("系统初始化失败");
        return 1;
    }
    
    /* 加载所有数据 */
    init_sale_id_counter();
    load_schedules();
    load_pending_sales();
    load_purchases();
    load_purchase_items();
    load_transaction_logs();
    load_stock_logs();
    load_combos();
    load_stores();
    load_store_stocks();
    load_transfers();
    load_supplier_finances();
    load_payables();
    load_payment_records();
    load_vip_cards();
    load_vip_card_transactions();
    
    /* 检查是否需要初始化管理员 */
    int emp_count = 0;
    list_employees(&emp_count);
    if (emp_count == 0) {
        printf("\n");
        print_title_box_single("首次使用");
        print_info("系统中暂无员工，正在创建默认管理员...");
        
        Employee admin;
        memset(&admin, 0, sizeof(admin));
        strcpy(admin.name, "admin");
        strcpy(admin.role, "管理员");
        generate_salt(admin.salt);
        strcpy(admin.password_hash, hash_password("admin123", admin.salt));
        add_employee(&admin);
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
    print_title_box("套装管理");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足，需要管理员或店长权限");
        return;
    }
    
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
        
        case 0:
            break;
    }
}

// ==================== 库存调拨菜单 ====================

/**
 * 显示门店列表
 */
void show_store_list(void) {
    int count = 0;
    Store **stores = list_stores(&count);
    
    if (count == 0) {
        printf("暂无门店，请先添加门店\n");
        return;
    }
    
    printf("\n%-6s %-20s %-15s %-10s\n", "ID", "名称", "地址", "状态");
    printf("----------------------------------------------------\n");
    
    for (int i = 0; i < count; i++) {
        printf("%-6d %-20s %-15s %-10s\n",
               stores[i]->id, stores[i]->name, stores[i]->address,
               stores[i]->status == STORE_ACTIVE ? "营业中" : "已停业");
    }
    free(stores);
}

/**
 * 添加门店
 */
void add_new_store(void) {
    Store store;
    memset(&store, 0, sizeof(store));
    
    printf("门店名称: ");
    get_string_input(store.name, sizeof(store.name));
    printf("门店地址: ");
    get_string_input(store.address, sizeof(store.address));
    printf("联系电话: ");
    get_string_input(store.phone, sizeof(store.phone));
    
    int id = create_store(&store);
    if (id > 0) {
        printf("门店添加成功！ID: %d\n", id);
    } else {
        printf("添加失败\n");
    }
}

/**
 * 选择门店（返回门店ID）
 */
int select_store(const char *prompt) {
    int count = 0;
    Store **stores = list_stores(&count);
    
    if (count == 0) {
        printf("暂无门店，请先添加门店\n");
        return -1;
    }
    
    printf("\n%s\n", prompt);
    printf("\n%-6s %-20s %-15s\n", "序号", "ID", "名称");
    printf("----------------------------\n");
    for (int i = 0; i < count; i++) {
        printf("%-6d %-6d %-20s\n", i + 1, stores[i]->id, stores[i]->name);
    }
    printf("----------------------------\n");
    printf("请输入序号或ID (0取消): ");
    
    char input[32];
    if (fgets(input, sizeof(input), stdin) == NULL) {
        free(stores);
        return -1;
    }
    input[strcspn(input, "\n")] = '\0';
    
    int sel = atoi(input);
    free(stores);
    
    if (sel == 0) return -1;
    
    // 检查是序号还是ID
    if (sel > 0 && sel <= count) {
        return stores[sel - 1]->id;
    }
    
    // 按ID查找
    return sel;
}

void show_transfer_menu(void) {
    print_title_box("库存调拨");
    
    if (!check_permission("管理员") && !check_permission("店长") && !check_permission("库管")) {
        print_error("权限不足");
        return;
    }
    
    print_info("=== 门店管理 ===");
    printf("1. 添加门店\n");
    printf("2. 门店列表\n");
    printf("\n");
    print_info("=== 调拨管理 ===");
    printf("3. 创建调拨单\n");
    printf("4. 添加调拨商品\n");
    printf("5. 审批调拨单\n");
    printf("6. 出库确认\n");
    printf("7. 入库确认\n");
    printf("8. 调拨单列表\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_safe_int("", 0, 8);
    
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
        
        case 4: {
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
        
        case 5: {
            int id = get_safe_int("调拨单ID: ", 1, 99999);
            if (confirm_action("确认审批通过该调拨单？")) {
                if (approve_transfer(id, g_current_user_id) == 0) {
                    print_success("审批通过");
                }
            }
            break;
        }
        
        case 6: {
            int id = get_safe_int("调拨单ID: ", 1, 99999);
            if (confirm_action("确认出库？")) {
                if (confirm_out_transfer(id, g_current_user_id) == 0) {
                    print_success("出库确认完成");
                }
            }
            break;
        }
        
        case 7: {
            int id = get_safe_int("调拨单ID: ", 1, 99999);
            if (confirm_action("确认入库？")) {
                if (confirm_in_transfer(id, g_current_user_id) == 0) {
                    print_success("入库确认完成");
                }
            }
            break;
        }
        
        case 8: {
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
            break;
    }
}

// ==================== 供应商结算菜单 ====================

void show_supplier_settlement_menu(void) {
    print_title_box("供应商结算");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }
    
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
            break;
    }
}

// ==================== 促销管理菜单 ====================

void show_promotion_menu(void) {
    print_title_box("促销管理");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }
    
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
            sscanf(date_str, "%lld", (long long*)&promo.start_time);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            sscanf(date_str, "%lld", (long long*)&promo.end_time);
            
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
            sscanf(date_str, "%lld", (long long*)&promo.start_time);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            sscanf(date_str, "%lld", (long long*)&promo.end_time);
            
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
            sscanf(date_str, "%lld", (long long*)&promo.start_time);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            sscanf(date_str, "%lld", (long long*)&promo.end_time);
            
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
            sscanf(date_str, "%lld", (long long*)&promo.start_time);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            sscanf(date_str, "%lld", (long long*)&promo.end_time);
            
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
            sscanf(date_str, "%lld", (long long*)&promo.start_time);
            get_safe_string("结束时间(YYYYMMDD): ", date_str, sizeof(date_str));
            sscanf(date_str, "%lld", (long long*)&promo.end_time);
            
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
            break;
    }
}

// ==================== 储值卡菜单 ====================

void show_vipcard_menu(void) {
    print_title_box("储值卡管理");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }
    
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
            
            if (amount > 0 && confirm_action("确认消费？")) {
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
            break;
    }
}

// ==================== 会员管理 ====================

void show_member_menu(void) {
    print_title_box("会员管理");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        print_error("权限不足");
        return;
    }
    
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
                strncpy(m->phone, phone, sizeof(m->phone) - 1);
            }
            
            update_member(m);
            save_members();
            print_success("会员信息已更新");
            break;
        }
        
        case 5: {
            int id = get_safe_int("输入要删除的会员ID: ", 1, 999999);
            
            if (confirm_action("确定要删除该会员？")) {
                if (delete_member(id)) {
                    save_members();
                    print_success("会员已删除");
                } else {
                    print_error("删除失败");
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
            break;
    }
}

