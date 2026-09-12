# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-12 12:55
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1044
current_task_status: READY
last_completed_task: t1043 铁轨水蚀 + 矿井防水（裁-1 清偿：Rail 三族入 isAttachableBlock 水毁附着族 + placeMineshaft 生成期干燥门〔候选收集→walk 后切比雪夫距 2 邻域无水才落块〕；矩阵 534→536，final 536/0；阴性轮1 摘家族接入恰红 5〔旧 neg1 在案〕、轮2 摘干燥门恰红 535/1=t1043b 唯一红；断链恢复流程重走全链，详见 dev-plan t1043 断链与恢复段）
last_task_closure_commit: （docs 本提交；代码终态见 test(t1043) 提交）
last_verified_commit: test(t1043)（矩阵 536 PASS / 0 FAIL，matrix_t1043_final.log 权威，binary 与源对齐；voxelsandbox 重建 EXIT=0 + offscreen 冒烟 SMOKE_ALIVE_12S + 60fps 稳态 tail20 留存 voxelsandbox_t1043_tail20.log）
last_governance_review: 2026-09-11（audit #4 GREEN）
governance_review_due: true
completed_tasks_since_governance_review: 6
workspace_clean: true
mutation_restored: true
positive_log: matrix_t1043_pos2.log
final_log: matrix_t1043_final.log
next_task: **先执行 audit #5 治理审计**（completed_tasks_since_governance_review=6 已触发「每 5 个完整闭环」计数条件，用户 0912 评审确认 due；不拖到批次末）→ 之后 t1044 蜘蛛爬墙（裁-2）→ t1045 耕地退化（裁-3）→ t1046 parity 小修合集（含 ⑦ noteClips 析构补齐，用户 0912 评审 #4）→ R19.23 批次 review
next_task_source: docs/dev-plan.md R19.23 段 + docs/parity-ledger.md 用户裁决 + docs/dev-plan.md「用户评审落地（2026-09-12 上午）」段
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 收口 docs 提交，工作区干净；`.codex/` 豁免不删不提交。
- 纪律：①每任务 docs 闭环同步本文件；②验收日志带任务号；③阴性轮 restore 一律 Edit 反向（严禁 git checkout/reset）；④长构建 nohup 起手；⑤用户修正文件（docs/Review_*.md）开工前必读，事实优先级高于 dev-plan 旧口径；⑥**多轮阴性轮间摘 C++ 病灶后必须先重建再跑下一轮**（t1038 教训：未重建旧 binary 混入上一病灶签名）；⑦**工作区一致性硬门（2026-09-12 用户裁决）**：开工前 `git status` 与本 Control Block 交叉核对——工作区存在未提交改动 / 未恢复变异而 state 仍 READY 时，**禁止开始新任务**，置 `RECOVERY_REQUIRED`，只允许恢复、构建、测试、更新状态（t1043 断链教训：阴性轮②中断后变异残留，本文件仍报 READY+工作区干净，恢复 Agent 会拿错误状态继续）；⑧**规格变更门（2026-09-12 用户裁决）**：实现偏离原计划文字须先写 SPEC_CHANGE（dev-plan 任务行改写 + 偏差点明示）再实现，禁止收口文档单方面回溯改口径（t1028 案例登记于 dev-plan）；低风险纯实现细节可明示登记后继续，行为口径 / 资产形态 / 用户可见面的偏离挂起待裁决。

## Recovery Point

- 最近闭环：**t1043**（2026-09-12）：铁轨水蚀 + 矿井防水（parity bug 波首单，裁-1 清偿）——①blockregistry.h isAttachableBlock 追加 `|| isRail(id)`（普通 103/动力 127/探测 128 三族；review0906 #10 豁免废除）；②world.cpp placeMineshaft 轨铺设改「候选收集 → walk 完后统一干燥门落块」（切比雪夫距 2 的 5×5×5 邻域无水才落块；连接位 t565④ 统一重算不受影响）。矩阵 **534→536 PASS / 0 FAIL**（matrix_t1043_final.log 权威）；阴性轮两轮：轮1 摘 blockregistry 家族接入恰红 5（matrix_t1043_neg1.log，断链会话在案）、轮2 摘干燥门恰红 535/1 = t1043b 唯一红（matrix_t1043_neg2.log，恢复会话补齐）。**断链与恢复经过**（用户 0912 评审指认后重走）：断链会话阴性轮2 构建中断（僵尸 cmake/ninja 锁 build 目录）→ 干燥门变异残留 + 本文件曾误报 READY → 恢复会话：RECOVERY_REQUIRED 置态 → Edit 反向恢复 → 重建 → pos2 536/0 → 重注入 → neg2 恰红 → 恢复 → final 536/0 → voxelsandbox 重建 + 冒烟（SMOKE_ALIVE_12S，60fps 稳态，零 Repeater3D 孤儿告警）。**环境坑登记**：会话沙箱后台任务 stdout/stderr 重定向全面蒸发（shell/cmd/tee 三式 0 字节）→ 长任务矩阵/冒烟改 **PowerShell Start-Process detached 跑法**（stderr 实时落盘）；断链遗留挂起脚本会自发启动矩阵进程（wmic 按 CreationDate/ParentProcessId 甄别 taskkill）。
- 实机确认（累计）：R19.23 已闭环 6 项（t1038/t1039/t1041/t1042 各项 + **t1043 worldgen 矿井轨含水带出现率与流水冲玩家轨**）+ 用户 0912 评审追加：**批 1/2/4 桶池 Repeater 3D delegate 挂载实机渲染验证**（headless 冒烟零孤儿告警为间接信号；F3 draw call 前后对比 + 合批 Model 可见性）+ **Review_2026-09-12 #1 壳排除链 >128 溢出双壳/丢壳** + #2 stringPass 废钟（headless 不可见，实机/源码钉复核）+ R19.22 总表 16 项 + 回归三单（t1005/t1006/t1007）待用户实机数据。
- 长期方向（用户 0912 评审）：instancing 治理到路径 a 终点后**暂停新增 QML instancing 家族**；t1043-t1046 完成后转 R20 重构主线（R20.03 测试分层 → 20.04 基础类型 → 20.05 Command/Event/Snapshot → 20.06 GameSession → 20.07 WorldFacade → 20.09 Chunk 生命周期 → 20.11 后台 GenerationJob）。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。**断链恢复按纪律⑦硬门执行**（工作区与 Control Block 不一致即 RECOVERY_REQUIRED）。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11 R19.22 批次）GREEN。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。（R19.23 批次 t1038/t1039/t1040/t1042 四闭环 + t1041 收官后达「批次结束」触发 → audit #5。）
- 任务计数只统计完整 fix/test/docs 闭环。
