# 阶段 9：供应商财务

## 目标

将 SupplierFinance、Payable 和 PaymentRecord 迁移到 AbyssDB，并把采购收货、
应付生成和供应商汇总纳入同一个工作单元。

## 已实现

- 金额统一以整数分持久化，账期使用整数天。
- SupplierFinance 按 supplier_id 存储；Payable 提供状态、到期日、供应商和采购单索引；PaymentRecord 提供应付单时间索引。
- 采购收货同时提交商品、批次、库存流水、采购状态、应付单和供应商汇总。
- 单笔付款同时提交 PaymentRecord、Payable 状态和 SupplierFinance 汇总。
- 按供应商批量结算按到期日顺序处理，并在一个 UOW 中提交或回滚。
- 迁移会根据 PaymentRecord 重新核算 Payable 和 SupplierFinance，不信任旧文本中的陈旧汇总值。
- `supplier_finance.txt`、`payable.txt`、`payment_record.txt` 只作为阶段 9 首次迁移输入。

## 验收

- 编解码、主记录、索引、counter 与 reopen 一致。
- 采购收货自动生成且不能重复生成应付单。
- 部分付款、超额付款拒绝和批量结算保持原子性。
- 坏迁移输入不写数据或迁移标记，修复后可重试。

## 边界

TransactionLog、门店库存与日结已分别在阶段 11 和阶段 12 完成迁移。
