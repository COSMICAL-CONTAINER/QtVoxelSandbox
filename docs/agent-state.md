# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-09 21:12
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R19.21-batch-review
current_task_status: IN_PROGRESS
last_completed_task: t1027-batch1（R19.21 全六项 t1024-t1029 已闭环）
last_task_closure_commit: 6c13e51
last_verified_commit: 6c13e51
last_governance_review: 2026-09-09（audit #2 GREEN，30463ee）
governance_review_due: true（批次结束触发：R19.21 统计收尾时一并做）
completed_tasks_since_governance_review: 3（t1029 / review0909清偿 / t1027首批）
next_task: 批次review收口（整合发现→修真实问题→统计+治理）→ R19.22 立项
next_task_source: dev-plan R19.21 尾行「全部完成后照例只读 review + 统计」
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 6c13e51，工作区干净；唯一未跟踪 `.codex/` 为外部工具产物，豁免、不删不提交。
- 本文件 2026-09-09 21:12 前曾滞后（停在 t1026/t1028），系 t1028~t1027 期间仅回标 dev-plan 未同步本文件所致 DOC_DRIFT，已按 git 实况纠正；今后每任务 docs 闭环时必须同步本文件。

## Recovery Point

- 最近闭环：R19.21 六项 t1024-t1029 全 ✅✅（床/繁殖/麦种/音符盒/RNG缝/instancing首批），矩阵 480→505 PASS / 0 FAIL，全部带阴性轮 + app 构建 EXIT=0 + offscreen 冒烟（t1027 末轮 matrix_run_t1027b.log，binary 与 6c13e51 源对齐）。
- review0905/06/07/09 四份 review 已全部清偿（waves A-F2），低级发现修复、Info 登记。
- 进行中：R19.21 批次 review（b91cb87..6c13e51 十二笔，双只读 reviewer 并行；前一轮 reviewer 因上下文压缩丢失，本轮为重发）。
- 下一最小动作：整合两 reviewer 报告 → P0/P1/P2 真实问题按 wave 模式修复（修→阴轮→全矩阵→app冒烟→提交+回标）→ 批次统计 + 治理结论（批次结束触发）→ R19.22 立项继续跑。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化审计 GREEN（docs/governance-audit-2026-09-08.md）；2026-09-09 audit #2 GREEN（30463ee，覆盖五任务窗 t1024-t1028，flaky 族灭绝确认）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 docs/autonomous-governance-2026-09-08.md 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
