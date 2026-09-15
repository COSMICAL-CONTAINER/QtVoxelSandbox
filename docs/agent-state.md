# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 09:55（R20.08 闭环 + 治理审计 #7 GREEN）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.09 EditBuffer（plan §29.3 R20.09：一次 Tick 内方块修改合并成 WorldDelta——多次相邻编辑不产生重复通知、规则终态与旧实现一致、DirtyChunkSet 可被测试、mesh 调度不再直绑每一次 setBlock；gamesession 静默写收口正席）
current_task_status: READY
last_completed_task: R20.08 WorldFacade（src/World/worldfacade.h header-only 收窄视图：查询面 12 入口 + chunk 网格三门查询 + 命令语义写 setBlock/setBlockWithState 四转发，非 QObject 纯值语义包装；GameSession 命令写/回读迁移 + ChunkGeometry myChunk() 退役 = mesher 零 Chunk*；World 类零改动、QML 零迁移；实现由后台 agent 起头、网络断链后主控盘点续完成）
last_task_closure_commit: 6d7c649 docs(plan)（代码终态 = fix(r2008) f0517c6 + test(r2008) 30efc3b）
last_verified_commit: test(r2008) 30efc3b（矩阵 560 PASS / 0 FAIL ×2：matrix_r2008_pos/final.log EXIT=0；556 权威 diff = 4 新增 r2008a-d + 5 登记漂移类[t813 戳/t830 采样/t979 涉水计数 16→15/t997 计时/t1023c 文件数 133→134]；filter 面 r2008=4P/0F、r2007=4P/0F；阴性轮 matrix_r2008_neg.log 变异旁路→恰红 r2008c[正面钉 x0<2 + command-bypass 反探双红因]→Edit 反向还原→回绿；app 重建 EXIT=0 + 冒烟 EXIT=124 + tail20 零错误）
last_governance_review: 2026-09-15（audit #7 GREEN——R20 地基 5 闭环全绿放行；Mimosa ENOBUFS 环境注记在案）
governance_review_due: false
completed_tasks_since_governance_review: 0
next_task: R20.09 EditBuffer
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.09
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.08 代码终态（fix + test 已提交；docs 提交随后），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑨全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训；分段后单段增量 -j 4 实测安全）。**矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（秒级）+ `--filter <substring>` 只跑本任务腿；新腿落对应段文件，新段置尾 runAll 末执行、须 ≤500KB；section11 起「自建 fresh 小世界」先例（48×48×96 seed 82 + 天气双钉 setWeatherState(0)+setWeatherRemainingSec(3600)）。
- **R20.06 起值类型纪律**：src/Core/ 新值类型一律 result.h QObjectFree 编译期钉 + 头内 static_assert；命令/事件队列满载拒绝与快照队列覆盖最老是两域容量策略分化，勿「统一」。
- **R20.07 起编排壳纪律**：GameSession 只做编排（命令 → 整 tick 边界 → World::setBlock 权威），禁复制游戏逻辑；**QML 现行玩法路径零变化是 R20 主线不变量**（Main.qml 含 "GameSession" 即违零迁移阴性钉 r2007b）。
- **R20.08 起 facade 纪律**：新代码世界读写统一走 WorldFacade（收窄视图）；mesher 零 Chunk*（chunkgeometry 禁出反探常驻：myChunk/chunks().chunk/Chunk* 剥注释零命中）；GameSession 命令零旁路（m_world.setBlock 禁出 + m_facade.setBlock ×2 正面钉，r2008c 常驻）；Facade 不转发静默写族（收窄=不新增旁路，静默族收口归 R20.09 EditBuffer）。
- **探针钉纪律（R20.08 新教训）**：pinSet 失败条件 cnt<minCount → **0 计数 SrcPin 恒不红（空转钉）**；「禁出」钉必须走 minCount=1 反探（miss 非空=合规，复用剥注释器防注释提及误伤）或腿体内手工 contains（r2007b 先例）；新钉上 workload 前先做「变异→恰红」自证。
- **腿选址纪律（R20.08 新教训）**：涉 chunk 计数断言的腿，选址必须**结构性保证**跨 chunk（cx 分驻不同值），不靠地形扫描碰运气；「破/放目标」须显式验证非空气/空气（空操写不发作会塌事件计数）；腿 diag 带判别信息（pre/post id、逐事件、chunk 键）。

## Recovery Point

- 最近闭环：**R20.08 WorldFacade**（2026-09-15，fix(r2008) + test(r2008) + 本 docs）：① src/World/worldfacade.h——World 收窄视图（查询面 blockAt/stateAt/isSolidAt/isCollidableAt/isFullCubeAt/skyLightAt/blockLightAt/heightmapAt/heightAt/biomeIdAt/weatherStateAt/isPrecipitatingAt + chunk 网格门 chunkExistsAt/chunkDirtyAt/chunkFluidOnlyDirtyAt + 写入面 setBlock×2/setBlockWithState×2；非 QObject 纯转发零逻辑复制；public 面零 Chunk* 外泄）。② 双示范迁移：gamesession.h（m_facade 成员；命令写 ×2 + noteEdit 回读走 Facade；tick 泵家族仍直调 m_world——选型登记）+ chunkgeometry.{h,cpp}（myChunk() 退役，三门帮手经 WorldFacade(*m_world) 委托，每次现查不缓存指针防悬空口径保持；buildMesh 存在门 chunkExists）。③ section12_worldfacade.cpp 四腿（r2008a 查询等价全网格 13.8k 格 / r2008b 写入等价双生[返回值逐笔+全栅格+三信号] / r2008c 结构钉+禁出反探+mesher 脏门端到端 / r2008d 会话回归+OOB 拒+旧路径对照柱）；section11 委托钉同步 m_facade.setBlock ×2；CMakeLists/matrix_helpers 注册接线。矩阵 556→560；世界类零改动（r2007 全腿常驻全绿）。
- R20.07 GameSession（2026-09-15，7ca99e1/cc33c69/cf63c07）：编排壳五项迁移，矩阵 552→556。
- R20.06 Command/Event/Snapshot（2026-09-15）：三 Core 叶子 + 三队列两域容量分化；549→552。
- R20.05 基础类型（2026-09-15，a2c1bb9/4cdf51c）：mathtypes.h + result.h；545→549。
- R20.03 测试分层（2026-09-15，09b8cb7/3daab66）：tools/matrix/ 八段 + --filter；545/0；全量冷编 2m4s。
- t1049 GOV-20260914-1（2026-09-14）：review26-1 = 腿跨段泄漏 + 踩踏 RNG，非 UB。
- R19.23 批次全貌：t1038→…→t1049，矩阵 521→545。
- **下一最小动作**：R20.09 EditBuffer（治理审计 #7 已 GREEN 放行）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→闭环、#6（09-14）YELLOW→GOV-20260914-1 闭环、**#7（09-15）GREEN**（R20 地基 5 闭环放行，计数重算）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
