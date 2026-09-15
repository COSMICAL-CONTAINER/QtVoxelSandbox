# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 18:00（R20.12 后台 GenerationJob 闭环，矩阵 582；下一单 R20.13 MeshBuilder）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.13 MeshBuilder（plan §29.3：CPU 网格算法从 ChunkGeometry 抽出——MeshBuilder 不依赖 QQuick3D / 输入 snapshot / 输出 owning ChunkMeshData / 旧 QtQuick3DAdapter 可消费新结果 / 网格视觉与顶点统计与旧路径一致）
current_task_status: READY
last_completed_task: R20.12（后台 GenerationJob：src/World/terraingen.h 纯地形生成单一权威[自 world.cpp 逐行迁入：置换表/noise2/noise3/fbm/heightAt/biomeComputeAt/sea*/hashColumn/hashVoxel/kWaterLevel/generate() 列体 fillTerrainColumn；Biome 枚举迁入 + World using 别名 + 委托壳零调用点改动] + generationjob.h 异步协议[CompletedGeneration 持 Error 载荷（Result 不可默认构造）+ isAsynchronous/submitAsync/takeCompletedAsync 默认同步语义逐位不变 + pumpAsync 交接/收割两相 + m_inFlight 在途账本 + selectNextLiveJob 双泵共用] + src/World/backgroundgeneration.h BackgroundGenerationWorker = GenerationWorker 缝第三实现[std::thread+mutex+cv，worker 侧零 QObject（audit #8 红线）；kMaxQueuedTasks=64 满载 kErrQueueFull 背压；析构=stop+丢弃队内+join 有界；TerrainGen 构造后只读=无共享可变态；GeneratedChunkData QObjectFree 自持缓冲三要素戳（seed/key/generatorVersion）+ applyGeneratedChunkData 主线程守卫入口（ChunkManager::setBlock 5 参）] + 同步路径与新 worker 共用同一 fillTerrainColumn[worldgen 逐位恒等腿族守迁移零漂移] + t1023c 事实钉同变更修订[src/ 唯一受认可线程原语落点=World/backgroundgeneration.h 且仅 std::thread；F3 sync-meshing 行不动]；后台 worker 零生产接线 = 固定世界零变化 + app 面零变化）
last_task_closure_commit: docs(plan)（代码终态 = fix(r2012) fa5c3aa + test(r2012) eb7e88a，哈希见 git log）
last_verified_commit: test(r2012)（矩阵 582 PASS / 0 FAIL ×2：matrix_r2012_pos/final.log EXIT=0；578 权威 diff = +4 恰为本单 r2012a-d + 4 登记类[t813 戳/t997 计时/t979 涉水/t1023c 腿名同变更修订]，两轮 PASS 腿名 diff=空，零未归因漂移；filter 面 r2012=4P ×3 连跑无 flake、r2007=5P、r2008=4P、r2009=4P、r2010=4P、r2011=4P、worldgen=22P、determin=30P、re-gen=5P、store=43P、save=34P 全绿 EXIT=0；阴性轮 neg 摘 deliver epoch 子句[false &&]→恰红 r2012c 单腿[r2011 同步腿 4P 不误伤验证]+摘 applyGeneratedChunkData 守卫写调用点[if(id==0)→if(true)]→恰红 r2012d 单腿→各 Edit 反向还原→回绿 4P/0F[存证 matrix_r2012_neg.log 含 restore 段]；app 重建 EXIT=0 + 冒烟 EXIT=124 + tail20 稳态 60fps 零新错误）
last_governance_review: 2026-09-15（audit #8 GREEN——用户 0912 指令波 R20.03→11 收官放行，线程期红线已立：worker 禁 QObject/QML/QQuick3D；同日 audit #7 GREEN 在案；Mimosa ENOBUFS 环境注记延续）
governance_review_due: false
completed_tasks_since_governance_review: 1
next_task: R20.13 MeshBuilder（plan §29.3 五验收：MeshBuilder 不依赖 QQuick3D / 输入是 snapshot / 输出是 owning ChunkMeshData / 旧 QtQuick3DAdapter 可消费新结果 / 网格视觉和顶点统计与旧路径一致）
next_task_source: docs/refactor-plan-2026-09-08.md §29.3 R20.13
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.12 代码终态（fix fa5c3aa + test eb7e88a 已提交；docs 提交随后），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑨全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训；分段后单段增量 -j 4 实测安全）。**矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（秒级）+ `--filter <substring>` 只跑本任务腿；新腿落对应段文件，新段置尾 runAll 末执行、须 ≤500KB；section11 起「自建 fresh 小世界」先例（48×48×96 seed 82 + 天气双钉 setWeatherState(0)+setWeatherRemainingSec(3600)）。
- **R20.06 起值类型纪律**：src/Core/ 新值类型一律 result.h QObjectFree 编译期钉 + 头内 static_assert；命令/事件队列满载拒绝与快照队列覆盖最老是两域容量策略分化，勿「统一」。
- **R20.07 起编排壳纪律**：GameSession 只做编排（命令 → 整 tick 边界 → World::setBlock 权威），禁复制游戏逻辑；**QML 现行玩法路径零变化是 R20 主线不变量**（Main.qml 含 "GameSession" 即违零迁移阴性钉 r2007b）。
- **R20.08 起 facade 纪律**：新代码世界读写统一走 WorldFacade（收窄视图）；mesher 零 Chunk*（chunkgeometry 禁出反探常驻）；GameSession 命令零旁路（m_world.setBlock 禁出反探 + m_facade.setBlock/setBlockWithState 正面钉，r2008c 常驻）；Facade 不转发静默写族。
- **R20.09 起编辑面纪律**：Tick 内编辑统一经 EditBuffer（src/Core/editbuffer.h）登记——同格同 Tick 合并后写胜、Tick 内零通知、收口 takeDelta 单点发布 + 合并面派生 BlockChanged；**dirty chunk 集合统一 DirtyChunkSet**（三态 add 可测；WorldDelta::addAffected 保守面不扩）；PlaceBlock 命令带 state 五参权威落地（4 参丢 state 口径退役）。ChunkGeometry/QML 渲染路径仍零触碰（按集合调度重建归 R20.13）；静默写族接线归 R20.10+。
- **探针钉纪律（R20.08 新教训）**：pinSet 失败条件 cnt<minCount → **0 计数 SrcPin 恒不红（空转钉）**；「禁出」钉必须走 minCount=1 反探（miss 非空=合规，复用剥注释器防注释提及误伤）或腿体内手工 contains（r2007b 先例）；新钉上 workload 前先做「变异→恰红」自证；**minCount 对实际出现次数核，勿拍脑袋翻倍**（r2009d x1<2 误设教训）。**阴性变异选址（t1051 新教训）**：`if (<cond>) continue;` 形态的守卫，摘守卫用 `false &&` 前缀 = 把守卫变**无条件执行**（t1051 首版变异把「摘无撑轨」变「摘光全部轨」，rails 31537→7 假红形态）——正确阴性 = 摘**调用点**（`if (false) guard();`）；行为面发现力靠语料内嵌复现体 seed（t1051a 内嵌 42/166/207）。
- **腿选址纪律（R20.08/R20.09 新教训）**：涉 chunk 计数断言的腿，选址必须**结构性保证**跨 chunk（cx 分驻不同值），不靠地形扫描碰运气；「破/放目标」须显式验证非空气/空气，**破位还须验证顶上无附着**（破表面块会级联清除其上 TallGrass——r2009c chg=4 首红实证，空操/级联都会塌事件计数）；腿 diag 带判别信息（pre/post id、逐事件、chunk 键）。
- **R20.10 起状态机腿/ WorldStore rebind 纪律**：状态机扫略腿的「每对转移探测」必须**态独立成立**（每次探测前经合法路径重布点），禁依赖前序转移后的残留态（r2010a 假红 22 项教训——合法转移接受后续扫=陈旧前置态比较）；WorldStore 换目标世界必须显式 `setWorld(&new)`，loadChunks 写入面 = m_world（r2010d 首版漏 rebind=blob 回灌旧世界教训）；World::setBlock post-emit clearAllDirty 是**全表无条件**（t155g 既有行为）——mesher 门跳过重建时脏标记照样被清，任何「窗内编辑留脏待重载消费」类断言不成立（r2010c 教训，挂起编辑可见性走再编辑重建差分）。
- **R20.11 起恰红面设计纪律**：**阴性恰红面设计先于腿文定稿**——辅助腿（对照/可替换性类）只钉**相对恒等（A≡B）与快照锚**，绝对计数/绝对序归被测语义腿专钉；否则摘语义的阴性变异会把红扩散到辅助腿成假阳性面（r2011c 首版绝对 `==4` 被 NEG1 误伤实证，相对化后恰红面收缩到 r2011a 单腿）。`false &&` 变异用于「使分支永不可达」类摘除（合并分支、守卫分支）恰当时 = 语义精确摘除，变异后必须核对 diag 形态即被摘语义本体（r2011b [stale hit=1]=过期 job 被执行）。
- **R20.12 起线程期钉/迁移恒等纪律**：src/ 树线程原语唯一受认可落点 = `src/World/backgroundgeneration.h` 且仅 std::thread（t1023c 事实钉已修订为「白名单落点」语义——新增任何线程原语文件/记号（含 R20.13 mesher 线程化）必须同变更再修订 t1023c + F3 行）；worldgen 生成逻辑单一权威 = `src/World/terraingen.h`（同 seed 同坐标逐位同果）——同步与后台任何生成路径**禁复制第二份**，迁移必须「逐行搬移 + worldgen/determin/re-gen 三腿族即时回归」；worker 侧零 QObject（audit #8 红线），跨线程值通道持 Error 聚合（Result<void> 不可默认构造，不作队列成员）；std::thread 成员必须声明在所触碰的锁/队列**之后**（成员声明序=构造序，否则线程早于锁就绪=UB）。

## Recovery Point

- 最近闭环：**R20.12 后台 GenerationJob**（2026-09-15，fix(r2012) fa5c3aa + test(r2012) eb7e88a + 本 docs）：① src/World/terraingen.h 纯地形生成**单一权威**（自 world.cpp 逐行迁入 12 件：置换表/noise2/noise3/fbm/heightAt[含 heightWithBiome 防二次群系计算]/biomeComputeAt/seaCorner/seaColumnHeight/isSeaSandColumn/hashColumn/hashVoxel/列体 fillTerrainColumn + fade·lerp·grad2·grad3 + kWaterLevel/kChunkSize/kGeneratorVersion；Biome 枚举迁入 + World using 别名；未迁清单 31 项登记 R20.13+）。② generationjob.h 异步协议（CompletedGeneration 持 Error 载荷 + isAsynchronous/submitAsync/takeCompletedAsync **默认同步语义逐位不变**[r2011 全绿守] + pumpAsync 交接/收割两相：交接相共用 selectNextLiveJob 选活序逐 job submitAsync[满载背压留队]+边①+记 m_inFlight；收割相 FIFO → 边②[成功面]+deliver 投递[**取消/epoch 过滤在投递时刻生效**=验收③后台版]）。③ src/World/backgroundgeneration.h BackgroundGenerationWorker 真线程第三实现（std::thread+mutex+cv 选型依据=worker 侧零 QObject 红线；kMaxQueuedTasks=64 满载 kErrQueueFull 背压；完成/数据队列传递性有界[第三容量形态]；析构=stop+notify_all+join、队内未开工整体丢弃、join 上界=一个在途任务；TerrainGen 构造后只读=无共享可变态；GeneratedChunkData QObjectFree 自持缓冲[owning vector 非 trivially copyable 与消息面钉分化登记]三要素戳 + contentHash/identical + generateTerrainChunk + applyGeneratedChunkData 主线程守卫入口）。④section16 置尾 4 腿 r2012a-d（非阻塞+线程 id 身份对账/纯函数承重墙[双实例≡同步共享函数≡真世界 256 列锚+方向对照]/epoch 丢弃后台版+取消/三场景退出安全+Absent→Loading→Generated 互锁+守卫入口落格逐位+固定世界零变化+源码钉反探）；矩阵 578→582（band 580±2 内）；**t1023c 事实钉同变更修订**（src/ 唯一受认可线程原语落点=World/backgroundgeneration.h 且仅 std::thread；F3 sync-meshing 行不动——mesher 仍同步）。⑤验证链全绿：filter r2012=4P ×3 连跑、前置回归 r2007-r2011+worldgen 22P+determin 30P+re-gen 5P+store 43P+save 34P 全绿、**迁移零漂移**[worldgen/determin/re-gen 改前改后同果]、阴性轮双变异各恰红单腿（r2012c/r2012d）→还原回绿[存证 matrix_r2012_neg.log 含 r2011 不误伤验证+restore 段]、全矩阵 582×2[两轮腿名 diff=空]、app 重建+冒烟 EXIT=124 稳态 60fps。**登记剩余待办**：真实生产接线（Absent-only 策略层/World 挂点访问器/现网调用点迁移——后台 worker 零生产接线=零变化最强形态）+ Loading 失败恢复策略 + 其余结构路径后台化（迁移清单「未迁」面，MeshBuilder 先行）+ Edits-on-evict/streaming 驱逐器（延续 R20.10）+ 箱车裸键门残余（随下一波 parity）+ review0915 #4/#6/#7 维持登记。
- 最近闭环：**R20.11 GenerationJob 同步版 ChunkScheduler**（2026-09-15，fix(r2011) 6fe11b2 + test(r2011) 935e563 + 本 docs）：① src/World/generationjob.h（header-only World 层请求编排层，非 QObject 无 AUTOMOC，**零调用点迁移**——World/worldstore/QML 零触碰）：GenerationJobKind 三 kind（Generate/Load/Mesh）单调 requestId（1 起、0 哨兵、永不复用）+ pending 域 (kind,key) **重复合并别名制**（工作恰一次、每别名各收 outcome；完成后再请求=新 job；跨 kind 不合并）+ **两级丢弃均不投递**（取消 pending 专属+幂等拒重复、全别名死亡 job 整体跳过零执行；世界 epoch submit 快照、pump 时过期静默丢弃——R20.12 epoch 规则同步版前置）+ priority 0 最紧急（同优先级按提交序）+ job 队列 64 满载 kErrQueueFull 可见拒绝（outcome 队列刻意不设上界=同步内联无不对称，容量策略登记 R20.12）+ GenerationWorker 缝（Result&lt;void&gt; execute，Error 原样穿透 outcome）。②生命周期互锁选型（头注释立此存照）：可挂可变 ChunkManager*（与 World::setChunkLifecycle forwarder 同一 setLifecycle 终点），**只驱动边①②**（执行前 Absent→Loading / 成功后 Loading→Generated）best-effort——非法转移（默认稳态 Loaded→Loading）被守卫拒即忽略 = **固定世界零变化的结构性根据**（r2011d：Loaded chunk submit+pump 表快照逐位不动）；晋升③不驱动（驻留策略归后续）、失败停 Loading（六态表无失败边，恢复登记 R20.12）、取消在执行前=恒 Absent 不回退；刻意不加非 const chunks() 访问器，真实 World 接线+Absent-only 策略层归 R20.12。③section15 置尾 4 腿 r2011a-d（请求模型核心/取消+过期/失败穿透+双 worker 类可替换性无头实证/互锁+零变化+源码钉反探）；矩阵 574→578。④阴性轮两变异（摘合并 false && / 摘 pump epoch 子句）各恰红单腿（r2011a/r2011b）→还原回绿（存证 matrix_r2011_neg.log 含 restore 段）。⑤验证链全绿：filter r2011=4P、前置回归 r2007-r2010/determin/re-gen/store/save/worldgen 全绿、全矩阵 578×2、app 重建+冒烟 EXIT=124 稳态 60fps。**登记剩余待办**：R20.12 真后台线程化（线程队列容量/退出排干/GUI 不阻塞）+ Loading 失败恢复策略 + 真实生产接线（Absent-only 策略层/World 挂点访问器）+ Edits-on-evict/streaming 驱逐器（延续 R20.10）+ 箱车裸键门残余（随下一波 parity）+ review0915 #4/#6/#7 维持登记。
- **下一最小动作：R20.13 MeshBuilder**（plan §29.3 五验收：不依赖 QQuick3D / 输入 snapshot / 输出 owning ChunkMeshData / 旧 Adapter 可消费 / 视觉与顶点统计一致；线程原语白名单纪律见 Workspace Guard R20.12 段）。
- R20.10 Chunk lifecycle 六态状态机（2026-09-15，fix(r2010) + test(r2010) + docs bed3363）：src/World/chunklifecycle.h 六态类型+转移表单一权威（8 合法边/28 非法含自转移）+ ChunkManager 侧表 m_lifecycle（recreate 全表 Loaded 默认稳态=常驻已加载）+ setLifecycle 唯一守卫式转移入口 + World::setChunkLifecycle C++ forwarder（非 Q_INVOKABLE，QML 零生命周期访问面）+ WorldFacade 三门叠加 chunkLifecycleQueryable（{Loaded,Active} 可见，卸载三门恒 false/重载恢复，ChunkGeometry 零改动）；Edits-on-evict 最小解释登记=Evicting 只表达状态可达不落盘驱逐[数据保留=后续单]；worldstore 直读 blob 豁免域不受态影响 r2010d 存①→卸载→存②→回读逐位恒等实证；矩阵 570→574。
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
- **下一最小动作**：R20.13 MeshBuilder（plan §29.3，见 Current Control Block）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→闭环、#6（09-14）YELLOW→GOV-20260914-1 闭环、#7（09-15 上午）GREEN（R20 地基 5 闭环放行）、**#8（09-15 下午，docs/governance-audit-2026-09-15-b.md）GREEN**（用户 0912 指令波 R20.03→11 收官 + 5 闭环 [R20.09/t1050/t1051/R20.10/R20.11] 全绿放行；线程期红线已立：worker 禁 QObject/QML/QQuick3D）；计数自 R20.12 起重算。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
