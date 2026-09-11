# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-11 12:20
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R19.22 批次收口（批次 review + 统计 + audit #4）
current_task_status: READY（t1035 已闭环=批次六单+t1036 插单全闭环；收口工作项见 dev-plan R19.22 段尾与治理触发规则——批次结束即触发 audit #4）
last_completed_task: t1035 豹猫驯服盘点零缺口 + 分化口径现状钉（驯服链 t481/t949 在库；现库口径=现代 MC 跟随+瞬移+项圈，dev-plan 信任态设想登记过时；生鱼=钓鱼池产物在库；4459d33 探针 P-t1035）
last_task_closure_commit: （docs 本提交）
last_verified_commit: 4459d33（矩阵 520 PASS / 0 FAIL，binary 与源 mtime 对齐）
last_governance_review: 2026-09-09（audit #3 GREEN）
governance_review_due: true（audit #4：R19.22 批次结束触发；自 audit #3 已完成 7 个完整闭环）
completed_tasks_since_governance_review: 7
next_task: R19.22 批次 review + 统计 + audit #4（批次收口单）
next_task_source: docs/dev-plan.md R19.22 段 + autonomous-governance 触发规则
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1035 docs 提交（本提交）；前一 HEAD = 026681c（t1034 docs）。工作区仅剩未跟踪验收证据（matrix_t1035_pos/neg/final.log、smoke_t1035.log、logs/voxelsandbox_t1035_tail20.log、build_t1035_*.log——日志不入库惯例）与 `.codex/` 外部工具产物（豁免、不删不提交）。
- 纪律（2026-09-09 立）：每任务 docs 闭环必须同步本文件（t1028~t1027 期间曾滞后一次，已纠偏）。
- 验收日志新纪律：日志一律带任务号（matrix_reviewfix_pos/neg/final.log 模式），防覆写丢证据。
- 冒烟证据链新纪律（review0910 #2，t1030 首例执行）：app 冒烟除 EXIT=124 外同步留存 logs/voxelsandbox.log 尾部 20 行。
- 阴性轮 restore 纪律（2026-09-11 重申，t1035 执行）：**一律 Edit 工具反向复原，严禁 git checkout/reset**（t1034 曾违规未损但已记备注；t1035 复原后 git diff 零残留实证）。

## Recovery Point

- 最近闭环：**t1035 豹猫驯服盘点零缺口 + 分化口径现状钉（R19.22 批次末项）**（2026-09-11，4459d33 + 本 docs）：**零 src 功能改动**。盘点三问回标：①驯服链完整可达（t481 概率驯服/失败耗鱼/三色变体 + t949 实机右键缝 + t963 变体收口全在库）；②**驯服后行为口径=现代 MC**（跟随 kOcelotFollowSpeed=4.0 / kFollowMinDist=2.5 / >kOcelotTeleportDist=12 瞬移 + 坐站命令 + 红项圈 t963 + 不防御 t923）——dev-plan 本条目「信任态不跟随不项圈」设想被 t878/t963 先行演化掉，**登记过时、行为零改动、探针按现状钉**（真分化=无防御/生鱼驯服/三色变体）；③生鱼来源=钓鱼池在库（fishingPool RawFishId 最高权重 60/108 + t836 行为钉在库），无需补产出。矩阵 **519→520 PASS / 0 FAIL**（P-t1035：猫坐留守/站跟随/>12 瞬移+近距不跳/狼猫状态 accessor 跨型互查恒 false/钓鱼池直调钉；matrix_t1035_pos/final.log 均 520/0，pos 首跑一次过）。**阴性轮**：摘 aiOcelot 站态跟随 chase（`false &&` 前缀）→ 518/2 恰红同缝两腿（P-t1035(b) + review0830 batch C (a) 生存对照腿——盘点漏检的一处猫跟随既有钉，阴性轮实证共缝；其余 518 全绿；matrix_t1035_neg.log）→ **Edit 反向复原**（git diff 零残留）。voxelsandbox 重建 EXIT=0 + 冒烟 EXIT=124 + logs/voxelsandbox_t1035_tail20.log 留存（零 QML/C++ 错误）。构建环境教训：后台构建任务被环境 kill 一次（本次会话第二例），nohup 脱离重启即恢复——长构建一律 nohup 起手。
- 上一任务 t1034 门/床/活板门 sneakPlace 收口记录已入 dev-plan t1034 段。
- 下一任务 **R19.22 批次收口**：批次 review（六单 t1030-t1035 + 插单 t1036 全闭环核对）+ 统计（矩阵 505→520，+15 探针）+ **audit #4**（批次结束触发，读 autonomous-governance 章节写结论）。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；2026-09-09 audit #2 GREEN（30463ee）；2026-09-09 audit #3 GREEN（批次结束触发，R19.21 收口）。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论，不全文加载。
- 任务计数只统计完整 fix/test/docs 闭环，不统计单个 commit、矩阵 PASS 数或重试次数。
