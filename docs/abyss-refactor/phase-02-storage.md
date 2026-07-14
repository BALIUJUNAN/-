# 阶段 2：AbyssDB 存储基础设施

## 交付范围

阶段 2 建立可编译、可测试的存储边界，不迁移具体业务实体，也不改变现有文本运行路径。阶段 3 起 repository 只能通过这些接口访问 AbyssDB。

## 模块

- `storage/sm_store.*`：数据库生命周期、事务、状态映射、错误诊断、返回值释放和完整性验证。
- `storage/sm_key.*`：有界二进制 key builder、big-endian 整数、长度前缀字符串/字节串、UTF-8 校验和 prefix successor。
- `storage/sm_namespace.h`：应用 namespace 的唯一分配表。
- `storage/sm_codec.*`：版本化 TLV value writer/reader，提供长度、字段数、顺序、wire type、UTF-8 和截断校验。
- `tests/test_storage_phase2.c`：key 顺序、codec round-trip/损坏输入、事务 commit/rollback、完整校验和只读重启测试。

## 构建

AbyssDB 作为源码依赖单独使用 C17 编译；超市业务代码继续使用 C99。这样不混用 MSVC `.lib` 和 MinGW 对象，也不会复制引擎源码。

本机在 `config.mk` 中配置：

```make
ABYSS_ROOT := D:/ZHIJIGozgewan/TYMK
```

`config.mk` 被 Git 忽略。其他环境复制 `config.mk.example`，或直接执行：

```sh
make ABYSS_ROOT=/path/to/AbyssDB
make test-phase2 ABYSS_ROOT=/path/to/AbyssDB
```

## 所有权与事务规则

- `sm_store_open` 创建的 handle 必须由 `sm_store_close` 释放。
- `sm_store_txn_commit` 和 `sm_store_txn_rollback` 都消费事务 handle，并将调用方指针置空。
- `sm_store_txn_get` 返回的 value 必须用 `sm_store_value_free` 释放。
- repository 的写操作必须显式开启 write transaction；不提供隐式 commit 入口。
- 只读 store 在 `begin(write)` 时立即拒绝；同一 store 同时只允许一个 write transaction。
- store 存在活动事务时，`sm_store_close` 返回 busy，不释放底层数据库。
- `sm_store_close` 失败时保留 handle，调用方可以读取错误并重试。
- codec 输出由标准 `free` 释放；写入 AbyssDB 后引擎持有自己的副本。

## namespace 纠正

集成测试确认 AbyssDB 保留首字节 `0x01-0x2f`，用户结构化 key 必须从 `ABYSS_NAMESPACE_USER_MIN` (`0x30`) 开始。阶段 1 原先从 `0x01` 分配应用 key 会导致重启加载时被过滤，现已纠正：超市应用统一使用 `0x40-0xef`。

这个约束同时进入 AbyssDB 公开头文件和 README，不再依赖内部常量。

## 阶段 2 验收

- [x] 存储层不向业务暴露 AbyssDB handle 和 allocator 所有权。
- [x] key 数值顺序与字典序一致，并能构造精确范围上界。
- [x] codec 不序列化 struct/padding，拒绝乱序字段、截断、超长和非法 UTF-8。
- [x] write transaction commit 后重启可读。
- [x] rollback 数据重启后不存在。
- [x] 只读写入、双 writer 和活动事务期间 close 均被拒绝。
- [x] 只读打开和完整数据库校验通过。
- [x] Makefile、Windows 和 POSIX 构建入口统一。
