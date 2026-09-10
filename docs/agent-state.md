# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-10 16:05
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1031
current_task_status: READY（t1036 已闭环，t1031 未开工；狼驯服——骨头右键概率驯服 + 项圈渲染 + 坐下/跟随 + 参战，见 dev-plan R19.22 t1031）
last_completed_task: t1036 床位信号职责分离（d4be270 + d0cd4b3）
last_task_closure_commit: （docs 本提交）
last_verified_commit: d0cd4b3（矩阵 507 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 2
next_task: t1031（R19.22 主线第 2 项）
next_task_source: docs/dev-plan.md R19.22 t1031
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1036 test 提交（d0cd4b3）；本 docs 闭环后 HEAD = t1036 docs 提交。工作区仅剩未跟踪验收证据（matrix_t1036_pos/neg/final.log、smoke/logs 尾部留存、ninja 日志——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。

## Recovery Point

- 最近闭环：**t1036 床位信号职责分离**（2026-09-10，d4be270 + d0cd4b3 + 本 docs）：`bedSpawnValidChanged()` 无参化（纯属性 NOTIFY，Qt 6.11 约定）+ 播报源另立 `bedSpawnAnnounce(bool restored)`（只在入睡设锚沿 emit）；QML handler 改挂 onBedSpawnAnnounce；P-t1024a 探针双信号各自记录 + 三钉随迁。矩阵恒 **507 PASS / 0 FAIL**（matrix_t1036_pos/final.log；阴性轮摘 announce 发射恰红 506/1=P-t1024a（diag sleepAnnounce=false），matrix_t1036_neg.log）；voxelsandbox 重建 EXIT=0 + 冒烟 EXIT=124 + logs 尾部留存（review0910 #2 纪律沿用）。
- 上一任务 t1030 骨粉催熟闭环记录（eaef577 批）与探针 rig 教训（QML selectedBlock 绑定须显式建模）已入 lessons-learned / dev-plan t1030 段。
- 下一任务 **t1031 狼驯服**：骨头右键野狼概率驯服（MC 1/3 口径，烟雾/爱心粒子）+ 项圈红领渲染 + 坐下/跟随切换 + 跟随传送边界 + 驯服狼参战（attack-target 体系）+ 野狼中立不主动攻击。探针：驯服概率缝 / 坐下跟随 / 项圈渲染钉 / 参战。分层注意：驯服态在 Entities、项圈渲染在 QML、骨道物品在 Game（沿 t1025 引诱表先例）。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
