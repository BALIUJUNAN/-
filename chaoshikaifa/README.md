# 超市管理系统 (Supermarket Management System)

基于 ANSI C (C99) 开发的轻量化超市后台管理系统，采用纯文本文件持久化存储，无需数据库依赖。

## 功能特性

### 核心模块
- **人员管理**: 员工入职/离职、角色权限（收银员/库管/店长/管理员）
- **商品管理**: 商品多级分类、条码查询、库存预警、库存盘点
- **销售管理**: 扫码销售、挂单/支付、库存原子性扣减、支持多种支付方式
- **采购管理**: 采购订单创建→店长审批→库管收货，完整状态流转
- **排班管理**: 员工周排班（早班/晚班/休息）、班次统计
- **报表系统**: 销售报表、库存报表、采购报表、盈亏报告，支持导出 CSV/HTML

### 技术亮点
- **哈希表索引**: O(1) 查询性能，支持 ID/条码双索引
- **原子性写入**: 临时文件 + rename 实现数据安全
- **文件锁机制**: 支持 Windows/Linux 多平台并发控制
- **密码安全**: 盐值 + 哈希存储
- **事务日志**: 完整的操作审计追踪

## 项目结构

```
supermarket/
├── main.c            # 主程序入口
├── supermarket.h     # 头文件（数据结构、函数声明）
├── supermarket.c     # 核心实现（哈希表、文件IO、工具函数）
├── sale.c            # 销售模块
├── purchase.c        # 采购模块
├── schedule.c        # 排班模块
├── report.c          # 报表模块
├── build.bat         # Windows 编译脚本
├── build.sh          # Linux/macOS 编译脚本
├── README.md         # 说明文档
└── data/              # 数据目录（运行时自动创建）
    ├── employee.txt
    ├── product.txt
    ├── sales.txt
    └── ...
```

## 编译运行

### Windows (MinGW)
```batch
# 在项目目录下执行
build.bat

# 或者手动编译
gcc main.c supermarket.c sale.c purchase.c schedule.c report.c -o supermarket.exe -Wall -std=c99
```

### Linux/macOS
```bash
# 赋予执行权限
chmod +x build.sh

# 执行编译脚本
./build.sh

# 或者手动编译
gcc main.c supermarket.c sale.c purchase.c schedule.c report.c -o supermarket -Wall -std=c99
```

### 运行
```bash
# Windows
supermarket.exe

# Linux/macOS
./supermarket
```

## 首次使用

1. 编译并运行程序
2. 系统自动创建默认管理员账户:
   - **用户名**: `admin`
   - **密码**: `admin123`
3. 首次登录后请立即修改密码

## 数据文件格式

所有数据以 `|` 分隔的文本格式存储:

### employee.txt
```
ID|姓名|角色|密码哈希|盐值|状态|创建时间|更新时间
```

### product.txt
```
商品ID|名称|条码|售价|进价|库存|最低库存|分类ID|供应商ID|状态|创建时间|更新时间
```

### sales.txt
```
收银员ID|订单ID|总额|折扣|实收|支付方式|状态|创建时间|完成时间
```

### schedule.txt
```
排班ID|员工ID|年|周|周一|周二|周三|周四|周五|周六|周日|创建时间
```

## 系统菜单

```
========== 超市管理系统 ==========
1. 登录/注销
2. 人员管理
3. 商品管理
4. 销售管理
5. 采购管理
6. 排班管理
7. 报表管理
8. 系统设置
0. 退出系统
```

## 开发说明

### 数据结构
- 哈希表用于快速查找（商品ID/条码、员工ID、供应商ID）
- 链表用于存储同类记录集合
- 所有修改同时更新内存和文件

### 文件同步策略
1. 修改内存数据
2. 写入临时文件 `.tmp`
3. `rename()` 原子替换原文件

## 系统要求

- GCC 4.8+ 或 MinGW
- C99 标准支持
- Windows XP+ / Linux / macOS

## 许可证

MIT License

## 作者

基于 ANSI C 开发的教学示例项目
