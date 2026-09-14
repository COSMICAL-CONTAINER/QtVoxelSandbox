# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 04:40（R20.07 闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.08 WorldFacade（plan §29.3 R20.08：World 查询/写入接口收窄——新代码不再直取 Chunk 内部指针、规则走统一查询、Renderer 不持可变 World、旧 World 仍为 Implementation）
current_task_status: READY
last_completed_task: R20.07 GameSession 最小游戏循环编排壳（src/Game/gamesession.h header-only：stepTick 固定 Tick[整数 ms 累积器 + 13-tick 家族 Main.qml 同序] / pause-resume 停表无欠账 / enqueueCommand(Break/Place) 经 R20.06 队列整 tick 边界委托 World::setBlock 权威 / WorldDelta+BlockChanged 事件面；QML 零迁移——Main.qml 不挂 GameSession）
last_task_closure_commit: 本 docs 提交（代码终态 = feat(r2007) gamesession.h + test(r2007) section11 四腿）
last_verified_commit: test(r2007)（矩阵 556 PASS / 0 FAIL ×2：matrix_r2007_pos/final.log，EXIT=0；filter 面 r2007=4P/0F；worldgen 腿族逐位恒等；552 权威 PASS 行 diff = 4 新增 + 4 处登记漂移类（t813 戳/t830 采样 40→41/t997 计时/t1023c 文件计数 132→133=本单 1 新头）；app 重建 EXIT=0 + 冒烟 EXIT=124 存活 + logs/voxelsandbox_r2007_tail20.log）
last_governance_review: 2026-09-14（audit #6 YELLOW → GOV-20260914-1=t1049 已闭环；R20.03 为 R20 首单，治理计数从 0 起算）
governance_review_due: false
completed_tasks_since_governance_review: 4
next_task: R20.08 WorldFacade（plan §29.3 序）
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.08
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.07 代码终态（feat + test 已提交；docs 提交随后），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑧全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训）。**R20.03 起矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（秒级）+ `--filter <substring>` 只跑本任务腿，**禁再往单文件堆腿**；新腿落对应段文件（任务族分段见段文件头注释），新段须 ≤500KB。**R20.05 起新段置尾先例**：section09_foundations → section10_command_event_snapshot → section11_gamesession 在 runAll 末执行；section10 起类型段「rig 世界零接触」先例；**section11 起「自建 fresh 小世界」先例**（48×48×96 seed 82 四 setter 同构 incantation ×N 双生——不与 rig 共享写状态；双生 parity 探针须同 seed 同尺寸 + 天气双钉 setWeatherState(0)+setWeatherRemainingSec(3600) 隔离运行期 RNG）。
- **R20.06 起值类型纪律**：src/Core/ 新值类型一律 result.h QObjectFree 编译期钉 + 头内 static_assert（「摘即红」类型层化）；命令/事件队列满载拒绝（kErrQueueFull）与快照队列覆盖最老是刻意的两域容量策略分化，勿「统一」。
- **R20.07 起编排壳纪律**：GameSession 只做编排（命令 → 整 tick 边界 → 委托 World::setBlock 权威），**禁复制游戏逻辑**（掉落/成就等玩家域留 PlayerController/后续 intent 层）；命令执行只允许发生在 stepTick 整 tick 边界（r2007d「旁路即红」常驻探针守卫）；WorldDelta 仅覆盖带坐标语义事件（blockBroken/blockPlaced），静默写收口归 R20.09 EditBuffer；**QML 现行玩法路径零变化是 R20 主线不变量**——Main.qml 含 "GameSession" 即违零迁移阴性钉（r2007b 常驻）。

## Recovery Point

- 最近闭环：**R20.07 GameSession**（2026-09-15，feat(r2007) + test(r2007) + 本 docs）：src/Game/gamesession.h header-only 编排壳（Q_OBJECT/AUTOMOC 经矩阵源清单入册）——① stepTick(deltaSecs) 固定 Tick（整数 ms 累积器 qRound(dt×1000)；N×stepTick(0.1)==stepTick(N×0.1)==N tick；tick 体=Main.qml onTicked 桥接 13-tick 家族逐行同序[顺序 indexOf 源码钉]）② pause/resume 幂等 + 停表 dt 丢弃无欠账（与 WorldClock 停表同门；两闸并存登记）③④ enqueueCommand(Break/Place) 经 CommandQueue 在整 tick 边界到期 drain（targetTick 门控 + 未到期原序回队）→ **委托 World::setBlock「写栅格的唯一入口」权威**（零逻辑复制；PlayerController 前门无头不可达已登记留 intent 层）⑤ WorldDelta（blockBroken/blockPlaced 带坐标事件 → ChunkKey::fromWorld+Chunk::kSize 幂等入集）+ BlockChanged 事件入 EventQueue（丢弃可见计数器）+ tickCompleted(tick,delta) 信号。矩阵 552→556（r2007a-d 四腿，section11 新段置尾，自建 48×48×96 s82 双生 fresh 小世界）；**worldgen 腿族逐位恒等**；552 权威 PASS 行 diff 仅 4 处登记漂移类。验收三条全断言：无头 300-tick 连跑 / QML 零迁移阴性钉（Main.qml 无 GameSession）/ 旧新一致全栅格逐位+delta chunk 集等价。阴性轮=「旁路即红」常驻探针（入队未泵世界零变化）+ 源码钉，变异构建式豁免（编译失败天然承担）。**过程坑登记**：r2007b 首跑 1 红为腿自身——0.25s 泵 50ms 余量漏算 → 余量链显式重推复跑即绿；矩阵日志 grep 须 -a（CR/非 UTF-8 既录坑复认）。
- R20.06 Command/Event/Snapshot（2026-09-15，feat + test + docs）：command.h/event.h/snapshot.h 三 Core 叶子 + result.h 增补（kErrQueueFull + QObjectFree）；三队列两域容量策略分化；矩阵 549→552；rig 零接触先例。
- R20.05 基础类型（2026-09-15，a2c1bb9 feat + 4cdf51c test + docs）：src/Core/mathtypes.h + result.h 两枚 Core 叶子（floorDiv/floorMod / ChunkKey / BlockPos / Tick / Error+Result 雏形），只立类型 + 最小示范采用；矩阵 545→549；worldgen 逐位恒等 + 545 行仅 3 处登记漂移；阴性轮豁免三件替代证据。
- R20.03 测试分层（2026-09-15，09b8cb7 + 3daab66 + docs）：单 TU 3.4MB/-j1 ~85min 拆 tools/matrix/ 八段 + --filter SKIP 记账；545/0 ×2；全量冷编 2m4s / 单段增量秒级 / filter 面 1.6s。
- t1049 GOV-20260914-1 纠偏闭环（2026-09-14，48c2f09 + 3351a91 + docs）：review26-1 翻红根因 = 腿跨段 mob 泄漏 + t1045 踩踏 RNG，非 UB。
- R19.23 批次全貌：t1038→t1040→t1039→t1042→t1041→t1043→t1047→t1044→t1045→t1046→t1048→t1049，矩阵 521→545。
- **下一最小动作**：R20.08 WorldFacade（R20 主线；World 查询/写入接口收窄——新代码不再直取 Chunk 内部指针、World 规则统一查询、Renderer 不持可变 World、旧 World 暂仍为 Implementation）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→GOV-20260912-1 已闭环。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
