# 治理审计 #14 — 2026-09-20 01:4x（触发：治理计数 5/5——t1060/t1061/t1062/t1063 完整闭环 + t1064 关单在即；含 GLM5.3/ZCode → Claude Code 主控交接事件）

- 触发：audit #13 后五个任务窗口（t1060-t1064，矩阵 671→687）。审计执行时点 t1064 fix+test 已落、关单 docs 未落（主控脱离式复跑在途，PID 163558）——按 t1064 代码+测试证据已齐备现状审计，关单条目列强制补齐条件。
- 窗口/起止 commit：086cbae（#13 审计提交）→ b23387f（HEAD，test(t1064)），共 20 提交 = 4 完整闭环 × fix/test/docs + t1064 fix/test + 立项/设计/纯 docs 8 笔。
- 最近完成任务：t1060（fixed bake 异步化/C1 完全体）、t1061（P0 入口 livelock）、t1062（走回内容漂移）、t1063（差分池 patch）。当前任务：t1064 IN_PROGRESS（退出存档退避，关单流程中）。
- 批次：连续开发串行队列（P0 livelock 波 → 走回漂移 → 池 patch → 存档退避）；交接事件 1 起（94bed7c 起 Claude Code 接棒）。
- git 状态：main；工作区仅 `.codex/` 豁免未跟踪（零提交触及）；本地领先 origin/main 12 笔（origin/main = 94bed7c，即已推送笔）。
- binary 状态：矩阵 binary 与 pos/final 双日志对齐（t813 戳惯例）；app 重建 EXIT=0 + offscreen 冒烟 EXIT=124 各单关单条目在案（本轮未独立重跑 app——主控复跑面承载）。
- 矩阵状态：**687 PASS / 0 FAIL**（t1064 pos/final）；全窗基线 671→675→679→683→687 零 FAIL。
- 闭环数：4 完整 + t1064 两段（fix+test）。
- 阴性轮异常：无。五单 r203[4-8]_neg{1,2}_{red,restore} 共 20 份日志全在，红/还原方向全对，红面腿数与声明逐单吻合（见事实核对 3）。
- flaky：无新报（t997/t979/t830 登记类零漂移）。

## 事实核对

1. **账实相符**：t1060(fd30855/bed5ee5/12ce2e3)、t1061(2e1ece6/4c858c1/572b367)、t1062(6c63b77/5f0af30/b0bd934)、t1063(88c9c24/6656f2e/d9fb80f) 三段提交配对完整、hash 与 dev-plan ✅ 一致；t1064 fix(6f0a052)+test(b23387f) 在案。fix 提交触及 tools/matrix 均系钉面同变更修订（t1062→section27 r2024d；t1063 test 段→section24 r2021d/section36 r2035b；t1064→section06/08/23 t974 源钉 + t1023c/r2020d 线程白名单），零削钉。docs 提交只触 docs/；test 提交只触 tools/matrix + CMakeLists；无 build/logs/saves 误提交。
2. **硬门**：QML 全窗两处登记例外面——t1063 池段四 hunk（Main.qml @328/@402/@781/@4725，+115/−32 集中池机器段，`_refreshChunkVisibility`/`kickWorldMeshSync` 两消费端零触碰）+ t1064 存档链三 hunk（@1205/@1216/@1247，runExitSave/onClosing 既有 t974/t1057 登记面内，两份 0ms 重试散写收敛为 `runExitSaveWithBackoff` 唯一实现 = 去重方向）；**玩法路径零迁移**保持。worldstore.{h,cpp} 全窗零 diff——t1064 探针走独立命名连接绕开冻结域（savebridge.cpp 实现头注三选型对比立证：saveProgress 会破 t974「成功=+3」口径、只读 SELECT 测不出 EXCLUSIVE 持锁）。数据安全零迁移。
3. **验证纪律（逐项实测）**：矩阵五单 ×2 双日志在案且 `grep -ac '^PASS'` 实测 = 671/675/679/683/687，全 0 FAIL（build/matrix_r2034_{pos,final}.log + build/matrix_t106[1-4]_{pos,final}.log）；t1064 腿集合恒等（前 110 字符键 LC_ALL=C diff = 空，首轮 sort locale 警告系 LC_ALL 只前缀给 diff 所致，干净版复跑 IDENTICAL）；阴性五单红面与声明吻合——r2034 N1=0P/4F{a,b,c,d}/N2=3P/1F{a}；r2035 3P/1F{b}/2P/2F{c,d}；r2036 3P/1F{b}/2P/2F{b,c}；r2037 8P/1F{d}/2P/2F{b,c}；r2038 2P/2F{b,d}/2P/2F{c,d}（**声明面文本待 t1064 关单 docs 补录**）；还原日志全绿（4P/0F 或对应基线）；相邻 filter 零污染日志在案（build/matrix_t1064_adj_{t974,r2020,r2027,r2031,t1023,r2038}.log、matrix_t1063_adj_{r2021,r2035,r2036}.log）。
4. **主控复跑**：t1061 两段部分复跑 487/535 PASS 全 0 FAIL（外部中止如实披露，覆盖 ~79%）；t1062 复跑 679/0 完整、t1063 复跑 683/0 完整（matrix_t1062_orch_verify.log / matrix_t1063_orch_verify.log 实测）；t1064 复跑在途（matrix_t1064_orch_verify.log 审计时点 423 PASS / 0 FAIL，仍在写）。
5. **账本**：dev-plan t1060-t1063 ✅ 落地条目与 git 全对应；t1065 QUEUED 立项在案（docs/dev-plan.md:4297-4299，前置既有面非回归、修法三候选、验收面齐）；agent-state 控制块（t1064 IN_PROGRESS、filter r2038）与 Recovery Point（最新闭环 t1063）一致。**发现一处缺口：dev-plan 无 t1064 立项条目**——d9fb80f 提交宣称 "open t1064" 但该提交的 dev-plan diff 仅新增 t1065 段（@@ -4292,7 +4292,11，唯一 +### 为 t1065）；全文件 grep `t1064` 仅命中 t1062/t1063 关单条目的「下一任务」提及与 t1065 条目排期引用。任务定义现仅存 agent-state 控制块。→ 纠偏条件见「自动动作 1」。
6. **署名尾注（窗口实测 10 笔，非交接披露的 3 笔）**：`Co-Authored-By: ZCode` ×7 = 61c9887/fd30855/bed5ee5/12ce2e3（t1060 全批）+ ebb5ec9/4110dd6/faa757f（readme/survey/review 三纯 docs）——交接前 ZCode 时代自身实践，落库在 #13 审计提交之后故 #13 未覆盖；`Co-Authored-By: Claude Opus 5` ×3 = 94bed7c（**已推远端**，origin/main 即此笔）+ 2e1ece6/4c858c1（未推送）——已披露交接期三笔。**自 572b367（t1061 关单）起 12 笔连续零尾注，停用已实证**。评估：**登记级、非 YELLOW**——尾注实践已停且经 12 笔验证；7/10 属前代历史记录非本主控行为；唯一已入远端的 94bed7c 若要抹除只能历史重写（风险 > 卫生收益，不建议）；对已推送笔的留置由用户表态即可，不阻塞任何管线工作。
7. **异常指标**：无未知工作区修改；无大范围无关文件变化；无 flaky 新报；运营新知两条（矩阵全量 ~15-35 分钟、背景长任务外部中止面/脱离式跑法规避）已按披露入档（572b367 提交注 + agent-state 运营纪律）。

## 架构方向（对照 refactor-plan R20 纪律，逐面）

- **单一权威只减不增**：t1062 删「在途邻专分支」与 Absent 邻统一脚手架一路（第二路径 retired，r2024d 钉面同变更留痕）；t1063 组创建单点化 `_createChunkGroup` + `resetChunkSlotPool` 收口 enterWorld 流式分支唯一调用（世界换代面，子 agent 自主发现收口）；t1064 两路径退避收敛唯一 wrapper + 探针独立连接零计数面；t1060 MeshBuilder 单一权威反探延续（world.cpp 零 build/kFaces）+ 收割宿主单点收口四文件。
- **每帧直发信号**：反向——t1061 `World::ResidentSetBatch` 把逐翻转同步发射收敛为稳态 ≤1 沿/tick（livelock 根因即每沿 O(池) 重建放大环路），批内计数语义不动（r2021b 对账腿零扰动实证）、fixed 世界零变化墙。
- **上行 include**：窗口 src/ 新增 include 全部同层/向下（world.h+meshworker.h 同层；savebridge Qt/World 向下；QPointer 悬垂自愈），零上行。
- **线程白名单第三落点**：savebridge.cpp 携 `QThread::msleep`（同步单档 150ms、零线程派生、F3 threads 行事实不动）按 t1023c + r2020d 同变更自登记程序转正（6f0a052 内 section08_recent/section23_meshworker 双钉面修订），程序合规。
- **值类型/QObject 纪律**：worker 侧零 QObject 零新增；无新全局状态；无第二套权威；无新特殊分支累积（七问见下）。

## 方向收益（§4 Step 7 七问·逐单简要）

| 单 | 用户目标 | 重构阻力 | 可复用 Module | 重复权威 | 可测试性 | 后台/联机就绪 | 特殊分支 |
|---|---|---|---|---|---|---|---|
| t1060 | ✅ 黎明 10fps 原病灶 | 低（env 回退保旧路径逐位） | MeshWorker fixed 归属 + 收割拍 | 减少 | ✅ 逐位等价墙 | ✅ 与 W4 同构 | 否 |
| t1061 | ✅ P0 入口不可用 | 低（RAII 批口） | ResidentSetBatch | 减少（发射收口） | ✅ 复现腿修前红 | ✅ 泵拍收敛 | 否 |
| t1062 | ✅ 走回内容漂移 | **负阻力（删专分支）** | 统一物化链 | 减少（死代码退役） | ✅ 时序无关腿 | ✅ 防御路径转正 | 否（净删） |
| t1063 | ✅ 走查池重建卡顿 | 低 | patchChunkSlotPool | 减少（组创建单点） | ✅ 增量计量腿 | 中性 | 否（自愈兜底） |
| t1064 | ✅ 退出存档锁窗可靠性 | 低 | exitSaveRetryBackoff 桥面 | 减少（两散写→一实现） | ✅ 真锁退避腿 | 中性（同步退避有界 ≤300ms） | 否（探锁一档） |

无连续特殊分支累积；五单均净减重复权威或净删路径。

## 结论：**GREEN**

四完整闭环证据链零错位；t1064 代码+测试证据已齐（687/0 ×2、复跑在途）。交接期纪律连续性成立——配对提交/双日志/阴性恰红/钉面同变更修订/相邻零污染全部延续 #13 惯例。两项登记事项（尾注 10 笔如实入档、dev-plan t1064 立项缺口）均有确定纠偏路径，不构成方向问题。

## 自动动作

1. **t1064 关单 docs 提交（进行中流程内，强制）**：①补录 dev-plan t1064 立项条目（review0901 #36 登记源、≤300ms 口径、修法要点——清偿事实核对 5 缺口）；②记录 r2038 NEG-1/NEG-2 声明面 {r2038b,d} / {r2038c,d}；③登记 QSQLITE 默认 busy timeout 秒级面（潜在 t1066 = 保存链 BUSY_TIMEOUT=0 + 退避收敛，主控已预告）；④引用 orch_verify 终态（完成核 687/0）后清治理计数 = 0。
2. 尾注处置：维持停用（已实证）；**不做历史重写**；94bed7c 留置待用户表态（不阻塞）。
3. 阴性声明面补录缺失类缺口若再犯（下一窗），升级为 GOV 纠偏任务。

## 纠偏任务

无独立 GOV 任务——自动动作 1 全部在 t1064 关单 docs 提交内清偿（下一审计首查项）。

## 人工待验

- P5 用户设备会话清单不变（dev-plan 各关单条目累积 + t1060 六项）：主项 = 黎明风暴 recheck；含 t1061 四项、t1062 走回观感、t1063 四项（换代收口/patch 小数值/fixed 逐位/渐进恢复）。
- 尾注历史留置表态（可选，不阻塞）。

## 下一治理触发点

t1065 闭环（流式→固定切换池残留，filter r2039）后计数 1/5；其后 R19.12 MC 词根改名批。若 t1064 关单 docs 未按自动动作 1 补录，下一审计将该事项升级。

## 亲核证据补强（本轮审计实际执行的验证，可复追）

- 提交/文件面：`git log --oneline 086cbae..HEAD`（20 笔）+ `git log --name-status` 逐笔核文件域；`git status --porcelain` 仅 `?? .codex/`；`git log -- .codex/` 窗内 0 笔。
- 日志计数：`grep -ac '^PASS' / '^FAIL'` 十份 pos/final（671/675/679/683/687，全 0F）+ 四份 orch_verify（487/535 部分、679/683 完整、t1064 423 在途）+ 二十份阴性日志（红面 FAIL>0、还原全绿）。
- 腿集合恒等：`LC_ALL=C diff <(grep -a '^PASS' matrix_t1064_pos.log | cut -c1-110 | sort) <(…final…)` = 空。
- 尾注：`git log --format=%B` 逐笔 grep -i co-authored → 10 笔（7 ZCode + 3 Claude Opus 5），572b367 起 12 笔连续干净；`git branch -r --contains 94bed7c` = origin/main。
- 账本缺口：`git show d9fb80f -- docs/dev-plan.md` 仅 +t1065 段；`grep -n 't1064' docs/dev-plan.md` 无专属条目。
- 架构抽查：`git diff 086cbae..HEAD -- src/ | grep '^+.*#include'` 全向下；t1063/t1064 Main.qml hunk 逐个读（限登记面）；savebridge.cpp 探针实现 + t1023c/r2020d 钉面修订 diff 亲读。
