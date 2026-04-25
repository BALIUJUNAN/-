/**
 * @file main.c
 * @brief 超市管理系统主程序 - 控制台交互界面
 */

#include "supermarket.h"
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

int login(const char *username, const char *password);
void logout(void);

// ==================== 工具函数 ====================

/**
 * 打印分隔线
 */
void print_line(void) {
    printf("================================\n");
}

/**
 * 打印标题
 */
void print_title(const char *title) {
    printf("\n========== %s ==========\n", title);
}

/**
 * 等待按键
 */
void wait_key(void) {
    printf("\n按任意键继续...");
    getch();
}

/**
 * 获取整数输入
 */
int get_int_input(void) {
    int value;
    char buffer[64];
    
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return 0;
    }
    
    // 去除换行符
    buffer[strcspn(buffer, "\n")] = '\0';
    
    // 检查是否为空或只有空白
    if (strlen(buffer) == 0) {
        return 0;
    }
    
    // 尝试解析整数
    if (sscanf(buffer, "%d", &value) != 1) {
        // 如果不是有效整数，返回 -1 表示错误
        return -1;
    }
    
    return value;
}

/**
 * 获取浮点数输入
 */
float get_float_input(void) {
    float value;
    scanf("%f", &value);
    while (getchar() != '\n');
    return value;
}

/**
 * 获取字符串输入
 */
void get_string_input(char *buffer, int size) {
    fgets(buffer, size, stdin);
    trim(buffer);
}

// ==================== 登录/权限 ====================

/**
 * 用户登录
 */
int login(const char *username, const char *password) {
    // 简化版：根据用户名查找员工
    Employee **employees = list_employees(&g_auto_id_counter);
    int count = 0;
    free(employees);
    employees = list_employees(&count);
    
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
        printf("\n");
        print_title("超市管理系统");
        
        if (g_current_user_id > 0) {
            Employee *emp = find_employee_by_id(g_current_user_id);
            printf("当前用户: %s [%s]\n", 
                   emp ? emp->name : "未知", g_current_user_role);
        } else {
            printf("未登录\n");
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
        printf("F. 数据看板\n");
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
                case 'F': print_dashboard(); wait_key(); break;
                default: printf("无效选择，请重试\n");
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
                printf("感谢使用，再见！\n");
                cleanup_system();
                return;
            default: printf("无效选择，请重试\n");
        }
    }
}

/**
 * 登录菜单
 */
void show_login_menu(void) {
    print_title("登录/注销");
    
    if (g_current_user_id > 0) {
        printf("当前已登录，是否注销？\n");
        printf("1. 注销\n");
        printf("0. 返回\n");
        
        int choice = get_int_input();
        if (choice == 1) {
            logout();
        }
        return;
    }
    
    char username[MAX_NAME_LEN];
    char password[50];
    
    printf("用户名: ");
    get_string_input(username, sizeof(username));
    
    printf("密码: ");
    get_string_input(password, sizeof(password));
    
    if (login(username, password) == 0) {
        Employee *emp = find_employee_by_id(g_current_user_id);
        printf("登录成功！欢迎 %s [%s]\n", emp->name, emp->role);
    } else {
        printf("用户名或密码错误\n");
    }
}

// ==================== 人员管理 ====================

void show_employee_menu(void) {
    while (1) {
        print_title("人员管理");
        printf("1. 添加员工\n");
        printf("2. 员工列表\n");
        printf("3. 查询员工\n");
        printf("4. 编辑员工\n");
        printf("5. 删除员工(离职)\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_int_input();
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                if (!check_permission("管理员") && !check_permission("店长")) {
                    printf("权限不足，需要管理员或店长权限\n");
                    break;
                }
                
                Employee emp;
                memset(&emp, 0, sizeof(emp));
                
                printf("姓名: ");
                get_string_input(emp.name, sizeof(emp.name));
                printf("角色 (收银员/库管/店长/管理员): ");
                get_string_input(emp.role, sizeof(emp.role));
                printf("密码: ");
                char password[50];
                get_string_input(password, sizeof(password));
                
                generate_salt(emp.salt);
                strcpy(emp.password_hash, hash_password(password, emp.salt));
                
                int id = add_employee(&emp);
                if (id > 0) {
                    printf("添加成功！员工ID: %d\n", id);
                    save_employees();
                }
                break;
            }
            
            case 2: {
                int count = 0;
                Employee **list = list_employees(&count);
                
                printf("\n%-8s %-10s %-10s %-10s\n", "ID", "姓名", "角色", "状态");
                printf("--------------------------------\n");
                
                for (int i = 0; i < count; i++) {
                    printf("%-8d %-10s %-10s %-10s\n",
                           list[i]->id, list[i]->name, list[i]->role,
                           list[i]->status == STATUS_ACTIVE ? "在职" : "离职");
                }
                
                printf("\n共 %d 名员工\n", count);
                free(list);
                break;
            }
            
            case 3: {
                printf("员工ID: ");
                int id = get_int_input();
                
                Employee *emp = find_employee_by_id(id);
                if (emp) {
                    printf("\nID: %d\n", emp->id);
                    printf("姓名: %s\n", emp->name);
                    printf("角色: %s\n", emp->role);
                    printf("状态: %s\n", emp->status == STATUS_ACTIVE ? "在职" : "离职");
                } else {
                    printf("未找到该员工\n");
                }
                break;
            }
            
            case 4: {
                printf("员工ID: ");
                int id = get_int_input();
                
                Employee *emp = find_employee_by_id(id);
                if (emp) {
                    printf("姓名 [%s]: ", emp->name);
                    char input[MAX_NAME_LEN];
                    get_string_input(input, sizeof(input));
                    if (strlen(input) > 0) strncpy(emp->name, input, MAX_NAME_LEN - 1);
                    
                    printf("角色 [%s]: ", emp->role);
                    get_string_input(input, sizeof(input));
                    if (strlen(input) > 0) strncpy(emp->role, input, 19);
                    
                    update_employee(emp);
                    save_employees();
                    printf("更新成功\n");
                } else {
                    printf("未找到该员工\n");
                }
                break;
            }
            
            case 5: {
                printf("员工ID: ");
                int id = get_int_input();
                
                if (delete_employee(id) == 0) {
                    save_employees();
                    printf("已标记为离职\n");
                } else {
                    printf("操作失败\n");
                }
                break;
            }
            
            default:
                printf("无效选择\n");
        }
    }
}

// ==================== 商品管理 ====================

void show_product_menu(void) {
    while (1) {
        print_title("商品管理");
        printf("1. 添加商品\n");
        printf("2. 商品列表\n");
        printf("3. 条码查询\n");
        printf("4. 编辑商品\n");
        printf("5. 库存预警\n");
        printf("6. 库存盘点\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_int_input();
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                Product prod;
                memset(&prod, 0, sizeof(prod));
                
                printf("商品ID: ");
                get_string_input(prod.id, sizeof(prod.id));
                printf("商品名称: ");
                get_string_input(prod.name, sizeof(prod.name));
                printf("条码: ");
                get_string_input(prod.barcode, sizeof(prod.barcode));
                printf("售价: ");
                prod.price = get_float_input();
                printf("进价: ");
                prod.cost = get_float_input();
                printf("库存: ");
                prod.stock = get_int_input();
                printf("最低库存预警: ");
                prod.min_stock = get_int_input();
                printf("供应商ID: ");
                get_string_input(prod.supplier_id, sizeof(prod.supplier_id));
                
                if (add_product(&prod) == 0) {
                    save_products();
                    printf("添加成功！\n");
                }
                break;
            }
            
            case 2: {
                printf("\n%-12s %-20s %-12s %-8s %-8s\n", 
                       "商品ID", "名称", "条码", "库存", "价格");
                printf("----------------------------------------------------\n");
                
                for (int i = 0; i < HASH_TABLE_SIZE; i++) {
                    HashNode *node = g_product_hash[i];
                    while (node) {
                        Product *p = (Product*)node->data;
                        if (p->status == 1) {
                            printf("%-12s %-20s %-12s %-8d %-8.2f\n",
                                   p->id, p->name, p->barcode, p->stock, p->price);
                        }
                        node = node->next;
                    }
                }
                break;
            }
            
            case 3: {
                printf("请输入条码: ");
                char barcode[30];
                get_string_input(barcode, sizeof(barcode));
                
                Product *p = find_product_by_barcode(barcode);
                if (p) {
                    printf("\n%-12s %-20s %-12s %-8s %-8s\n", 
                           "商品ID", "名称", "条码", "库存", "价格");
                    printf("%-12s %-20s %-12s %-8d %-8.2f\n",
                           p->id, p->name, p->barcode, p->stock, p->price);
                } else {
                    printf("未找到该商品\n");
                }
                break;
            }
            
            case 4: {
                printf("商品ID: ");
                char id[MAX_ID_LEN];
                get_string_input(id, sizeof(id));
                
                Product *p = find_product_by_id(id);
                if (p) {
                    printf("名称 [%s]: ", p->name);
                    char input[50];
                    get_string_input(input, sizeof(input));
                    if (strlen(input) > 0) strncpy(p->name, input, MAX_NAME_LEN - 1);
                    
                    printf("售价 [%.2f]: ", p->price);
                    float f = get_float_input();
                    if (f > 0) p->price = f;
                    
                    printf("库存 [%d]: ", p->stock);
                    int n = get_int_input();
                    if (n >= 0) p->stock = n;
                    
                    update_product(p);
                    save_products();
                    printf("更新成功\n");
                } else {
                    printf("未找到该商品\n");
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
                printf("无效选择\n");
        }
    }
}

// ==================== 销售管理 ====================

void show_sale_menu(void) {
    while (1) {
        print_title("销售管理");
        printf("1. 扫描销售\n");
        printf("2. 挂单列表\n");
        printf("3. 完成支付\n");
        printf("4. 取消挂单\n");
        printf("5. 退款处理\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_int_input();
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                printf("请输入条码: ");
                char barcode[30];
                get_string_input(barcode, sizeof(barcode));
                printf("数量: ");
                int qty = get_int_input();
                
                int sale_id = scan_and_sell(g_current_user_id, barcode, qty);
                if (sale_id > 0) {
                    printf("已添加到挂单，订单号: %d\n", sale_id);
                }
                break;
            }
            
            case 2: {
                printf("\n%-8s %-10s %-12s %-10s\n", 
                       "订单号", "收银员", "时间", "状态");
                printf("--------------------------------\n");
                
                Sale *sale = g_pending_sales;
                while (sale) {
                    char time_str[32];
                    format_time(sale->created_at, time_str);
                    printf("%-8d %-10d %-12s %-10s\n",
                           sale->id, sale->cashier_id, time_str,
                           sale->status == SALE_PENDING ? "挂单" : "已完成");
                    sale = sale->next;
                }
                break;
            }
            
            case 3: {
                printf("订单号: ");
                int sale_id = get_int_input();
                
                // 先检查订单是否存在并显示应付金额
                Sale *sale_check = find_pending_sale(sale_id);
                if (!sale_check) {
                    printf("未找到该挂单\n");
                    break;
                }
                printf("应付金额: ¥%.2f\n", sale_check->final_amount);
                
                printf("支付方式 (现金/微信/支付宝): ");
                char method[20];
                get_string_input(method, sizeof(method));
                printf("额外折扣(%%，0表示无额外折扣): ");
                float discount = get_float_input();
                
                float cash_received = sale_check->final_amount;  // 默认实收金额
                if (strcmp(method, "现金") == 0) {
                    printf("实收金额: ");
                    cash_received = get_float_input();
                }
                
                if (complete_sale(sale_id, method, discount, cash_received) == 0) {
                    // 显示找零金额
                    if (strcmp(method, "现金") == 0) {
                        float change = cash_received - (sale_check->final_amount * (1 - discount/100));
                        printf("实收: ¥%.2f | 找零: ¥%.2f\n", 
                               cash_received, change > 0 ? change : 0);
                    }
                    printf("支付完成！\n");
                } else {
                    printf("支付失败\n");
                }
                break;
            }
            
            case 4: {
                printf("订单号: ");
                int sale_id = get_int_input();
                
                if (cancel_sale(sale_id) == 0) {
                    printf("挂单已取消\n");
                }
                break;
            }
            
            default:
                printf("无效选择\n");
        }
    }
}

// ==================== 采购管理 ====================

void show_purchase_menu(void) {
    while (1) {
        print_title("采购管理");
        printf("1. 创建采购订单\n");
        printf("2. 待审核订单\n");
        printf("3. 审批订单\n");
        printf("4. 库管收货\n");
        printf("5. 订单列表\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_int_input();
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                Purchase pur;
                memset(&pur, 0, sizeof(pur));
                
                printf("供应商ID: ");
                get_string_input(pur.supplier_id, sizeof(pur.supplier_id));
                printf("供应商名称: ");
                get_string_input(pur.supplier_name, sizeof(pur.supplier_name));
                pur.creator_id = g_current_user_id;
                
                int purchase_id = create_purchase(&pur);
                printf("采购订单已创建，订单号: %d\n", purchase_id);
                
                // 添加采购明细
                printf("是否添采购明细？(y/n): ");
                char ans[4];
                get_string_input(ans, sizeof(ans));
                
                if (strcmp(ans, "y") == 0 || strcmp(ans, "Y") == 0) {
                    while (1) {
                        printf("商品条码 (输入q结束): ");
                        char barcode[30];
                        get_string_input(barcode, sizeof(barcode));
                        
                        if (strcmp(barcode, "q") == 0) break;
                        
                        Product *prod = find_product_by_barcode(barcode);
                        if (!prod) {
                            printf("商品不存在\n");
                            continue;
                        }
                        
                        printf("商品: %s\n", prod->name);
                        printf("采购数量: ");
                        float qty = get_float_input();
                        printf("采购单价: ");
                        float price = get_float_input();
                        
                        PurchaseItem item;
                        memset(&item, 0, sizeof(item));
                        strncpy(item.product_id, prod->id, MAX_ID_LEN - 1);
                        strncpy(item.product_name, prod->name, MAX_NAME_LEN - 1);
                        item.quantity = qty;
                        item.price = price;
                        
                        add_purchase_item(purchase_id, &item);
                        save_purchase_item(&item);
                        printf("已添加\n");
                    }
                }
                break;
            }
            
            case 2: {
                int count = 0;
                Purchase **list = list_purchases(PURCHASE_PENDING, &count);
                
                printf("\n待审核订单 (%d个)\n", count);
                printf("%-8s %-15s %-12s %-10s\n",
                       "订单号", "供应商", "创建时间", "金额");
                printf("----------------------------------------------------\n");
                
                for (int i = 0; i < count; i++) {
                    char time_str[32];
                    format_time(list[i]->created_at, time_str);
                    printf("%-8d %-15s %-12s %-10.2f\n",
                           list[i]->id, list[i]->supplier_name,
                           time_str, list[i]->total_amount);
                }
                free(list);
                break;
            }
            
            case 3: {
                printf("订单号: ");
                int id = get_int_input();
                printf("1. 审批通过  2. 拒绝: ");
                int op = get_int_input();
                
                if (op == 1) {
                    if (approve_purchase(id, g_current_user_id) == 0) {
                        printf("审批通过\n");
                    }
                } else if (op == 2) {
                    if (reject_purchase(id, g_current_user_id) == 0) {
                        printf("已拒绝\n");
                    }
                }
                break;
            }
            
            case 4: {
                printf("订单号: ");
                int id = get_int_input();
                
                if (receive_purchase(id, g_current_user_id) == 0) {
                    printf("收货完成，已入库\n");
                } else {
                    printf("收货失败\n");
                }
                break;
            }
            
            case 5: {
                printf("1.全部 2.待审核 3.已审核 4.已完成 5.已拒绝: ");
                int status = get_int_input();
                
                int count = 0;
                Purchase **list = list_purchases(status - 2, &count);
                
                printf("\n%-8s %-15s %-10s %-10s\n",
                       "订单号", "供应商", "状态", "金额");
                printf("----------------------------------------------------\n");
                
                for (int i = 0; i < count; i++) {
                    printf("%-8d %-15s %-10s %-10.2f\n",
                           list[i]->id, list[i]->supplier_name,
                           get_purchase_status_str(list[i]->status),
                           list[i]->total_amount);
                }
                free(list);
                break;
            }
            
            default:
                printf("无效选择\n");
        }
    }
}

// ==================== 排班管理 ====================

void show_schedule_menu(void) {
    while (1) {
        print_title("排班管理");
        printf("1. 创建周排班\n");
        printf("2. 周排班表\n");
        printf("3. 班次统计\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_int_input();
        
        if (choice == 0) break;
        
        switch (choice) {
            case 1: {
                int year, week;
                printf("年份: ");
                year = get_int_input();
                printf("周数: ");
                week = get_int_input();
                
                printf("输入员工ID: ");
                int emp_id = get_int_input();
                
                if (!find_employee_by_id(emp_id)) {
                    printf("员工不存在\n");
                    break;
                }
                
                printf("输入周一到周日的班次 (早/晚/休):\n");
                char shifts[7][4];
                for (int i = 0; i < 7; i++) {
                    printf("周%d: ", i + 1);
                    get_string_input(shifts[i], sizeof(shifts[i]));
                }
                
                if (batch_create_schedule(emp_id, year, week, shifts) > 0) {
                    printf("排班创建成功\n");
                }
                break;
            }
            
            case 2: {
                int year, week;
                printf("年份: ");
                year = get_int_input();
                printf("周数: ");
                week = get_int_input();
                print_schedule_table(year, week);
                break;
            }
            
            case 3: {
                int year, week;
                printf("年份: ");
                year = get_int_input();
                printf("周数: ");
                week = get_int_input();
                count_shifts_by_type(year, week);
                break;
            }
            
            default:
                printf("无效选择\n");
        }
    }
}

// ==================== 报表管理 ====================

void show_report_menu(void) {
    while (1) {
        print_title("报表管理");
        printf("1. 销售报表\n");
        printf("2. 库存报表\n");
        printf("3. 采购报表\n");
        printf("4. 盈亏报告\n");
        printf("5. 导出报表\n");
        printf("0. 返回\n");
        printf("\n请选择: ");
        
        int choice = get_int_input();
        
        if (choice == 0) break;
        
        TimeRange range;
        
        switch (choice) {
            case 1:
                printf("统计天数 (1-30): ");
                int days = get_int_input();
                if (days <= 0) days = 7;
                if (days > 30) days = 30;
                get_date_range(&range, days);
                generate_sales_report(range.start, range.end, "控制台");
                break;
            
            case 2:
                generate_inventory_report("控制台");
                break;
            
            case 3:
                printf("统计天数: ");
                days = get_int_input();
                if (days <= 0) days = 30;
                get_date_range(&range, days);
                generate_purchase_report(range.start, range.end, "控制台");
                break;
            
            case 4:
                printf("统计天数: ");
                days = get_int_input();
                if (days <= 0) days = 30;
                get_date_range(&range, days);
                generate_profit_loss_report(range.start, range.end);
                break;
            
            case 5:
                show_export_menu();
                int export_choice = get_int_input();
                handle_export(export_choice);
                break;
            
            default:
                printf("无效选择\n");
        }
    }
}

// ==================== 系统设置 ====================

void show_system_menu(void) {
    print_title("系统设置");
    printf("1. 数据备份\n");
    printf("2. 系统信息\n");
    printf("3. 初始化默认管理员\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_int_input();
    
    switch (choice) {
        case 1:
            printf("数据备份功能（待实现）\n");
            break;
        
        case 2:
            printf("\n超市管理系统 v1.0\n");
            printf("基于ANSI C开发\n");
            printf("数据存储: 纯文本文件(.txt)\n");
            printf("编译环境: GCC/MinGW\n");
            break;
        
        case 3: {
            // 检查是否已有管理员
            int count = 0;
            Employee **list = list_employees(&count);
            free(list);
            
            if (count > 0) {
                printf("系统中已有员工，无需初始化\n");
                break;
            }
            
            // 创建默认管理员
            Employee admin;
            memset(&admin, 0, sizeof(admin));
            strcpy(admin.name, "admin");
            strcpy(admin.role, "管理员");
            strcpy(admin.salt, "00000000000000000000000000000000");
            strcpy(admin.password_hash, hash_password("admin123", admin.salt));
            
            add_employee(&admin);
            save_employees();
            printf("已创建默认管理员:\n");
            printf("用户名: admin\n");
            printf("密码: admin123\n");
            printf("请首次登录后立即修改密码！\n");
            break;
        }
        
        default:
            break;
    }
}

// ==================== 小票打印 ====================

void show_printer_menu(void) {
    print_title("小票打印");

    printf("当前打印机: %s\n", printer_get_device_path());
    printf("打印机状态: %s\n", printer_is_available() ? "可用" : "不可用");
    printf("\n");

    printf("1. 打印测试页\n");
    printf("2. 设置打印机端口\n");
    printf("3. 打印历史小票\n");
    printf("0. 返回\n");
    printf("\n请选择: ");

    int choice = get_int_input();

    switch (choice) {
        case 1: {
            printf("正在打印测试页...\n");
            if (printer_test_page() == 0) {
                printf("测试页已发送到打印机\n");
            } else {
                printf("打印失败(打印机可能不可用，测试页已保存到文件)\n");
            }
            break;
        }

        case 2: {
            printf("请输入打印机端口:\n");
            printf("  Windows: LPT1, COM1\n");
            printf("  Linux: /dev/usb/lp0, /dev/lp0\n");
            printf("端口: ");
            char device[256];
            get_string_input(device, sizeof(device));
            if (strlen(device) > 0) {
                printer_set_device(device);
                printf("已设置打印机端口: %s\n", device);
            }
            break;
        }

        case 3: {
            // 列出当天的小票文件
            printf("\n当天小票文件:\n");
            printf("提示: 小票以 receipt_YYYYMMDD_XXXXXX.txt 格式保存\n");
            break;
        }

        default:
            break;
    }
}

// ==================== 主函数 ====================

int main(void) {
    printf("\n");
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║      超市管理系统 v1.0               ║\n");
    printf("  ║      Supermarket Management System   ║\n");
    printf("  ╚══════════════════════════════════════╝\n");
    
    // 初始化系统
    if (init_system() != 0) {
        fprintf(stderr, "系统初始化失败\n");
        return 1;
    }
    
    // 加载所有数据
    load_schedules();
    load_pending_sales();
    load_purchases();
    load_purchase_items();
    load_transaction_logs();
    load_stock_logs();
    load_combos();           // 加载套装数据
    load_stores();           // 加载门店数据
    load_store_stocks();     // 加载门店库存
    load_transfers();        // 加载调拨单
    load_supplier_finances(); // 加载供应商财务信息
    load_payables();         // 加载应付款
    load_payment_records();   // 加载付款记录
    load_vip_cards();        // 加载储值卡
    load_vip_card_transactions(); // 加载储值卡交易
    
    // 检查是否需要初始化管理员
    int emp_count = 0;
    list_employees(&emp_count);
    if (emp_count == 0) {
        printf("\n首次使用系统，请先初始化管理员账号\n");
        Employee admin;
        memset(&admin, 0, sizeof(admin));
        strcpy(admin.name, "admin");
        strcpy(admin.role, "管理员");
        strcpy(admin.salt, "00000000000000000000000000000000");
        strcpy(admin.password_hash, hash_password("admin123", admin.salt));
        add_employee(&admin);
        save_employees();
        printf("已创建默认管理员: admin / admin123\n");
    }
    
    // 显示主菜单
    show_main_menu();
    
    return 0;
}

// ==================== 套装管理菜单 ====================

void show_combo_menu(void) {
    print_title("套装管理");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        printf("权限不足，需要管理员或店长权限\n");
        return;
    }
    
    printf("1. 创建套装\n");
    printf("2. 套装列表\n");
    printf("3. 添加套装商品\n");
    printf("4. 套装详情\n");
    printf("5. 下架套装\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_int_input();
    
    switch (choice) {
        case 1: {
            ProductCombo combo;
            memset(&combo, 0, sizeof(combo));
            
            printf("套装名称: ");
            get_string_input(combo.name, sizeof(combo.name));
            printf("套装条码: ");
            get_string_input(combo.barcode, sizeof(combo.barcode));
            printf("套装售价: ");
            combo.price = get_float_input();
            combo.cost = 0;
            
            int combo_id = create_combo(&combo);
            if (combo_id > 0) {
                printf("套装创建成功！ID: %d\n", combo_id);
                printf("请使用选项3添加子商品\n");
            }
            break;
        }
        
        case 2: {
            int count = 0;
            ProductCombo **list = list_combos(&count);
            
            printf("\n%-8s %-20s %-15s %-10s\n", "ID", "名称", "条码", "售价");
            printf("----------------------------------------------------\n");
            
            for (int i = 0; i < count; i++) {
                printf("%-8d %-20s %-15s %-10.2f\n",
                       list[i]->id, list[i]->name, list[i]->barcode, list[i]->price);
            }
            printf("\n共 %d 个套装\n", count);
            free(list);
            break;
        }
        
        case 3: {
            printf("套装ID: ");
            int combo_id = get_int_input();
            ProductCombo *combo = find_combo_by_id(combo_id);
            if (!combo) {
                printf("套装不存在\n");
                break;
            }
            
            printf("商品条码: ");
            char barcode[30];
            get_string_input(barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (!prod) {
                printf("商品不存在\n");
                break;
            }
            
            printf("数量: ");
            int qty = get_int_input();
            
            ComboItem item;
            memset(&item, 0, sizeof(item));
            strncpy(item.product_id, prod->id, MAX_ID_LEN - 1);
            strncpy(item.product_name, prod->name, MAX_NAME_LEN - 1);
            item.quantity = qty;
            
            if (add_combo_item(combo_id, &item) == 0) {
                printf("已添加: %s x%d\n", prod->name, qty);
            }
            break;
        }
        
        case 0:
            break;
    }
}

// ==================== 库存调拨菜单 ====================

void show_transfer_menu(void) {
    print_title("库存调拨");
    
    if (!check_permission("管理员") && !check_permission("店长") && !check_permission("库管")) {
        printf("权限不足\n");
        return;
    }
    
    printf("1. 创建调拨单\n");
    printf("2. 添加调拨商品\n");
    printf("3. 审批调拨单\n");
    printf("4. 出库确认\n");
    printf("5. 入库确认\n");
    printf("6. 调拨单列表\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_int_input();
    
    switch (choice) {
        case 1: {
            int from_id, to_id;
            printf("源门店ID: ");
            from_id = get_int_input();
            printf("目标门店ID: ");
            to_id = get_int_input();
            
            printf("备注: ");
            char remark[256];
            get_string_input(remark, sizeof(remark));
            
            int id = create_transfer_order(from_id, to_id, g_current_user_id, remark);
            if (id > 0) {
                printf("调拨单创建成功！ID: %d\n", id);
            }
            break;
        }
        
        case 2: {
            printf("调拨单ID: ");
            int transfer_id = get_int_input();
            printf("商品条码: ");
            char barcode[30];
            get_string_input(barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (!prod) {
                printf("商品不存在\n");
                break;
            }
            
            printf("数量: ");
            int qty = get_int_input();
            
            if (add_transfer_item(transfer_id, prod->id, qty) == 0) {
                printf("已添加\n");
            }
            break;
        }
        
        case 3: {
            printf("调拨单ID: ");
            int id = get_int_input();
            if (approve_transfer(id, g_current_user_id) == 0) {
                printf("审批通过\n");
            }
            break;
        }
        
        case 4: {
            printf("调拨单ID: ");
            int id = get_int_input();
            if (confirm_out_transfer(id, g_current_user_id) == 0) {
                printf("出库确认完成\n");
            }
            break;
        }
        
        case 5: {
            printf("调拨单ID: ");
            int id = get_int_input();
            if (confirm_in_transfer(id, g_current_user_id) == 0) {
                printf("入库确认完成\n");
            }
            break;
        }
        
        case 6: {
            int count = 0;
            TransferOrder **list = list_transfers_by_status(-1, &count);
            
            printf("\n%-8s %-10s %-10s %-10s\n", "ID", "源门店", "目标门店", "状态");
            printf("----------------------------------------------------\n");
            
            const char *status_str[] = {"待出库", "已出库", "已完成", "已拒绝", "已取消"};
            for (int i = 0; i < count; i++) {
                printf("%-8d %-10s %-10s %-10s\n",
                       list[i]->id, list[i]->from_store_name, list[i]->to_store_name,
                       status_str[list[i]->status]);
            }
            free(list);
            break;
        }
        
        case 0:
            break;
    }
}

// ==================== 供应商结算菜单 ====================

void show_supplier_settlement_menu(void) {
    print_title("供应商结算");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        printf("权限不足\n");
        return;
    }
    
    printf("1. 应付款汇总\n");
    printf("2. 按供应商查看\n");
    printf("3. 付款\n");
    printf("4. 设置供应商账期/评级\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_int_input();
    
    switch (choice) {
        case 1:
            print_payables_summary();
            break;
        
        case 2: {
            printf("供应商ID: ");
            int supplier_id = get_int_input();
            time_t now = time(NULL);
            time_t start = now - 90 * 24 * 3600;  // 近90天
            generate_supplier_statement(supplier_id, start, now);
            break;
        }
        
        case 3: {
            printf("应付单ID: ");
            int payable_id = get_int_input();
            printf("付款金额: ");
            float amount = get_float_input();
            printf("付款方式: ");
            char method[20];
            get_string_input(method, sizeof(method));
            printf("参考号: ");
            char ref[50];
            get_string_input(ref, sizeof(ref));
            
            if (record_payment(payable_id, amount, method, ref, g_current_user_id, "") == 0) {
                printf("付款成功\n");
            }
            break;
        }
        
        case 4: {
            printf("供应商ID: ");
            int supplier_id = get_int_input();
            printf("账期(天): ");
            float days = get_float_input();
            printf("评级(A/B/C/D): ");
            char rating[2];
            get_string_input(rating, sizeof(rating));
            
            update_supplier_finance(supplier_id, days, rating[0]);
            printf("更新成功\n");
            break;
        }
        
        case 0:
            break;
    }
}

// ==================== 促销管理菜单 ====================

void show_promotion_menu(void) {
    print_title("促销管理");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        printf("权限不足\n");
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
    
    int choice = get_int_input();
    
    switch (choice) {
        case 1: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            printf("促销名称: ");
            get_string_input(promo.name, sizeof(promo.name));
            printf("商品条码: ");
            char barcode[30];
            get_string_input(barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            printf("折扣率(如0.8表示8折): ");
            promo.discount_rate = get_float_input();
            printf("开始时间(YYYYMMDD): ");
            char date_str[20];
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.start_time);
            printf("结束时间(YYYYMMDD): ");
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.end_time);
            
            promo.type = PROMOTION_TYPE_DISCOUNT;
            promo.priority = 1;
            
            create_promotion(&promo);
            printf("促销创建成功\n");
            break;
        }
        
        case 2: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            printf("促销名称: ");
            get_string_input(promo.name, sizeof(promo.name));
            printf("满减门槛: ");
            promo.threshold = get_float_input();
            printf("减免金额: ");
            promo.discount_amount = get_float_input();
            printf("开始时间(YYYYMMDD): ");
            char date_str[20];
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.start_time);
            printf("结束时间(YYYYMMDD): ");
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.end_time);
            
            promo.type = PROMOTION_TYPE_OVERRIDE;
            promo.priority = 10;
            
            create_promotion(&promo);
            printf("满减促销创建成功\n");
            break;
        }
        
        case 3: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            printf("促销名称: ");
            get_string_input(promo.name, sizeof(promo.name));
            printf("商品条码: ");
            char barcode[30];
            get_string_input(barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            printf("第几件优惠(如2): ");
            promo.nth_item = get_int_input();
            printf("折扣率(如0.5表示半价): ");
            promo.nth_discount_rate = get_float_input();
            printf("开始时间(YYYYMMDD): ");
            char date_str[20];
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.start_time);
            printf("结束时间(YYYYMMDD): ");
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.end_time);
            
            promo.type = PROMOTION_TYPE_NTH_DISCOUNT;
            promo.priority = 2;
            
            create_promotion(&promo);
            printf("第N件优惠创建成功\n");
            break;
        }
        
        case 4: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            printf("促销名称: ");
            get_string_input(promo.name, sizeof(promo.name));
            printf("商品条码: ");
            char barcode[30];
            get_string_input(barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            printf("买M件: ");
            promo.buy_quantity = get_int_input();
            printf("送N件: ");
            promo.free_quantity = get_int_input();
            printf("开始时间(YYYYMMDD): ");
            char date_str[20];
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.start_time);
            printf("结束时间(YYYYMMDD): ");
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.end_time);
            
            promo.type = PROMOTION_TYPE_BUY_M_GET_N;
            promo.priority = 2;
            
            create_promotion(&promo);
            printf("买M赠N促销创建成功\n");
            break;
        }
        
        case 5: {
            Promotion promo;
            memset(&promo, 0, sizeof(promo));
            
            printf("促销名称: ");
            get_string_input(promo.name, sizeof(promo.name));
            printf("商品条码: ");
            char barcode[30];
            get_string_input(barcode, sizeof(barcode));
            Product *prod = find_product_by_barcode(barcode);
            if (prod) {
                strncpy(promo.product_id, prod->id, MAX_ID_LEN - 1);
            }
            printf("会员专属价: ");
            promo.member_price = get_float_input();
            printf("适用会员等级(0=所有,1=银卡,2=金卡,3=钻卡): ");
            promo.member_level = get_int_input();
            printf("开始时间(YYYYMMDD): ");
            char date_str[20];
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.start_time);
            printf("结束时间(YYYYMMDD): ");
            get_string_input(date_str, sizeof(date_str));
            sscanf(date_str, "%ld", &promo.end_time);
            
            promo.type = PROMOTION_TYPE_MEMBER_PRICE;
            promo.priority = 3;
            
            create_promotion(&promo);
            printf("会员专属价促销创建成功\n");
            break;
        }
        
        case 6: {
            int count = 0;
            Promotion **list = list_promotions(&count);
            
            printf("\n%-6s %-15s %-10s %-10s %-10s\n", "ID", "名称", "类型", "开始", "结束");
            printf("---------------------------------------------------------------------\n");
            
            const char *type_str[] = {"单品折扣", "满减", "第N件", "买M赠N", "会员价"};
            for (int i = 0; i < count; i++) {
                char start_str[12], end_str[12];
                format_time(list[i]->start_time, start_str);
                format_time(list[i]->end_time, end_str);
                printf("%-6d %-15s %-10s %-10s %-10s\n",
                       list[i]->id, list[i]->name, 
                       type_str[list[i]->type < 5 ? list[i]->type : 0],
                       start_str, end_str);
            }
            free(list);
            break;
        }
        
        case 0:
            break;
    }
}

// ==================== 储值卡菜单 ====================

void show_vipcard_menu(void) {
    print_title("储值卡管理");
    
    if (!check_permission("管理员") && !check_permission("店长")) {
        printf("权限不足\n");
        return;
    }
    
    printf("1. 创建储值卡\n");
    printf("2. 储值卡充值\n");
    printf("3. 储值卡消费\n");
    printf("4. 储值卡列表\n");
    printf("5. 交易记录查询\n");
    printf("6. 余额汇总\n");
    printf("0. 返回\n");
    printf("\n请选择: ");
    
    int choice = get_int_input();
    
    switch (choice) {
        case 1: {
            printf("设置支付密码: ");
            char password[20];
            get_string_input(password, sizeof(password));
            
            VipCard *card = create_vip_card(password);
            if (card) {
                printf("储值卡创建成功！\n");
                printf("卡号: %s\n", card->card_no);
                printf("请妥善保管卡号和密码\n");
            }
            break;
        }
        
        case 2: {
            printf("储值卡号: ");
            char card_no[30];
            get_string_input(card_no, sizeof(card_no));
            printf("充值金额: ");
            float amount = get_float_input();
            
            if (recharge_vip_card(card_no, amount, g_current_user_id, "") == 0) {
                printf("充值成功！\n");
            }
            break;
        }
        
        case 3: {
            printf("储值卡号: ");
            char card_no[30];
            get_string_input(card_no, sizeof(card_no));
            printf("消费金额: ");
            float amount = get_float_input();
            
            if (consume_vip_card(card_no, amount, 0, g_current_user_id, "") == 0) {
                printf("消费成功！\n");
            }
            break;
        }
        
        case 4: {
            int count = 0;
            VipCard **list = list_vip_cards(&count);
            
            printf("\n%-20s %-10s %-10s %-10s\n", "卡号", "会员ID", "余额", "状态");
            printf("---------------------------------------------------------------------\n");
            
            const char *status_str[] = {"已注销", "正常", "冻结", "过期"};
            for (int i = 0; i < count; i++) {
                int st = list[i]->status;
                if (st < 0 || st > 3) st = 0;
                printf("%-20s %-10d %-10.2f %-10s\n",
                       list[i]->card_no, list[i]->member_id, 
                       list[i]->balance, status_str[st]);
            }
            printf("\n共 %d 张储值卡\n", count);
            free(list);
            break;
        }
        
        case 5: {
            printf("储值卡号: ");
            char card_no[30];
            get_string_input(card_no, sizeof(card_no));
            
            int count = 0;
            VipCardTransaction **list = query_vip_card_transactions(card_no, &count);
            
            printf("\n%-8s %-8s %-10s %-10s %-10s\n", "ID", "类型", "金额", "前余额", "后余额");
            printf("---------------------------------------------------------------------\n");
            
            const char *type_str[] = {"充值", "消费", "退款"};
            for (int i = 0; i < count; i++) {
                int t = list[i]->type;
                if (t < 0 || t > 2) t = 0;
                printf("%-8d %-8s %-10.2f %-10.2f %-10.2f\n",
                       list[i]->id, type_str[t], list[i]->amount,
                       list[i]->balance_before, list[i]->balance_after);
            }
            free(list);
            break;
        }
        
        case 6:
            print_vip_card_summary();
            break;
        
        case 0:
            break;
    }
}
