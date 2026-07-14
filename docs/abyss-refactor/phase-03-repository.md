# 阶段 3：Repository 与索引一致性

## 交付范围

阶段 3 提供所有实体仓储共用的工作单元、schema、counter、索引和范围扫描能力。现有员工、商品、销售等业务入口仍未切换到数据库；实体编码和具体 CRUD 从阶段 4 开始实现。

## 模块

- `repo/sm_repository.*`：repository 生命周期和 schema 版本门禁。
- `sm_repo_uow`：显式 read/write 工作单元，commit/rollback 消费句柄。
- 唯一索引：claim/release 协议，value 保存完整 primary key。
- 非唯一索引：primary key 进入 index key，value 使用固定 marker。
- 持久化 counter：在调用方 write UOW 内分配，与实体写入一起提交。
- `sm_repo_scan`：封装 storage snapshot cursor，提供半开范围和前缀扫描。

## 原子索引协议

创建记录时，同一 write UOW 内依次写入主记录、claim 全部唯一索引、添加全部非唯一索引，最后 commit。任一 claim 冲突时整个 UOW rollback。

更新唯一字段时，先 claim 新索引，再 release 旧索引，最后更新主记录。claim 同一 primary key 是幂等成功；claim 到其他 primary key 返回 `SM_REPO_CONFLICT`。

release 会验证索引当前 owner。索引不存在或 owner 不匹配返回 `SM_REPO_ERR_CORRUPT`，避免在不变量已损坏时继续写入。

删除记录时，同一 UOW 内 release 唯一索引、删除非唯一索引、删除主记录。阶段 4 的实体仓储负责提供完整索引集合。

## Schema 与 Counter

- 新库第一次以读写模式打开时写入 `SM_NS_SCHEMA_VERSION`。
- 只读打开缺少 schema 的数据库会失败。
- schema 版本不完全匹配时返回 `SM_REPO_SCHEMA_MISMATCH`，不自动迁移。
- counter value 保存 next id，首次分配返回 1。
- counter 增长属于调用方事务；rollback 后 ID 可以再次分配。
- 达到 `UINT64_MAX` 时返回 storage full，禁止回绕。

## 扫描生命周期

- scan 创建当前 generation 的 snapshot，并持有到 scan close。
- scan 范围是 `[start, end)`；prefix scan 用字典序 successor 生成 end。
- key/value 是 cursor 借用内存，只在下一次移动或 close 前有效。
- scan 存活期间 repository/store close 返回 busy。
- scan 创建后发生的提交不会进入该 scan，新 scan 可以看到新 generation。

## 阶段 3 验收

- [x] 新库 schema 初始化，版本不匹配和只读缺失均拒绝。
- [x] counter commit 后持久化，rollback 后不消耗 ID。
- [x] 唯一索引冲突不会留下主记录或部分索引。
- [x] 唯一索引换键和 owner 校验通过。
- [x] 非唯一前缀扫描只返回目标范围。
- [x] snapshot scan 隔离后续提交，并正确 pin 生命周期。
- [x] repository 活动 UOW/scan 阻止 close。
- [x] 重启、只读打开和 AbyssDB full verify 通过。
- [x] 阶段 2、阶段 3 测试和完整应用干净构建通过。

## 阶段 4 前置条件

阶段 4 为 Employee、Product、Supplier、Member 和 SystemConfig 分别实现版本化 codec 与实体 repository。业务层只调用实体仓储，不直接调用本文件中的 raw key/value 方法。
