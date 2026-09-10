# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-11 06:05
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1034
current_task_status: READY（t1033 已闭环，t1034 未开工；门/床/活板门 useBlock 三分支补 !sneakPlace 存量缺门收口——review0909 #2 存量登记项，见 dev-plan R19.22 t1034）
last_completed_task: t1033 床锚自然破坏联动（World::blockDestroyedBed 语义信号 + PlayerController seedChanged 同款直连 → clearBedSpawn + bedSpawnLost；12f4558 + 14a789e）
last_task_closure_commit: （docs 本提交）
last_verified_commit: 14a789e（矩阵 516 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 5
next_task: t1034（R19.22 主线第 5 项）
next_task_source: docs/dev-plan.md R19.22 t1034
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1033 docs 提交（本提交）；前一 HEAD = 15fe901（t1032 docs）。工作区仅剩未跟踪验收证据（matrix_t1033_pos/neg/final.log、smoke_t1033.log、logs/voxelsandbox_t1033_tail20.log、build_t1033_*.log——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。

## Recovery Point

- 最近闭环：**t1033 床锚自然破坏联动（review0909 #4 清偿）**（2026-09-11，12f4558 + 14a789e + 本 docs）：爆炸清块单点 World::destroySphereSilent 毁前 capture 床格 state → 末尾逐格 emit blockDestroyedBed(x,y,z,state) 语义信号；PlayerController::onWorldBedBlockDestroyed 沿 seedChanged 收口模式（setWorld 内 UniqueConnection C++ 直连）判锚（谓词与 finishMiningAt 床分支逐字同构，bedPartnerOffset 解自毁前 state）→ 命中 clearBedSpawn + bedSpawnLost（t1024 既有播报面自动生效零新增）。两半同爆双信号被 m_bedSpawnValid 门幂等塌缩成一次播报；未设锚/非锚床早退零 emit。**选型登记**：契约两变体取 seedChanged 字面（C++ 直连）——bedSpawnLost 须由 PlayerController 发 + 矩阵 harness C++ 直编不可达 QML 面；分层不破（World 只发语义事件）。**口径登记**：岩浆/火不毁床（flammable() 无床，路径不存在）；水冲清单 isAttachableBlock 无床（探针钉）；世界换代走 seedChanged 既有链（刻意静默）。矩阵 **513→516 PASS / 0 FAIL**（matrix_t1033_pos/final.log；阴性轮摘 setWorld 内 connect 恰红 515/1 = P-t1033a〔diag broken=true invalidated=false lost=0 = 病灶本体〕，matrix_t1033_neg.log，restore 反向）；voxelsandbox 重建 EXIT=0 + 冒烟 EXIT=124 + logs/voxelsandbox_t1033_tail20.log 留存（零 QML/C++ 错误）。
- 上一任务 t1032 掉落物 instancing 批 2 + review0910 #2/#3/#4 收口记录（482b9fa 批）已入 dev-plan t1032 段。
- 下一任务 **t1034 门/床/活板门 sneakPlace 存量缺门收口**：useBlock 三分支补 `!sneakPlace` 门（review0909 #2 存量登记项；对齐机关件模式；防「潜行持方块无法对门/床/活板门使用面放置」）。探针：三件潜行右键放置 / 非潜行正常使用两向。**先盘点：useBlock 三分支现状行号 + 机关件 `!sneakPlace` 前置模式（playercontroller.cpp :3473-3496 先例）+ 潜行状态读口。**
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
