# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 12:10（t1050 review0915 两项纠偏闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1051（Review_2026-09-13 #1：矿井干燥门缺支撑校验→悬空轨，worldgen 面改动会移位生成腿——独立成单全矩阵核移位）；完成后回主线 R20.10 Chunk 生命周期
current_task_status: READY
last_completed_task: t1050（Review_2026-09-15 两项纠偏一单两修：① gamesession.h stepTick 入口双钳制[方案①：负 dt/NaN→零累积、dt>kMaxStepSecs(1.0s) 整体丢弃+qWarning+droppedDtCount 背压可见——qRound 溢出洞结构性焊死]；② playercontroller useBlock 12 门收窄 sneakPlaceBlock[手持方块才旁路放置；空手/持非方块 sneak 右键机关/门/活板门/床/七 UI 族照常交互——MC wiki "use prioritizes held item" 引证入 dev-plan 关单]；矩阵 r2007e + t1050a-d 共 5 新腿 + 5 源码钉同步）
last_task_closure_commit: docs(plan)（代码终态 = fix(t1050) + test(t1050)，哈希见 git log）
last_verified_commit: test(t1050)（矩阵 569 PASS / 0 FAIL ×2：matrix_t1050_pos/final.log EXIT=0；564 权威 diff = 5 新增[t1050a-d + r2007e] + 3 登记漂移类[t813 戳/t997 计时/t979 涉水；t1023c 零漂移=本单无新文件]；filter 面 t1050=5P/0F、r2007=5P、t1034=3P、t1046 面、t1028=3P 全绿；阴性轮 neg1 摘负 dt 门→恰红 r2007e→Edit 反向还原→回绿、neg2 摘合取项→恰红 t1050a-d 零误伤→还原→回绿；app 重建 EXIT=0 + 冒烟 EXIT=124 + tail20 零错误）
last_governance_review: 2026-09-15（audit #7 GREEN——R20 地基 5 闭环全绿放行；Mimosa ENOBUFS 环境注记在案）
governance_review_due: false
completed_tasks_since_governance_review: 2
next_task: t1051（review0913 #1 悬空轨）→ 之后 R20.10 Chunk 生命周期
next_task_source: docs/Review_2026-09-13.md #1（Review_2026-09-15 清偿状态表维持登记）+ docs/refactor-plan-2026-09-08.md §29.3 R20.10
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

- 最近闭环：**t1050 review0915 两项纠偏**（2026-09-15，fix(t1050) + test(t1050) + 本 docs）：① 修1（Review0915 #1，中）src/Game/gamesession.h stepTick 入口双钳制（方案①，选型依据在头注——方案②留 qRound 溢出洞）：负 dt/NaN→零累积（`!(deltaSecs>=0.0)` 一并拦）、dt>kMaxStepSecs(1.0s) 整体丢弃+qWarning+droppedDtCount（超界=异常墙钟差不 catch-up——与暂停「dt 丢弃不欠账」同门）；r2007e 四柱腿（3600 丢弃+计数 / 丢弃后泵不受污染 / 负 dt 无负时间债 / 恰上界 1.0s=10 tick）。② 修2（Review0915 #2，低）src/Game/playercontroller.cpp useBlock 12 门收窄 `sneakPlaceBlock = sneakPlace && m_selectedBlock != Air`（selectedBlock 经 hotbar 非方块槽已归 Air→判据恰为「手持方块」）：空手/持非方块 sneak 右键拉杆扳动/按钮按/门开合/活板门翻板/床入睡/七 UI 族/音符盒调音照常交互（51cc43c③ 回归收口）；MC 三元组引证入 dev-plan 关单（wiki Sneaking "use prioritizes held item"/Java 12w49a/minecraft.wiki 2026-09-15 实读）；t1034/t1046 持方块腿逐位兼容零改写、5 源码钉针句同步、旧口径注释改写；t1050a-d 四族腿各配持方块对照柱 + t1050a 判据合取钉。③ 矩阵 564→569；阴性轮×2 恰红/还原存证 matrix_t1050_neg{1,2}.log + restore。**登记剩余待办**：Review0915 #4（嵌格生物=R20 walk 登记维持）/#5/#7（维持登记）/#6（顺手修搭下一笔 entitymanager）+ 箱子矿车裸键门残余（空手 sneak 同疾，随下一波 parity 单）+ **Review_2026-09-13 #1（矿井干燥门缺支撑校验→悬空轨）仍待修 → 下一单 t1051**（worldgen 面改动会移位生成腿，独立成单全矩阵核移位，不混入本单）。
- R20.09 EditBuffer（2026-09-15，fix(r2009) + test(r2009) + docs c760606）：src/Core/editbuffer.h（DirtyChunkSet + EditBuffer）+ GameSession 首消费方 + Review0915 #3① Command.blockState 五参权威；矩阵 560→564；section13 四腿。World/QML/PlayerController/ChunkGeometry 零触碰。
- R20.08 WorldFacade（2026-09-15，f0517c6/30efc3b/6d7c649）：WorldFacade 收窄视图 + GameSession/ChunkGeometry 双示范迁移，矩阵 556→560。
- R20.07 GameSession（2026-09-15，7ca99e1/cc33c69/cf63c07）：编排壳五项迁移，矩阵 552→556。
- R20.06 Command/Event/Snapshot（2026-09-15）：三 Core 叶子 + 三队列两域容量分化；549→552。
- R20.05 基础类型（2026-09-15，a2c1bb9/4cdf51c）：mathtypes.h + result.h；545→549。
- R20.03 测试分层（2026-09-15，09b8cb7/3daab66）：tools/matrix/ 八段 + --filter；545/0；全量冷编 2m4s。
- t1049 GOV-20260914-1（2026-09-14）：review26-1 = 腿跨段泄漏 + 踩踏 RNG，非 UB。
- R19.23 批次全貌：t1038→…→t1049，矩阵 521→545。
- **下一最小动作**：t1051（review0913 #1 悬空轨——worldgen 面独立成单，全矩阵核生成腿移位；worldstore mineshaft placeMineshaft 干燥门补支撑校验 + P-t1043b 悬空轨==0 针）→ 之后 R20.10 Chunk 生命周期（治理计数 2/5）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→闭环、#6（09-14）YELLOW→GOV-20260914-1 闭环、**#7（09-15）GREEN**（R20 地基 5 闭环放行，计数重算）；此后新闭环计数：R20.09 = 1/5、t1050 = 2/5。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
