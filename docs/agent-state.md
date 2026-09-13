# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-14 04:20
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1045
current_task_status: READY
last_completed_task: t1044 蜘蛛爬墙（parity 裁-2 清偿：aiHostile 三追击分支 aiSpiderWallClimb 贴面攀爬脉冲，Spider 家族门含 cave spider；29a8afe/5377f16/docs）
last_task_closure_commit: （docs 本提交；代码终态 5377f16）
last_verified_commit: 5377f16（矩阵 538 PASS / 0 FAIL，matrix_t1044_final.log 权威，binary 与源对齐）
last_governance_review: 2026-09-12（audit #5 YELLOW → GOV-20260912-1=t1047 已闭环，纠偏完成）
governance_review_due: false
completed_tasks_since_governance_review: 3
next_task: t1045 耕地踩踏/水蚀退化（裁-3；parity bug 波尾单 → t1046 合集 → R19.23 批次 review → R20 主线）
next_task_source: docs/dev-plan.md R19.23 段（audit #5 节「顺序再更新」）
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 收口 docs 提交，工作区干净；`.codex/` 豁免不删不提交。
- 纪律：①每任务 docs 闭环同步本文件；②验收日志带任务号；③阴性轮 restore 一律 Edit 反向（严禁 git checkout/reset）；④长构建 nohup 起手；⑤用户修正文件（docs/Review_*.md）开工前必读，事实优先级高于 dev-plan 旧口径；⑥**多轮阴性轮间摘 C++ 病灶后必须先重建再跑下一轮**（t1038 教训：未重建旧 binary 混入上一病灶签名）；⑦**工作区一致性硬门（2026-09-12 用户裁决）**：开工前 `git status` 与本 Control Block 交叉核对——工作区存在未提交改动 / 未恢复变异而 state 仍 READY 时，**禁止开始新任务**，置 `RECOVERY_REQUIRED`，只允许恢复、构建、测试、更新状态（t1043 断链教训：阴性轮②中断后变异残留，本文件仍报 READY+工作区干净，恢复 Agent 会拿错误状态继续）；⑧**规格变更门（2026-09-12 用户裁决）**：实现偏离原计划文字须先写 SPEC_CHANGE（dev-plan 任务行改写 + 偏差点明示）再实现，禁止收口文档单方面回溯改口径（t1028 案例登记于 dev-plan）；低风险纯实现细节可明示登记后继续，行为口径 / 资产形态 / 用户可见面的偏离挂起待裁决。

## Recovery Point

- 最近闭环：**t1044**（2026-09-14）：蜘蛛爬墙（parity bug 波 2/4，裁-2 清偿）——①`aiSpiderWallClimb`（entitymanager.cpp:3822 区）：aiHostile 三条追击分支（仇恨狼/铁傀儡/玩家）水平位移双轴皆撤回（撞墙挡死）时调，被阻轴贴面前探列（halfW+0.25 越过 AABB 前沿一个 AI 步长；固定 0.6 偏移对宽体停位缝隙临界漏探，阳轮首跑实证后修正）脚位/身体层有可碰撞方块 → vy=kSpiderClimbSpeed（2.4 b/s 名义登记，净 ≈1.2 b/s，AI tick 4 帧窗内 vy 恒正不触发落地扫描回弹）+ 解除 resting；爬过墙顶水平试探自然解锁 → 越檐走既有追击水平移动（登记简化）；②能力门仅 Spider 家族（MobSpider+MobCaveSpider，MC cave spider 同爬，同批纳入）；游荡不入口（登记）；③**t285 勘误落地**（dev-plan 任务表行注记：「可爬墙 ✅」自本单起成立）。矩阵 **536→538 PASS / 0 FAIL**（matrix_t1044_final.log 权威）；阳性 matrix_t1044_pos.log 538/0（首跑 537/1 暴露探针 0.6 临界漏探 → halfW+0.25 修 → pos 538/0）；阴性轮摘 isClimber 门（false &&）→ **恰红 536/2 = t1044a/b 唯二红**（matrix_t1044_neg.log）→ Edit 反向 restore（纪律③）→ 重建 → 终跑 538/0（binary 03:59 > 全部改动源）。voxelsandbox 重建 EXIT=0 + offscreen 冒烟存活 12s（EXIT=124）+ logs/voxelsandbox_t1044_tail20.log（60fps 稳态零孤儿告警；注：app 日志走 logs/voxelsandbox.log 文件非 stderr——smoke 重定向 0 字节为既录环境坑，tail20 从日志文件取）。**待实机确认（观感类）**：蜘蛛/洞穴蜘蛛追击爬墙越檐动画观感（攀爬帧 moveSpeed 走追击速腿摆语义）。
- 实机确认（累计）：R19.23 已闭环 6 项（t1038/t1039/t1041/t1042 各项 + **t1043 worldgen 矿井轨含水带出现率与流水冲玩家轨**）+ 用户 0912 评审追加：**批 1/2/4 桶池 Repeater 3D delegate 挂载实机渲染验证**（headless 冒烟零孤儿告警为间接信号；F3 draw call 前后对比 + 合批 Model 可见性）+ **Review_2026-09-12 #1 壳排除链 >128 溢出双壳/丢壳** + #2 stringPass 废钟（headless 不可见，实机/源码钉复核）+ R19.22 总表 16 项 + 回归三单（t1005/t1006/t1007）待用户实机数据。
- 长期方向（用户 0912 评审）：instancing 治理到路径 a 终点后**暂停新增 QML instancing 家族**；t1043-t1046 完成后转 R20 重构主线（R20.03 测试分层 → 20.04 基础类型 → 20.05 Command/Event/Snapshot → 20.06 GameSession → 20.07 WorldFacade → 20.09 Chunk 生命周期 → 20.11 后台 GenerationJob）。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。**断链恢复按纪律⑦硬门执行**（工作区与 Control Block 不一致即 RECOVERY_REQUIRED）。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11 R19.22 批次）GREEN。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。（R19.23 批次 t1038/t1039/t1040/t1042 四闭环 + t1041 收官后达「批次结束」触发 → audit #5。）
- 任务计数只统计完整 fix/test/docs 闭环。
