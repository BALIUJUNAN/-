# 超市管理系统

基于 ANSI C (C99) 开发的轻量化超市后台管理系统，同时提供基于 Python Flask 的 Web 版本。C 终端版使用 [AbyssDB](https://github.com/BALIUJUNAN/ABYSS-DB) 作为嵌入式权威存储，无需独立数据库服务器；Web 版使用 SQLite。原有文本文件仅作为尚未迁移模块的兼容存储，或已迁移数据的首次导入来源。

[![License](https://img.shields.io/badge/license-MIT-lightgrey.svg)](LICENSE)
[![Storage](https://img.shields.io/badge/storage-AbyssDB-blue.svg)](https://github.com/BALIUJUNAN/ABYSS-DB)

## 项目关系

这三个项目展示了从需求发现、通用引擎抽象到独立应用验证的完整过程：

- [Abyssal Whispers](https://github.com/BALIUJUNAN/abyssal-whispers) 中的复杂状态、存档、时间线和恢复需求为 AbyssDB 提供了设计灵感；该游戏当前未直接使用 AbyssDB。
- [AbyssDB](https://github.com/BALIUJUNAN/ABYSS-DB) 将这些理念抽象为独立的 C17 嵌入式状态存储引擎。
- Abyss-Supermarket 是当前的实际集成应用，使用 AbyssDB 执行业务数据持久化、事务提交、遗留数据迁移、持久化计数器和审计记录。

## 特性亮点

- **连续订单编号**：销售订单采用独立计数器，自动跳过历史最大编号，确保编号连续不间断
- **专业 UI 界面**：双线 ASCII 边框、ANSI 颜色支持、表格化展示、分页导航
- **安全密码输入**：隐藏式密码输入，防止密码泄露
- **操作确认机制**：关键操作二次确认，降低误操作风险
- **模块化架构**：UI 与业务逻辑分离，易于维护扩展
- **轻量级部署**：C 终端版集成 AbyssDB，无需部署独立数据库服务器
- **双版本支持**：C 终端版 + Python Flask Web 版，满足不同使用场景

## 系统要求

### C 终端版

- GCC 4.8+ 或 MinGW-w64
- C99 标准支持
- AbyssDB 源码目录（存储层使用 C17 单独编译）
- Windows XP+ / Linux / macOS
- 终端支持 ANSI 颜色（大多数现代终端均支持）

### Web 版

- Python 3.6+
- Flask 框架
- 现代浏览器

## 功能模块

### 基础管理

| 模块 | 功能 |
|------|------|
| 人员管理 | 员工入职/离职、角色权限（收银员/库管/店长/管理员） |
| 商品管理 | 商品多级分类、条码查询、库存预警、库存盘点 |
| 销售管理 | 扫码销售、挂单/支付、多种支付方式（现金/微信/支付宝）、连续订单编号 |
| 采购管理 | 采购订单创建→审批→收货入库，完整状态流转 |
| 排班管理 | 员工周排班（早班/晚班/休息）、班次统计 |
| 报表管理 | 销售报表、库存报表、采购报表、盈亏报告、CSV 导出 |

### 营销管理

| 模块 | 功能 |
|------|------|
| 促销管理 | 单品折扣、满减促销、第N件优惠、买M赠N、会员专属价 |
| 会员管理 | 会员注册、积分累计/兑换、等级调整（普通/银卡/金卡/钻卡） |
| 储值卡管理 | 储值卡创建、充值/消费、交易记录、余额汇总 |

### 供应链管理

| 模块 | 功能 |
|------|------|
| 套装管理 | 套装商品创建、子商品组合、套装上下架 |
| 库存调拨 | 多门店管理、调拨单流程（创建→审批→出库→入库） |
| 供应商结算 | 应付款管理、账期/评级设置、付款记录 |

### 系统功能

| 模块 | 功能 |
|------|------|
| 小票打印 | 支持 ESC/POS 指令打印机、测试页打印、历史小票查看、小票文件保存 |
| 系统设置 | 数据备份、系统信息查看、默认管理员初始化 |

## 项目结构

```
.
├── main.c                  # 主程序入口、菜单交互、业务流程控制
├── supermarket.h           # 合并头文件（宏定义、数据结构、函数声明）
├── supermarket.c           # 核心实现（哈希表、文件IO、员工管理）
├── sale.c                  # 销售模块（扫码、挂单、支付、订单编号）
├── purchase.c              # 采购模块（订单、审批、收货）
├── schedule.c              # 排班模块（周排班、统计）
├── report.c                # 报表模块（各类报表、CSV导出）
├── marketing.c             # 营销模块（促销、会员管理）
├── finance.c               # 财务模块（储值卡、供应商结算）
├── store_ops.c             # 门店模块（调拨、套装）
├── utility.c               # 工具函数（时间、验证、文件操作）
├── ui.c / ui.h             # UI界面模块（边框、表格、颜色、安全输入）
├── hash.h                  # 哈希表数据结构与 SHA-256 声明
├── config.mk.example       # 本地 AbyssDB 路径配置示例
├── Makefile                # Makefile 构建脚本（支持增量编译）
├── build.bat               # Windows 编译脚本（MinGW）
├── build.sh                # Linux/macOS 编译脚本
│
├── domain/                 # 领域实体（与 ABI 无关的持久化数据结构）
│   ├── sm_base_entities.h
│   ├── sm_sales_entities.h
│   ├── sm_inventory_entities.h
│   ├── sm_purchase_entities.h
│   ├── sm_finance_entities.h
│   └── sm_operations_entities.h
│
├── storage/                # AbyssDB 存储层（生命周期、事务、Key/Value 编解码）
│   ├── sm_store.h / .c     # 存储引擎（B+ 树）
│   ├── sm_key.h / .c       # Key 编码
│   ├── sm_codec.h / .c     # Value 编解码
│   └── sm_namespace.h      # 命名空间管理
│
├── repo/                   # 仓储层（UOW、codec、CRUD、原子索引）
│   ├── sm_repository.h / .c          # 仓储入口
│   ├── sm_base_codec.h / .c          # 基础实体编解码
│   ├── sm_base_repository.h / .c     # 基础实体仓储
│   ├── sm_sales_codec.h / .c         # 销售编解码
│   ├── sm_sales_repository.h / .c    # 销售仓储
│   ├── sm_inventory_codec.h / .c     # 库存编解码
│   ├── sm_inventory_repository.h / .c # 库存仓储
│   ├── sm_purchase_codec.h / .c      # 采购编解码
│   ├── sm_purchase_repository.h / .c # 采购仓储
│   ├── sm_finance_codec.h / .c       # 财务编解码
│   ├── sm_finance_repository.h / .c  # 财务仓储
│   ├── sm_operations_codec.h / .c    # 运营编解码
│   ├── sm_operations_repository.h / .c # 运营仓储
│   └── sm_control_repository.c       # 控制/审计仓储
│
├── migration/              # 数据迁移层（旧文本文件 → AbyssDB）
│   ├── sm_legacy_import.h / .c       # 基础数据迁移
│   ├── sm_sales_import.h / .c        # 销售迁移
│   ├── sm_inventory_import.h / .c    # 库存迁移
│   ├── sm_purchase_import.h / .c     # 采购迁移
│   ├── sm_finance_import.h / .c      # 财务迁移
│   └── sm_operations_import.h / .c   # 运营迁移
│
├── app/                    # 应用服务层（业务编排、UOW 协调）
│   ├── sm_app_context.h / .c         # 应用上下文
│   ├── sm_base_service.h / .c        # 基础服务
│   ├── sm_sales_service.h / .c       # 销售服务
│   ├── sm_inventory_service.h / .c   # 库存服务
│   ├── sm_purchase_service.h / .c    # 采购服务
│   ├── sm_finance_service.h / .c     # 财务服务
│   ├── sm_store_service.c            # 门店服务
│   ├── sm_operations_service.h       # 运营服务
│   ├── sm_control_service.c          # 控制/审计服务
│   ├── sm_vip_legacy.c               # 会员兼容层
│   ├── sm_finance_legacy.c           # 财务兼容层
│   ├── sm_store_legacy.c             # 门店兼容层
│   └── sm_control_legacy.c           # 控制兼容层
│
├── tests/                  # 12 阶段增量测试（storage → repo → entity → migration → business）
│   ├── test_storage_phase2.c
│   ├── test_repository_phase3.c
│   ├── test_base_entities_phase4.c
│   ├── test_migration_phase5.c
│   ├── test_sales_phase6.c
│   ├── test_inventory_phase7.c
│   ├── test_purchase_phase8.c
│   ├── test_supplier_finance_phase9.c
│   ├── test_vip_phase10.c
│   ├── test_store_transfer_phase11.c
│   └── test_control_phase12.c
│
├── docs/                   # 重构文档
│   └── abyss-refactor/     # 12 阶段重构设计文档
│       ├── phase-01-design.md
│       ├── phase-02-storage.md
│       ├── phase-03-repository.md
│       ├── phase-04-base-entities.md
│       ├── phase-05-migration.md
│       ├── phase-06-sales.md
│       ├── phase-07-inventory-ledger.md
│       ├── phase-08-purchase-fifo.md
│       ├── phase-09-supplier-finance.md
│       ├── phase-10-vip-card.md
│       ├── phase-11-store-transfer.md
│       └── phase-12-control-audit-v1.md
│
├── web/                    # Web 版本（Python Flask）
│   ├── app.py              # Flask 应用主程序
│   ├── start.bat           # Windows 启动脚本
│   ├── README.md           # Web 版说明文档
│   ├── instance/           # 数据库目录
│   │   └── supermarket.db  # SQLite 数据库
│   └── templates/          # HTML 模板
│
├── data/                   # 数据目录（纯文本存储，运行时自动创建）
│   ├── employee.txt        # 员工信息
│   ├── product.txt         # 商品信息
│   ├── sales.txt           # 销售记录
│   ├── member.txt          # 会员信息
│   ├── promotion.txt       # 促销活动
│   └── store.txt           # 门店信息
│
├── output/                 # 输出目录（小票、报表）
│   └── receipt_*.txt       # 销售小票文件
│
├── README.md               # 说明文档
├── .gitignore              # Git 忽略规则
└── .clang-format           # 代码格式化配置
```

## 快速开始

### C 终端版

#### 编译

**使用 Makefile（推荐，支持增量编译）**

先复制 `config.mk.example` 为 `config.mk`，并设置本机的 AbyssDB 源码路径：

```make
ABYSS_ROOT := C:/path/to/AbyssDB
```

```bash
make
make test-phase2
make test-phase3
make test-phase4
make test-phase5
make test-phase6
make test-phase7
make test-phase8
make test-phase9
make test-phase10
make test-phase11
make test-phase12
```

**Windows (MinGW)**

```batch
build.bat
```

`build.bat` 统一调用 Makefile，可附加 `ABYSS_ROOT=...` 参数。

**Linux / macOS**

```bash
chmod +x build.sh
./build.sh
```

`build.sh` 统一调用 Makefile，可附加 `ABYSS_ROOT=...` 参数。

#### 运行

```bash
# Windows
supermarket.exe

# Linux/macOS
./supermarket
```

#### 清理编译产物

```bash
make clean
```

### Web 版

```bash
# 进入 web 目录
cd web

# 安装依赖
pip install flask

# 启动服务
python app.py
```

或双击 `web/start.bat` 启动，然后浏览器访问 http://127.0.0.1:5000

### 首次使用

1. 编译并运行程序（或启动 Web 版）
2. 系统自动检测并创建默认管理员：
   - **用户名**: `admin`
   - **密码**: `admin123`
3. 首次登录后请立即修改密码

## 订单编号机制

销售订单采用 AbyssDB 中的持久化编号系统：

- **事务计数器**：订单和明细 ID 在数据库写事务中分配
- **迁移水位**：首次导入旧记录时保留原 ID，并推进计数器到历史最大值之后
- **重启安全**：编号水位随数据库恢复，不扫描文本文件
- **不会重叠**：取消或删除订单不会回退持久化计数器

## 技术特性

| 特性 | 说明 |
|------|------|
| 分层架构 | domain → storage → repo → app，职责清晰 |
| AbyssDB 存储 | B+ 树引擎，支持事务、原子写入、文件锁 |
| 持久化订单编号 | 事务 counter + 迁移水位，重启后不会重复 |
| 原子性写入 | 单个 UOW 同时提交订单、库存和会员状态 |
| 文件锁机制 | 支持 Windows/Linux 多平台并发控制 |
| 密码安全 | 盐值 + SHA256 哈希存储 |
| 事务日志 | 完整的操作审计追踪 |
| 增量编译 | Makefile 支持仅重编译修改过的文件 |
| 12 阶段测试 | 每层独立测试，覆盖 storage → repo → entity → migration → business |

## UI 特性

系统采用自定义 UI 模块，提供专业美观的交互界面：

### 边框样式

```
╔════════════════════════════════╗
║         标题文字               ║
╚════════════════════════════════╝
```

### 表格展示

```
┌──────────┬──────────┬──────────┐
│ 列标题1  │ 列标题2  │ 列标题3  │
├──────────┼──────────┼──────────┤
│ 数据1    │ 数据2    │ 数据3    │
└──────────┴──────────┴──────────┘
```

### 颜色提示

- `GREEN` - 操作成功
- `RED` - 错误提示
- `YELLOW` - 警告信息
- `CYAN` - 导航提示
- `BLUE` - 标题信息

### 安全输入

- 整数/浮点数输入带范围验证
- 密码输入隐藏显示
- 操作确认二次验证
- 分页数据导航

## 数据存储

### C 终端版

核心运行时数据存储在 `data/supermarket.abdb`。下列 `|` 分隔文本仅供尚未迁移模块使用，或作为已迁移模块的首次导入来源：

| 文件 | 说明 |
|------|------|
| employee.txt | 员工信息 |
| product.txt | 商品信息 |
| sales.txt | 销售记录 |
| pending_sales.txt | 待支付销售记录 |
| purchase.txt / purchase_item.txt | 旧采购数据，仅首次迁移输入 |
| batch.txt | 旧批次数据，仅首次迁移输入 |
| promotion.txt | 促销活动 |
| member.txt | 会员信息 |
| vipcard.txt / vipcard_trans.txt | 旧储值卡及交易，仅首次迁移输入 |
| store.txt | 门店信息 |
| store_stock.txt | 门店库存 |
| transfer.txt | 调拨单 |
| supplier.txt | 供应商信息 |
| supplier_finance.txt / payable.txt / payment_record.txt | 旧供应商财务数据，仅首次迁移输入 |
| schedule.txt | 排班记录 |
| combo.txt | 套装信息 |
| combo_item.txt | 套装商品 |
| stock_log.txt | 旧库存日志，仅首次迁移输入 |
| transaction_log.txt | 事务日志 |

### Web 版

使用 SQLite 数据库，数据文件保存在 `web/instance/supermarket.db`

## 开发说明

### 编译参数

```c
-std=c99       // 使用 C99 标准
-Wall          // 启用所有警告
-Wextra        // 启用额外警告
-Wpedantic     // 启用严格标准检查
-O2            // 优化级别 2
-flto          // 链接时优化
```

### 源码规模

| 目录/文件 | 代码行数 | 说明 |
|-----------|---------|------|
| main.c | 3,016 | 主程序、菜单、业务流程 |
| marketing.c | 2,655 | 营销模块（促销、会员） |
| store_ops.c | 1,800 | 门店模块（调拨、套装） |
| supermarket.c | 1,279 | 核心实现（哈希表、文件IO） |
| ui.c | 862 | UI 界面模块 |
| utility.c | 776 | 工具函数 |
| report.c | 719 | 报表模块 |
| sale.c | 722 | 销售模块 |
| purchase.c | 453 | 采购模块 |
| finance.c | 444 | 财务模块 |
| schedule.c | 390 | 排班模块 |
| supermarket.h | 1,024 | 合并头文件 |
| ui.h | 321 | UI 头文件 |
| hash.h | 46 | 哈希表头文件 |
| **业务层小计** | **~15,000** | |
| | | |
| domain/ | 424 | 领域实体定义 |
| storage/ | 1,139 | AbyssDB 存储引擎 |
| repo/ | 5,939 | 仓储层（UOW、codec、CRUD） |
| migration/ | 2,404 | 数据迁移层 |
| app/ | 3,792 | 应用服务层 |
| tests/ | 2,537 | 12 阶段测试 |
| **架构层小计** | **~16,200** | |
| | | |
| **合计** | **~33,300** | |

### 架构分层

```
┌─────────────────────────────────────────┐
│              main.c (UI 入口)            │
├─────────────────────────────────────────┤
│  app/      应用服务层 (业务编排、UOW)    │
├─────────────────────────────────────────┤
│  repo/     仓储层 (CRUD、codec、索引)    │
├─────────────────────────────────────────┤
│  domain/   领域实体 (数据结构定义)        │
├─────────────────────────────────────────┤
│  storage/  存储引擎 (AbyssDB B+ 树)     │
├─────────────────────────────────────────┤
│  migration/ 数据迁移 (旧文件 → AbyssDB)  │
└─────────────────────────────────────────┘
```

### 主要数据结构

- **哈希表**：用于快速查找（商品ID/条码、员工ID、会员手机号）
- **链表**：用于存储同类记录集合
- **结构体**：模块化数据封装
- **AbyssDB B+ 树**：持久化键值存储，支持事务和原子写入

### 文件同步策略

1. 修改内存数据
2. 写入临时文件 `.tmp`
3. `rename()` 原子替换原文件

## AbyssDB 集成状态

12 阶段 C 语言重构已完成。员工、商品、供应商、会员、销售、库存台账、批次、采购、供应商财务、储值卡、门店、调拨、促销、商品套装、排班、日结和审计记录均使用 `data/supermarket.abdb` 作为运行时权威存储。

当前代码通过 `ABYSS_ROOT` 指向 AbyssDB 源码目录，编译并链接其公开 C API。为保持可复现性，集成时应使用 AbyssDB 的已发布版本或记录明确的提交号。

旧版业务文本文件仅作为首次迁移输入。正常应用的创建、更新、删除、加载、保存、报表和备份路径不会重写它们。导入前验证完整源数据集，在一个事务中执行迁移，保留遗留 ID，推进计数器水位线，提交后仅写入持久的每阶段完成标记。

详见 `docs/abyss-refactor/phase-11-store-transfer.md` 和 `docs/abyss-refactor/phase-12-control-audit-v1.md`。

## 许可证

本项目使用 [MIT License](LICENSE)。项目所使用的 AbyssDB、Python 依赖及其他第三方组件仍适用各自的许可证。
