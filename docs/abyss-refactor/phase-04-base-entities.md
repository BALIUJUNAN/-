# 阶段 4：基础实体仓储

## 交付范围

阶段 4 为 `Employee`、`Product`、`Supplier`、`Member` 和
`SystemConfig` 建立与旧哈希表、链表及文本格式无关的持久化模型。每个实体都有
版本化 codec、主键 CRUD、完整二级索引和范围查询。

本阶段不让 repository 依赖 `supermarket.h`。旧结构中的 `float` 金额、
`time_t` 和 `Member.next` 不进入磁盘格式：金额统一使用整数分，时间统一使用
`int64_t` Unix 秒，链表指针完全忽略。

## 模块

- `domain/sm_base_entities.h`：五类基础持久化实体及稳定字段容量。
- `repo/sm_base_codec.*`：版本 1 TLV codec，要求必需字段存在并验证类型、长度、
  UTF-8、实体类型和截断输入；未知的未来字段可由底层 reader 跳过。
- `repo/sm_base_repository.*`：实体 CRUD、唯一查询、索引范围查询和配置单例。
- `storage/sm_key.*`：增加有边界检查的复合 key reader。
- `sm_repo_scan_get`：使用 scan 持有的同一个 snapshot 回表，避免跨 generation
  读取主记录。

## 索引规则

- Employee：`id` 主记录，`role + id`、`status + id` 非唯一索引。
- Product：`id` 主记录，barcode 唯一索引，category、supplier、low-stock
  非唯一索引。low-stock 索引同时记录 0/1 状态，查询只扫描状态 1。
- Supplier：`id` 主记录，规范化名称加 `id` 的非唯一索引。规范化只裁剪首尾
  ASCII 空白并折叠 ASCII 大小写，不修改 UTF-8 非 ASCII 字节。
- Member：`id` 主记录，phone 唯一索引，`level + id` 非唯一索引。
- SystemConfig：固定单例 key，不使用扫描寻找配置。

创建、更新和删除只在调用方提供的 write UOW 中执行。主记录与全部索引使用
同一次 commit；函数失败后调用方必须 rollback。employee、supplier、member ID
和 product 展示 ID 都来自阶段 3 的事务型 counter。

## 查询一致性

二级索引查询先创建 snapshot cursor，再使用该 cursor 的 snapshot 读取主记录。
查询会验证索引 key 中的字段、主键和解码后的主记录一致。孤立索引、错指 owner
或字段不一致均返回 `SM_REPO_ERR_CORRUPT`，不会静默过滤。

## 阶段 4 验收

- [x] 五类 codec round-trip、错误实体类型、错误 wire type 和截断输入测试。
- [x] 五类主记录创建、读取、更新、删除和重启一致性。
- [x] product barcode、member phone 唯一冲突可整体回滚。
- [x] role/status/category/supplier/low-stock/name/level 索引范围查询。
- [x] 更新索引字段时移除旧索引并建立新索引。
- [x] 物理删除同步清理唯一和非唯一索引。
- [x] counter 回滚不消耗 ID；配置单例持久化。
- [x] AbyssDB full verify、阶段 2/3/4 测试和完整应用构建通过。

## 后续边界

旧 `data/*.txt` 仍是当前业务入口的数据源。本阶段没有加入双写或静默回退。
下一阶段通过独立 migration/service 边界执行一次性导入并切换员工、商品、供应商、
会员和系统配置业务入口；切换完成后运行时不再直接读写这些文本文件。
