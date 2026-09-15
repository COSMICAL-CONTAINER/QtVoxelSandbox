# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 11:00（R20.09 EditBuffer 闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.10 Chunk 生命周期（plan §29.3 R20.10：Absent/Loading/Generated/Active/Loaded/Evicting 六态——固定 10×10 世界仍可运行、Chunk 可卸载重载、无未保存数据静默丢失、QML 不再决定 Chunk 真实生命周期）
current_task_status: READY
last_completed_task: R20.09 EditBuffer（src/Core/editbuffer.h header-only 值类型组件：DirtyChunkSet 独立可测集合[三态 add/幂等/插入序/64 容量] + EditBuffer 累积器[同格合并后写胜/三态 record/takeDelta 单点投影]；GameSession 首消费方——noteEdit→record 委托、Tick 内零通知、收口 takeDelta + 合并面派生事件、lastDirtyChunks 调度视图；Review_2026-09-15 #3① 顺带清偿 Command.blockState + PlaceBlock 五参权威；World/QML/PlayerController/ChunkGeometry 零触碰）
last_task_closure_commit: docs(plan)（代码终态 = fix(r2009) + test(r2009)，哈希见 git log）
last_verified_commit: test(r2009)（矩阵 564 PASS / 0 FAIL ×2：matrix_r2009_pos/final.log EXIT=0；560 权威 diff = 4 新增 r2009a-d + 4 登记漂移类[t813 戳/t997 计时/t979 涉水 16→15/t1023c 文件数 134→135 随新头]；filter 面 r2009=4P/0F、r2006=3P、r2007=4P、r2008=4P；阴性轮 matrix_r2009_neg.log 变异 takeDelta 收口注释→恰红 3 腿[r2009a 不误伤]→Edit 反向还原→回绿 matrix_r2009_restore.log；app 重建 EXIT=0 + 冒烟 EXIT=124 + tail20 零错误）
last_governance_review: 2026-09-15（audit #7 GREEN——R20 地基 5 闭环全绿放行；Mimosa ENOBUFS 环境注记在案）
governance_review_due: false
completed_tasks_since_governance_review: 1
next_task: R20.10 Chunk 生命周期
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.10
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.09 代码终态（fix + test 已提交；docs 提交随后），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑨全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训；分段后单段增量 -j 4 实测安全）。**矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（秒级）+ `--filter <substring>` 只跑本任务腿；新腿落对应段文件，新段置尾 runAll 末执行、须 ≤500KB；section11 起「自建 fresh 小世界」先例（48×48×96 seed 82 + 天气双钉 setWeatherState(0)+setWeatherRemainingSec(3600)）。
- **R20.06 起值类型纪律**：src/Core/ 新值类型一律 result.h QObjectFree 编译期钉 + 头内 static_assert；命令/事件队列满载拒绝与快照队列覆盖最老是两域容量策略分化，勿「统一」。
- **R20.07 起编排壳纪律**：GameSession 只做编排（命令 → 整 tick 边界 → World::setBlock 权威），禁复制游戏逻辑；**QML 现行玩法路径零变化是 R20 主线不变量**（Main.qml 含 "GameSession" 即违零迁移阴性钉 r2007b）。
- **R20.08 起 facade 纪律**：新代码世界读写统一走 WorldFacade（收窄视图）；mesher 零 Chunk*（chunkgeometry 禁出反探常驻）；GameSession 命令零旁路（m_world.setBlock 禁出反探 + m_facade.setBlock/setBlockWithState 正面钉，r2008c 常驻）；Facade 不转发静默写族。
- **R20.09 起编辑面纪律**：Tick 内编辑统一经 EditBuffer（src/Core/editbuffer.h）登记——同格同 Tick 合并后写胜、Tick 内零通知、收口 takeDelta 单点发布 + 合并面派生 BlockChanged；**dirty chunk 集合统一 DirtyChunkSet**（三态 add 可测；WorldDelta::addAffected 保守面不扩）；PlaceBlock 命令带 state 五参权威落地（4 参丢 state 口径退役）。ChunkGeometry/QML 渲染路径仍零触碰（按集合调度重建归 R20.13）；静默写族接线归 R20.10+。
- **探针钉纪律（R20.08 新教训）**：pinSet 失败条件 cnt<minCount → **0 计数 SrcPin 恒不红（空转钉）**；「禁出」钉必须走 minCount=1 反探（miss 非空=合规，复用剥注释器防注释提及误伤）或腿体内手工 contains（r2007b 先例）；新钉上 workload 前先做「变异→恰红」自证；**minCount 对实际出现次数核，勿拍脑袋翻倍**（r2009d x1<2 误设教训）。
- **腿选址纪律（R20.08/R20.09 新教训）**：涉 chunk 计数断言的腿，选址必须**结构性保证**跨 chunk（cx 分驻不同值），不靠地形扫描碰运气；「破/放目标」须显式验证非空气/空气，**破位还须验证顶上无附着**（破表面块会级联清除其上 TallGrass——r2009c chg=4 首红实证，空操/级联都会塌事件计数）；腿 diag 带判别信息（pre/post id、逐事件、chunk 键）。

## Recovery Point

- 最近闭环：**R20.09 EditBuffer**（2026-09-15，fix(r2009) + test(r2009) + 本 docs）：① src/Core/editbuffer.h——DirtyChunkSet（独立可测集合：Added/AlreadyDirty/Full 三态、插入序 at、64 容量 = WorldDelta::kMaxAffectedChunks 单一权威）+ EditBuffer（同格合并后写胜、Recorded/Merged/Overflowed 三态、1024 上界溢出可见、takeDelta const 幂等投影、负坐标 floorDiv 路由；落 Core 三依据见头注）。② GameSession 首消费方：noteEdit→m_edits.record 委托（Tick 内零通知）、runOneTick 收口 takeDelta + 合并面派生 BlockChanged、lastDirtyChunks() 调度视图、droppedEditCount 溢出可见。③ Review #3① 顺带清偿：command.h blockState 字段（工厂尾参缺省 0 向后兼容）+ executeCommand 转 setBlockWithState 五参权威。④ section13_editbuffer.cpp 四腿（r2009a 类型面直测 / r2009b 同格三连写合并+双生终态 parity / r2009c 跨 3 chunk 批量收口+集合面单源+零残留 / r2009d 源码钉+旧路径禁出反探+blockState 行为）；section11/12 旧钉同步（m_facade.setBlock ×2 前缀式 + setBlockWithState 钉 + Chunk::kSize 路由模长钉）；CMakeLists/matrix_helpers 注册接线。矩阵 560→564；World/QML/PlayerController/ChunkGeometry 零改动零触碰（r2006-r2008 全腿常驻全绿）。非目标登记：静默写族接线 R20.10+、集合调度重建 R20.13、ranges 化 R20.13、Review #1/#2 维持原登记。
- R20.08 WorldFacade（2026-09-15，f0517c6/30efc3b/6d7c649）：WorldFacade 收窄视图 + GameSession/ChunkGeometry 双示范迁移，矩阵 556→560。
- R20.07 GameSession（2026-09-15，7ca99e1/cc33c69/cf63c07）：编排壳五项迁移，矩阵 552→556。
- R20.06 Command/Event/Snapshot（2026-09-15）：三 Core 叶子 + 三队列两域容量分化；549→552。
- R20.05 基础类型（2026-09-15，a2c1bb9/4cdf51c）：mathtypes.h + result.h；545→549。
- R20.03 测试分层（2026-09-15，09b8cb7/3daab66）：tools/matrix/ 八段 + --filter；545/0；全量冷编 2m4s。
- t1049 GOV-20260914-1（2026-09-14）：review26-1 = 腿跨段泄漏 + 踩踏 RNG，非 UB。
- R19.23 批次全貌：t1038→…→t1049，矩阵 521→545。
- **下一最小动作**：R20.10 Chunk 生命周期（治理计数 1/5，audit #7 GREEN 放行后第 1 单）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→闭环、#6（09-14）YELLOW→GOV-20260914-1 闭环、**#7（09-15）GREEN**（R20 地基 5 闭环放行，计数重算）；此后新闭环计数：R20.09 = 1/5。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
