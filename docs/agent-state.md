# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-08
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1026
current_task_status: IN_PROGRESS
last_completed_task: t1025
last_task_closure_commit: cb23dd0
last_verified_commit: 0682795
last_governance_review: 2026-09-08
governance_review_due: false
completed_tasks_since_governance_review: 0
next_task: t1028
next_task_source: docs/dev-plan.md R19.21 execution order
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- 已核对的源码工作区：无未提交源码修改。
- 已存在但不属于当前任务的未跟踪 Agent 配置：`.codex/agents/voxel-*.toml`；不得删除、覆盖或顺手提交。
- 如果再次出现未提交修改，必须先判断属于当前任务、用户修改、其他 Agent 还是未知来源。
- 未知来源、并发写入或无法确认的活动进程，统一进入 `NEEDS_HUMAN`，不得继续写代码。

## Recovery Point

- 最近闭环：t1025 动物繁殖（11a1a2f/9bcaff0/cb23dd0，矩阵 483→485）。daf76f0 为 amend 前悬空对象，作废。
- 治理审计：2026-09-08 首次初始化审计 **GREEN**（docs/governance-audit-2026-09-08.md）。
- 进行中：t1026 小麦农业（子agent WIP 4 文件，未提交——blockregistry.h/playercontroller/探针 +310）。
- 下一最小动作：续接收口 t1026（三教训：mtime/可达域/pinSet），绿后回标并进 t1028。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 本状态文件创建于 2026-09-08；由于此前没有本机制的治理锚点，第一次恢复必须先审计，不把历史 PASS 数量当作任务计数。
- 后续每完成 5 个完整 fix/test/docs 任务，或遇到批次结束、架构阶段切换、异常指标、每周定时触发，都要读取 `docs/autonomous-governance-2026-09-08.md` 并写治理结论。
- 任务计数只统计完整闭环任务，不统计单个 commit、矩阵 PASS 数或重试次数。
