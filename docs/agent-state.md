# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-11 14:20
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R19.23 立项（R19.22 批次收口 docs 为过渡项：批次 review A/B 发现登记 + 统计 + audit #4）
current_task_status: READY（t1037 插单已闭环=批次 review A P3-2 清偿；R19.22 批次六单+插单 t1036/t1037 全闭环；收口工作项见 dev-plan R19.22 段尾与治理触发规则——批次结束即触发 audit #4）
last_completed_task: t1037 睡眠中锚床被毁即醒（MC 床毁即醒口径；批次 review A P3-2 定夺修；clearBedSpawn 单点睡眠中断=统一收口；1e7eca3 fix + 521ab79 test）
last_task_closure_commit: （docs 本提交）
last_verified_commit: 521ab79（矩阵 521 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: true（audit #4：R19.22 批次结束触发；自 audit #3 已完成 8 个完整闭环）
completed_tasks_since_governance_review: 8
next_task: R19.22 批次收口 docs（批次 review A P3-1/P3-2 等发现清偿登记 + 统计 505→521 + audit #4）→ R19.23 立项
next_task_source: docs/dev-plan.md R19.22 段 + docs/Review_2026-09-11.md + autonomous-governance 触发规则
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1037 docs 提交（本提交）；前三 HEAD = 521ab79（t1037 test）/ 1e7eca3（t1037 fix）/ e673860（t1035 docs）。工作区仅剩未跟踪验收证据（matrix_t1037_pos/neg/final.log、smoke_t1037.log、logs/voxelsandbox_t1037_tail20.log、build_t1037_*.log——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。
- 阴性轮 restore 纪律（2026-09-11 重申，t1037 再执行）：**一律 Edit 工具反向复原，严禁 git checkout/reset**（t1034 曾违规未损但已记备注；t1035/t1037 复原后 git diff 零残留实证）。

## Recovery Point

- 最近闭环：**t1037 睡眠中锚床被毁即醒（批次 review A P3-2 定夺修，MC 口径=床毁即醒）**（2026-09-11，1e7eca3 + 521ab79 + 本 docs）。**病灶**：睡眠 Settled 计时中锚床被爆炸摧毁（t1033 路径 blockDestroyedBed → clearBedSpawn 清锚播报）后 m_sleeping 仍真 → sleepAdvanceToDawn 无条件重写 m_spawnPos/m_bedAnchor 回已毁床位并把 m_bedSpawnValid 翻回 true → 重生点在已不存在床位上重新武装。**修法=统一收口（盘点先行后定夺）**：clearBedSpawn 头部 `if (m_sleeping) cancelSleep();`——三调用面组合语义登记：t1033 爆炸槽（病灶本体）/ onWorldSeedChanged（换代前必经 release 已清睡态，防御）/ finishMiningAt（睡眠中输入域冻结与挖床互斥，防御）；中断后 updateSleep 早退 → sleepAdvanceToDawn 永不可达（绝不跳晨，醒时相位保持）；出床瞬移走 cancelSleep→leaveBedTeleport（床周首个可站位格）。契约红线守约：sleepAdvanceToDawn 本体 / 天气清态 / bedSpawnAnnounce 零触碰。矩阵 **520→521 PASS / 0 FAIL**（P-t1037 行为级：trySleepAt 真实入睡 → 泵至 Settled → TNT 真引信链固定 dt 400×0.015625 只泵实体（与玩家睡眠计时正交）→ 断言醒+相位不变（setRunning(false) 冻结时钟=跳晨单一判别器）+ valid 假 + lost 恰 1 + spawn pristine + 冲过原 Settling 窗不重新武装；matrix_t1037_pos/final.log 均 521/0，pos 首跑一次过）。**阴性轮**：摘 clearBedSpawn 睡眠中断（`false &&` 前缀）→ **恰红 520/1 = P-t1037**（diag `woke=false noRearm=false valid=true spawn=20.5 41 20.5 phase=0.75` = 病灶全签名重现；matrix_t1037_neg.log）→ **Edit 反向复原**（git diff 零残留）。voxelsandbox 重建 EXIT=0 + 冒烟 EXIT=124 + logs/voxelsandbox_t1037_tail20.log 留存（60fps 稳态，零 QML/C++ 错误）。
- 上一任务 t1035 豹猫驯服盘点记录已入 dev-plan t1035 段。
- 下一任务 **R19.22 批次收口 docs → R19.23 立项**：批次 review 发现清偿登记（Review_2026-09-11 #1 94 书相位待定夺 / #2 实机定向冒烟待办等）+ 统计（矩阵 505→521，+16 探针）+ **audit #4**（批次结束触发，读 autonomous-governance 章节写结论）→ R19.23 批次立项。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
