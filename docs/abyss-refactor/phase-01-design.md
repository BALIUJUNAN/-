# C 版 AbyssDB 重构：阶段 1 设计基线

## 范围与结论

本重构只覆盖 C 终端版。`web/`、Flask 和 SQLite 不在范围内，也不作为兼容目标。

重构完成后，AbyssDB 是运行时唯一事实来源。进程内哈希表和链表只能作为可丢弃缓存；`data/*.txt` 只能用于一次性导入、显式导出和回滚工具，业务代码不得直接读写它们。

## 当前代码基线

- 基线提交：`ff5e0a6149dea88c9332e407a71051337a296a57`
- 分支：`main`
- 基线包含尚未提交的用户修改，重构不得覆盖或回退这些修改。
- 2026-07-13 使用 GCC 13.2.0、C99、`-Wall -Wextra -Wpedantic` 对全部 C 源文件执行独立输出编译，结果通过且无警告。
- 当前 `data/` 目录为空，因此迁移测试必须使用专门构造的 fixture，不能把空目录当作真实数据迁移验证。

当前程序由 `main.c` 编排业务流程，各业务模块同时承担领域逻辑、全局内存状态和文本持久化：

- `supermarket.c`：员工、商品、供应商、系统配置。
- `marketing.c`：会员、促销、套装、批次、储值卡及交易。
- `sale.c`：挂单、销售、库存日志。
- `purchase.c`：采购、采购明细、事务日志。
- `schedule.c`：排班。
- `finance.c`：日结，并直接扫描销售文本。
- `store_ops.c`：门店、门店库存、调拨、供应商财务、应付与付款。
- `report.c`、`utility.c`：直接解析销售、销售明细和库存日志文本生成统计。

因此迁移不能只替换 `load_*` / `save_*`；报表和仪表盘的只读路径也必须进入 repository 层。

## 目标依赖方向

```text
main/ui
  -> service (业务用例和事务边界)
    -> repository (实体与查询接口)
      -> storage (AbyssDB 生命周期、事务、游标、编码)
        -> AbyssDB

legacy_text -> migration -> repository
repository -> export -> text/csv/html
```

约束：

- UI 不得调用 AbyssDB 或拼接存储 key。
- service 不得解析文本文件。
- repository 不得包含菜单、打印或交互逻辑。
- storage 不得依赖带链表指针的运行时结构。
- 主记录和所有二级索引必须在同一次 commit 中更新。
- 运行时不得在数据库失败后静默回退到文本文件。

## 持久化模型清单

`Cart`、`CartItem`、`PromotionResult`、`DiscountInfo`、`PrinterConfig`、`Receipt` 和 `ReceiptItem` 是运行时或输出模型，不作为数据库主记录。所有持久化编码都忽略链表 `next` 指针。

| 聚合 | 主记录 | 旧文本来源 | 必需索引/查询 | 迁移阶段 |
|---|---|---|---|---|
| 系统 | SystemConfig、ID counters、schema version | `config.txt` 及启动扫描 | counter kind | 4 |
| 员工 | Employee | `employee.txt` | id、active、role | 4 |
| 商品 | Product、Category | `product.txt` | id、barcode、category、supplier、low-stock | 4 |
| 供应商 | Supplier | `supplier.txt` | id、name | 4 |
| 会员 | Member、MemberConsumeRecord | `member.txt` | id、phone、level | 4/7 |
| 销售 | Sale、SaleItem、pending state | `sales.txt`、`sale_item.txt`、`pending_sales.txt` | id、status、cashier、member、time | 5 |
| 库存 | StockLog | `stock_log.txt` | product+time、type+time | 7 |
| 采购 | Purchase、PurchaseItem | `purchase.txt`、`purchase_item.txt` | id、status、supplier、time | 8 |
| 排班 | Schedule | `schedule.txt` | employee+year+week | 7 |
| 促销 | Promotion | `promotion.txt` | status+time、product、priority | 7 |
| 套装 | ProductCombo、ComboItem | `combo.txt`、`combo_item.txt` | id、barcode、status | 7 |
| 批次 | Batch | `batch.txt` | batch_no、product+expiry、product+received | 8 |
| 日结 | DailySettlement | `daily_settlement.txt` | cashier+date、status+date | 7 |
| 门店 | Store、StoreStock | `store.txt`、`store_stock.txt` | id、name、store+product | 6 |
| 调拨 | TransferOrder、TransferItem | `transfer.txt`、`transfer_item.txt` | id、status、from/to store | 6 |
| 供应商财务 | SupplierFinance、Payable、PaymentRecord | `supplier_finance.txt`、`payable.txt`、`payment_record.txt` | supplier、status、due date、payable | 9 |
| 储值卡 | VipCard、VipCardTransaction | `vipcard.txt`、`vipcard_trans.txt` | card_no、member、status、card+time | 10 |
| 审计 | TransactionLog | `transaction.log` | type+time、operator+time | 7 |

## AbyssDB 键规范

所有 key 都是二进制字节串：

```text
[namespace:u8][component...]
```

- 每一种主记录和索引使用独立 namespace，范围扫描直接限制 namespace，不在业务层事后过滤混合记录。
- 无符号整数使用 big-endian，保证字典序等于数值顺序。
- 字符串组件使用 `[length:u16-be][utf8 bytes]`，不依赖 `NUL` 终止符。
- 时间使用 `u64-be` Unix seconds；需要稳定顺序时追加主键。
- 索引 key 包含被索引字段和主键；唯一索引 value 保存主键，非唯一索引 value 为空。
- 金额不使用 `float` 排序或持久化，阶段 3 统一转换为整数分。

namespace 分配：

AbyssDB 保留 `0x01-0x2f`。应用 namespace 从 `0x40` 开始，和
`ABYSS_NAMESPACE_USER_MIN` (`0x30`) 保持安全间隔。

| 范围 | 用途 |
|---|---|
| `0x40-0x4f` | schema、迁移状态、counter、系统配置 |
| `0x50-0x5f` | 员工 |
| `0x60-0x6f` | 商品、分类、批次 |
| `0x70-0x7f` | 供应商和供应商财务 |
| `0x80-0x8f` | 会员和储值卡 |
| `0x90-0x9f` | 销售和销售明细 |
| `0xa0-0xaf` | 库存日志、门店库存和调拨 |
| `0xb0-0xbf` | 采购和采购明细 |
| `0xc0-0xcf` | 促销和套装 |
| `0xd0-0xdf` | 排班和日结 |
| `0xe0-0xef` | 审计日志 |

第一版核心键表：

| ns | 名称 | key components | value |
|---|---|---|---|
| `40` | schema/version | 无 | codec/schema version |
| `41` | migration/state | 无 | source fingerprint、状态、时间 |
| `42` | counter | counter kind | next id |
| `43` | system/config | 无 | SystemConfig |
| `50` | employee/by-id | employee_id | Employee |
| `51` | employee/by-role | role, employee_id | empty |
| `52` | employee/by-status | status, employee_id | empty |
| `60` | product/by-id | product_id | Product |
| `61` | product/by-barcode | barcode | product_id |
| `62` | product/by-category | category_id, product_id | empty |
| `63` | product/by-supplier | supplier_id, product_id | empty |
| `64` | product/low-stock | low_stock flag, product_id | empty |
| `65` | category/by-id | category_id | Category |
| `66` | batch/by-no | batch_no | Batch |
| `67` | batch/by-product-expiry | product_id, expiry, batch_no | empty |
| `68` | batch/by-product-received | product_id, received_at, batch_no | empty |
| `70` | supplier/by-id | supplier_id | Supplier |
| `71` | supplier/by-name | normalized name, supplier_id | empty |
| `72` | supplier-finance/by-supplier | supplier_id | SupplierFinance |
| `73` | payable/by-id | payable_id | Payable |
| `74` | payable/by-status-due | status, due_at, payable_id | empty |
| `75` | payable/by-supplier | supplier_id, payable_id | empty |
| `76` | payment/by-id | payment_id | PaymentRecord |
| `77` | payment/by-payable | payable_id, paid_at, payment_id | empty |
| `78` | payable/by-purchase | purchase_id | payable primary key |
| `80` | member/by-id | member_id | Member |
| `81` | member/by-phone | phone | member_id |
| `82` | member/by-level | level, member_id | empty |
| `83` | member-consume/by-member | member_id, time, record_id | MemberConsumeRecord |
| `84` | vip-card/by-no | card_no | VipCard |
| `85` | vip-card/by-member | member_id, card_no | empty |
| `86` | vip-card/by-status | status, card_no | empty |
| `87` | vip-tx/by-card-time | card_no, time, tx_id | VipCardTransaction |
| `88` | vip-tx/by-id | tx_id | VipCardTransaction |
| `90` | sale/by-id | sale_id | Sale |
| `91` | sale-item/by-sale | sale_id, item_id | SaleItem |
| `92` | sale/by-status | status, sale_id | empty |
| `93` | sale/by-time | completed_at, sale_id | empty |
| `94` | sale/by-cashier-time | cashier_id, completed_at, sale_id | empty |
| `95` | sale/by-member-time | member_id, completed_at, sale_id | empty |
| `a0` | stock-log/by-id | log_id | StockLog |
| `a1` | stock-log/by-product-time | product_id, time, log_id | empty |
| `a2` | stock-log/by-type-time | type, time, log_id | empty |
| `a3` | store/by-id | store_id | Store |
| `a4` | store/by-name | normalized name, store_id | empty |
| `a5` | store-stock/by-store-product | store_id, product_id | StoreStock |
| `a6` | transfer/by-id | transfer_id | TransferOrder |
| `a7` | transfer-item/by-transfer | transfer_id, item_id | TransferItem |
| `a8` | transfer/by-status | status, transfer_id | empty |
| `a9` | transfer/by-store | store_id, direction, transfer_id | empty |
| `b0` | purchase/by-id | purchase_id | Purchase |
| `b1` | purchase-item/by-purchase | purchase_id, item_id | PurchaseItem |
| `b2` | purchase/by-status | status, purchase_id | empty |
| `b3` | purchase/by-supplier-time | supplier_id, created_at, purchase_id | empty |
| `c0` | promotion/by-id | promotion_id | Promotion |
| `c1` | promotion/by-product | product_id, priority, promotion_id | empty |
| `c2` | promotion/by-active-time | active flag, start_at, promotion_id | empty |
| `c3` | combo/by-id | combo_id | ProductCombo |
| `c4` | combo-item/by-combo | combo_id, item_id | ComboItem |
| `c5` | combo/by-barcode | barcode | combo_id |
| `d0` | schedule/by-id | schedule_id | Schedule |
| `d1` | schedule/by-employee-week | employee_id, year, week | schedule_id |
| `d2` | settlement/by-id | settlement_id | DailySettlement |
| `d3` | settlement/by-cashier-date | cashier_id, business_date | settlement_id |
| `e0` | audit/by-id | log_id | TransactionLog |
| `e1` | audit/by-time | time, log_id | empty |
| `e2` | audit/by-operator-time | operator_id, time, log_id | empty |
| `e3` | audit/by-type-time | type, time, log_id | empty |

## 值编码规范

值使用版本化、字段化的二进制格式：

```text
[codec_version:u8]
[entity_type:u8]
[field_count:u16-be]
repeated [field_id:u16-be][wire_type:u8][length:u32-be][payload]
```

- 未知 field_id 必须可跳过，便于向前兼容。
- 缺失可选字段使用默认值；缺失必需字段返回损坏数据错误。
- 解码器限制最大字段数、最大字符串长度和总 value 长度。
- 解码到业务结构时显式设置 `next = NULL`。
- 禁止直接 `memcpy(struct)`，避免 ABI、对齐、端序和 padding 问题。

repository 使用 `abyss_cursor_open_range` 构造 `[namespace+prefix, namespace+prefix_end)` 范围。例如会员销售查询只扫描 `0x95 + member_id + [start,end)`，采购状态查询只扫描 `0xb2 + status`。

## 必须原子提交的业务边界

### 完成销售

- Sale 从 pending 变为 completed，写入 SaleItem。
- 扣减 Product、Batch 或 StoreStock。
- 更新 Member 积分/消费统计。
- 可选扣减 VipCard 并写 VipCardTransaction。
- 写 StockLog、TransactionLog 和全部销售索引。

### 采购收货

- 更新 Purchase 状态和实收数量。
- 增加 Product、Batch 或 StoreStock。
- 创建或更新 Payable/SupplierFinance。
- 写 StockLog、TransactionLog 和索引。

### 门店调拨

- 更新 TransferOrder 状态。
- 同时更新源门店和目标门店库存。
- 写 TransferItem、StockLog、TransactionLog 和索引。

### 储值卡交易

- 更新 VipCard 余额和累计金额。
- 写 VipCardTransaction，必要时关联 Sale/PaymentRecord。
- 写 TransactionLog 和索引。

## ID、缓存与迁移规则

- ID 由持久化 counter 分配，不再扫描全库寻找最大值。
- counter 增加和新记录写入处于同一提交；允许跳号，禁止重复。
- 商品 `P0001` 可保留为展示 ID，数据库 key 使用长度明确的字节串。
- 销售单号保证唯一且单调递增，不承诺崩溃后绝不跳号。
- repository 缓存必须可丢弃，启动不得全量加载所有值模拟数据库。
- 列表、报表和历史查询使用范围 cursor，不得先复制全库再过滤。
- `legacy_text/` 只负责解析旧格式并写入新库，或从新库显式导出。
- 迁移状态和源数据 fingerprint 写入数据库；重复迁移必须拒绝，不能重复追加。

## 阶段 1 完成标准

- [x] C 版在现有用户修改上可无警告编译。
- [x] 盘点持久化实体、文本文件和直接读取点。
- [x] 定义目标分层、键空间、值编码和关键事务边界。
- [x] 明确旧文本只作为迁移/导出边界。
- [ ] 阶段 2 创建可编译的 `storage/` 基础设施并链接 AbyssDB。

后续每迁移一个聚合，都必须同时交付编解码 round-trip、损坏输入、主键 CRUD、二级索引、范围查询、重启一致性、原子提交和旧文本 fixture 导入测试。
