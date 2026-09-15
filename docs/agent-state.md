# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 14:45（R20.10 Chunk 生命周期闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.11 后台 GenerationJob 同步版 ChunkScheduler（refactor-plan §29.3：不引入多线程，统一请求/优先级/取消/结果模型——request ID、重复请求合并、过期请求丢弃、换 worker 不改调用者；收口后治理计数满 5 → 先治理审计 #8 再续）
current_task_status: READY
last_completed_task: R20.10（Chunk lifecycle 六态状态机：src/World/chunklifecycle.h 六态类型+转移表单一权威（8 合法边/28 非法含自转移）+ ChunkManager 侧表 m_lifecycle（recreate 全表 Loaded 默认稳态=常驻已加载）+ setLifecycle 唯一守卫式转移入口 + World::setChunkLifecycle C++ forwarder（非 Q_INVOKABLE，QML 零生命周期访问面）+ WorldFacade 三门叠加 chunkLifecycleQueryable（{Loaded,Active} 可见，卸载三门恒 false/重载恢复，ChunkGeometry 零改动）；Edits-on-evict 最小解释登记=Evicting 只表达状态可达不落盘驱逐[数据保留=后续单]；worldstore 直读 blob 豁免域不受态影响 r2010d 存①→卸载→存②→回读逐位恒等实证）
last_task_closure_commit: docs(plan)（代码终态 = fix(r2010) + test(r2010)，哈希见 git log）
last_verified_commit: test(r2010)（矩阵 574 PASS / 0 FAIL ×2：matrix_r2010_pos/final.log EXIT=0；570 权威 diff = +4 新增[r2010a-d] + 3 登记漂移类[t813 戳/t997 计时/t1023c 文件数 135→136]，零未归因漂移；filter 面 r2010=4P、r2007=5P、r2008=4P、r2009=4P、determin=30P、re-gen=5P、store=42P、save=34P 全绿；阴性轮 neg 摘 setLifecycle 转移守卫调用点→恰红 2 腿同源[r2010a 28 非法转移被接受 + r2010d 守卫钉 x0<1]→b/c 不误伤→还原→回绿[存证 matrix_r2010_neg.log + restore]；app 重建 EXIT=0 + 冒烟 EXIT=124 + tail20 零错误稳态 60fps）
last_governance_review: 2026-09-15（audit #7 GREEN——R20 地基 5 闭环全绿放行；Mimosa ENOBUFS 环境注记在案）
governance_review_due: false
completed_tasks_since_governance_review: 4
next_task: R20.11 后台 GenerationJob → 之后治理审计 #8（计数满 5）→ 再续
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.11（review0915 #4/#7、箱车裸键门残余维持登记）
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.10 代码终态（fix + test 已提交；docs 提交随后），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑨全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训；分段后单段增量 -j 4 实测安全）。**矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（秒级）+ `--filter <substring>` 只跑本任务腿；新腿落对应段文件，新段置尾 runAll 末执行、须 ≤500KB；section11 起「自建 fresh 小世界」先例（48×48×96 seed 82 + 天气双钉 setWeatherState(0)+setWeatherRemainingSec(3600)）。
- **R20.06 起值类型纪律**：src/Core/ 新值类型一律 result.h QObjectFree 编译期钉 + 头内 static_assert；命令/事件队列满载拒绝与快照队列覆盖最老是两域容量策略分化，勿「统一」。
- **R20.07 起编排壳纪律**：GameSession 只做编排（命令 → 整 tick 边界 → World::setBlock 权威），禁复制游戏逻辑；**QML 现行玩法路径零变化是 R20 主线不变量**（Main.qml 含 "GameSession" 即违零迁移阴性钉 r2007b）。
- **R20.08 起 facade 纪律**：新代码世界读写统一走 WorldFacade（收窄视图）；mesher 零 Chunk*（chunkgeometry 禁出反探常驻）；GameSession 命令零旁路（m_world.setBlock 禁出反探 + m_facade.setBlock/setBlockWithState 正面钉，r2008c 常驻）；Facade 不转发静默写族。
- **R20.09 起编辑面纪律**：Tick 内编辑统一经 EditBuffer（src/Core/editbuffer.h）登记——同格同 Tick 合并后写胜、Tick 内零通知、收口 takeDelta 单点发布 + 合并面派生 BlockChanged；**dirty chunk 集合统一 DirtyChunkSet**（三态 add 可测；WorldDelta::addAffected 保守面不扩）；PlaceBlock 命令带 state 五参权威落地（4 参丢 state 口径退役）。ChunkGeometry/QML 渲染路径仍零触碰（按集合调度重建归 R20.13）；静默写族接线归 R20.10+。
- **探针钉纪律（R20.08 新教训）**：pinSet 失败条件 cnt<minCount → **0 计数 SrcPin 恒不红（空转钉）**；「禁出」钉必须走 minCount=1 反探（miss 非空=合规，复用剥注释器防注释提及误伤）或腿体内手工 contains（r2007b 先例）；新钉上 workload 前先做「变异→恰红」自证；**minCount 对实际出现次数核，勿拍脑袋翻倍**（r2009d x1<2 误设教训）。**阴性变异选址（t1051 新教训）**：`if (<cond>) continue;` 形态的守卫，摘守卫用 `false &&` 前缀 = 把守卫变**无条件执行**（t1051 首版变异把「摘无撑轨」变「摘光全部轨」，rails 31537→7 假红形态）——正确阴性 = 摘**调用点**（`if (false) guard();`）；行为面发现力靠语料内嵌复现体 seed（t1051a 内嵌 42/166/207）。
- **腿选址纪律（R20.08/R20.09 新教训）**：涉 chunk 计数断言的腿，选址必须**结构性保证**跨 chunk（cx 分驻不同值），不靠地形扫描碰运气；「破/放目标」须显式验证非空气/空气，**破位还须验证顶上无附着**（破表面块会级联清除其上 TallGrass——r2009c chg=4 首红实证，空操/级联都会塌事件计数）；腿 diag 带判别信息（pre/post id、逐事件、chunk 键）。
- **R20.10 起状态机腿/ WorldStore rebind 纪律**：状态机扫略腿的「每对转移探测」必须**态独立成立**（每次探测前经合法路径重布点），禁依赖前序转移后的残留态（r2010a 假红 22 项教训——合法转移接受后续扫=陈旧前置态比较）；WorldStore 换目标世界必须显式 `setWorld(&new)`，loadChunks 写入面 = m_world（r2010d 首版漏 rebind=blob 回灌旧世界教训）；World::setBlock post-emit clearAllDirty 是**全表无条件**（t155g 既有行为）——mesher 门跳过重建时脏标记照样被清，任何「窗内编辑留脏待重载消费」类断言不成立（r2010c 教训，挂起编辑可见性走再编辑重建差分）。

## Recovery Point

- 最近闭环：**R20.10 Chunk lifecycle 六态状态机**（2026-09-15，fix(r2010) + test(r2010) + 本 docs）：① src/World/chunklifecycle.h（header-only 六态类型 + 转移表单一权威：8 合法边 Absent→Loading→Generated→Loaded⇄Active / Loaded→Evicting→{Absent,Loaded}，其余 28 组合含自转移非法）+ chunklifecycleQueryable（{Loaded,Active} 门可见面）。② ChunkManager 侧表 m_lifecycle（与 m_chunks 同布局同索引；**选型=manager 侧表非 Chunk 内字段**：store 豁免域隔离 + Absent/Evicting 需实例缺席表态 + 稠密 vector 同生同灭；recreate 全表 **Loaded 默认稳态=常驻已加载**→固定世界行为零变化）+ setLifecycle 唯一守卫式转移入口 + lifecycleAt（越界 Absent）+ World::setChunkLifecycle C++ forwarder（非 Q_INVOKABLE——QML 根本无生命周期访问面=plan 验收④）。③ WorldFacade 三门（chunkExistsAt/chunkDirtyAt/chunkFluidOnlyDirtyAt）叠加生命周期门（保 `chunks().chunk(` ×3 逐字不破 r2008c 钉；ChunkGeometry 零改动）。④ **Edits-on-evict 最小解释登记**：Evicting/Absent 只表达状态可达、数据零销毁、路由读写不受门控（worldgen/store 豁免域前提），**不实现落盘驱逐**（数据保留策略=后续单）。⑤ section14 置尾 4 腿 r2010a-d（纯图 36 对互证+全 8 边走通 / 默认稳态零变化实证 / 卸载→三门 false→mesher 门跳过→重载→栅格逐位恒等+门复 live / 源码钉+QML 反探+worldstore 存①卸载存②回读逐位恒等）；矩阵 570→574。⑥ 阴性轮摘转移守卫调用点→恰红 2 腿同源（r2010a 行为 + r2010d 结构钉）→还原回绿（存证 neg/restore）。**登记剩余待办**：落盘驱逐/数据保留策略（后续单，届时须与 saveAll 协调 Absent 态 chunk 是否入 blob 语义）+ 玩家距离驱逐器/streaming（R20.11+/后续）+ 箱车裸键门残余（随下一波 parity）+ review0915 #4/#6/#7 维持登记。
- t1051 review0913 #1 矿井悬空轨清偿（2026-09-15，fix(t1051) + test(t1051) + docs f368f6e）：placeMineshaft 干燥门 isTopFlushSupport 支撑校验（处方式，400 语料零过滤=契约面+防御完备）+ pruneUnsupportedWorldgenRails carveCanyon 后置守卫（真实机制清偿——悬空轨产生者=峡谷掏空已铺轨地板 seed 42/166/207 共 7 根）；P-t1051a 27 世界悬空轨==0 腿 + 三源钉；顺手修 review0915 #5 filtered 汇总行；矩阵 569→570。
- t1050 review0915 两项纠偏（2026-09-15，fix + test + docs 4172927）：stepTick dt 入口双钳制（方案①+droppedDtCount）+ 空手潜行右键交互 MC 口径恢复（sneakPlaceBlock 手持方块判据）；矩阵 564→569；阴性轮×2 存证。
- R20.09 EditBuffer（2026-09-15，fix(r2009) + test(r2009) + docs c760606）：src/Core/editbuffer.h（DirtyChunkSet + EditBuffer）+ GameSession 首消费方 + Review0915 #3① Command.blockState 五参权威；矩阵 560→564；section13 四腿。World/QML/PlayerController/ChunkGeometry 零触碰。
- R20.08 WorldFacade（2026-09-15，f0517c6/30efc3b/6d7c649）：WorldFacade 收窄视图 + GameSession/ChunkGeometry 双示范迁移，矩阵 556→560。
- R20.07 GameSession（2026-09-15，7ca99e1/cc33c69/cf63c07）：编排壳五项迁移，矩阵 552→556。
- R20.06 Command/Event/Snapshot（2026-09-15）：三 Core 叶子 + 三队列两域容量分化；549→552。
- R20.05 基础类型（2026-09-15，a2c1bb9/4cdf51c）：mathtypes.h + result.h；545→549。
- R20.03 测试分层（2026-09-15，09b8cb7/3daab66）：tools/matrix/ 八段 + --filter；545/0；全量冷编 2m4s。
- t1049 GOV-20260914-1（2026-09-14）：review26-1 = 腿跨段泄漏 + 踩踏 RNG，非 UB。
- R19.23 批次全貌：t1038→…→t1049，矩阵 521→545。
- **下一最小动作**：R20.11 后台 GenerationJob 同步版 ChunkScheduler（refactor-plan §29.3：不引入多线程，统一请求/优先级/取消/结果模型——request ID、重复请求合并、过期请求丢弃、换 worker 不改调用者；**收口后治理计数满 5 → 先治理审计 #8 再续**）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→闭环、#6（09-14）YELLOW→GOV-20260914-1 闭环、**#7（09-15）GREEN**（R20 地基 5 闭环放行，计数重算）；此后新闭环计数：R20.09 = 1/5、t1050 = 2/5、t1051 = 3/5、**R20.10 = 4/5**（R20.11 收口即满 5 → 先治理审计 #8 再续）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
