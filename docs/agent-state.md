# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 03:55（R20.06 闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.07 GameSession（plan §29.3 R20.07：固定 Tick / 暂停 / BreakBlock / PlaceBlock / WorldDelta 五项迁移入 GameSession）
current_task_status: READY
last_completed_task: R20.06 Command/Event/Snapshot 立类型（src/Core/command.h + event.h + snapshot.h 三枚 Core 叶子头 + result.h 增补 kErrQueueFull/QObjectFree；三队列单线程最小语义；生产调用点零迁移——plan §29.3 R20.06 原文「先只定义数据结构和队列」）
last_task_closure_commit: 本 docs 提交（代码终态 = feat(r2006) 三头一增补 + test(r2006) section10 三腿）
last_verified_commit: test(r2006)（矩阵 552 PASS / 0 FAIL ×2：matrix_r2006_pos/final.log，EXIT=0；filter 面 r2006=3P/0F；worldgen 数值头逐位恒等；549 权威 PASS 行 diff 仅 4 处登记漂移类（t813 戳/t979/t997 计时/t1023c 文件计数 129→132=本单 3 新头，线程原语仍 0）；app 重建 EXIT=0 + 冒烟 EXIT=124 存活 + logs/voxelsandbox_r2006_tail20.log）
last_governance_review: 2026-09-14（audit #6 YELLOW → GOV-20260914-1=t1049 已闭环；R20.03 为 R20 首单，治理计数从 0 起算）
governance_review_due: false
completed_tasks_since_governance_review: 3
next_task: R20.07 GameSession 最小游戏循环封装（plan §29.3 序）
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.07
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.06 代码终态（feat + test 已提交；docs 提交随后），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑧全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训）。**R20.03 起矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（秒级）+ `--filter <substring>` 只跑本任务腿，**禁再往单文件堆腿**；新腿落对应段文件（任务族分段见段文件头注释），新段须 ≤500KB。**R20.05 起新段置尾先例**：section09_foundations → section10_command_event_snapshot 在 runAll 末执行；section10 起类型段「rig 世界零接触」先例（纯类型/队列行为面，零残留由构造保证）。
- **R20.06 起值类型纪律**：src/Core/ 新值类型一律 result.h QObjectFree 编译期钉 + 头内 static_assert（「摘即红」类型层化）；命令/事件队列满载拒绝（kErrQueueFull）与快照队列覆盖最老是刻意的两域容量策略分化，勿「统一」。

## Recovery Point

- 最近闭环：**R20.06 Command/Event/Snapshot**（2026-09-15，feat(r2006) + test(r2006) + 本 docs）：① command.h（CommandKind Break/Place + Command 值类型[plan §4.2 必带六项落位] + breakBlock/placeBlock 工厂 + CommandQueue FIFO 256 满载拒绝）② event.h（EventKind 7 席 + Event + WorldDelta 64 容量 chunk 集幂等去重 + EventQueue 同门）③ snapshot.h（BlockEdit + WorldSnapshot 自持改动面[拷贝即深拷贝] + SnapshotQueue 容 4 环形覆盖最老）④ result.h 增补（kErrQueueFull=101 错误码域首枚 + QObjectFree concept 编译期钉权威）。矩阵 549→552（r2006a-c 三腿，section10 新段置尾，rig 零接触）；**worldgen 数值头逐位恒等**；549 权威 PASS 行 diff 仅 4 处已登记漂移类。阴性轮豁免（纯加类型登记）：替代证据 = 新腿全绿 + QObject 编译期摘即红 + worldgen 恒等。**过程坑登记**：r2006c 方向二首红为腿自身——副本期望态须显式逐字段钉（对 baseline 比较会把方向一刻意的副本改动误判串扰）；冒烟 stdout 0 字节 = GUI 子系统既录坑，tail20 从 app 自写 logs/voxelsandbox.log 取。修腿红利 ~5s/轮（section10 单段 + filter 面）。
- R20.05 基础类型（2026-09-15，a2c1bb9 feat + 4cdf51c test + docs）：src/Core/mathtypes.h + result.h 两枚 Core 叶子（floorDiv/floorMod / ChunkKey / BlockPos / Tick / Error+Result 雏形），只立类型 + 最小示范采用（chunkmanager 路由 + worldclock 声明点 + BlockPos 加性重载）；矩阵 545→549；worldgen 逐位恒等 + 545 行仅 3 处登记漂移；阴性轮豁免三件替代证据。
- R20.03 测试分层（2026-09-15，09b8cb7 + 3daab66 + docs）：单 TU 3.4MB/-j1 ~85min 拆 tools/matrix/ 八段 + --filter SKIP 记账；545/0 ×2；全量冷编 2m4s / 单段增量秒级 / filter 面 1.6s。
- t1049 GOV-20260914-1 纠偏闭环（2026-09-14，48c2f09 + 3351a91 + docs）：review26-1 翻红根因 = 腿跨段 mob 泄漏 + t1045 踩踏 RNG，非 UB。
- R19.23 批次全貌：t1038→t1040→t1039→t1042→t1041→t1043→t1047→t1044→t1045→t1046→t1048→t1049，矩阵 521→545。
- **下一最小动作**：R20.07 GameSession（R20 主线；把 QML 直接编排的最小游戏循环包起来——第一步只迁移固定 Tick / 暂停 / BreakBlock / PlaceBlock / WorldDelta，验收「无头程序可执行一段游戏 Tick + QML 经 Adapter + 新旧路径结果一致」）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→GOV-20260912-1 已闭环。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
