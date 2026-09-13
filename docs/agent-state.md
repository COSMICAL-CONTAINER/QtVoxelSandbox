# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-14 07:30
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1046
current_task_status: READY
last_completed_task: t1045 耕地踩踏/水蚀退化（parity 裁-3 清偿：踩踏概率回泥 P=clamp(fall−0.5,0,1) 玩家+mob 同公式 + 流水冲耕转换回 Dirt）
last_task_closure_commit: （docs 本提交；代码终态见 fix/test 两提交）
last_verified_commit: test(t1045)（矩阵 541 PASS / 0 FAIL，matrix_t1045_final.log 权威，binary 07:07:56 > 全部改动源）
last_governance_review: 2026-09-12（audit #5 YELLOW → GOV-20260912-1=t1047 已闭环，纠偏完成）
governance_review_due: false
completed_tasks_since_governance_review: 4
next_task: t1046 parity 小修合集（低-1 幼崽血量 / 低-2 音符盒 glass→hat / 低-3 拉杆按钮 !sneakPlace / 低-4 种子基准钉死 / 低-5 天气时长持久化 / 低-6 ride_minecart 1km+原创标注 / noteClips 析构补齐）→ R19.23 批次 review → audit #6
next_task_source: docs/dev-plan.md R19.23 段 t1046 条目（parity bug 波尾单，合集七项）
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 收口 docs 提交，工作区干净；`.codex/` 豁免不删不提交。
- 纪律：①每任务 docs 闭环同步本文件；②验收日志带任务号；③阴性轮 restore 一律 Edit 反向（严禁 git checkout/reset）；④长构建 nohup 起手；⑤用户修正文件（docs/Review_*.md）开工前必读，事实优先级高于 dev-plan 旧口径；⑥**多轮阴性轮间摘 C++ 病灶后必须先重建再跑下一轮**（t1038 教训：未重建旧 binary 混入上一病灶签名）；⑦**工作区一致性硬门（2026-09-12 用户裁决）**：开工前 `git status` 与本 Control Block 交叉核对——工作区存在未提交改动 / 未恢复变异而 state 仍 READY 时，**禁止开始新任务**，置 `RECOVERY_REQUIRED`，只允许恢复、构建、测试、更新状态（t1043 断链教训：阴性轮②中断后变异残留，本文件仍报 READY+工作区干净，恢复 Agent 会拿错误状态继续）；⑧**规格变更门（2026-09-12 用户裁决）**：实现偏离原计划文字须先写 SPEC_CHANGE（dev-plan 任务行改写 + 偏差点明示）再实现，禁止收口文档单方面回溯改口径（t1028 案例登记于 dev-plan）；低风险纯实现细节可明示登记后继续，行为口径 / 资产形态 / 用户可见面的偏离挂起待裁决。

## Recovery Point

- 最近闭环：**t1045**（2026-09-14）：耕地踩踏/水蚀退化（parity bug 波 3/4，裁-3 清偿）——①踩踏概率回泥：World 层单一掷骰 `farmlandTrampleRoll(fall)`（MC Java onFallenUpon 公式 P=clamp(fall−0.5,0,1)，wiki 三元组引证；kFarmlandTrampleFallMin=0.5 概率地板；缝 setTrampleRollOverride 千分比=t1031 同式，生产零调用全局 RNG）；玩家踩踏分支概率化（t639④ fall>1.0 恒踩退役）+ mob 落地沿踩踏（t970 落地沿锚 restCellY + 新信号 farmlandTrampledByMob→PlayerController::onMobTrampledFarmland 直连清苗+dropCropDrops 弹落，掉落表不出 Game 层）；mobGriefing 门/0.512 尺寸豁免门不入（无 gamerule 系统/Beta1.0 基准，登记）；②流水冲耕：tickWaterFlow tryWashFarmland 转换变体（扩散落点耕地→setWaterSilent(Dirt,0) 湿润态随 id 消失、不灌水、零 blockDroppedAsItem，与附着块冲毁掉落分型；静水源邻接不冲=hydration 基建面选型，仅流水(state>0)冲；维基缺口：Java/Bedrock wiki 均无流水毁耕，按裁-3 定案实现台账注记）；③矩阵 **538→541 PASS / 0 FAIL**（matrix_t1045_final.log 权威）；阳性 matrix_t1045_pos.log 541/0（首跑 539/2：t1045c rig 水源放地板层下坠排空（rig 几何 bug 改地板 y=83）+ review26-1 行为性打破）；阴性轮一构建双摘（掷骰本体 + 水冲两调用点 false&&）→ **恰红 538/3 = t1045a/b/c 唯三红**（matrix_t1045_neg.log）→ Edit 反向 restore（纪律③）→ 重建 → 终跑 541/0（binary 07:07:56 > 源 07:07:34）。**连带修复**：review26-1 耕地走廊腿僵尸出生高 kRigY+2→kRigY+1（概率化后 2 格高落 P≈0.56 打破 feetOff 断言；该腿验证行走非坠落，断言未放宽）。voxelsandbox 重建 EXIT=0（build_t1045_app.log 零 error）+ offscreen 冒烟存活 13s（60fps 稳态；smoke 重定向 0 字节既录环境坑，tail20 从 logs/voxelsandbox.log 取）+ logs/voxelsandbox_t1045_tail20.log 留存。**待实机确认（观感类）**：跳跃踩农田概率回泥（跳一次 75% 不再必坏）/ mob 路过农田偶发踩坏 / 瀑布浇耕地流水漫田渐次回泥观感。
- 实机确认（累计）：R19.23 已闭环 7 项（t1038/t1039/t1041/t1042 各项 + t1043 worldgen 矿井轨 + **t1045 踩踏/冲耕观感**）+ 用户 0912 评审追加：**批 1/2/4 桶池 Repeater 3D delegate 挂载实机渲染验证**（headless 冒烟零孤儿告警为间接信号；F3 draw call 前后对比 + 合批 Model 可见性）+ **Review_2026-09-12 #1 壳排除链 >128 溢出双壳/丢壳** + #2 stringPass 废钟（headless 不可见，实机/源码钉复核）+ R19.22 总表 16 项 + 回归三单（t1005/t1006/t1007）待用户实机数据。
- 长期方向（用户 0912 评审）：instancing 治理到路径 a 终点后**暂停新增 QML instancing 家族**；t1043-t1046 完成后转 R20 重构主线（R20.03 测试分层 → 20.04 基础类型 → 20.05 Command/Event/Snapshot → 20.06 GameSession → 20.07 WorldFacade → 20.09 Chunk 生命周期 → 20.11 后台 GenerationJob）。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。**断链恢复按纪律⑦硬门执行**（工作区与 Control Block 不一致即 RECOVERY_REQUIRED）。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11 R19.22 批次）GREEN。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。（R19.23 批次 t1038/t1039/t1040/t1042 四闭环 + t1041 收官后达「批次结束」触发 → audit #5。）
- 任务计数只统计完整 fix/test/docs 闭环。
