# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 03:05（R20.05 闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.06（Command/Event/Snapshot 立类型——plan §29.3 R20.06；编号正名后）
current_task_status: READY
last_completed_task: R20.05 基础类型（floorDiv/floorMod、ChunkKey、BlockPos、Tick、Error/Result 只立类型 + 最小示范采用。编号正名：plan 原文 R20.04=测试矩阵拆分已由 R20.03 实质覆盖，本单实为 plan §29.3 R20.05；原 dev-plan 本地序列「R20.05 Command/Event/Snapshot」「R20.06 GameSession」顺次正名为 plan R20.06/R20.07）
last_task_closure_commit: 4cdf51c（代码终态 = a2c1bb9 feat + 4cdf51c test + 本 docs）
last_verified_commit: 4cdf51c（矩阵 549 PASS / 0 FAIL ×2：matrix_r2005_pos/final.log，EXIT=0；filter 面 r2005=4P/501S 1.7s；worldgen 数值头逐位恒等 + 545 权威 PASS 行仅 3 处登记漂移（t813 戳/t997 计时/t1023c 文件计数 127→129=本单两枚新头）；app 重建 EXIT=0 + 冒烟 EXIT=124 存活 + logs/voxelsandbox_r2005_tail20.log）
last_governance_review: 2026-09-14（audit #6 YELLOW → GOV-20260914-1=t1049 已闭环；R20.03 为 R20 首单，治理计数从 0 起算）
governance_review_due: false
completed_tasks_since_governance_review: 2
next_task: R20.06 Command/Event/Snapshot 立类型（plan §29.3 序）
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.06
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.05 代码终态（4cdf51c test；docs 提交随后），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑧全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训）。**R20.03 起矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（12-19s）+ `--filter <substring>` 只跑本任务腿（秒级），**禁再往单文件堆腿**；新腿落对应段文件（任务族分段见段文件头注释），新段须 ≤500KB。**R20.05 起新段置尾先例**：section09_foundations 在 runAll 末执行，世界基线面零接触（setBlock 即写即还原本格），不与原八段共享 rig 写状态。

## Recovery Point

- 最近闭环：**R20.05 基础类型**（2026-09-15，a2c1bb9 feat + 4cdf51c test + 本 docs）：src/Core/mathtypes.h + result.h 两枚 Core 叶子头（floorDiv/floorMod 负坐标单一权威 / ChunkKey 有符号键（packed=quint32 补码拼接 int32 域双射、hash=hashMix64 双射无盐）/ BlockPos 6 邻+运算 / Tick kClockTickMs=100 / Error+Result<T>+Result<void> 雏形），**只立类型 + 最小示范采用**（chunkmanager 路由 floorDiv/floorMod×3 + ChunkKey::flatIndex×2、worldclock.h kTickMs 声明点回指、BlockPos 加性重载 ×5 非 Q_INVOKABLE）；其余 6 处路由点未迁移（登记后续面）。矩阵 545→549（r2005a-d 四腿，section09 新段置尾）；**worldgen 腿族逐位恒等**（worldgen 数值头 diff 权威=空；545 PASS 行仅 t813 戳/t997 计时/t1023c 文件计数 127→129 三处登记漂移）。阴性轮重构类豁免（登记 dev-plan）：以「新腿全绿 + r2005d 摘即红源码钉（pinSet：floorDiv(×6/floorMod(×4/ChunkKey×2/Tick::kClockTickMs×1）+ worldgen 恒等」三件替代。**过程坑登记**：r2005c 首跑 2 红均为腿自身——manhattanLength 期望笔误（53≠13）+ rig 外 (90,41,170) worldgen 山体可达 y=41 致写同 id 合法拒绝（sb=0/w=3 实锤）→ 交换式「读原 id→写异 id→还原本格」。分层红利实收：section09 修腿→重编+链接+filter 面重跑全程 ~40s ×2 轮。
- R20.03 测试分层（2026-09-15，09b8cb7 + 3daab66 + docs）：单 TU 3.4MB/-j1 ~85min 拆 tools/matrix/ 八段 + --filter SKIP 记账；545/0 ×2；全量冷编 2m4s / 单段增量 12-19s / filter 面 1.6s。
- t1049 GOV-20260914-1 纠偏闭环（2026-09-14，48c2f09 fix + 3351a91 test + docs）：review26-1 翻红根因 = 腿跨段 mob 泄漏 + t1045 踩踏 RNG，非 UB；矩阵 545/0 ×3。
- R19.23 批次全貌：t1038→t1040→t1039→t1042→t1041→t1043→t1047→t1044→t1045→t1046→t1048→t1049，矩阵 521→545。
- **下一最小动作**：R20.06 Command/Event/Snapshot 立类型（R20 主线；先只定义数据结构和队列不大规模迁移）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→GOV-20260912-1 已闭环。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
