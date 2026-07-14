# 阶段 10：储值卡

## 目标

将 VipCard 和 VipCardTransaction 迁移到 AbyssDB，取消销售流程中的“先扣卡、
销售失败再退款”补偿方案。

## 已实现

- 卡余额、累计充值额和交易金额统一以整数分持久化。
- 卡号主键、会员索引、状态索引和卡号+时间交易索引随主记录原子维护。
- 充值、消费、退款同时更新卡余额并创建不可变交易流水。
- 储值卡销售在一个 UOW 中提交 Sale、库存/批次、StockLog、会员积分、卡余额和 VipCardTransaction。
- 库存不足、卡状态异常、过期或余额不足时整笔销售不产生任何部分写入。
- 旧公开 C API 已改为 AbyssDB 服务适配器；`vipcard.txt` 和 `vipcard_trans.txt` 只作为首次迁移输入。

## 验收

- 编解码、索引、counter、状态变更和 reopen 一致。
- 销售失败不扣余额、不写卡流水；成功只产生一条消费流水。
- 迁移采用独立 `{migration namespace, 0x0a}` 标记，坏引用整批回滚。

## 边界

交易审计和日结已在阶段 12 进入 TransactionLog 与 DailySettlement 聚合。
