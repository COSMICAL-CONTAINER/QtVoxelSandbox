# t827 — 附魔全效果逐项审计（12 项）

用户原话：「镀膜效果还是需要一项一项的检查」。本审计对全部 14 附魔 id（归并 12 个效果面 + 附魔书路径）
逐项给出「验证方式 + 预期 + 结果」，缺的补实现（本轮补：t824 池过滤 / t825 显示同源 / t826 击退强度；
燃焰链运行时探针）。机制基线 = MC Java 1.0.0。

## 审计矩阵

| # | 附魔（id） | 生效点（代码位置） | 验证方式 | 预期 | 结果 |
|---|---|---|---|---|---|
| 1 | 锐锋 Sharpness (1) | `PlayerController::attackMob` → `EnchantRegistry::weaponAttackDamage`（暴击前叠 base；+0.5×级） | 矩阵 t763①：钻石剑基础 7 + III = 8.5；t825：显示桥接同公式逐级枚举 0..5 | 实战伤害与 tooltip 攻击行同步 +0.5/级 | **已通**（t825 收口单一权威后结构化成立） |
| 2 | 亡灵杀手 UndeadSlay (2) | attackMob：mobType ∈ {Shambler, Bones} → +2.5×级 | 静态接线核对（attackMob 分支）+ t824 探针锁「仅剑/斧池可出」 | 对亡灵族每级 +2.5 HP；镐不出此附魔 | **已通**（实战分支 t476 起在；t824 前镐可附出 = 用户症状，t824 修） |
| 3 | 节肢克星 ArthropodSlay (3) | attackMob：mobType == Spider → +2.5×级 | 同上静态接线 | 对蜘蛛 +2.5/级 | **已通** |
| 4 | 击退 Knockback (4) | attackMob → `EntityManager::knockback(strength)`；strength = `EnchantRegistry::knockbackStrength`（1+3.0×级） | 矩阵 t826：真 EntityManager 三猪位移 0.128s II 3.24 > I 1.74 > 无 0.46 格严格递增 | I 明显推离 / II 更远（MC 1.0 量级） | **本轮修**——旧 1+0.5×级 令 II 仅 ~2.3 格总位移，与游荡抖动同量级 →「实战无效」感 |
| 5 | 燃焰 FireAspect (5) | attackMob → `EntityManager::ignite(4s×级)`；tick 火烧分支 1HP/s 扣血 + 致死 burned=true 掉熟肉 | 矩阵 t827 运行时探针：点燃即 isBurningAt、≥5/10 计量猪 1.76s 内实际扣血、1HP 猪 burned=true 死亡 | 打中即着火、持续掉血、烧死掉熟肉 | **已通**（全链运行时验证；此前仅静态接线确认） |
| 6 | 效率 Efficiency (6) | `ToolRegistry::miningTime(block, tool, effLevel)` 加法叠 level²+1；仅工具-方块匹配时生效 | 矩阵 t798 六档递减表 + 泥土/沙恒定对照 + 黑曜石采掘门槛 | 每级固定增幅、错配方块零加成 | **已通**（t798 修） |
| 7 | 耐久 Unbreaking (9) | 工具：`Hotbar::damageSelectedItem` 掷骰跳过（100/(级+1)% 免耗）；护甲：`damageArmor` 同式 | 矩阵 t763③：无附魔对照 50 次必损 50；耐久 III 400 次损耗 ∈ [260,340]（期望 300） | 按级概率跳过损耗 | **已通** |
| 8 | 保护 Protection (10) | `Hotbar::armorProtectionFactor(cause)` 通用 1 EPF/级 → Main.qml takeDamage 前 ratio 减伤（cap 0.85） | 矩阵 t763② EPF 断言 + Main.qml 两处消费点静态核对 | 通用减伤按级生效 | **已通**（EPF 路由 t615/t763 全接） |
| 9 | 火焰保护 FireProtection (11) / 弹射物保护 ProjectileProt (13) / 摔落保护 FeatherFall (12) | armorProtectionFactor 按 cause 专项路由 ×2 EPF/级（Fire=9/15/8/11；Projectile=6；Fall=1/12/16） | 矩阵 t763②：火焰保护 III → cause 9/15 均 6；靴摔落 II + 头盔保护 II → cause 1/16 = 6 | 专项伤害双倍减免；摔落仅靴、水上亲和仅头盔（部位精判） | **已通**（Emberling/EnderPearl 路由为 t763 补） |
| 10 | 时运 Fortune (8) | finishMiningAt：isFortuneOre（七种矿石）→ count ×(1+[0,级])；silk 互斥 silk 优先 | playercontroller.cpp:1420-1434 静态接线核对 + isFortuneOre 表 | 矿石掉落按级放大；石/圆石不放大 | **已通**（概率区间语义无法确定性断言，接线与表已核；如需可后续加统计探针） |
| 11 | 精准采集 SilkTouch (7) / 水上亲和 AquaAffinity (14) | Silk：finishMiningAt 掉 brokenId 自身（含冰→冰、沙砾→沙砾特例）；Aqua：updateMining kUnderwaterMiningTimeMul=5 惩罚免除（t763 补） | 静态接线核对（两处 calc point 均在） | 掉方块本体 / 水下挖掘速度正常 | **已通** |
| 12 | 附魔书路径（BookItem） | 附魔台附书：selectEnchantsForItem(BookId)=全池 → EnchantedBookId；铁砧书合并：enchantApplicableTo 逐条过滤 + conflictsWith 冲突拒；战利品：LootTable::enchantedBookEnchants | 矩阵 t822b SQLite round-trip（附魔书存取）；t824 书池=全 14 断言；t792 铁砧 47 探针 | 书全池随机、上铁砧按物品过滤、存档保真 | **已通** |

## 本轮结论

- **真缺的（本轮修）**：t824 池过滤（镐出亡灵杀手 = 大类 mask 门漏洞）、t825 显示-实战同源收口
  （九处 tooltip 公式副本 → 单一权威）、t826 击退强度（+50%/级 → +300%/级，MC 量级）。
- **重点疑四项均已在**：燃焰点燃（t827 运行时探针首次自动化覆盖）、耐久跳过（t763③ 统计探针）、
  保护减伤（t763② EPF 路由）、时运掉落加成（接线核对 + 七矿表）。
- 锄（Hoe）：MC 1.0 无适用附魔 → categoryForItem 判 None 不可附魔（附魔台拒入 / 铁砧书合并拒 /
  选择器空池三重门），t824 起。
- 弓 / 剪刀 / 钓竿专属附魔（力量 / 无限 / 海之眷顾等）：不在 1.0 附魔台池内，本工程未实现（既有口径不变）。

## 自动化覆盖（redstone_matrix_test）

- t824：全 seed 扫池逐物品断言（1200 抽/物）+ 桥接一致性 + enchantSelected 门。
- t825：权威公式精确值 + 显示桥接 round 一致性（含 .5 半上、缺项降级、0..5 逐级枚举）。
- t826：公式面 1.0/4.0/7.0 + 真 EntityManager 位移严格递增（短窗 0.128s，理论 0.46/1.83/3.19 格）。
- t827：燃焰链运行时探针（点燃即显 / 1HP/s 结算 / burned=true 死亡链）。

矩阵：**230 PASS / 0 FAIL**（基线 226 + 4）。
