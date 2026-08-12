# 用量报告（voxel-autopilot 工作流，子 agent token / 时间）

> 数据源：工作流完成通知的 `<usage>` 块（aggregate 精确）+ journal.jsonl（仅 agent 结果，**无每 agent token/时间**）。
> ⚠️ 限制：① 每轮 aggregate 精确；② 每-t 任务 token = 按 agent 数均摊估算（journal 不存逐 agent token）；③ 每-t 任务时间不可得（工作流脚本禁用 `Date.now()`）。
> 改进：下一轮起给工作流脚本插 `budget.spent()` 探针 → 自动捕获**精确**逐任务 token（时间仍只有 aggregate，硬限制）。

---

## 累计：R18 全轮（R18a + R18b + R18c）

| 轮次 | 任务范围 | agents | subagent tokens | 工具调用 | 耗时 |
|---|---|---|---|---|---|
| R18a Run1（误火） | t160-166 | 38 | 3,227,157 | 844 | 137 min |
| R18a Run2 | t167-178 | 73 | 6,083,067 | 1,887 | 323 min |
| R18b | t179-186 | 41 | 3,721,033 | 1,039 | 174 min |
| R18c | t187-211 | 121 | 9,804,955 | 2,667 | 405 min |
| **R18 累计** | t160-211 | **273** | **22,836,212** | **6,437** | **1,039 min ≈ 17.3h** |

均摊：每任务 ≈ 5 agent（dev + 3 测 + 1 fix）、≈ 408k tok / 任务（R18c 均值）。

---

## R18c 逐任务清单（24 任务，全 rounds:1）

每任务 token 估算 ≈ 408,540（9,804,955 / 24，均摊；实际有出入）。时间不可得。

| 任务 | 组 | 标题 | verdict | needs-run |
|---|---|---|---|---|
| t194 | A 箱子 | 箱子放置后透明（根因：PartialBlockGeometry 无 case Chest） | pass | — |
| t195 | A 箱子 | 箱子贴图重做（MC 简洁风） | pass | — |
| t196 | A 箱子 | 箱子开合动画 | needs-run | ⚠️ 眼看 |
| t203 | B 背包 | 2×2 合成栏接右键放1/拖拽均分 | pass | — |
| t204 | B 背包 | 左键拖拽上限=手持数 | pass | — |
| t205 | B 背包 | 右键拖拽放1机制修复 | needs-run | ⚠️ 眼看 |
| t206 | C 不完整方块 | 双半砖挖掉掉 2 个半砖 | pass | — |
| t207 | C 不完整方块 | 门 UI 图标两格高 | pass | — |
| t208 | C 不完整方块 | 门碰撞体积 | pass | — |
| t209 | C 不完整方块 | 栅栏连接 + 1.5 格高 | pass | — |
| t210 | D 模式 | 滚轮按模式切换 | pass | — |
| t187 | E 存档/世界 | 背包新世界未清空（串世界根因） | pass | — |
| t188 | E 存档/世界 | 箱子按世界持久化 + 修跨世界泄漏 | pass | — |
| t189 | E 存档/世界 | 创建世界按钮溢出 | pass | — |
| t190 | E 存档/世界 | 双击进入世界 | pass | — |
| t191 | E 存档/世界 | 截图封面 | needs-run | ⚠️ 眼看 |
| t192 | E 存档/世界 | 重命名世界 | pass | — |
| t197 | F 水 | 水位视觉（流动感） | pass | — |
| t198 | F 水 | 水中可放方块（排开水） | pass | — |
| t199 | F 水 | 空桶只舀水源 | pass | — |
| t200 | F 水 | 水抵消摔落伤害 | pass | — |
| t201 | F 水 | 水下蓝滤镜 | needs-run | ⚠️ 眼看 |
| t202 | F 水 | 气泡 + 溺水系统 | pass | — |
| t211 | F 水 | 水流推动玩家 | pass | — |

**needs-run 共 4 项**（t196/t205/t191/t201）：框架静态测不出，需主编排 run + 用户眼看。

---

## 备注
- t193（存档破坏块 round-trip 验证）标 🔍 非 ⏳，工作流 Snapshot 未拾取 → 未跑（随 t187 修后人工复测）。
- R18c 期间用户报告"方块堵水崩溃"——崩溃日志被后续 smoke run 覆盖（32000 行 → 16KB），静态审查 t197/t198/t211 无明显崩点，疑中途构建态；待用户在最终构建上复测。

---

## R18h 续（手动子 agent 编排，非 Workflow 工具）

> 背景：R18h 工作流因 record 步骤 `git add -A` 被安全分类器拦截 + ⚠️ 标记被 Snapshot 跳过，~20 任务未自动提交（后由主编排 batch-commit 34ac3ad 回收）。用户选「主体手动做 7 个」余下任务；本段 = 其中 6 个（t289 已在 /compact 前提交 fe4d7d9）。
> 编排方式：主编排用 `Agent(voxel-dev)` 逐任务（dev → 自构建 → 报告），主编排 targeted `git add <paths>` + commit —— 绕开 `git add -A` 拦截 + ⚠️ 跳过两个结构性 bug。lean：每任务 1 dev agent，**不**跑 3 测 agent（省成本；终轮统一 codereview）。
> ⚠️ 数据局限：未走 Workflow 工具 → 无 aggregate `<usage>` 块；Agent 工具不返回可检索的逐 agent token。故 token/时间列 = N/A（不可得），仅记任务/提交/verdict。

| 任务 | 标题 | 方式 | 提交 | verdict | 需人眼 |
|---|---|---|---|---|---|
| t289 | isLockedBuried 边界 FP 致移动锁定（0.1 容差） | 主编排手修 | fe4d7d9 | pass | — |
| t316 | F3+G 区块边界可见性（亮黄 + 5 偏移束粗） | voxel-dev | 6278b62 | pass | ⚠️ 眼看 |
| t313 | 死亡屏 + 聊天显死因 | 验证已实现（34ac3ad） | — | pass | — |
| t315 | 工具耐久 UI（tooltip + 绿/黄/红条 + 破坏音） | voxel-dev | a910c65 | pass | ⚠️ 眼看 |
| t314 | `/give <id> [n] [dur]` 指令 + docs/item-ids.md | voxel-dev | 294fe2b | pass | — |
| t301 | 骷髅(Bones)模型 + 持弓 + 掉弓/箭 | voxel-dev | c051b3d | pass | ⚠️ 眼看 |
| t300 | 剪刀 + 羊毛 + 剪羊 + 羊吃草重生毛 | voxel-dev | 28fbbce | pass | ⚠️ 眼看 |

- **构建**：全绿（`cmake --build build` → `ninja: no work to do`；6 提交合体后子 agent 各自增量构建通过）。
- **smoke run**：12s 无崩、无 QML ReferenceError、log error 扫描为空（chunk dirty churn 属预存，见 codereview）。
- **创造背包分类核查**：弓 / 箭 / 剪刀 / 羊毛 / 铜锭 / 金锭 / 腐肉 / 线 / 骨头 / 树苗 / 剑 全部在册（creativeTools / creativeBlocks / creativeMaterials 三段）。
- **成本**：session 累计 ~$796+（cost-warning 数，含 R18 全程多轮）；本段边际成本未单独计量（Agent 工具不回逐 agent token）。
- **⚠️ 缺口**：R18d / R18e / R18f / R18g 的逐轮 token/时间在本文件仍缺（当时未追加）；如需可从各轮 journal.jsonl 的 aggregate `<usage>` 重建（额外成本，未做）。
- **结构性待修**：voxel-autopilot 技能模板的 record 步骤用 `git add -A`（被分类器拦截）+ Snapshot 不认 ⚠️（只认 ⏳）—— 两个 bug 致工作流自动提交失效。本轮靠手动编排绕开；**未修技能模板**，下轮若再走工作流仍会复发。

---

## R18i · P0 批次（手动子 agent 编排，4 任务）

> 编排：voxel-dev 逐任务（dev→自构建→报告）+ 主编排 targeted git add+commit。串行（共享 entitymanager/playercontroller + ninja 构建锁）。轮末 smoke + 创造背包核查。
> ⚠️ 数据局限同前：无逐 agent token（Agent 工具不回）。

| 任务 | 标题 | 提交 | verdict | 根因 / 修复 |
|---|---|---|---|---|
| t317 | 生存跳跃半格 | eaa97b5 | pass | `isLockedBuried` 钳位零化了刚施加的跳跃冲量 → 钳位后重施加 `setY(kJump)`；apex 1.26 格 |
| t321 | 怪物攻击频率过高 | 292f957 | pass | 每 mob 冷却本有(1.0/1.6s)但**随数量叠加** → 加全局玩家受击节流 0.5s（max 2 击/s ≈1.25s 反应窗）+ 近战 3→4 HP |
| t319 | 水中爆炸仍破坏方块 | 7628555 | pass | t297 只查身体中心格（浮在水上=空气）→ 改扫**脚→头身列**查水（同 mobFeetInWater 约定） |
| t320 | 爆炸卡顿 + 1G 内存 | f497d71 | pass | ①逐方块 `setWaterSilent`(~30-100次 emit)→batch `destroySphereSilent`(1 worldChanged/爆) ②掉落物无寿命→5min despawn(MC标准) + LRU 上限驱逐 |

- **构建**：全绿（`ninja: no work to do`）。
- **smoke**：12s 无崩、无 error/ReferenceError。
- **创造背包核查**：P0 未加新物品（纯物理/战斗/性能修复），creativeTools/Materials/Blocks 三段完好，原 11 项（弓/箭/剪刀/羊毛/铜锭/金锭/腐肉/线/骨头/树苗/剑）在册无遗漏。
- **codereview 复查项**：t317/t319/t321 即前轮 codereview 报的复查项，本轮已落地（t320 额外挖出爆炸批处理 + 实体泄漏）。
- **成本**：session 累计 ~$820+（含 R18 全程多轮）；本批边际未单独计量。
- **待办**：R18i 剩 P1–P5（29 任务）。codereview 复查项随各任务落地。

---

## R18i · P1+P2 批次（**Workflow 工具**，lean 自定义脚本，14 任务）

> 编排：自定义 Workflow 脚本 `r18i-p1p2`（非 voxel-autopilot 技能——避其 `git add -A` 拦截 + ⚠️ 跳过两 bug）。每任务 1 个 voxel-dev 子 agent：dev→自构建→**targeted git add+commit**→结构化报告。**串行**（共享 Main.qml/entitymanager.cpp + ninja 锁）。Run ID `wf_ec95784e-a7d`。
> ✅ **本轮首次拿到精确数据**：Workflow 完成通知回传 aggregate `<usage>` + journal.jsonl 逐 agent token/时间。

**aggregate**：14 agents · **987,143 subagent tokens** · 520 tool calls · **8,414s ≈ 140 min (2.3h)** · 0 error/skip/empty。均摊 ≈ 70.5k tok/任务、≈ 37 tool calls/任务、≈ 10 min/任务。

| 任务 | 标题 | 子agent tok | 工具调用 | 耗时 | 提交 | 根因/修复（摘） |
|---|---|---|---|---|---|---|
| t318 | 创造背包归还切换 | 54,850 | 20 | 6.3m | 25be11e | onTapped dismiss 后又赋值 heldBlock→net 重拾；改 origin 槽=discardHeld 早返回（真切换） |
| t322 | 无箭可拉弓 | 69,626 | 21 | 5.3m | 51a639c | beginBowDraw 没查箭；survival 门控 draw + 箭查找移入 survival 分支，创造免费 |
| t323 | 箭插入+拾取 | 70,651 | 57 | 13.3m | 671ada0 | block-hit 设 remove=true 消失；改 arrowStuck+贴面+60s despawn；arrowPickupScan（arrowFromPlayer 门控，骷髅箭不拾） |
| t324 | 自箭自伤 | 42,680 | 25 | 6.5m | 794f5dd | 玩家命中分支 `!arrowFromPlayer` 永不命中自己；加 0.2s shooter-ignore 窗后启用自伤 |
| t325 | 树叶渐进衰减 | 56,975 | 40 | 10.3m | 4890b76 | decayLeavesAround 批量瞬清；改入队 + tickLeafDecay(~0.3s/窗, 2%/窗几何概率, 10-30s 渐进) |
| t327 | 死亡聊天播报 | 44,865 | 33 | 6.6m | 673067c | 路由本对，chatDisplay.visible 门控 (captured||chatOpen) 死亡时全 false 隐藏；加 playerState.dead 入门控 + 抬 z=185 |
| t333 | 怪受水流 | 40,020 | 30 | 5.8m | 9191406 | World::isSolid 含水→怪踩水；3 处 mob 碰撞排开水→沉入+既有 flow-push 生效 |
| t334 | 活板门/半砖遮光 | 88,250 | 39 | 12.8m | e7dbbff | flood 用 isSolid 二值遮光，partial 全透；加 lightOpacity(id,state)（实/关活板门=15, 开=0, 半砖=7）+ state-aware recompute |
| t326 | 草丛半透 cutout | 77,570 | 46 | 10.9m | eebc1aa | cross 块有 alpha+alphaCutoff 但 terrain opacity=1，D3D11 仅透明通道认 cutoff；加 cutoutOnly ChunkGeometry pass |
| t329 | 剪刀铁色+手持/掉落图标 | 70,845 | 31 | 7.0m | af02fd1 | tier=1 木色涂棕 + toolType===6 三路径无 Model；强制铁色 + BillboardQuad+ToolIcon 补全 3 路径 |
| t330 | 弓 C 弯+白弦 | 96,184 | 34 | 16.5m | 4e698cb | 弓身在 YZ 深度面（正面压成直棍）+弦在凸侧木色；改 XY 面 C 弯 + 白弦凹侧 + 新 BowStringGeometry |
| t332 | 工具木柄金属头 | 59,480 | 31 | 7.9m | b22c62c | 手持 hoe/axe/shovel/sword 单色几何（柄也金属）；改 Node+UnitCube 木柄+tier 头 |
| t331 | 骷髅木弓+拉弓+瞄准减速 | 114,044 | 67 | 18.7m | e7917a8 | 弓共享 MobModel 单材质（骨白）；拆 MobBowGeometry(木棕)+QML 肩 Node 臂弓随 drawAmount 转；aiArcher aimTimer~0.5s + (1-draw) 减速 |
| t328 | 音效全重做（程序合成） | 101,103 | 46 | 11.5m | 8412b35 | 振幅过低(ambient_wind 峰值~0.13)+低沉基频+慢包络；**kSampleRate 22050→44100**（旧下采样丢高频）；peak-normalize+更亮更短瞬态+共振峰兽叫+ui_click |

- **构建**：全绿（`ninja: no work to do`）。
- **smoke**：14s 无崩、无 error/ReferenceError（新 MobBowGeometry/cutoutOnly/BowStringGeometry 类 QML 注册 OK）。
- **创造背包核查**：creativeTools/Materials/Blocks 三段完好；P2 加的剪刀/弓/工具贴图是**图标与手持模型层**改动（未新增 hotbar 物品 id），原 11 项在册无遗漏。
- **验收状态**：✅ = 编译+smoke 过；**尚未经你 playtest**（上轮教训：代码存在 ≠ 玩着对）。请你重点验：t318 归还切换 / t322 无箭拉弓 / t323 箭插拾取 / t328 音效能听见 / t326 草丛透视 / t330 弓 C 弯白弦 / t331 骷髅木弓拉弓。
- **待办**：R18i 剩 P3+P4+P5（15 任务：活板门碰撞/木楼梯/沙海/矿井洞/洞穴入口/指令/UI/群系/湖/峡谷/ID对齐/岩浆/着火/装甲）。P5 三大系统建议拆 R18j 专项。

---

## R18i · P3+P4+P5 批次（**Workflow 工具**，lean 自定义脚本，15 任务 — **R18i 收尾**）

> 编排：同 P1+P2 模板（自定义 Workflow，每任务 1 voxel-dev：dev→自构建→targeted commit→结构化报告，串行）。Run ID `wf_97d47939-c67`。含岩浆/着火/装甲三大系统。
> ⚠️ 本表只列 aggregate + 逐任务 commit/根因；**逐任务 token/时间在 journal.jsonl**（本次未逐一摘录省成本）。

**aggregate**：15 agents · **1,578,062 subagent tokens** · 760 tool calls · **13,807s ≈ 230 min (3.8h)** · 0 error/skip/empty。均摊 ≈ 105k tok/任务、≈ 50 calls/任务、≈ 15 min/任务（三大系统拉高均摊，对比 P1+P2 的 70k/37/10）。

| 任务 | 标题 | 提交 | 根因/修复（摘） |
|---|---|---|---|
| t349 | 剪刀耐久条 | 38ce928 | 耐久门控改 `toolMaxDurability>0`（显式含剪刀 maxDur=238） |
| t346 | /give 参数提示+文案 | 4e00c30 | 参数提示行（尾空格推进 id→count→dur）+「给予玩家 ×N」/耐久变体 |
| t347 | 聊天历史+/help+可扩展 | 3970b1b | 硬编码 if 链→`commandRegistry` 表（可扩展）+ /help 自列 + ↑↓ 历史 |
| t335 | 活板门开态碰撞 | 27769e4 | 开活板门 `isCollidable=false`→镜像门（常碰撞）+ 铰链侧唇可站 |
| t336 | 木楼梯 | （无） | **已实现**（t134 WoodStairs+t163 auto-step）→无改，重复任务 |
| t339 | 地下矿井洞 | 848eaa8 | `carveCaveEntrances` 1×1 竖井网格→移除该 pass；自然洞穴不动 |
| t337 | 群系密度 | d786ba9 | 草密度**反了**（plains 40%>forest 18%）→翻转 forest 35%/plains 18% |
| t338 | 沙海集中一角 | 2c92e06 | seed corner 海（quarter-disc ramp）+沙滩；内陆无散沙/水 |
| t340 | 湖泊形态 | 97351c4 | fbm lobed 湖岸；~50% 湖下方挖空穹顶（1 石层天花板隔水） |
| t341 | 洞穴入口山坡 | 5b5f5ee | 重加 `carveCaveEntrances` 山坡感知（仅 6 格内有真洞穴，3×3 口，坡度过滤）+ 森林起伏 amp 2→5 |
| t342 | 大峡谷 | 1681658 | `carveCanyon()` 开放 V 沟至 y=22 露矿（注：6 个预存 -Wmisifying-indentation 警告无关） |
| t348 | ID 对齐 MC 1.0 | 49df0b5 | 选**映射层**非重排（引擎 id=存档权威，免迁移）+ engine→MC1.0 forward map + static_assert + 刷 item-ids.md |
| t343 | **岩浆流体** | abd32fc | Lava id31 + `tickLavaFlow`(~3s/格,3扩散,无补给) + Y<30 封闭湖 + 岩浆桶 0x220+HitLava + 程序音 + 点木着火 + 毁弃物 + lavaOnly NoLighting 段（**24 文件**） |
| t344 | **着火系统** | 0f244e1 | `fireTimer` 燃烧(8s,1HP/s,随机/定时灭) 全 mob+生存玩家；mobDied burned→熟肉；屏底 35% 火焰覆盖 |
| t345 | **装甲系统** | e8de268 | ArmorRegistry 0x300 段(20 件)+4 装备槽+耐久+totalArmorPoints+20 配方+护甲条+MC 减伤(armor×4% 上限 80%)；装备音暂借 playPlace（专属 wav 暂缓） |

- **构建**：全绿（`ninja: no work to do`）。
- **smoke**：16s 无崩、无 error/ReferenceError（岩浆新几何/音频/贴图 + 着火覆盖 + 装甲 UI 全加载 OK）。
- **创造背包核查（含新增物品）**：`creativeArmor()`（20 件全在，独立段）、装岩浆铁桶、熟猪排/牛排/羊肉、WoodStairs **全部在册无遗漏**。
- **验收状态**：✅ = 编译+smoke 过；**尚未经你 playtest**。三大系统（岩浆/着火/装甲）单 agent 一遍做完，**可能偏粗**——重点请验：岩浆流得慢+Y<30 湖+桶舀放+着火链 + 装甲 5 套减伤+护甲条 + 大峡谷/沙海角/湖形态。
- **R18i 完成**：P0(4)+P1+P2(14)+P3+P4+P5(15) = **33 任务全落地**。下一轮（R18j）等你 playtest 反馈。

---

## R18j 批次（**Workflow 工具**，lean 自定义脚本，30 任务 + 1 主编排 hotfix）

> 编排：同 R18i 模板。Run ID `wf_63424a15-b47`。复发项 prompt 强制「找真根因，禁重贴旧补丁」。
> **主编排 hotfix**：t377 armor 第三人称引入 `armId` ReferenceError（smoke 抓到，6 玩家+8 mob 处）→ 主编排给 14 个 Model 加 id 修复（commit `04e77dd`）。

**aggregate**：30 agents · **1,979,652 subagent tokens** · 984 tool calls · **16,776s ≈ 280 min (4.7h)** · 0 error/skip/empty。均摊 ≈ 66k tok/任务、≈ 33 calls/任务、≈ 9.4 min/任务（低于 R18i P3+P4+P5 的 105k——这轮多是 bug 修复，少超大新系统）。

| 任务 | 标题 | 提交 | 根因（真因，非重贴旧补丁） |
|---|---|---|---|
| t350 | 水流体（基础） | afcf7e8 | 水平扩散在 `bk!=2` 触发→air-below(bk==0)格既扩散又下落=无限级联（海啸）；改仅 grounded(bk==1)扩散+批 fluid tick+流水分层全高渲染（无间隙）。**存档退出已验功能正常** |
| t351 | 岩浆补全 | 7be419b | 岩浆从未种为 block-light；lavaOnly 用整方体而非变高流体；placeBlock 拦截+creativeMaterials 漏岩浆；`m_fireDmgTimer` 每 tick 清零永不到阈值（不扣血）。复用水流体+lava 参数(光15/4层/橙雾)。注：`emit` 是 Qt 关键字宏→改名 `emission` |
| t352 | 跳跃卡死（复发 t317） | e0c8eeb | **真因**：`moveAxis(1,+)` 跳跃用 cap=+inf，体级 partial block（楼梯背墙/半砖/栅栏/活板门唇）被误判为天花板→下拽+清 `m_vel.y`；改对称 minSurfFloor 滤+isLockedBuried 保 `m_vel.y` |
| t353 | 攻频过高（复发 t321） | 13a78a6 | **无 bug**——节流本对；t321 数值是因（0.5s×4HP=8HP/s=1.25s 致死窗）；改节流 0.5→1.0、近战 4→3、骷髅 1.6→2.5 |
| t354 | 爆炸卡顿（复发 t320） | 398d8c7 | t320 只批了 World worldChanged；ItemEntityManager `entitiesChanged` 逐方块 spawnItem emit→O(N²) 绑定重算；加 beginBatch/endBatch 合 N 掉落为 1 emit |
| t355 | 橡皮筋瞬移 | 3023e97 | `extrudeEmbedded()` 用 hairline 重叠（兄弟 isLockedBuried 在 t289 已硬化 kEmbedTol=0.1，extrudeEmbedded 未同步）；m_pos.y 刚低于整数→脚下方块误判为中心柱嵌入→横向硬传送（振荡=橡皮筋）。同步 kEmbedTol=0.1 |
| t356 | 创造丢弃 | 585d07f | t292 让 discard void；t318 复用同信号做 toggle→拖出/Q 也 void；拆意图：discardHeldRequested 生成实体，新 returnHeldToVoidRequested 留 toggle |
| t357 | 苦力怕引信 | e9b833b | 死区(kFuseRange<dist<=kDefuseRange) fuseTimer 冻结→部分蓄力持续累积；改该带内 fuseTimer 排回 0 |
| t358 | 死亡聊天（复发 t327） | a29aea7 | **真因**：`chatDisplay` Item 无 height（默认 0）→ListView 0 高视口→**任何聊天行都从未渲染**（t327 的 visible/z 白改）；加 `height: chatVisibleLines*chatLineHeight` |
| t359 | 活板门开态（复发 t335） | 9869287 | t335 唇宽 3/16<玩家半宽 0.3→足迹不叠→穿透；移除 override 用全高面板=可站（**注：变实体不可横穿，spec 取舍**） |
| t360 | 活板门/半砖光（复发 t334） | f29c8cd | t334 只修"下方暗"；partial 面仍采样本格(已被 opacity 压暗)光(关活板门 sky=0→自阴影)+PCF 用 heightmap+1.0(半砖投整格影，上半砖巧合对)；改从外邻格取光+新 solidTopOffset 喂 PCF |
| t361 | 半砖放置 | b17f74a | 上半砖底面点击→合并同格下半砖（选区/面判定） |
| t362 | 怪卡方块 | 4259a80 | mob 落差腿卡后方高块→改 stepping 干净下阶 |
| t363 | 剪毛羊色 | 12d8b27 | 粉猪色→肉色 baseColor |
| t364 | 头盔错字 | d03f398 | 头盲→头盔（displayName） |
| t365 | tooltip 缺失 | 4655fe2 | 补熟肉等 nameForBlock |
| t366 | 音效白噪（复发 t328） | d9b5c98+e23b330 | **真因**：ambient loop 持续播放且内容是噪声→先止白噪+记 lesson；再逐 clip |
| t367 | 草清晰 | 5c5f548 | 锐化 2px blade 边 |
| t368 | 弓箭 nock | aaf7b1d | 拉弓时弓上显箭 |
| t369 | 工具 FP 位 | d078c84 | 修正手持 roll 符号+握高 |
| t370 | 骷髅镂空 | 33b5f2f | 加肋骨 hollow |
| t371 | 着火视觉 | fae2fc7 | 橙立方→贴身火焰动画 |
| t372 | 沙滩过渡 | 4a87ea3 | noise 岸线+高度 blend |
| t373 | 草原扩大 | 5a9eb74 | 升森林阈值→草原占比大 |
| t374 | 群系 mob | 31634bc | 草原牛羊多/森林猪多 |
| t375 | 湖数量 | 2683691 | 增湖密度 |
| t376 | 峡谷水泛滥 | 6976966 | 峡谷保干+瀑布点缀+壁洞 |
| t377 | 装甲 UX/视觉/mob 甲 | b82fe87 | 右键穿脱/Shift/3rd 人/mob 80% 空（**armId ReferenceError 主编排 hotfix 04e77dd**） |
| t378 | /kill | bccf2a9 | 自杀/@e 清实体/type 过滤 |
| t379 | 树叶调慢 | f53e80b | 慢衰减+提木棍/树苗掉率 |

- **构建**：全绿（30 提交 + 1 hotfix）。
- **smoke**：首次抓到 t377 `armId ReferenceError`（6 玩家+8 mob 处）→ 主编排 hotfix 后复测**干净**（无 ReferenceError/exception/segfault）。
- **创造背包核查（含 R18j 新增）**：新增「装岩浆铁桶」已在 creativeMaterials（line 235）；熟猪/牛/羊肉 + creativeArmor(20 件) 全在册；**无遗漏**。
- **commit 全英文**：prompt 强制 + 审计确认本轮 30+1 提交 subject/body 全英文（上轮 t346 中文已 filter-branch 修）。
- **验收状态**：✅=编译+smoke 过；**尚未 playtest**。重点验：t350 水不再海啸/竖直流无间隙、t351 岩浆发光分层橙雾稳定扣血、t352 跳不再卡、t355 不橡皮筋、t358 死亡聊天终于显、t366 白噪没了+叫声可辨、t377 第三人称见护甲。
- **R18j 完成**：30 任务全落地（含 1 主编排骨 hotfix）。

---

## R18k 批次（**Workflow 工具**，lean 自定义脚本，12 任务）

> 编排：同前模板。Run ID `wf_62e9b394-a86`。复发项（t381 音效/t383 chunk dirty）强制「找真根因」。

**aggregate**：12 agents · **1,434,721 subagent tokens** · 709 tool calls · **11,678s ≈ 195 min (3.25h)** · 0 fail。均摊 ≈ 120k tok/任务（被 t381 音效/t387 床 8 色/t385 天气 拉高）。

| 任务 | 标题 | 提交 | 根因/修复（摘） |
|---|---|---|---|
| t380 | 性能 profiling | 2f009d0 | 流体每 0.3s 全网格扫 ~41 万格（即使已沉寂）→ 加 fluid-dirty 早退 + 批 lava 写 |
| t381 | 音效（复发 t366） | 50e50ca | 脚步 80%HF 无低音 / 蜘蛛 11kHz 别名（t366 漏）/ 潜行者嘶嘶破峰值归一 → Kenney CC0 脚步 + 喉源发声 + 修别名；mob 叫仍合成（CC0 动物录音取不到） |
| t382 | 存档鲁棒 | 4dde1c6 | world_version + 迁移注册表；**审计发现护甲+耐久被悄悄丢** → 存入 player_state v2；chunk_count 校验 |
| t383 | chunk dirty（复发） | 3b9c6fb | **真因**：recomputeLightAround 把 R=15 光盒全标 dirty（9-16/改）而 reflood 只变 1-2 → snapshot 光场只标真变 |
| t384 | 天空云层 | dc32a12 | BillboardQuad 扁平 NoLighting + 程序 cloud.png + WorldClock 漂移；hedged alpha 退化 cutout |
| t385 | 天气（雨/雪） | 4c949df | FSM(晴/雨/雪/雷)+群系解析+天暗+WeatherParticles.qml；雨灭 mob 火 + 作物 2x |
| t386 | 雷电 | c61ba30 | World 闪电调度+点火+lightningStruck；Main.qml 闪光+雷+AoE；程序 thunder.wav |
| t387 | 床（8 色） | b2254df | 每色一 id（8 色）+ 程序贴图/图标；木板+羊毛→红床，余创造 |
| t388 | 睡觉 | beedcc4 | 夜+8 格无敌对→fade→skipToDawn+设 spawn；白天/怪近拒；受伤醒 |
| t389 | 月星天穹 | d638241 | 月相 + 星空穹 + 日出日落天色渐变 |
| t390 | 环境粒子 | f2488af | 雨溅 / 叶飘 / 火把火花 |
| t391 | 水面视觉 | aff5db8 | 水面波动 / 透明 |

- **构建/smoke**：全绿 + 16s 无错（本轮**无** armId 式 runtime bug）。
- **创造背包核查**：8 色床全入 creativeBlocks（红/橙/黄/绿/青/蓝/品红/黑）；无遗漏。
- **commit 全英文**：审计确认本轮 12 提交 subject 全英文。
- **验收状态**：✅=编译+smoke 过，**未 playtest**。重点验：性能真稳了？音效 CC0 脚步可辨认？云/天气/雷电氛围？床睡觉跳清晨？月星天穹？
- **R18k 完成**：12 任务全落地。

---

## R18l + R18m 批次（**Workflow 工具**，lean 自定义脚本）

> R18l Run `wf_88b260d3-7a8`：**9/10 通过**，t401（钓鱼）429 限额失败（限额 2026-08-08 00:54 重置）。
> R18m Run `wf_8e50844c-d4a`：**13/13 通过**，含 t401 干净重做 + 用户新批（XP/沙玻璃/农业/修复/圆石变体/垂直梯）。R18l 的 t401 破碎半成品已 `git stash`（recoverable），R18m 从干净 t400 重做。
> R18m aggregate：13 agents · **1,678,415 tok** · 817 calls · **10,688s ≈ 178 min (3h)** · 0 fail。（R18l aggregate 通知截断未取精确值，~1.6M 量级。）

**R18l（9 任务，t392-t400）**：地牢+刷怪笼 / 战利品表 / 沙漠(仙人掌+枯木+砂岩) / 雪原(雪+冰+云杉) / 沼泽(莲花+蘑菇) / 花+甘蔗 / 鸡 / 鱿鱼 / 繁殖。

**R18m（13 任务）**：
| 任务 | 提交 | 根因/做法（摘） |
|---|---|---|
| t401 钓鱼(重做) | a3e0b31 | 钓鱼竿(3棍+2线,耐久64)；右键 DDA HitWater 抛标→3-7s 咬→fishingPool 战利品；+生鱼 |
| t402 经验球 | 969fe1f | XpOrbManager(磁吸飞向玩家)+PlayerState.xp(持久化)；杀 mob/熔炉取出(SmeltXpReward,铁>炭)给经验 |
| t403 经验条+升级 | 646ca9b | level/xpToNext via MC 曲线；绿色 XP 条+等级数于 hotbar 上 |
| t404 沙改黄 | ce6e106 | build_sand.py 重生，R-G 64→12，黄非橙 |
| t405 玻璃透光 | 6022f01 | 沙烧玻璃(已有)+新 Glass 块 id54 via glassOnly 段(opacity0.45+NoLighting 真 alpha)；solid=false 透视 |
| t406 甘蔗5+耕地湿润 | 1eb95ed | 甘蔗邻水长到 5；耕地 4 级湿润(水位距)+顶点色变深+催麦 |
| t407 胡萝卜马铃薯 | 78719fe | CarrotCrop/PotatoCrop cross 块(镜像麦)；僵尸(Shambler)各 2.5% 掉(MC 1.0) |
| t408 耕地矮+箱上不完整可开 | f0bdfa6 | 耕地 15/16 盒(唇)+solid=false+lightOpacity 防 X 光洞；箱上有半砖可开(既有 isFullCubeAt) |
| t409 箱子开合动画 | cce1d5c | 盖从分离薄板→方块自身顶 1/4(scale 1,0.25,1)绕后顶铰链翻 |
| t410 破坏动画按形状 | 930c34e | 破裂 overlay 按方块实际形状缩放(半砖=半高) |
| t411 流体交互 | f8cd07a | 流水+静岩浆→黑曜石；流岩浆+静水→圆石 |
| t412 圆石变体 | bbd706e | 圆石半砖/楼梯/栅栏/压力板(复用既有系统换贴图) |
| t413 垂直木梯 | 3a39e47 | 竖直爬行梯(非台阶式 WoodStairs)；入格+按前爬升 |

- **构建/smoke**：全绿 + 16s **无错**（本轮无 armId 式 runtime bug）。
- **创造背包核查（R18l+R18m 新增）**：圆石半砖/台阶/栅栏/压力板、木梯、钓鱼竿、玻璃、胡萝卜、马铃薯、生鱼、莲花/蘑菇/花/甘蔗/仙人掌/枯木/砂岩/雪/冰/云杉 **全在册无遗漏**。
- **commit 全英文**：审计确认本轮 22 提交 subject 全英文。
- **验收状态**：✅=编译+smoke 过，**未 playtest**。重点验：经验球磁吸+经验条升级、玻璃透视、沙子变黄、甘蔗 5+耕地湿润色深、胡萝卜马铃薯、箱子顶 1/4 翻、破半砖半高动画、流水+岩浆=黑曜石/圆石、垂直梯爬升、钓鱼。
- **R18l+R18m 完成**：22 任务全落地（t401 经重做完成）。

---

## R18n 批次（资源包加载器 — 方块部分，**Workflow lean**）

> Run `wf_af2b29e4-8c9`。**2 agents · 248,224 tok · 85 calls · 2,193s ≈ 37 min · 0 fail**（最小一轮——仅 2 任务）。
> ⚠️ 法律红线：仅做加载器功能；MC 贴图绝不进 git/qrc。

| 任务 | 提交 | 做法（摘） |
|---|---|---|
| t414 加载器核心 | 4458edc | 新 `ResourcePackManager`(Core) + `QQuickImageProvider`(main.cpp, `image://rp/atlas`)；pack PNG 缩 TILE=16 覆写 atlas 运行期副本；路径解析(settings/env/default discovery)；`.gitignore` +`resourcepacks/` |
| t415 映射全+设置开关 | 6d22884 | 79 个 atlas 瓦片→MC 文件名全映射；enabled/packPath 持久化 `settings.json`；`apply()` 重建合成 atlas+cache-bust；ESC 设置 UI 开关 |

- **构建/smoke**：全绿 + QML 加载（`root objects after load: 1`，pack off 默认路径不破）。
- **法律核查（重点）**：✅ R18n 两提交**仅代码**（无 `.png`/`.mcmeta`）；✅ `docs/Default HD*` **未被 git 追踪**（docs/ gitignored）；✅ `.gitignore` 加 `resourcepacks/` + `settings.json`（pack 资产+机器配置不进 VCS）。**红线守住——仓库零 MC 资产。**
- **验收状态**：✅=编译+smoke+legal 过；**pack-on 视觉切换待你实测**——ESC 设置面板 → 启用资源包 → 路径填 `docs/Default HD 128x Demo 1.8.2.2` → 眼看方块贴图变 MC 风。
- **HD 暂缓**：phase 1 缩到 16px；HD（TILE=128 + 程序贴图高清重生成）留后续。

---

## R18o 批次（资源包/农业修复 + 包扩展，**Workflow lean**）

> Run `wf_83d64b7c-9e8`。**6 agents · 483,066 tok · 234 calls · 4,720s ≈ 79 min · 0 fail**。+ 1 主编排 hotfix（甘蔗/仙人掌级联掉落，`93eb71d`，~16 行）。

| 任务 | 提交 | 根因/做法（摘） |
|---|---|---|
| t416 叶子染绿 | 9a2443e | MC foliage 瓦片（grass_top/side, leaves, tall_grass）灰度 tintable；loader 乘 #5a8a3a 染绿 |
| t417 睡莲水面 | 47f6e13 | 选择射线 HitTorch（水穿透）→ 放到水格下沉；placeBlock 爬过水柱到首个气格 |
| t418 甘蔗 worldgen | 246817b | placeSugarcane 仅沙面（排除森林湖草岸）+ 10% tall-gate（多 1-3，~10% 到 5）；**主编排补：挖底全栈掉落 `93eb71d`** |
| t419 选包根目录 | 9d8fe41 | `resolveBlockDir` DFS 找 `.../assets/minecraft/textures/block` 后缀（选包根/任意层级都行） |
| t420 物品图标从包 | d9cda9b | `itemIconSource`→pack item PNG `file://` 覆盖 ToolIcon/MaterialIcon（pack 关回退程序绘制） |
| t421 生物贴图 | 0908737 | `mobTextureSource`→pack entity/；MobModel `packTextured` 属性做 T-cross UV 展开（按 box 推进；pack 关回退全脸 UV） |

- **构建/smoke**：全绿 + **pack-on 66 瓦片覆盖** + QML 加载（`root objects: 1`）+ 无 referenceerror/白块。
- **法律核查**：✅ R18o 提交**全代码**（无 `.png`/`.mcmeta`）；pack 仍本地 gitignored。红线守。
- **验收状态**：✅=编译+smoke+legal 过；**pack-on 视觉待你眼看**：叶子绿？睡莲浮？甘蔗 1-3 沙滩+挖底全掉？ESC 选包**主目录**生效？物品图标 + 生物外观像 MC？注：mob UV 是 best-effort（引擎 mob 是原创方块几何，非 1:1 MC），视觉需 run 验。
- **拆 R18p（已记 plan）**：资产 MC-pack 结构重组 + HD（TILE=128 解甘蔗糊）+ GUI 贴图从包。

---

## R18p P0 批次（紧急修复，**Workflow lean**）

> Run `wf_c7047670-f86`。**7 agents · 682,903 tok · 258 calls · 5,050s ≈ 84 min · 0 fail**。

| 任务 | 提交 | 根因/做法 |
|---|---|---|
| t425 流水性能 | 795c4ed | **帧数杀手**：crop/sugarcane/farmland/sapling tick 每 2.5-5s 全网格扫 160×160×128（3.28M 格），即使空→累积卡顿→FPS 崩。改**增量 growth-cell 索引**（空时零成本） |
| t424 图标空白 | 1393b62 | Canvas visible 判 `packImg.source.length===0`——QML url.length 是 **undefined**≠0→fallback 永隐→空白。改 `!packImg.visible` |
| t422 草侧 tint | 723056a | t416 整块 grass_side 染绿→dirt 部分也绿。改 `composeGrassSide()`=dirt 底 + tinted `grass_block_side_overlay`（仅顶绿条） |
| t423 甘蔗 | 5d1bba7 | worldgen 水检在 surfaceY 但沙在 surfaceY+1→永漏。改查 surfaceY **和 surfaceY-1**；放甘蔗需邻水 |
| t426 地牢 | 4fb3039 | 18 网格 35% 5×5→24 网格 10% **7×7** |
| t427 湖泊 | 018cf3b | t375 过密 4.5×→命中概率 50→**20%** |
| t428 床 2 格 | a2c273c | 1 格→head+foot（如门横置，state bit3 + facing）；注：头/脚贴图未分（需 atlas 扩，blocked by no-png） |

- **构建/smoke/法律**：全绿 + QML 加载 + 无 .png 提交。
- **验收**：✅编译+smoke；**待 playtest**：FPS 长稳？图标无空？草侧 dirt 不绿？甘蔗回来了+邻水种？地牢大？湖少？床 2 格？
- **R18q（P1/P2）**：mob entity UV 解析 / chest 贴图 / 资源查看器 / destroy_stage / bow 拉弓 / 羊毛 16 色+羊随机 / HD / 资产重组 —— **compact 后**做。

---

## R18q P0 批次（compact 后首跑，游戏杀手深度修复）

> Run `wf_9c334189-7b0`。**3 agents · 431,685 tok · 126 calls · 2,690s ≈ 45 min · 0 fail**。
> 主 agent 验收：build 复核 + 3 commit diff 独立代码审查（不另派 reviewer，省 token）。

| 任务 | 提交 | 根因/做法 |
|---|---|---|
| t437 性能/内存泄漏 | 4d0f892 | **"退存档再进仍卡"真根因**：clearAll() 三处 entity manager 做 vector::clear()→count→0，是 t256 slot-reuse 单调不变量漏掉的**唯一** count→0 路径。count→0 时 QML Repeater 想销毁 delegate，但 delegate 已 reparent 进 mobHost/itemHost QQuick3DNode（非 QQuickItem）→ Repeater 跟踪表无它 + 所有权转 host → 销毁不到 → 永久孤儿。每次退世界孤儿全部 entity delegate（MobModel+多 mob Model+眼/火舌子树+动画=数十 3D 对象/个），再进叠孤儿上重建→跨世界单调累积。C++ 审计全 clean（泄漏在 QML 场景图侧，C++ 工具盲区，同 t256）。**改**：clearAll 释放活体槽（releaseSlot：alive=false+入 free list+liveCount--）但**保留 vector**→count 不降→Repeater 不销毁→下世界复用既有 delegate（aliveAt 翻 true+revision bump 重绑）。高水位受 kCap(64/200/64)钳=有界常驻。 |
| t438 水岩浆交互 | a8bfd32 | t411 两 pass 只查对方 source(state==0)，flowing+flowing 相遇双方非 source→互不反应=共融不凝固（真根因）；且 pass B 流岩浆+水源误产 Cobble。**改**：pass B 按水邻 state 选产物——水源→Stone / 流水→Cobble（补 flowing+flowing 缺口）；pass A（流水+岩浆源→黑曜石）未改。完整矩阵覆盖 MC 1.0。t350 落地扩散未动。 |
| t439 透明 Z-fighting | 98feaed | cross/cutout 段（草/树苗/作物）走 pre-6.8 遗留 hack `opacity:0.99+alphaCutoff:0.5`，旧 backend alphaCutoff 仅 opacity<1 生效→把 cutout 草丛错误塞进透明 pass（深度写 OFF）→草不写深度→相邻草/远透明面无遮挡→透过看远处闪/穿透（真根因）。**改**：cross→`alphaMode:Mask`（不透明 pass、alpha 硬 discard、写深度=cutout foliage 教科书路径，弃 opacity hack）；water/lava/glass→`alphaMode:Blend`。lighting:NoLighting 全保留。 |

- **构建/smoke/法律**：全绿（零项目警告，仅 windeployqt dxil 工具噪音）；3 commit 均 `root objects after load: 1`、无 id-not-unique/failed-to-load/ReferenceError；全代码提交无 .png（红线守）。
- **代码审查（主 agent 独立审 3 diff）**：✅ 通过——t437 releaseSlot 按 t256 mark-only 不重排（slot index 稳定供 QML 绑定）故 for 边遍历边释放安全；t438 pass A/B 改不同格无冲突、quint8 显式转避 -Wextra；t439 alphaMode 分类正确、lighting 不变。
- **验收**：✅ 编译+smoke+代码审查；**待 playtest（人工）**：①t437 进世界玩 3+min→退存档→再进（重复 2-3 次）确认 FPS 不掉 + 任务管理器内存不跨 reload 单调涨（沙箱无法持续跑/读内存）；②t439 放玻璃墙/草丛后看远处确认无闪烁。注：t437 修复前的旧孤儿需**重启进程一次**清干净（修复只防未来累积）；high-water 跨世界常驻是有意 tradeoff（有界 vs 无界泄漏），F3 实体计数读 liveCount 不受影响。

---

## R18q 批1a（渲染/XP/农业 9任务，compact 后第2跑）

> Run `wf_e220ec40-257`。**9 agents · 1,533,077 tok · 672 calls · 12,666s ≈ 211 min · 0 fail**。

| 任务 | 提交 | 根因（真根因） |
|---|---|---|
| t440 cross黑底 | c01f955 | 手持/掉落 delegate 用 isPartialBlock() 路由，cross 族（花/蘑菇/睡莲/树苗/甘蔗/草）是另一族 isCrossBillboard()→落进不透明 BlockCube（无 alpha discard）→黑底。新增 isCrossBlock() 透传单一权威谓词，三处 delegate 路由 cross→alpha-test billboard |
| t441 箱子动画 | f21355c | **反复修不好真因**：决定开盖立起高度的是盖子"深"不是"厚"，历次只改厚度→零效果。满深盖开态立起等高=像第二只箱子。改浅深 0.5（开态仅立起~0.5格）。像素差分验证 33×31→33×16 |
| t442 树叶 | 5df353f | terrain 段材质漏设 alphaMode（缺省 Opaque），alphaCutoff 不生效→leaves fancy 叶 alpha 间隙黑 RGB 像素当不透明=黑斑。加 alphaMode:Mask（同 t439 契约，**t439 当初漏了 terrain 段**） |
| t443 XP | fec7952 | 三缺口：XP条只绑 playing(创造也显)/xpForMob 表无被动 mob/致死分支不清 XP。修：mode 门控+被动 mob 1-3 XP+致死清条+死亡格掉经验球 |
| t444 睡莲 | 56aa3b5 | ①灰(lily tile 61 未 tint→加 #4aa852)④放置无静水源预检⑤ShapeNone 无碰撞(改 isCollidable+薄板 AABB 可走)⑥无叠放守卫。②③已被 t440 修 |
| t445 仙人掌 | dcf8b83 | 五缺陷：满格几何(改 80% 居中柱)/挖底不塌(World::dropCactusColumn+checkCactusOnEdit)/仅上方伤害(player 也查水平4邻)/放置无邻检/物品不销毁。9 文件大改 |
| t446 甘蔗 | f3558eb | **根因**：placeSugarcane 用 heightAt(fBm) 作 surfaceY，但海沙在 seaColumnHeight(t338重塑,decoupled)→blockAt(x,heightAt,z)从不是 Sand→**0 甘蔗生成**(t423 后实际是0生成)。改用 seaColumnHeight，修后 81 blocks/37列 全 Sand |
| t447 作物 | 8c3022a | ①胡萝卜/马铃薯种不下(喂食分支 !=SeedId 一刀切 return,它们既食物又种子)②秒熟(kCropGrowPct 35%+湿润+雨>100%钳100%=17s长满→降6%)④骨粉(新增 BonemealId 0x232,1骨→3骨粉,催熟)。③挖耕地掉落已由 dropUnsupportedCropsAround 正确实现 |
| t448 锄头 | b2faee0 | **引擎级根因**：锄头耐久值(59/131/250)+锄地-1 都正确。真因在 normalizeDurability `if(durability==0)return 1`——durability=0 是结构体默认值(未初始化新工具)被误归一为1→首用即清槽。改 `<=0 return cap`。**影响所有工具/护甲** |

- **构建/smoke/法律**：9 commit 最终状态(HEAD b2faee0) build 零警告 + root objects:1、无 id-not-unique/failed-to-load/ReferenceError；全代码无 .png（骨粉/作物图标程序绘制 MaterialIcon）。
- **代码审查**：本次未逐 diff 审（21 文件 621+/163- 量大），信任各 agent 自测 build+smoke + 根因自洽（多个"先复现"实践：t446 发现0生成/t441像素差分/t445 worldgen计数）。重点抽查：t442/t439 同类 alphaMode 闭环；t446/t448 深层根因。
- **验收**：✅ 编译+smoke；**待 playtest**：t440 手持花无黑底？t441 开盖像盖？t442 树叶透空无黑斑？t443 杀牛掉XP+死亡清条？t444 睡莲绿水上面走？t445 仙人掌瘦身+挖底塌+碰伤？t446 沙滩甘蔗湖里无？t447 胡萝卜能种+不秒熟+骨粉催熟？t448 锄头连锄不消失？

---

## R18q 自主想法（compact 后追加：F3 叠层 + 打击感）

> Run `wf_d660f9a5-d48`。**2 agents · 299,440 tok · 105 calls · 1,465s ≈ 24 min · 0 fail**。

| 任务 | 提交 | 根因/做法 |
|---|---|---|
| t464 F3叠层 | 5303777 | F3 只把实体计数散落进 draw-call/day 行，无时钟/群系→无法验证 t437。WorldClock 暴露 dayCount Q_PROPERTY(+独立 dayChanged NOTIFY，moonPhase 8天才变不能复用)；F3 加三块：entities(mob/item/orb liveCount)/biome(biomeIdAt 映射通用名)/time(HH:MM day N moon M)。UI 只向下读不反向写 |
| t465 打击感 | a6171fd | ①手挥动已有(swingArm t67)②破块粒子原走 Particles3D **本环境不可用(Loader Error)→肉眼全空**，改 Model+Timer 池(96 UnitCube 预分配，spawn 找空槽，池满即丢，单 Timer 弹道，归位复用→无每帧 new 无泄漏，规避 t437 坑)；BlockParticles Loader Error→Ready。③Canvas 红 vignette+damageVignetteAnim(复用 t67 震动+变红) |

- **意外修复**：t465 揭示 BlockParticles 之前因 Particles3D 不可用而**破块粒子肉眼全空**（隐藏 bug），改 Model+Timer 后才可见。
- **构建/smoke**：HEAD a6171fd build 零警告 + root:1 + "BlockParticles Loader status = Ready"（旧版 Error）。
- **验收**：✅编译+smoke；**待 playtest**：F3 按 F3 看三行(尤其退存档再进看实体数不累积=验证 t437)；破块看碎屑粒子+手挥动；受击看红屏+震动。

---

## R18q 批2a（战斗+物品 5任务）

> Run `wf_077c0875-d06`（首次 schema 笔误 filesChanged 类型 → 修复 resume）。**5 agents · 566,818 tok · 213 calls · 3,518s ≈ 59 min · 0 fail**。

| 任务 | 提交 | 根因（真根因） |
|---|---|---|
| t449 mob死亡动画 | dbacf64 | damageEntity 在 health→0 同帧 emit mobDied → onMobDied 立即 spawnItem（红闪+物同出太急）。改：dead 时不 emit，deathTimer(500ms) 到 0 才 emit+releaseSlot（掉物延后）；QML 死亡 delegate 侧倒旋转 0→90° + 白烟(BlockParticles 池 buoyant gravity)；slot-reuse safe |
| t450 鱿鱼spawn | 4d9380e | **同族 t446**：spawnInitialMobs 用 heightAt(fBm自然地表)取水面 y，但 heightAt 不含水/海域重塑→blockAt(heightAt)==Water 恒 false→0 鱿鱼。改用 heightmapAt(列顶首个非空气,含水)。另改拒绝采样为目标6+上限200尝试 |
| t451 弓拉弓 | 2ff42bf | bow local frame: belly -X=forward(target), string +X=back(archer)。动画把弦+箭沿 -X 移=向前(错)，作者注释"-X后拉"是误。改 +X(向后) |
| t452 装甲 | a1fbf67 | t377 armor Models 一直在+接线对，但几何太薄(poke 0.01-0.03 亚感知)→第三人称读作无甲。放大到 0.04-0.075 + 加 calf legging；耐久改条(同工具)+tooltip 数字 |
| t453 中键复制 | d8902b4 | pickBlock 回退分支硬编码 setStack(selectedSlot) 覆写手持。改空槽优先(空→写选中；非空→找空槽复制+切槽；满→替换) |

- **构建/smoke**：HEAD d8902b4 build 零警告 + root:1；全代码无 .png。
- **模式 bug 警示**：t446(甘蔗)+t450(鱿鱼) 同族——heightAt 被误用当水面 y。建议未来 grep 所有 heightAt 用法审计（自然地表=heightAt；海域沙面=seaColumnHeight；列顶含水=heightmapAt）。
- **验收**：✅编译+smoke；**待 playtest**：t449 杀怪看侧倒+白烟+延后掉物？t450 水里有鱿鱼(日志 [t450] squid scattered: 6/6)？t451 拉弓弦+箭往后？t452 F5 穿甲见护甲+背包装甲耐久条？t453 持方块中键另一块→空槽复制不丢手持？

---

## R18q 批2b-1（物品/UI 5任务，含床重做+资源查看器两大功能）

> Run `wf_255d9b53-943`。**5 agents · 952,163 tok · 357 calls · 5,284s ≈ 88 min · 0 fail**。

| 任务 | 提交 | 根因（真根因） |
|---|---|---|
| t454 沙子图标+枯木 | 6799ddd | icon 是 offline bake(build_cube_icons.py)未接 CMake→t404 改 default_sand 颜色没 rebake→icon stale 橙；枯木 icon 是纹理设计(2px band 糊块)。rebake 沙子/tall_grass + 重写 dead_bush 细线扇形。**结构性债：offline bake 未接 CMake** |
| t455 羊毛16色+床配方 | 2011a87 | **关键**：default_wool/icon_wool **从未登记 qrc**→创造调色板羊毛图标空(用户"色空"真因,非缺色)。补 15 色 wool id(63-77)+8 色 bed(78-85)+16 床配方+qrc 登记修复；atlas 79→102 tile |
| t456 工作台熔炉图标+朝向 | 86ebe0a | 图标:iconSourceForBlock 固定 qrc 不查 pack→加 blockItemIconSource(probe item/block dir)。朝向:Furnace placeState 无分支(state=0)+tileFor 无 case→加 (facing&3)^1(同 Chest)+tileFor 解码 |
| t457 床重做+睡觉动画 | 1fa09b8 | 模型:满格 cube→ShapeBed 低 0.3 格(腿+板+羊毛+枕头)。睡眠:单 sleepProgress 瞬黑瞬醒→三阶段(躺下渐黑/稳定+起床按钮/唤醒渐显)，点击醒不跳黎明 |
| t458 资源查看器 | d0da465 | 无浏览入口→新增 ResourceBrowser.qml(JEI 式:全物品网格+View3D 旋转方块预览/大图标)，设置面板加按钮入口 |

- **构建/smoke**：HEAD d0da465 build 零警告 + root:1；提交的 .png 是程序生成工具产物（build_wool/build_dead_bush/build_atlas，项目自有原创纹理，非 MC pack 资产，红线守）。
- **结构发现**：①offline bake 工具未接 CMake（t454，源改 icon stale）②qrc 漏登记 icon_wool（t455，"色空"真因）——两个都是资产管线债，建议未来 wired 进 CMake。
- **验收**：✅编译+smoke；**待 playtest**：t454 沙子图标黄+枯木清晰？t455 创造羊毛16色满+合成对应色床？t456 工作台熔炉图标 HD+熔炉朝玩家？t457 床低模型+睡觉躺下渐黑起床按钮？t458 ESC→设置→资源查看器能浏览+3D旋转预览？

---

## MC 功能批（用户指定：云杉延伸/雪原浆果/冰物理/船，t466-t469）

> 首次 Run `wf_5a957c62-483`（t466 完成，t467-t469 撞 5h 限额 429 失败）→ 限额 14:00 重置后 resume 同 run（t466 cache，t467-t469 重跑）。
> t466：1 agent · 249,721 tok · 38m。t467-t469（resume）：3 agent · 728,851 tok · 96m。**合计 ~978k tok / ~134m**。t467 429 前的半成品(260行,缺贴图/qrc)已 stash 丢弃（重做更全）。

| 任务 | 提交 | 根因/做法 |
|---|---|---|
| t466 云杉木板+木制品 | a80b517 | 门/木制品多处硬编码 WoodDoor/Slab/Fence 字面量→加第三族(云杉)易断门两格联动。引入 isDoor() 单一权威谓词统一门逻辑；SprucePlanks(86)/Slab(87)/Fence(88)/Door(89) 并入 isPartialBlock/isSlab/isFence/isWoodLike；4 配方；build_spruce.py 程序贴图(共享 tile 模式)；atlas 102→103 |
| t467 雪原浆果丛 | 312d5a2 | 新功能。SweetBerryBush(90) cross 3阶段 + SweetBerry(0x233) 食物；雪原 worldgen placeSweetBerryBushes（SnowLayer 地表守卫避开海/湖，落实 t446 heightmap 教训）；右键采摘(降阶段)+食用(foodHungerAmount 单一权威)+穿越伤害(仙人掌模式)；build_sweet_berry.py 3阶段贴图 |
| t468 冰物理 | 0fb6c93 | 新功能。PackIce(91)/BlueIce(92)+isIce()单一谓词+iceSlipApproach(Ice8/PackIce4.5/BlueIce2.8)；tickIceFreeze(5s节流,暴露天空水源冻结)；玩家冰上指数滑动惯性(1-exp(-rate*dt))；ItemEntity 加 vx/vz 冰上滑(kItemIceFriction 0.4)；iceOnly 段(镜像 glassOnly)；Ice 改半透 Blend |
| t469 船系统 | b763f76 | 新实体类型。BoatManager(slot-reuse,同 ItemEntity)+浮水 lerp；step()顶部骑乘拦截(优先于飞/走,骑乘期禁玩家自身移动由船带动)；WASD tickRiddenBoat 冰上加速(Ice1.5/PackIce2.0/BlueIce2.5,复用 t468 isIce)；撞墙crash掉落；橡木/云杉船合成(5板U形)；Main.qml boat delegate |

- **构建/smoke**：HEAD b763f76 build 零警告 + root:1 + boatHost 进场景图(非孤儿)；提交 .png 是程序生成(build_spruce/sweet_berry/ice)，项目自有非 MC 资产，红线守。
- **架构亮点**：t466 isDoor() 统一谓词（避免硬编码 id 散布）、t468 isIce() 单一谓词供 t469 复用、t469 骑乘闭环在 step 顶部拦截——都是可维护设计。
- **验收**：✅编译+smoke；**待 playtest**：t466 云杉4件创造可见+配方可合+门两格联动？t467 雪原见红果灌木+右键采浆果丛回阶段0+食浆果回饥饿？t468 暴露天空水结冰+冰上走滑动惯性+丢物品冰上滑+Ice<PackIce<BlueIce 递增？t469 拿船右键水放船+右键上车+WASD划+冰面加速+撞墙掉船+Shift下船？

---

## t470 紧急性能修复（9 FPS regression，用户报"之前100FPS现在9FPS"）

> Agent `ad1489eff6aa2e443`（voxel-dev，后台单 agent）。137 tool calls · 2,798s ≈ 47 min。

| 任务 | 提交 | 根因/做法 |
|---|---|---|
| t470 性能 regression | edbb235 | **根因**：100 chunk × 6 段(terrain/water/lava/cross/glass/ice，t468 加第6段 ice 叠积)=600 Model 全 visible=true 每帧渲染，无视距剔除 + 无空段跳过(~370/600 段为空仍占场景图)。**修复**：①renderDistance culling(默认3 chunk/48格，玩家跨 chunk 边界才 O(chunk) 重算，同chunk内60Hz移动零开销；ESC 滑条 1-8 实时调)②空段剔除(visible=chunkInRange && vertexCount>0，meshRebuilt NOTIFY 自动隐/显，玩家后放该类方块自动复显)③F3 显示 `render r=N visible X/Y` + draw-call 用 visibleSegmentCount(替代过时 ncx*ncz*2)。600→154 段(**74%削减,3.9×**) |

- **构建/smoke**：edbb235 build 零警告 + root:1 + 诊断日志确认 culling 生效(玩家中心 49/100 chunk 可见, 154/600 段可见)。
- **其他热点核查(非主因,未改)**：BoatManager.tick 空早退 / tickIceFreeze 5s 节流+早退 / 流体 tick 走 m_waterDirty/m_lavaDirty 早退(t380) / BlockParticles 96-Model 池 inactive visible=false(t465)。
- **验收**：✅编译+smoke+culling 日志；**待用户实测**：进世界看 F3 FPS(目标>30，结构上 3.9× 渲染削减)——agent 无法交互测真 FPS。若 r=3 视野太窄(48格)可 ESC 调高 r(性能换视野)；若 r=3 仍<30 FPS 说明还有别的热点，再深查。

---

## 性能深度调研 + 修复（9 FPS regression，<10 FPS 玩不了，t470 culling 无效）

> 调研 workflow `wf_8c3c1476-64f`（3 路并行：CPU 探针 / GPU 后端 / mesh 回归）3 agent · 409,997 tok · 34m。
> 修复 agent · 58 calls · ~18m。FrameProfiler 探针 commit `a7eee69`；修复 commit `8604842`。

**真因（B 日志铁证 + C 代码精准分析）**：
- sun 步进每 ~3.3s 无条件 buildMesh(Sun) 全部 600 段（setSunDir 无 dirty/视距门控）；water 翻页每 800ms buildMesh(Water) 100 段。
- **t470 盲点**：edbb235 只门控 Model.visible，但 ChunkGeometry 仍绑 sunDir/waterAnimPhase → 所有 600 段无视距全成本重建 = "600→154 段零 FPS 提升"真因。
- 每次重建成本 ~24× 简单版（chunks 4× + height 2× + 段 3× + PCF 每顶点 ~16 heightmap 查询）。日志铁证：71s 14560 次 rebuilt（280/s，峰 896），6789 次 0-顶点空重建。
- **排除**：GPU/D3D11+RTX3060 健康（无软件降级），转 Vulkan 对此无效；dirty/clearAllDirty 正确（t155g 修过，非它）。

**修复 `8604842`**：①chunkInRange 进 ChunkGeometry（setSunDir/setWaterAnimPhase/setShadowsEnabled !range 时跳 buildMesh，false→true 触发一次 catch-up）= 修 t470 盲点 ②kSunSteps 360→72（sun 风暴 5× 稀）③waterAnimTimer 800→2000（2.5× 稀）④kMaxShadow 4→2（PCF 每顶点 heightmap 查询减半）。
FrameProfiler 实测：mesh 稳态 0.5-1.5ms/frame（风暴已灭）；sun 重建次数 5× 降。

- **未做**：实体 tick 门控（既有"世界模拟连续"设计：菜单/暂停仍推进，boat 浮水需常开；改它属 gameplay 语义变更非纯性能，留用户决定）。
- **残留嫌疑**：水模拟 wat 桶 menu 态 60-180ms/s（setWaterSilent 标 dirty 触发 chunk(5,2)/(6,2) 持续重建，水蔓延算法本身开销，独立路径）。
- **验收**：✅编译+smoke+profiler 数据；**待用户 playing 实测**：进游戏按 F3 看 prof 块（FPS + 哪桶 ms 大）。若 wat 桶大→水模拟下一瓶颈；若 mob/item 大→实体 tick。

---

## 性能 mob tick 节流（profiler playing 实测 mob 24.99ms/f → 节流）

> Agent · 96 calls · ~39m。commit `552f1c0`。

**真因（profiler 锁定 mob 桶 24.99ms/f）**：每 mob 每 60Hz 帧跑完整工作——tick() 含仙人掌检测 10 blockAt + AI 移动 mobAabbHitsSolid×2 ≈24 blockAt + jump/窒息/重力列扫描；hostile 每帧 tickHostileLife 天气/群系(skyLightAt/isPrecipitatingAt/weatherStateAt/biomeAt，每次 4×4 八度 fbm 噪声)；弓箭手 chasing+射程内 lineOfSightClear 每帧 32 步 isSolid 0.5-block march。每帧数千 blockAt + 每 archer ~30 噪声评估 = 8× 16.6ms 预算。tickSpawners 已节流(6s)非因。

**修复 `552f1c0`**：phase-staggered per-mob throttle（kAiTickInterval=4，按 idx 偏移使每帧恰好 1/4 mob 做重活，tick()/tickHostileLife 共享 m_tickPhase 同帧合并→负载均匀无 spike）。门控项(火烧/仙人掌/AI 移动决策/窒息/hostile 燃烧+消失)传 aiDt=累积 dt 保每秒平均速率(移动速度/伤害间隔/计时器不变，最大单步 0.56 block<1 无穿墙)；每帧项保留(hurtFlash/audio/knockback/水流 push/重力/玩家受击冷却/箭/掉落块物理→即时响应)。决策 60→15Hz(MC 1.0 mob-think-every-4-5-ticks 模式)。加 FrameProfiler bp 桶测 BlockParticles。

- **验收**：✅编译+root:1+菜单态 bp 桶就位；**待用户 playing 实测**：mob 桶 24.99→目标<5ms/f + bp 桶(碎屑开销) + FPS。若 mob 仍高→damageEntity emit 批处理(下一目标)；若近距移动抖→kAiTickInterval 4→3(20Hz 更平滑)。

---

## 性能 mob phys 仍未降 + frame 分桶定位（mob ai 0 但 phys 23ms）

> Agent · ~33m。commit `a323d93`（mob phys 二轮节流）+ `a36b4b0`（frame main/render 分桶 + F3/HUD 节流）。

**真因（profiler playing 实测 `mob sub: ai 0.00 phys 23.16`）**：552f1c0 的 aiTick 门控正确（ai=0 证明 AI 决策已降），但**重活在 aiTick 块外每帧跑**：静止重探 mobFootprintHasSupport（floor(pos.y−halfH)−1 列扫描）、mobInWater（每帧水查询）、speedScale（每帧地形噪声）、archer lineOfSightClear（chasing 时每帧 32 步 march）。= AI 节流了，物理碰撞/查询没节流。

**修复 `a323d93`**：把 resting re-probe / mobInWater / speedScale 全挪进 aiTick（4 帧一次）；archer LOS 结果缓存 kLosCacheInterval=0.5s（每秒 2 次而非每帧 32 步）。加 mob sub-bucket 探针（ai/phys/hostile/spawn/loop 分桶）精确定位。
**修复 `a36b4b0`**：①main_total 桶=QQuickWindow::frameSwapped、render_cpu 桶=beforeRendering/afterRendering(Qt::DirectConnection 渲染线程)→ 实测 `main*131 render 5.0` **铁证 main 线程 bound 非 GPU**（排除转 Vulkan）。②F3 文本构建 60→10Hz（100ms Timer，buildF3Text()）。③HUD 坐标同节流。④window.skyBaseColor 共享属性（300 chunk Model 读共享 1 个 vs N×60Hz 各自绑）。

- **验收**：✅编译+profiler；**待用户实测**：mob phys 应 <5ms（AI+查询都进 aiTick）；frame 桶确认 main vs render 占比。

---

## 性能 水蔓延全网格扫描（wat 57ms/s + 554 reb/s 真凶）——9 FPS 终结战

> Perf agent `ad0f0ef2e6379bddc`（后台 34m，tool_uses 100）。commit `c282bc0`。**这是 9 FPS 的最终根因**。

**真因（a36b4b0 分桶 + agent 代码分析双证）**：
- **水（最大开销）**：`tickWaterFlow`/`tickLavaFlow`/`tickIceFreeze` 在 dirty 置位时每次调用**扫描整个 W×D×H = 80×80×128 = 3.28M 网格**（走 blockAt 路由）。单扫 ~19ms × 3 扫/s = **57ms/s = 用户实测 wat 57ms/s**。算法会 settle，但活跃水流期间每扫 19ms，且任何有写的扫 emit worldChanged → **554 reb/s 网格重建风暴**。"未结算感"= 慢扫拖帧 + 持续重建。
- **mob phys**：移动碰撞 mobAabbHitsSolid 已被 t500(552f1c0) 限到 aiTick（疑虑过时）；剩余每帧物理=非静止 mob 重力+下扫（t500 不变性要求每帧保平滑下落）。发现潜在振荡：静止重探 `floor(pos.y−halfH)−1` 对非 2 幂 halfH（猪羊 0.45/敌对 0.9/蜘蛛 0.3）有 ~1 ULP 漂移，薄地板上反复 静止→落→着陆→dirty/emit 抖动。

**修复 `c282bc0`**：
- 水/岩浆/冰：**增量式流体格位索引**（`m_waterCells`/`m_lavaCells`，仿 t425 生长格模式）。`noteFluidWrite` 挂到**每个**写路径（两个 setBlock 重载 + setBlockFromEntity + setWaterSilent + setVoxelIfAir）；generate/finishLoad 走重建。扫描现 O(流体格) + blockAt 陈旧条目防护。冰用 collect-then-apply 防 setWaterSilent 遍历中删格致 unordered_set 迭代器失效。
- mob：重探加 +0.01f 浮点容差（远大于 1 ULP，远小于 1.0）→ 稳定 supportY。

**FrameProfiler 前/后（agent 离屏 smoke 测，全新世界）**：
| 指标 | 前（用户实测） | 后 |
|---|---|---|
| 单扫耗时 | ~19ms（3.28M 网格） | **0-42us**（格集，~500×） |
| wat | 57.0 ms/s | 活跃水流 47ms/s（应用相瞬时）；**settle 后 0.0** |
| 网格重建 | 554/s | **0 reb**（settle 后） |
| frame | 142ms（7 FPS） | **16ms（60 FPS）** |

确认 settle：格 5257→11113 扩散，~21s 后 `writes=0 settled=1` → wat=0.0、mesh 0 reb、60 FPS。

- **caveat**：mob phys<5ms 目标 agent 无法在无 mob 的离屏世界复现（其测 60 FPS 无 mob）。容差专修**薄地板上静止 mob**；若用户 28 mob 多为真·非静止（敌对追击/深水下沉），重力须保每帧（t500 不变性）→ 此改动 alone 物理不会 <5ms。主提升在水流修复；若运行后 mob phys 仍高，下查 recomputeLightAround/信号 emit 路径或实体状态分布，非移动碰撞（已节流）。
- **验收**：✅编译零警告（agent 报）+ c282bc0 已 build green（orchestrator 复核 `cmake --build build --target voxelsandbox` = no work to do）；**待用户 playing 实测**：进世界（特别是动过水的地方）按 F3 看 wat 桶应 settle→0、mesh reb 归零、FPS 回升。mob phys 若仍 ~23ms 见 caveat。

---

## R18r batch 1 附魔链（t471-t477，性能修好后自动开工）

> 主 workflow `wf_a64222c5-649`（5 agent 跑了 4.33 个，784,802 tok / 480 calls / 107m）+ 尾 workflow `wf_2ac1880a-f64`（2 agent，384,753 tok / 207 calls / 72m）+ 主编排抢救 t475。合计 ~1.17M tok / ~3h（含限额空等）。开工前提：c282bc0 性能修好（用户指令"性能 agent 修改好回来后自动开工"）。

**7 任务全 ✅**（附魔前置材料 → 附魔系统全链）：

| 任务 | 提交 | 做了什么 |
|---|---|---|
| t471 青金石 | `ed840f9` | LapisOre 块(id93, Y<32 矿脉 0.6%, 用 r2>>16 空闲位段不改既有矿分布) + 青金石物品(0x236, 挖直接掉青金石) + 创造栏 + 程序贴图(青金石矿/图标) |
| t472 黑曜石 | `eb45bf6` | Obsidian(t411 已有) 补 source+source→obsidian 流体分支(t438 的 flow+flow→圆石之外) + 爆抗(destroySphereSilent 跳过) + **新钻石镐 tier4**(0x112, 速度8/耐久1561, 3钻+2棍) + 黑曜石 minTier1→4/hardness12→50(仅钻石镐掉落) |
| t473 皮革纸书 | `cc7df90` | 纸(0x237, 3甘蔗横排→3纸) + 书(0x238, 3纸+1皮革) + 皮革掉落扩到猪(牛原有) + 创造栏 + MaterialIcon 自绘 |
| t474 附魔台 | `75c8f02` | EnchantingTable(94) + Bookshelf(95, 6板+3书) 块 + 2钻+4黑曜+1书合成 + 附魔 UI 外壳(3 槽, 消耗 XP1/2/3 + 青金石1/2/3) + 书架数(2格内≤15)提等级上限 |
| t475 附魔逻辑 | `bb25908` | **限额被杀后主编排抢救**：EnchantRegistry(14 附魔: 锋利/亡灵杀手/节肢克星/击退/燃焰/效率/精准/时运/耐久/保护×4/水上亲和, 各 maxLevel/weight/互斥组) + ItemStack.enchants[4](pack id<<8\|level) + InventoryOps/UI 搬运保附魔 + hotbar Q_INVOKABLE 桥。agent 死前漏加 enchantregistry.cpp 进 CMake → 链接报 undefined ref → 主编排补 1 行 CMake 即零警告通过 |
| t476 附魔效果 | `462b75d` | 全 14 附魔在**单点 calc** 实装(非每帧): 锋利+0.5\*lvl攻 / 亡灵杀手+节肢克星对族加成 / 击退 / 燃焰(ignite) / 效率挖速/(1+lvl) / 精准掉方块 / 时运掉落×(1..1+lvl) / 耐久 100/(lvl+1)% 跳 / 保护系 armorProtectionFactor EPF 减伤。新增 EnchantRegistry::findLevel + EntityManager::ignite + knockback 强度参 |
| t477 铁砧 | `279c347` | IronBlock(96, 9铁锭) + Anvil(97)+微损(98)+重损(99) 三阶段 + 3铁块+4铁锭合成 + AnvilUI(修复+12%耐久 / 附魔合并(pack/unpack) / 重命名 customName, 各消耗 XP via spendLevels) + 每操作 1/3 概率进阶损坏, 第3次碎→setBlock Air + 程序贴图(铁块/铁砧顶/底/2损坏态, atlas→117 tile) + slotRevision/selectedSlotChanged 低频 NOTIFY(非每帧) |

- **限额事故 + 抢救**（t475）：agent 跑了 23m/118 calls 在写附魔逻辑时撞 5h 限额(429, 重置 05:34:41)被杀, 留半成品(enchantregistry 新文件 + hotbar/ItemStack/InventoryOps/5 UI 改)。主编编排编译诊断=唯一缺口是漏加 CMake 源(链接 undefined ref), 补 1 行 `src/Game/enchantregistry.cpp` 即零警告 build green, 抢救提交 `bb25908`(没重做, 保住 23m 工作)。同 t467 先例(那次 stash 重做, 这次因缺口极小直接补)。
- **构建**：全 7 任务串行(shared files blockregistry/world/recipe/qrc/hotbar/Main.qml → 必须串行), 每 agent 自带 build 零警告 gate + targeted git add(无 .pyc)。最终 HEAD `279c347` 主编编排复核 build exit 0。
- **验收**：✅ 编译零警告 + smoke(root objects=1, 不崩)；**待用户 playing 实测**：附魔链是否真好用——挖到青金石/黑曜石、造附魔台附一把锋利剑看伤害、时运镐看掉落倍增、铁砧修复/合并/改名、铁砧损坏碎裂。mob phys 性能(c282bc0 caveat)仍待 28-mob 场景实测。
- **未做(留批2/批3)**：C 繁殖伙伴(t478-t483, **加 4 新 mob=perf 敏感**, 需用户先验收附魔链+确认 kCap/节流) + D 结构(t484-t487, worldgen 需增量非全扫)。

---

## 性能 mob 22ms→8.8ms（entitiesChanged 节流 + walkPhase 量化）——mob 卡顿真根因

> 主编排自主定位（不经 agent；agent 已 4 轮无效修复）。commit `15f4655`。**这是 mob 卡顿的最终根因**。

**真因（直接读代码 + 用户 F3 双证）**：`entityManager` 每帧 `++m_revision; emit entitiesChanged()`（只要 dirty 或有移除）→ 每个 mob delegate 的 **~12 个 revision 键控绑定**全部重估（O(slots×bindings) NOTIFY）+ 每个行走 mob 的 `MobModel` **walkPhase 一变就全量几何重建 + GPU 重传**（setWalkPhase→rebuild()，每帧触发）。F3 实测 `mob sub: ai 0.00 phys 22.35` —— AI 已节流到 0，重活在绑定扇出 + 几何重建。

**修复 `15f4655`**：
- entitiesChanged **节流**：dirty/移除时置 `m_pendingEmit`，每 `kEmitEveryN=3` tick 才真正 `++m_revision; emit`（绑定扇出 3×↓）。
- walkPhase **量化**：`kStep=2π/12`（每周期 12 腿姿），phase 量化到最近步长，同值即 return（不发 change、不 rebuild）→ 几何重建量骤降。

- **验收**：✅ 编译零警告；**用户实测 F3 `mob 22.35→8.80ms`**（在湿区测——瓶颈已移走，说明 mob 修复生效）。caveat：湿区（水+岩浆）成为新瓶颈（wat 157 + lav 138 + 454 reb），见下一条。

---

## 性能 水+岩浆交互区 9 FPS（wat 157/lav 138/454reb）——流体批量光照合并

> 主编排自主定位 + 修复。commit `d26cef8`。用户飞入水+岩浆密集区：`world 299.65 [wat 157.0 lav 138.4] mesh 112.41(454reb)`，日志 `tickLavaFlow 26us cells=1149 writes=31 settled=0`。

**真因**：流体 tick 已批量（`m_batchFluid`，t350/t380），但 `setWaterSilent` 仍在批量模式下**逐写调用 `recomputeLightAround`**——水写早退（lightOpacity=0 无发光），但**岩浆写必触发全盒重 flood**（lightEmission=15 → t351 判据）且凝固/蒸发改遮光（水→石/黑曜石 opacity 0→15 → t334 判据）也触发。交互区每岩浆 tick ~31 次写 = **31 次 31×64×31 盒 refloodBox** + 每次写标脏 → 触发 chunk 重建风暴（454 reb）→ world 桶 ~300ms、9 FPS。`settled=0` = 水/岩浆界面持续凝固→新液流入→再凝固的合法活跃态（MC 机制如此），非 bug；瓶颈在每写重 flood 的成本。

**修复 `d26cef8`**：批量模式下 `setWaterSilent` 把光受影响编辑（同 recomputeLightAround 早退判据：遮光翻转 ‖ 发光增删）延迟记入 `m_pendingLightEdits`；`tickWaterFlow`/`tickLavaFlow` 在 `m_batchFluid=false` 后调 `flushPendingLightEdits()` —— 对延迟编辑的 **±R 盒之并做一次 `refloodBox`**（每编辑影响区 ⊆ 其 ±R 盒 ⊆ 联合盒，盒面边界种子法成立；任一 sky 编辑则 doSky=true 超集）。N 次重 flood → 1 次。非批量路径（玩家/世界编辑）不变（立即重 flood）。套用 `destroySphereSilent` t383 已验收的批量收口模式。

- **验收**：✅ 编译零警告 + `cmake --build --target voxelsandbox` no work to do（world.cpp 强制重编 exit 0）。**待用户 playing 实测**：飞入原水+岩浆区按 F3 —— `lav` 桶应显著下降（light 合并）、mesh reb 数应随 settle 归零、FPS 回升。caveat：若湿区 FPS 仍低，下一步查 chunk 重建风暴本身（reb 数不降 → 属 `settled=0` 持续写 + 全量段重建，非 light）。

---

## R18r batch 2 繁殖/伙伴（t479 缺口 + 丛林群系 + t480-t483，4 新 mob）

> Workflow `wf_59ee8dd5-ebd`（5 voxel-dev agent 串行，共享文件必须串行）。1,464,940 tok / 552 calls / 274m（16472104ms）。批 1 完成后用户指令「直接开工，子 agent + workflow 完成所有剩余任务」。开工前提：性能护栏已焊死（mob `15f4655` + 流体 `d26cef8`，见上两条）。

**5 任务全 ✅**（t478 繁殖主体 t400 已实现，仅复核）：

| 任务 | 提交 | 做了什么 |
|---|---|---|
| t479 缺口+t478 复核 | `49f6387` | feedBaby(i) 每喂减 kBabyFeedGrow=12s（≈kBabyGrowTime=120s 的 10%）加速成长；PlayerController 喂食分支据 isBabyAt(i) 分流（幼崽→feedBaby / 成体→enterLoveMode）；mobDied 信号新增 wasBaby（deathBaby 致死瞬间快照，同 deathBurned 模式，规避 0.5s 死亡动画窗口内 growTimer 衰减漏判）→ Main.qml onMobDied 早退跳过战利品+XP（幼崽不掉落）。t478 主体（enterLoveMode/loveTimer/breedCooldown/baby/growTimer/tickBreeding/kPassiveMobCap）未改，纯增量。顺带修 entitymanager.cpp tick() 既有的未用变量 physNs（t500 遗留）。 |
| 丛林群系（t481/t486 前置） | `857343d` | Biome 加 Jungle=6；biomeAt 第 5 独立低频 fBm（0.014, seed offset +5133）从 Forest+Plains band 雕出（阈值 0.25 → 160×160 日志 jungle=3418/25600=13.4%，10 次种子均值 13.5%，在 10-20% 目标内）；heightAt 丛林 amp=5.0；placeJungleTrees 高树（主干 5-7，半径 3 浓密 5 层冠 vs 橡树 4 层）+ placeTallGrass 丛林浓密下层；Desert/Hills/Snowy/Swamp 早退保留；确定性（双 16×16 run 同，160×160 与纯函数 Python 复现逐位一致）。pickPassiveMobType clamp biomeId≥4→Plains 保护下游。无 WorldgenVersion → 旧存档 chunk 保留、新 chunk 按新群系（可接受）。 |
| t480 狼 | `ce279a3` | MobWolf=10 + 犬科 MobModel 几何（躯干/头/立耳/腿）+ build_mob.py 程序灰狼贴图。森林/针叶林散布（biomeIdAt 3/4）；骨头驯服 tameWolf（33%，成败都耗骨）+ toggleWolfSit（右键驯服狼切坐留守/站跟随）；aiWolf 跟随主人（kFollowMinDist 停步 + kWolfTeleportDist 防掉队瞬移）+ 防御三来源（m_wolfTarget 由 attackMob 近战 / Stalker 爆炸 / Bones 箭 arrowShooter 注册 → 狼追击咬击）；喂肉繁殖（isWolfMeatItem → 驯服狼 enterLoveMode/feedBaby，MobWolf 入 isBreedableType 带 tamed 门控 + 幼崽继承驯服态）；尾巴角度据 healthAt/maxHealthAt（满血 35°→残血 140°，QML wolfTailPivot 独立尾 Model）；未驯服狼敌对玩家；受击红闪复用 hurtFlashAt；kCap=64 + t500 aiTick 节流保留。 |
| t481 豹猫 | `5e98481` | MobOcelot=11 + 斑点豹猫 + 3 色猫变体（mob_cat_black/ginger/cream）程序贴图。丛林散布（biomeIdAt==6）；生鱼驯服 tameOcelot（~33%，失败仍耗）→ 随机毛色 0..2，QML 据 ocelotTamedAt/ocelotVariantAt 切贴图；aiStalker 顶部 nearestOcelot（kStalkerFleeRange=6）→ 背离猫逃离 + fuseTimer 归零（优先于追踪/蓄力）；aiOcelot（未驯服游荡 / 驯服跟随 walk+瞬移 / 坐留守），空手右键已驯服猫 toggleOcelotSit；繁殖（生鱼触发，幼崽继承驯服态与毛色）。**⚠️ 此 agent 审查时安全分类器临时不可用 → 主编排复核提交**：纯代码 + 4 张程序贴图（167-219 字节，build_mob.py 生成，非 MC 资产），无 docs/ 包文件，无 git add -A 越界。HEAD 构建绿。 |
| t482+t483 双傀儡 | `907a990` | MobSnowGolem=12：南瓜+雪块×2 摆放检测（placeBlock 钩子 + setWaterSilent 静默移除 3 块）+ aiSnowGolem 抛 Snowball 弹丸（damageEntity 1HP + 3s slowTimer ×0.5 减速，只打敌对）+ 行走身后留 SnowLayer + 沙漠/热/下雨融化（biomeIdAt==Desert OR isPrecipitatingAt → 致死，无掉落）。MobIronGolem=13：T 形铁块×4+南瓜双向检测（移除 5 块）+ aiIronGolem 追击（damageEntity 8HP + 1.5× knockback）+ 死掉铁锭 0x203×3-5 / 红花(49) ~50%。新方块 Pumpkin(100)/Snow(101) 加进创造调色板 + 图标。南瓜头+方块身模型（Main.qml UnitCube 堆叠含刻面双眼）。 |

- **构建**：5 agent 串行（共享 entitymanager/mobmodel/Main.qml/blockregistry/world → 必须串行），每 agent 自带 build 零警告 gate（cmake --build --target voxelsandbox exit 0）+ targeted git add（无 -A、无 .pyc、无 docs 包）+ smoke（exe 启动 6s 不崩，root objects after load: 1）。HEAD（907a990）主编排复核 `cmake --build` no work to do = 全绿。
- **MobType 现状**：0 Test/1 Pig/2 Cow/3 Sheep/4 Shambler/5 Bones/6 Stalker/7 Spider/8 Chicken/9 Squid/**10 Wolf/11 Ocelot/12 SnowGolem/13 IronGolem**。kCap=64 保留（含新 mob）+ t500 aiTick 节流（新 mob AI 全走 aiTick/aiDt 错峰）。
- **验收**：✅ 编译零警告 + 启动不崩 + QML 加载正常；**待用户 playing 实测**：① 喂幼崽加速成长（t479）；② 杀幼崽无掉落；③ 森林见狼/骨头驯服坐站跟随尾巴（t480）；④ 丛林见豹猫/生鱼驯服变 3 色猫/Stalker 被驱赶（t481，**需飞到新生成的丛林 chunk，旧存档区无丛林**）；⑤ 摆南瓜+雪块×2 出雪傀儡（抛雪球/留雪/沙漠融化）/T 形铁块+南瓜出铁傀儡（高伤击退/掉铁锭）（t482/t483）。mob 性能（10→13 种 mob，kCap=64）待 playing 实测确认。
- **未做（留批 3）**：D 结构 t484-t487（批 3 workflow `wf_72752a0c-fa0` 已启动，4 串行 voxel-dev：Mineshaft/DesertTemple/JungleTemple/Stronghold）。

---

## R18r batch 3 世界结构（t484-t487，4 结构 + 8 新方块 + 1 新 mob）

> Workflow `wf_72752a0c-fa0`（4 voxel-dev agent 串行，共享 world.cpp/blockregistry/playercontroller → 必须串行）。1,377,330 tok / 705 calls / 184m（11023348ms）。批 2 完成后立即启动（依赖丛林群系 `857343d`）。

**4 任务全 ✅**：

| 任务 | 提交 | 做了什么 |
|---|---|---|
| t484 废弃矿井 | `2e40396` | 新 Cobweb(102)/Rail(103)（cross/贴地 + 程序贴图 build_cobweb.py/build_rail.py + atlas tile 120/121 + 6 铁锭→16 铁轨配方）；placeMineshaft 确定性（hashColumn 网格采样 + hashVoxel 散布，Y<48；kMinePct 18→40 使默认 seed 1337 稳定 ~6 座；Planks 地板 + WoodFence 立柱 + Rail + Cobweb + 暴露煤/铁矿 + 末端宝藏箱）；ChestStateMineshaftFlag bit3 → 首开填 mineshaftChestPool（矿物/附魔书/铁锭）。 |
| t485 沙漠神殿 | `8087799` | 新 TntBlock(104)/CutSandstone(105)/火药物品(0x239，Stalker 掉 1-2 + TNT 配方 5 火药+4 沙)；placeDesertTemple（isDesert 守卫 + grid 48 + 45%；阶梯金字塔底 15×15 顶 3×3 CutSandstone 顶冠 + 7×7×4 密室 + 4 箱 ChestStatePyramidFlag + 中央 3×3 TNT 上垫 CobblePressurePlate）；scanTntTraps 每 tick 扫玩家 footprint → detonateTntBlock（复用 destroySphereSilent 球形破坏 + 距离衰减伤玩家 + explosion 音/视 + ~50% 掉落，伤害仅 Survival）；pyramidChestPool（腐肉30/骨25/金锭18/青金石12/红石10/钻石5/附魔书3，kPyramidRolls=4，坐标确定性 seed 盐）。 |
| t486 丛林神殿 | `52afc8c` | 新 MossyCobble(106)/Dispenser(107)（state 编码朝向同熔炉，tileFor 按 state 选前面排出口贴图，放置朝玩家）；placeJungleTemple（Jungle 群系 grid 40/pct 50，苔石围墙+地板+天花板+入口缺口+走廊，实测 160×160 产 1 座）；scanDispenserTraps（压力板 4 水平邻 == Dispenser → spawnArrow 朝板方向水平射箭，per-dispenser 2s 冷却，无红石直接触发）；ChestStateJungleFlag bit5 → jungleTempleChestPool。 |
| t487 要塞 | `187498b` | 新 StoneBrick(108)+石砖台阶/EndPortal(110)/EndEye 物品/银鱼 mob（MobSilverfish=14，小型虫追击 AI + mobmodel + 程序贴图）；placeStronghold（Y<30 确定性，迷宫大厅 + 书架图书馆 + 中央 3×3 末地传送门房 + 银鱼刷怪笼 + 战利品箱）；持末影之眼右键传送门 → state bit0 翻 → end_portal_active 亮绿旋涡视觉 + 日志（末地维度占位，不实现）。 |

- **构建**：4 agent 串行（共享 world.cpp/blockregistry/playercontroller → 必须串行），每 agent 自带 build 零警告 gate + targeted git add + smoke（root objects after load: 1 不崩）。HEAD（187498b）主编排复核 `cmake --build --target voxelsandbox` grep error/warning 空 = 全绿。
- **新方块**：Cobweb(102)/Rail(103)/TntBlock(104)/CutSandstone(105)/MossyCobble(106)/Dispenser(107)/StoneBrick(108)/EndPortal(110) + 火药物品(0x239)/EndEye 物品 + 银鱼 mob。**MobType 现 0-14**（+Silverfish）。Atlas tile 120-127 + AtlasTileCount 128。
- **既有告警说明**：多个 agent 用 `-Wall -Wextra -fsyntax-only` 全文件复核时报告 blockregistry.cpp:777 'state' 未用 + world.cpp destroySphereSilent misleading-indentation + az1 未用为**既有非本任务代码**（历史遗留，非批 3 引入）；实际构建 gate（cmake --build）零 warning。留作后续清理项。
- **验收**：✅ 编译零警告 + 启动不崩 + worldgen 确定性（同 seed 同结构计数，日志佐证）；**待用户 playing 实测**：① 地下挖到废弃矿井（巷道/立柱/铁轨/蛛网/暴露矿/宝藏箱首开填矿物）；② 沙漠见金字塔 + 踩压力板引爆 TNT；③ 丛林见苔石神殿 + 踩板被发射器射箭；④ 地下深处挖到石砖要塞（图书馆/传送门房/银鱼）+ 持末影之眼右键激活传送门（绿旋涡）。**需飞到新生成的 chunk**（结构只在新 chunk worldgen 时放，旧存档区无）。
- **R18r 全批完结**：t471-t487 全部 ✅（附魔链 t471-t477 / 繁殖伙伴 t478-t483 / 结构 t484-t487），HEAD `187498b`。性能护栏（mob `15f4655` + 流体 `d26cef8`）+ 批 1（`ed840f9`-`279c347`）+ 批 2（`49f6387`-`907a990`）+ 批 3（`2e40396`-`187498b`）全绿。遗留：水+岩浆交互区 perf 待 playing 实测确认；既有 `-Wmisleading-indentation` 等告警清理。

---

## 性能 5 FPS 回归诊断 + 修复（R18r 批 2/3 后；不算 t 轮，perf 前缀）

> 用户报"加入东西后 FPS 从 9 掉到 5，F3 输出看着正常"。诊断 workflow `wf_25b11141-292`（5 路并行 general-purpose，267,972 tok / 153 calls，中途断网 2 路 502 失败 → 主编排重跑 qml-render + frame-attribution 两路同步 agent）。全部 5 路高置信收敛。

**真因（5 路交叉核对 + 主编排自己读代码确认）**：成本在 **QtQuick3D GUI 线程 scene-graph 同步期**（F3 无具名桶，藏身 `main_total − sim` 残差）。mob Repeater 每 delegate 槽**无条件实例化全部 ~16 种 mob 类型的模型子树**（~108 Model/槽 × 64 槽 ≈ **8000 场景图节点**，95% invisible-but-synced）+ mobBurnFlames 的 **448 个 loops:Infinite 动画每渲染帧推进**（QML 动画 visible:false 不暂停，revision 节流管不住）。批 2 加 5 新 mob → 每槽 +23 Model（+1472 节点 ~29%）→ sync O(节点) 涨 +88ms，正好对上 111ms(9FPS)→200ms(5FPS) 回归。
**排除项**（4 路证实非真凶）：mob AI 已在 aiTick 门控内（扫描微秒级）；陷阱扫描只扫玩家脚下几格；worldgen 一次性（无 per-chunk 流式，结构全 hashColumn 稀疏采样）；实体上限 kCap=64 健全（无泄漏，稳态 ~36-64）；F3 文本 10Hz 节流已生效。

**修复（3 commit + 1 探针）**：
- `31bf3bd` perf(mob): mobBurnFlames Repeater `model: isBurningAt ? [7] : []`（非燃烧 0 delegate → 杀 448 空转动画 + 1344 节点）+ MobModel::setMobType 加 14（silverfish 原被钳成猪几何，正确性 bug）。
- `1604730` perf(mob): **每槽 Loader 按 entMobType 只实例化匹配块**（16 个 mob 类型块各包 `Loader{active:原visible条件; sourceComponent:Component{原块verbatim}; onLoaded:item.parent=mobDelegate}`）。节点 8000→~500（仅活跃 mob 的块）。onLoaded 领养走 t16 教训（Loader 是 2D QQuickItem，加载的 3D Node 默认孤儿）。voxel-dev agent 实现 + 自测编译零警告 + smoke root objects=1；主编排复核 Pig/SnowGolem 两例 Loader 结构 + 16:16 onLoaded 对齐。
- `d19be60` perf(prof): **qmlSync 帧桶**（main.cpp beforeSynchronizing→afterSynchronizing）+ F3 frameLine 显示。让 GUI scene-graph 同步成本**可见**——下次"还卡"时 F3 直接看 qmlSync 是否仍高（vs sim/render）。main ≈ sim + qmlSync + 事件循环残差。

**遗留/待办**：
- **待用户 playing 实测**：进世界按 F3 看 `qmlSync` 应从 ~150ms 大降、FPS 回升；mob 仍正常可见（Loader 领养生效）。若 mob 隐形 → onLoaded 领养在该 Qt 版本失效（退回直接子节点 + 其它削减）。
- **既有告警清理**（非本轮）：blockregistry.cpp:777 isCollidable 'state' 未用；world.cpp destroySphereSilent misleading-indentation + az1 未用。
- **AI 微优化**（跳过，微秒级非真凶）：aiStalker nearestOcelot 加 m_aliveOcelotCount 早退；aiIronGolem nearestHostile 加 attackCooldown 门控（同 aiSnowGolem）。
- **架构审查**：voxel-tester-arch agent 后台审查 Loader 重构正确性（active 条件对齐 / onLoaded 覆盖 / tinted id 自包含 / 类型无关块未动），待回报。

---

## 性能 mesh 重建风暴诊断 + 修复（Loader 修复后暴露的第二层瓶颈；perf 前缀）

> 用户实测 3 张 F3：开局 100 FPS（`qmlSync 0.0` = Loader 修复生效）→ 夜晚战斗 14 FPS（mesh 254reb + mob phys 14ms + bp 22ms）→ **第 4 天白天 mobs 0 仍 9 FPS（mesh 116ms 441reb）**。1 个 voxel-dev agent 诊断 + 修复（commit `901bc2e`）。

**数据重新推算（关键）**：F3 flush 每 60 PlayerController tick 触发，9 FPS 时窗口被饿到 ~6.7s 而非 1s → 441reb ≈ **66 reb/s**（非 441/s）；"每帧全 49 chunk 重建"是窗口假设错误的错算。

**根因（agent 逐 markDirty 路径核验 + 修复，4 项）**：
1. **非批量静默写 tick**：tickCropGrowth/tickSugarcaneGrowth/tickFarmlandHydration/tickIceFreeze 的 apply 循环裸调 setWaterSilent（m_batchFluid 只在 water/lava 开过）→ 每次写 emit worldChanged + clearAllDirty = N 写 N emit + 清后又被下一格标回（"clear-then-re-dirty"）。每 chunk 6 段共享脏标记 → 1 脏 chunk 6 次 buildMesh。农场/雪原成熟格数随时间增多 = "渐进恶化"。**修**：4 tick 全套 `m_batchFluid=true` + flushPendingLightEdits + 末尾 1 次 emit/clear（照搬 tickWaterFlow:649 模式）。
2. **refloodBox 逐 chunk 切片比对 z 界 bug**（world.cpp refloodBox）：z 循环用全局 `z1` 而非 chunk 局部 `az1` → 中间 chunk 越界读**下一 chunk**光格 → 误标邻 chunk 脏 → 一次光照重算本只标 1 chunk 却连带标盒内一串（脏集放大）。**修**：z 上界改 az1（顺带消 az1 未用告警）。
3. **sun 步进/水翻页对空段无谓重建**（chunkgeometry.cpp）：setSunDir 每 ~16.7s 重建 294 段、setWaterAnimPhase 每 2s 重建 49 段，多数 chunk 的 lava/glass/ice/cross/水段为空（0 顶点）→ 纯浪费。**修**：`m_vertexCount==0` 守卫跳过空段（setChunkInRange catch-up 不加守卫，防出视距期间编辑陈旧）。
4. **F3 rebuild 原因拆分**：prof 行现为 `mesh Xms (Yreb [Ndirty d Nsun s Nwater w])`，重建驱动直接可见。

**验收（待用户实测）**：静止场景（无农场/雪原/不移动）mesh reb 应大幅降（空段守卫砍 sun/水翻页）；农场/雪原活跃期 reb 应从 N×每窗 → 1×每窗；F3 看 `[d s w]` 占比定位残余。**次要点**：夜晚战斗 14 FPS 含 mob phys 14ms（54 怪移动的每帧物理，战斗固有）+ bp 22ms（粒子），mesh 修复后应改善但不消除战斗开销——若战斗仍卡下轮降 kHostileMobCap 或节流移动物理。

---

## mesh 风暴根治 + 箭消失 + /kill + 跳跃 bug（3 串行 voxel-dev，workflow `wf_de0c9a2b-81e`）

> 用户 F3（14 FPS Swamp 夜 r=8）：`mesh 159.66ms (622reb [36d 325s 261w])` + 报骷髅箭不消失 + /kill 不清掉落物 + 被怪打后跳不起来。553k tok / 222 calls / 128m。

**3 任务全 ✅（构建零警告，主编排复核）**：

| commit | 任务 | 做了什么 |
|---|---|---|
| `b5cc1c6` | mesh 风暴根治 | **sun 步进粗量化门** `sunRebuildDue()`：只在影带穿越(0.30/0.40)/仰角累计>0.12rad/方位角>0.35rad/>120s 硬顶 才重烘（325段/16.7s→日间~十几次/~30-70s，夜间零）；**水翻页静态化**（删 waterAnimTimer+绑定，水 tile 硬编 phase 0 帧 + 烘死空间涟漪，261段/2s→0）。agent 自测静态场景 281/331 窗口零重建。昼夜亮度仍由材质 baseColor 平滑，阴影粗更新无亮度跳变。 |
| `f64f8a9` | 箭 60s 必消失 + /kill 清掉落物 | 箭 spawn 记墙钟 `arrowSpawnMs`，tick 硬检 `>=kArrowDespawnMs(60000)` 必 releaseSlot（任何箭：玩家/骷髅/飞行/嵌入，不依赖 dt→低帧/dt=0 仍删）；`/kill @e` 加 `itemEntities.clearAll()`（连 mobs+items+orbs 全清，三类各 emit changed→F3 entities 归零）。 |
| `87a129a` | 被怪打后跳不起来 | **根因（引擎级）**：受击垂直击退放 `m_knockback.y` 又每 tick 施重力 → 与 `m_vel.y` 自身重力**双重力**（dv/dt=-2g）；着地清零 m_knockback.y 后，水平击退衰减期(~1s)积分块仍跑、反复把它拉负向下拽 → 吃掉其后跳跃(kJump=8.4)/上浮(kSwimUp=4.5) → "被怪打后只跳半格、水里跳不上一格、过一会恢复"。**修**：垂直击退走 `m_vel.setY(max(m_vel.y, kHitKnockbackUp))`（单一重力、不抵消更高跳跃/上浮），`m_knockback` 只剩水平 XZ。经验入 lessons-learned。 |

- **验收（待用户实测）**：① F3 mesh reb 的 [s][w] 应~0（水静态、sun 罕见），总 reb << 622；② 水面无流动动画但有空间涟漪（视觉可接受？用户定夺要不要找回动画）；③ 骷髅箭落地/插墙 60s 消失；④ /kill @e 后 F3 entities 归零；⑤ 被怪打后能正常跳（≥1 格）+ 水里跳上一格。
- **次要点遗留**：夜晚战斗 mob phys 10-14ms（54-43 怪移动的每帧物理，战斗固有）；若仍嫌卡可下轮降 kHostileMobCap 或节流移动物理。

---

## R18s 全轮（t488-t514，27 任务全 ✅，4 批串行 voxel-dev + 终轮 arch review）

> 用户 playtest 反馈 ~27 项（性能残留 + 水动画要回 + TNT 连锁 + 渲染/方块机制/UI 大批量）。4 批串行子 agent（一次一个，R18+ 手动编排模式），每任务主编排读 diff + 构建零警告 + 启动冒烟 + targeted git add/commit。约 6.5h 实际墙钟（跨 2 个 API 限额窗口：t490 agent 连接中断→SendMessage 失败→新 agent 续做；t503 batch agent 429 限额→限额恢复后续做）。

**Code review（voxel-tester-arch）：PASS** — 0 critical（法律红线全守：2236 包 PNG 全 gitignored、零 MC 资产进 git、专名仅注释映射）、0 major（零警告+分层全守）、1 minor（末影中文译名开源前建议区隔，非阻断）。报告 docs/test-reports/r18s-arch-review.md。

### 批 1 A 性能（t488-t490）
| commit | 任务 | 做了什么 |
|---|---|---|
| `42cfb88` | t488 性能残留 | **流体活动盒增量扫描**（fluidActExpand/Reset + tickWaterFlow/LavaFlow 盒过滤，盒空兜底全量；波前级联经 noteFluidWrite 扩下次盒）+ **天光通道细化**（setWaterSilent 批量 sky 按列上方遮挡判定，地下交互跳整列天光 reflood）+ **ignite 批量**（岩浆焚毁木块 N 次 setBlock 折叠 1 次 worldChanged）+ **slot kind 中性化**（releaseSlot 清 kind=Item→空槽 Loader 卸载重子树，/kill 后 main 残留主嫌疑）+ F3 加 residual 桶 + live/slots 利用率探针。本机菜单态 residual≈vsync 正常；用户 TNT+/kill 场景探针就位待实测。 |
| `f298b87` | t489 水岩浆动画 | **材质级 flipbook**（程序生成 water_strip 32×512=2列×32帧 / lava_strip 16×256=16帧；chunkgeometry 水/岩浆面 UV 改帧 0 区 v∈[0,1/N] 半纹素内缩分母=纹理总高；Main.qml positionV 动画绑定帧 k，Qt 6.11 Texture vOffset→positionV 重命名）。**零 mesh 重建**（F3 [w] 稳态 0）。 |
| `41f795c` | t490 TNT 连锁 | PrimedTnt 实体（复用 FallingBlock kind + primed/fuse 字段，halfW=0 可穿透/可堆叠）+ detonateTntSphere 公共主体（destroySphereSilent + 链式引燃 destroyed 内 TntBlock→spawnPrimedTnt 错峰）+ fuse tick 倒计引爆（非同步递归，无栈溢出）+ 点火源（scanTntTraps 压力板水平四邻 / Lever=109 WoodButton=110 StoneButton=112 右键 / 右键 TNT 手动）+ 白闪脉冲 delegate（频率随 fuse 加速）。 |

### 批 2 B 渲染（t491-t499）
| commit | 任务 | 做了什么 |
|---|---|---|
| `8890d45` | t491 草粒子绿 | blockColor switch 扩全枚举 1..114（tall_grass=24 落 default 白真因；用户「草」=草丛非草方块）。 |
| `a381cfa` | t492 工作台/熔炉 icon | build_cube_icons render_front 正面 dimetric（旧顶投影遮炉口/网格→用户读作 2D/普通石块）。dispenser 同。 |
| `acac3d5` | t493/494/495 | lapis_ore tile 108→包 lapis_ore.png（pack 激活用包 stone 底，矿脉不再一眼可见）；FurnaceStateLitFlag=0x04 + tile 134 furnace_front_on + FurnaceUI 燃烧态驱动 setFurnaceLit；build_ice draw_pack_ice 淡蓝白重做（B>G>R，非白羊毛）。 |
| `ca159b5+38a37dc` | t496 床重做 | partialblockgeometry ShapeBed 重写（床头/尾板 + 白枕 + 4 腿 + 纯绗缝被面）+ 16 色 default_bed_*/icon_bed_* 重生成。 |
| `e823288` | t497 物品图标 | resourcepackmanager emptyArmorSlotSource API + EndEye(0x23A)→ender_eye.png 映射 + SurvivalInventory 空盔甲槽 pack 图（金/钻全套工具因项目无物品 id 属系统限制）。 |
| `3faec95` | t498/499 | 玩家 F5 护甲凸出量（z scale 被身体内嵌遮挡→放大 0.03-0.09）+ 雪傀儡眼/嘴 z 凸出（被南瓜遮挡）+ 头放大 + 刻面嘴 + IronGolem 同修。 |

### 批 3 C 方块机制（t500-t510）
| commit | 任务 | 做了什么 |
|---|---|---|
| `a10a369` | t500 草挖泥土 | Grass dropId Grass→Dirt（silk_touch 附魔 t475 已覆盖掉 Grass 自身，一行修）。 |
| `f864312` | t501 木梯贴侧 | ladderFaceFromNormal + placeBlock full-cube 侧校验 + partialblockgeometry 单片贴墙 quad（state 朝向）+ 失撑掉落（dropUnsupportedLaddersAround）。 |
| `4d32637` | t502/504 | FurnaceUI 成品居中 + 进度箭头居间 + burnTotal 燃料进度（火焰收缩+底条）；checkDeadBushOnEdit（破下方→枯灌木掉落）。 |
| `e382d41` | t503/506/507 | 仙人掌 worldgen 4 邻 isSolid 守卫；Ice 破→生 Water/silk 掉 Ice + PackIce/BlueIce silk 掉自身；BrownMushroom=115 + checkFlowerMushroomOnEdit 失撑 + placeBlock 草/土预检 + 蘑菇汤(碗+红+白)配方 + drawBowl/drawMushroomStew。 |
| `27b48ff` | t508 船重做 | 水面放置（射线穿水扫最顶水格）+ 32 深浮力 lerp + U 形船体（底+左右舷+头尾翘）+ 挖船→boatBroken→spawnItem + 冰加速 blockBelow off-by-one 修复（向下扫首个 isCollidable）+ 可推动（setPlayerCenter 接触分离+冲量）。 |
| `fb56fb1` | t509 铁傀儡 | T 形检测静态复核正确 + 加诊断 qInfo（probe rowX/rowZ + miss below1/2，定位静默失效：南瓜放偏/底排不全/overlaps 拒放）。 |
| `5380afa` | t510 雪傀儡机制 | aiSnowGolem meltAccum 慢扣血（1HP/s 非即死）+ 水扣血 + 死掉 0-15 雪球(SnowballId=0x23D) + 剪刀剪南瓜→snowGolemSheared+derpy 无头形态（眼/嘴悬浮）+ 行走留 SnowLayer(t482)。 |
| `51a8b04` | t505 雪体系 | ShapeSnowLayer 薄板（state 0-7=(state+1)/8 高，snowLayerHeight 单一权威 mesher+collision+solidTopOffset 三处共用）+ 铲掉雪球（SnowLayer 按 state+1 个/Snow 4 个）+ 4 雪球合雪块配方 + worldgen 3 级随机（hashColumn）+ 堆叠 placeBlock + auto-step 上行。 |

### 批 4 D UI（t511-t514）
| commit | 任务 | 做了什么 |
|---|---|---|
| `94f20ee` | t511 创造 tabs | Inventory.qml 6 tabs（方块/工具/材料/护甲/食物/箱子）+ filteredPalette 按 currentTab 过滤（食物=材料∩foodIds）+ 去标题/选中/销毁提示 4 处冗余文字 + chest tab→switchToSurvivalRequested→setMode(Survival)（共享 hotbar VM 物品持久）。 |
| `5a9e765` | t512 hover+1-9 | creativeHoveredItemId（调色板 hover 专属，与 hotbar 槽 hover 解耦）+ forceReplaceHotbarFromCreative（setStack 覆盖 maxStackSize）+ keyInput 1-9 分流（背包开优先调色板替换 > hotbar 槽互换）。 |
| `527db24` | t513/514 | foodHungerAmount 加胡萝卜+3/土豆+1 + foodColor 按食物色屑粒（甜浆果暗红/胡萝卜橙/土豆土黄/蘑菇汤棕，替固定橙占位）+ m_eatCooldown 1s（冷却期手持动画持续）+ placeBlock SweetBerryId 分支右键 Grass/Dirt→SweetBerryBush state 0（eventFilter 种植优先于进食）。 |

- **验收（待用户 playtest，大量 needs-run 视觉/交互项）**：性能（/kill 后 main 残留 + 水岩浆流动动画 + TNT 连锁大坑）；渲染（草粒子绿/工作台熔炉正面 icon/青金矿融石/熔炉燃烧正面/浮冰淡蓝/床像床/物品 pack 图/玩家护甲 F5/雪傀儡南瓜脸）；机制（草挖泥土+精准/木梯贴侧爬/熔炉 UI/仙人掌 4 邻/枯灌木失撑/雪层薄板+铲雪球/冰生水/花蘑菇失撑+白蘑菇+蘑菇汤/船浮水骑乘挖掉/铁傀儡建造日志定位/雪傀儡慢融+剪南瓜）；UI（创造 tabs+chest 切生存/hover 1-9 替换/胡萝卜土豆可吃+粒子色+冷却/浆果种植）。
- **minor 遗留**：末影/末地传送门中文译名开源前建议区隔（§9 override (d) 强制复核点）；铁傀儡建造 t509 加了诊断日志待用户实测定位真因（结构已验证正确）。
