# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-10 22:30
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1032
current_task_status: READY（t1031 已闭环，t1032 未开工；掉落物 instancing 批 2 + 补桶满 8 降级路径行为腿 + 8×16ms 定时器收口评估，见 dev-plan R19.22 t1032）
last_completed_task: t1031 狼驯服收口（9bc2c73 + 31bc798）
last_task_closure_commit: （docs 本提交）
last_verified_commit: 31bc798（矩阵 509 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 3
next_task: t1032（R19.22 主线第 3 项）
next_task_source: docs/dev-plan.md R19.22 t1032
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1031 test 提交（31bc798）；本 docs 闭环后 HEAD = t1031 docs 提交。工作区仅剩未跟踪验收证据（matrix_t1031_pos/neg/final.log、smoke/logs 尾部留存、ninja 日志——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。

## Recovery Point

- 最近闭环：**t1031 狼驯服收口**（2026-09-10，9bc2c73 + 31bc798 + 本 docs）：Entities 层增量（驯服概率测试缝 `setTameRollOverride`——缺省 -1 生产零改动、0/999 两端钉死；野狼**中立方收口**——aiWolf 未驯服分支纯 aiWander、旧 t480 追咬玩家分支退役 + kWolfDetectRange 删除、激怒反击面登记不做）+ 遗产五面收口验证（Game 骨头分流/空手坐站切换/参战接线、QML 项圈 collarVisible、超距传送 kWolfTeleportDist=12——全为 t480/t831/t878/t986 遗产零改动）。矩阵恒 **509 PASS / 0 FAIL**（matrix_t1031_pos/final.log；阴性轮摘驯服分流恰红 508/1=P-t1031a（diag tamed=false/sitOn=false），matrix_t1031_neg.log；r0830C(d) #14 rig 随收口迁驯服狼防御分支供流）；voxelsandbox 重建 EXIT=0 + 冒烟 EXIT=124 + logs/voxelsandbox_t1031_tail20.log 留存。
- 上一任务 t1030 骨粉催熟闭环记录（eaef577 批）与探针 rig 教训（QML selectedBlock 绑定须显式建模）已入 lessons-learned / dev-plan t1030 段。
- 下一任务 **t1032 掉落物 instancing 批 2 + review 登记收口**：3D 形状族 per-id 桶（ItemShapeGeometry 几何缓存共享）+ **补桶满 8 降级路径行为腿**（≥9 种同屏整立方 id，第 9 种起 delegate 渲染不断链不双渲——P3 登记①）+ **8×16ms 定时器收口评估**（共享单 ticker 或全桶空 `running:false` 门控——P3 登记②，若做须阴性轮定时器行为）。探针：族 2 分桶/降级腿/定时器门控腿。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
