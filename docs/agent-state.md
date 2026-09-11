# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-11 08:50
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1035
current_task_status: READY（t1034 已闭环，t1035 未开工；豹猫驯服——生鱼喂食驯服 / 信任态不跟随不项圈与狼分化 / 生鱼来源盘点，见 dev-plan R19.22 t1035）
last_completed_task: t1034 门/床/活板门 sneakPlace 存量缺门收口（placeBlock 右键链三分支补 !sneakPlace 门；181ad6d + 13347f9）
last_task_closure_commit: （docs 本提交）
last_verified_commit: 13347f9（矩阵 519 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 6
next_task: t1035（R19.22 主线第 6 项 / 批次末项）
next_task_source: docs/dev-plan.md R19.22 t1035
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1034 docs 提交（本提交）；前一 HEAD = 234eb50（t1033 docs）。工作区仅剩未跟踪验收证据（matrix_t1034_pos/neg/final.log、smoke_t1034.log、logs/voxelsandbox_t1034_tail20.log、build_t1034_*.log——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。

## Recovery Point

- 最近闭环：**t1034 门/床/活板门 sneakPlace 存量缺门收口（review0909 #2 存量登记项清偿）**（2026-09-11，181ad6d + 13347f9 + 本 docs）：placeBlock 右键链床/门/活板门三分支补 `!sneakPlace` 门（对齐同函数机关件八分支前置模式）；**床门加分支头**（先于 trySleepAt 调用——潜行放置整链优先，夜/雷暴/怪物门不触达，trySleepAt 直调面不变；MC 潜行右键床=放置）；铁门不受影响（t722）；t523 注释旧口径「门/活版门/床不绕过」同步改写。空手潜行 → `m_selectedBlock==Air` 守卫拦（t1028 口径）。矩阵 **516→519 PASS / 0 FAIL**（P-t1034a/b/c 真玩家链两向探针 + 各自门行 pinSet 针；matrix_t1034_pos/final.log；阴性轮单构建三摘恢复 pre-fix 条件行 → 恰红 516/3，床腿 refused=2=潜行点击进睡链病灶签名，matrix_t1034_neg.log，git checkout 反向 restore）。**rig 教训**：床碰撞盒=床垫低盒 y[0,~0.31]，侧脸 aim 须在带内（y 15.15）——aim 15.5 掠沿命中顶面 +Y 致放置落床顶格假红（首跑暴露已修）。voxelsandbox 重建 EXIT=0 + 冒烟 EXIT=124 + logs/voxelsandbox_t1034_tail20.log 留存（零 QML/C++ 错误）。
- 上一任务 t1033 床锚自然破坏联动记录（12f4558 批）已入 dev-plan t1033 段。
- 下一任务 **t1035 豹猫驯服**：生鱼喂食驯服（MC 口径走近缓慢喂食，驯服后信任态不跟随不项圈——与狼模型刻意分化）；生鱼来源盘点（若钓鱼无鱼则登记简化或补钓鱼产出）。探针：驯服/信任态/与狼的分化口径。**先盘点：钓鱼产出面（fishing loot table）、生鱼物品 id、狼驯服链复用面（t1031 setTameRollOverride 缝）、豹猫模型/生成面。**
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
