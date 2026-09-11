# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-12 03:25
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1041
current_task_status: READY
last_completed_task: t1042 被动型受击惊逃（panic flee MC 原版口径）+ 野狼唯一反击（526→530，阳性一次过；阴性恰红 a/b/c；t1031b 中立门针重钉）
last_task_closure_commit: （docs 本提交；代码终态见 test(t1042) 提交）
last_verified_commit: test(t1042)（矩阵 530 PASS / 0 FAIL，matrix_t1042_final.log 权威，binary 与源对齐）
last_governance_review: 2026-09-11（audit #4 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 4
next_task: t1041（R19.23 执行顺序 t1038✅→t1040✅→t1039✅→t1042✅→t1041 收官；之后 parity bug 波 t1043-t1046（e131797 用户裁决「一切按原版」）→ R19.23 批次 review + audit #5）
next_task_source: docs/dev-plan.md R19.23 段
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 收口 docs 提交，工作区干净；`.codex/` 豁免不删不提交。
- 纪律：①每任务 docs 闭环同步本文件；②验收日志带任务号；③阴性轮 restore 一律 Edit 反向（严禁 git checkout/reset）；④长构建 nohup 起手；⑤用户修正文件（docs/Review_*.md）开工前必读，事实优先级高于 dev-plan 旧口径；⑥**多轮阴性轮间摘 C++ 病灶后必须先重建再跑下一轮**（t1038 教训：未重建旧 binary 混入上一病灶签名）。

## Recovery Point

- 最近闭环：**t1042**（2026-09-12 03:25）：被动型受击惊逃 panic flee（牛/羊/猪/鸡/未驯服豹猫 + 狼幼崽共用；kPanicDuration=8s/kPanicSpeed=2×walk；aiPanicFlee 三消费点置顶压求偶/引诱/幼随/吃草）+ 野狼唯一反击（setWolfProvoked 受击沿写点 chasing=玩家，只在受击沿置位不复活旧「见人就咬」，超时/超距/不可锁定清除同 hostile 收口，咬击 4HP 走 t321 节流）+ 驯服狼零反击维持 + 幼狼恒驯服（t480 继承）受击只惊逃，矩阵 **526→530 PASS / 0 FAIL**（matrix_t1042_final.log 权威），阴性单轮三摘恰红（527/3 = P-t1042a/b/c 惊逃腿，t1042d 针腿恒绿，matrix_t1042_neg.log），voxelsandbox EXIT=0 + 冒烟 EXIT=124 + tail20 留存。**登记**：Q_UNUSED(playerTargetable) 退役 → P-t1031b cpp-wolf-neutral-gate 针重钉为 aiWolf 未驯服分支头（t1031「激怒反击面登记不做」按用户纠正口径清偿）；主人打自家驯服狼时 setWolfTarget 共享目标语义会让其它驯服狼追咬该狼（t480 既有，非本单范围）。
- 下一最小动作：派 t1041 instancing 批 4 收官（工具 3D 族五几何+tier 色 per-instance color + 工具/材料 billboard 族 per-id 贴图桶 + 异形 billboard 族；盘点 t1039 GlowShellInstancing 第三池先例与 ItemEntityManager 族谓词面；F3 draw call 对比列实机；矩阵基线 530）——之后按 e131797 用户裁决插入 parity bug 波 t1043-t1046，再 R19.23 批次 review + audit #5。
- 实机确认：R19.23 已闭环 4 项（t1038 书台同相 / t1039 壳观感四项 / t1042 惊逃与狼反击观感）+ R19.22 总表 16 项 + 回归三单（t1005/t1006/t1007）待用户实机数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11 R19.22 批次）GREEN。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。（R19.23 批次 t1038/t1039/t1040/t1042 四闭环 + t1041 收官后达「批次结束」触发 → audit #5。）
- 任务计数只统计完整 fix/test/docs 闭环。
