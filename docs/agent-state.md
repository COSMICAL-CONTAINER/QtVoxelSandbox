# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 13:40（t1051 review0913 #1 矿井悬空轨清偿闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.10 Chunk 生命周期（refactor-plan §29.3：Absent/Loading/Generated/Active/Loaded/Evicting 六态；固定 10×10 世界仍可运行、Chunk 可卸载重载）
current_task_status: READY
last_completed_task: t1051（Review_2026-09-13 #1 矿井悬空轨清偿：placeMineshaft 干燥门内 isTopFlushSupport 支撑校验（处方式，400 语料零过滤=契约面+防御完备）+ pruneUnsupportedWorldgenRails carveCanyon 后置守卫（真实机制清偿——400 世界 sweep + 逐 pass 桩归因：悬空轨产生者是峡谷掏空已铺轨地板，seed 42/166/207 共 7 根，非 review 假设的同矿延迟落块形态）；P-t1051a 27 世界悬空轨==0 腿 + 三源钉；顺手修 review0915 #5 filtered 汇总行；MC wiki Rail 三元组引证入 dev-plan）
last_task_closure_commit: docs(plan)（代码终态 = fix(t1051) + test(t1051)，哈希见 git log）
last_verified_commit: test(t1051)（矩阵 570 PASS / 0 FAIL ×2：matrix_t1051_pos/final.log EXIT=0；569 权威 diff = +1 新增[t1051a] + 2 登记漂移类[t813 戳/t997 计时]，worldgen 腿零改写零未归因漂移；filter 面 t1051=1P、t1043=4P、mineshaft=27P、t1020=4P、t1014=8P、t929=1P、canyon=1P、t1003=1P、t1013=9P、t1011=1P、determin=30P、re-gen=5P 全绿；阴性轮 neg 摘 prune 调用点→悬空轨 7 根精确重现→t1051a 独红 525 SKIP 零误伤→还原→回绿[存证 matrix_t1051_neg.log + restore]；app 重建 EXIT=0 + 冒烟 EXIT=124 + tail20 零错误）
last_governance_review: 2026-09-15（audit #7 GREEN——R20 地基 5 闭环全绿放行；Mimosa ENOBUFS 环境注记在案）
governance_review_due: false
completed_tasks_since_governance_review: 3
next_task: R20.10 Chunk 生命周期 → 之后 R20.11 后台 GenerationJob
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.10（review0913 #1/#2/#3/#4、review0915 #4/#7、箱车裸键门残余维持登记）
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
- **探针钉纪律（R20.08 新教训）**：pinSet 失败条件 cnt<minCount → **0 计数 SrcPin 恒不红（空转钉）**；「禁出」钉必须走 minCount=1 反探（miss 非空=合规，复用剥注释器防注释提及误伤）或腿体内手工 contains（r2007b 先例）；新钉上 workload 前先做「变异→恰红」自证；**minCount 对实际出现次数核，勿拍脑袋翻倍**（r2009d x1<2 误设教训）。**阴性变异选址（t1051 新教训）**：`if (<cond>) continue;` 形态的守卫，摘守卫用 `false &&` 前缀 = 把守卫变**无条件执行**（t1051 首版变异把「摘无撑轨」变「摘光全部轨」，rails 31537→7 假红形态）——正确阴性 = 摘**调用点**（`if (false) guard();`）；行为面发现力靠语料内嵌复现体 seed（t1051a 内嵌 42/166/207）。
- **腿选址纪律（R20.08/R20.09 新教训）**：涉 chunk 计数断言的腿，选址必须**结构性保证**跨 chunk（cx 分驻不同值），不靠地形扫描碰运气；「破/放目标」须显式验证非空气/空气，**破位还须验证顶上无附着**（破表面块会级联清除其上 TallGrass——r2009c chg=4 首红实证，空操/级联都会塌事件计数）；腿 diag 带判别信息（pre/post id、逐事件、chunk 键）。

## Recovery Point

- 最近闭环：**t1051 review0913 #1 矿井悬空轨清偿**（2026-09-15，fix(t1051) + test(t1051) + 本 docs）：① 修1（处方式）src/World/world.cpp placeMineshaft 干燥门内加一行 `if (!BlockRegistry::isTopFlushSupport(m_chunks.blockAt(rx, ry-1, rz), m_chunks.stateAt(rx, ry-1, rz))) continue;`（与 t733 失撑坍落 / 门族放置同一单一权威谓词）——400 语料实测过滤数 0（review 假设的同矿延迟落块形态未触发），保留 = 契约面 + 防御完备。② 修2（真实机制清偿）新增 `pruneUnsupportedWorldgenRails()`：全图扫 Rail、正下方非齐平支撑 → 摘轨回「少一段轨」无害终态；位序 carveCanyon 后、pruneFloatingSnowLayers 毗邻（t716 ③ 同款 carve 类后置守卫先例）；只判 Rail 单 id。**机制归因修正**：400 世界 sweep + worldgen 逐 pass 计数桩实证悬空轨产生者 = carveCanyon 掏空已铺轨地板（seed 42/166/207 各 3/2/2 根共 7，跳变点精确落在 carveCanyon；要塞 Phase A 壳覆盖/Phase C 掏刻同域结构上产不出悬空轨）；MC wiki Rail 三元组引证（顶面带沿块才可铺 / 失撑掉自身 / Infdev 20100618 / minecraft.wiki 2026-09-15 实读）入 dev-plan。③ P-t1051a（section08）：27 世界（12 t1043b 同源 + 3 复现体 + 12 扩面）悬空轨==0 行为半 + world.cpp 三钉（gate/prune/prune-call）；顺手修 review0915 #5 filtered 汇总行（matrix_helpers.cpp，filter 模式 `=== filtered: legFilter=<s>, ran N legs ===`，零头文件改动）。④ 矩阵 569→570；阴性轮摘 prune 调用点→7 根精确重现→t1051a 独红零误伤→还原回绿（matrix_t1051_neg.log + restore）。**登记剩余待办**：箱子矿车裸键门残余（随下一波 parity）+ Review0915 #4/#7 维持登记 / #6 顺手修搭下一笔 entitymanager + review0913 #2/#3/#4 维持登记（#5 已顺手修）。
- t1050 review0915 两项纠偏（2026-09-15，fix + test + docs 4172927）：stepTick dt 入口双钳制（方案①+droppedDtCount）+ 空手潜行右键交互 MC 口径恢复（sneakPlaceBlock 手持方块判据）；矩阵 564→569；阴性轮×2 存证。
- R20.09 EditBuffer（2026-09-15，fix(r2009) + test(r2009) + docs c760606）：src/Core/editbuffer.h（DirtyChunkSet + EditBuffer）+ GameSession 首消费方 + Review0915 #3① Command.blockState 五参权威；矩阵 560→564；section13 四腿。World/QML/PlayerController/ChunkGeometry 零触碰。
- R20.08 WorldFacade（2026-09-15，f0517c6/30efc3b/6d7c649）：WorldFacade 收窄视图 + GameSession/ChunkGeometry 双示范迁移，矩阵 556→560。
- R20.07 GameSession（2026-09-15，7ca99e1/cc33c69/cf63c07）：编排壳五项迁移，矩阵 552→556。
- R20.06 Command/Event/Snapshot（2026-09-15）：三 Core 叶子 + 三队列两域容量分化；549→552。
- R20.05 基础类型（2026-09-15，a2c1bb9/4cdf51c）：mathtypes.h + result.h；545→549。
- R20.03 测试分层（2026-09-15，09b8cb7/3daab66）：tools/matrix/ 八段 + --filter；545/0；全量冷编 2m4s。
- t1049 GOV-20260914-1（2026-09-14）：review26-1 = 腿跨段泄漏 + 踩踏 RNG，非 UB。
- R19.23 批次全貌：t1038→…→t1049，矩阵 521→545。
- **下一最小动作**：R20.10 Chunk 生命周期（refactor-plan §29.3：Absent/Loading/Generated/Active/Loaded/Evicting 六态；固定 10×10 世界仍可运行、Chunk 可卸载重载；治理计数 3/5）→ 之后 R20.11 后台 GenerationJob。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→闭环、#6（09-14）YELLOW→GOV-20260914-1 闭环、**#7（09-15）GREEN**（R20 地基 5 闭环放行，计数重算）；此后新闭环计数：R20.09 = 1/5、t1050 = 2/5、t1051 = 3/5。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
