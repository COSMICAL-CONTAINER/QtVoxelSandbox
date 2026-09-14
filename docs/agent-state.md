# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-15 02:05（R20.03 闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.04（基础类型——R20 重构主线第 2 单）
current_task_status: READY
last_completed_task: R20.03 测试分层（测试大 TU 拆分 + 矩阵 --filter 两项提速投资；09b8cb7 refactor + 3daab66 feat + 本 docs）
last_task_closure_commit: 3daab66（代码终态 = 09b8cb7 + 3daab66）
last_verified_commit: 3daab66（矩阵 545 PASS / 0 FAIL ×2：matrix_r2003_split_run2/3.log；filter 面 t1046=7P/494S + review26-1=7P/494S/1.6s；app 重建 EXIT=0 + 冒烟 EXIT=124 存活 + logs/voxelsandbox_r2003_tail20.log）
last_governance_review: 2026-09-14（audit #6 YELLOW → GOV-20260914-1=t1049 已闭环；R20.03 为 R20 首单，治理计数从 0 起算）
governance_review_due: false
completed_tasks_since_governance_review: 1
next_task: R20.04 基础类型（refactor-plan R20 序）
next_task_source: docs/dev-plan.md R20.03 段「顺序（终）」
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = R20.03 feat 提交（3daab66），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑧全在案；**大 TU 编译一律 -j 1**（09-13 蓝屏教训）。**R20.03 起矩阵测试为 tools/matrix/ 分层结构**：改探针只重编对应段 TU（12-19s）+ `--filter <substring>` 只跑本任务腿（秒级），**禁再往单文件堆腿**；新腿落对应段文件（任务族分段见段文件头注释），新段须 ≤500KB。

## Recovery Point

- 最近闭环：**R20.03 测试分层**（2026-09-15，09b8cb7 refactor + 3daab66 feat + 本 docs）：tools/redstone_matrix_test.cpp 单 TU（3.4MB/-j1 编译 ~85min）拆为 tools/matrix/ = main + helpers（MatrixRun 承载原 main 局部共享 rig 状态）+ 8 段 TU（逐字节搬移，切段 md5 拼接 == 原 main 体 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）；逐段 diff 终检 18 行随迁豁免（14 处 __FILE__ ../src/ui→../../src/ui + despawn 钉路径→section02）；--filter 子串选腿 + SKIP 记账（406 语句+95 矩阵名=501 门控单元账面闭合）。矩阵 545→545（2× matrix_r2003_split_run2/3.log 全绿，PASS 行与 t1049 权威恒等——仅 t813 构建戳/t997 内嵌计时数同类漂移）；filter 面 t1046（7P/494S）+ review26-1（7P/494S，despawn 钉腿绿，1.6s）；无 filter 全跑回归逐位恒等。提速实测：单段增量 12-19s / 全量冷编 2m4s / filter 面 1.6s（对照拆前 85min 编译）。**过程坑登记**：段边界裸花括号配对表在 t1020 共享作用域切穿（S6 编译期实锤）→ 字符串感知深度扫描重定界；__FILE__ 相对路径随迁遗漏致首跑 530/15（QML real-chain temp 转存全断）→ 14 处补迁；git checkout -- tools/matrix/ 三次卷走未提交的 helpers 编辑（ discipline: 部分路径 checkout 前先查未提交面）。src/Renderer/mobmodel.h 与 src/Core/resourcepackmanager.cpp 注释仍指旧文件名（src 冻结，文档级漂移登记 R20.04+ 顺带修）。
- t1049 GOV-20260914-1 纠偏闭环（2026-09-14，48c2f09 fix + 3351a91 test + docs）：review26-1 翻红根因 = 腿跨段 mob 泄漏 + t1045 踩踏 RNG，非 UB；矩阵 545/0 ×3。
- R19.23 批次全貌：t1038→t1040→t1039→t1042→t1041→t1043→t1047→t1044→t1045→t1046→t1048→t1049，矩阵 521→545。
- **下一最小动作**：R20.04 基础类型（R20 主线第 2 单）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→GOV-20260912-1 已闭环。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
