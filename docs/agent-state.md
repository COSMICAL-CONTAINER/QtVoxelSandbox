# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-11 01:50
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1033
current_task_status: READY（t1032 已闭环，t1033 未开工；床锚自然破坏联动——爆炸/岩浆等非玩家路径销毁锚床 → clearBedSpawn，经信号/QML 编排守分层，见 dev-plan R19.22 t1033）
last_completed_task: t1032 掉落物 instancing 批 2 + review0910 #2/#3/#4 收口（482b9fa + 1af5316）
last_task_closure_commit: （docs 本提交）
last_verified_commit: 1af5316（矩阵 513 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 4
next_task: t1033（R19.22 主线第 4 项）
next_task_source: docs/dev-plan.md R19.22 t1033
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1032 test 提交（1af5316）；本 docs 闭环后 HEAD = t1032 docs 提交。工作区仅剩未跟踪验收证据（matrix_t1032_pos/neg/neg2/final.log、smoke_t1032.log、logs/voxelsandbox_t1032_tail20.log、build_t1032_*.log——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。

## Recovery Point

- 最近闭环：**t1032 掉落物 instancing 批 2 + review0910 #2/#3/#4 收口**（2026-09-11，482b9fa + 1af5316 + 本 docs）：3D 形状族 per-id 桶（BlockDropInstancing `shapeFamily` 扩维缺省 false 零回归 + Main.qml blockShapeInstHost 8 桶批 1 同构 + ItemShapeGeometry per-bucket 单几何 + 材质逐字同参 + 94 dropBookNode 上移保书〔世界变换逐位不变〕）；review0910 #3 做空转门（两池同款：构造不启钟 / refreshTicker 按桶内活体启停 / 活跃沿 start+markDirty 兜底 / probeTickerActive 探针）；#2 降级 headless 可达面做腿（P-t1032b，QML 编排面盲区如实登记 + 实机确认项）；#4 不让位维持现状（kept 钉 minCount=2）。矩阵 **509→513 PASS / 0 FAIL**（matrix_t1032_pos/final.log；阴性轮两轮：摘形状过滤恰红 512/1=P-t1032a〔matrix_t1032_neg.log〕、摘空转门恰红 512/1=P-t1032c〔matrix_t1032_neg2.log〕，均 restore 反向）；voxelsandbox 重建 EXIT=0 + 冒烟 EXIT=124 + logs/voxelsandbox_t1032_tail20.log 留存。rig 教训：空转门活跃沿腿 plain 模式 feeder 不可配 3D 族 id（谓词侧永不活跃=门正确行为）。
- 上一任务 t1031 狼驯服闭环记录（31bc798 批）与 #14 rig 迁移已入 dev-plan t1031 段。
- 下一任务 **t1033 床锚自然破坏联动**：爆炸/岩浆等非玩家路径销毁锚床 → clearBedSpawn（review0909 #4；经信号/回调转发守分层——Entities 清块路径不可反向依赖 Game，沿 seedChanged 收口模式经 QML 编排或信号）。探针：TNT 炸锚床 → 重生点失效 + 播报恰一次；水冲不触（床非附着块不在 wash 清单，口径登记）。**先盘点：Entities 破块路径的现有信号面（blockBroken 类）与 clearBedSpawn/bedSpawnAnnounce 消费沿。**
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
