# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-10 14:05
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1036
current_task_status: READY（t1030 已闭环，t1036 未开工；bedSpawnValidChanged 无参化 + bedSpawnAnnounce 分离，见 dev-plan Review_2026-09-10 处置段 #1）
last_completed_task: t1030 骨粉与作物催熟（eaef577）
last_task_closure_commit: （docs 本提交）
last_verified_commit: eaef577（矩阵 507 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 1
next_task: t1036（t1030 后顺手小单，改动面 ≈6 处）
next_task_source: docs/dev-plan.md Review_2026-09-10 处置段 #1
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1030 docs 闭环提交；工作区仅剩未跟踪验收证据（matrix_t1030_pos/neg/final.log、smoke_t1030*.log、ninja 日志——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。

## Recovery Point

- 最近闭环：**t1030 骨粉与作物催熟**（2026-09-10，d8e5756 + eaef577 + 本 docs）：功能本体零改动收口（盘点实情：t447 物品/配方/右键分流 + t791 applyBonemeal +2..3 + t301 掉骨链均已交付），本单=成熟口径登记注释 + P-t1030a/b 探针（+2）。矩阵 **505→507 PASS / 0 FAIL**（matrix_t1030_pos/final.log；阴性轮恰红 506/1=P-t1030a，matrix_t1030_neg.log）；app 重建 EXIT=0 + 冒烟 EXIT=124 + voxelsandbox.log 尾部证据。**探针 rig 教训已入 lessons-learned**（真消费端 rig 须显式建模 QML selectedBlock 绑定，材料段→Air——否则阴性轮摘分支后 fall-through 通用放置扣栈假红）。
- 上一棒中断现场（t1030 WIP 探针骨架）已由本棒清偿：WIP 中「引用未实现 src API」的判断系误报，实为 t447/t791 已交付；WIP 骨架经逐条对照源码验证 + 两处修正（P-t1030b 瞄准几何 y14.9→y14.5 防撞 y15 预置石；rig 补 setSelectedBlock(Air)）后编译验证收口。
- 下一任务 **t1036**：`bedSpawnValidChanged(bool restored)` → 无参 `bedSpawnValidChanged()` + 播报源另立 `bedSpawnAnnounce(bool restored)`（只在入睡设锚沿 emit）；改动面 ≈6 处（信号声明 + 4 emit + QML handler:3145 + 矩阵钉 hdr-bedSpawnValid-signal-src + P-t1024a (e) restored 断言改挂新信号）；阴性轮=摘播报信号腿恰红。详见 dev-plan Review_2026-09-10 处置段 #1。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
