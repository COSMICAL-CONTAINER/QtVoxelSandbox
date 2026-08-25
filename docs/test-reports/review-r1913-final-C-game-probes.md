# R19.13 终审 C 组：Game 层 + 矩阵探针审查报告

> 审查窗口：`08ffc71..HEAD(6f78e7f)`，负责提交：5f9a40c（t828-t833 Game 侧）/ f28ff44（t835）/ 15a1000（t856）/ 7437d5a（杂项组 Game 侧：t847 + 红石粉图标源 + 画作钩子迁移收尾）/ 65636e4（t823 探针）/ 0a1acb5（t836）+ 全窗口 `tools/redstone_matrix_test.cpp` 探针质量专项。
> 审查方式：无 bash 环境，逐文件 Read HEAD 全文 + 定向 Grep 交叉核对（含 dev-plan ✅✅ 记录、BlockRegistry/World/EntityManager/Hotbar/Smelting 对端）。本报告只审不改。

---

## 总体判定：通过（0 高危 / 2 中危 / 5 低危）

无 PLAN §2 不变量违反（A RHI 囚笼 / 分层铁律 / C 世界锁 / D 单一输入路径均过）、无 IP 泄漏、探针计数声称与实际结构一致。中危两项：t847 草丛失撑链不对称（机制缺口 + dev-plan 记录失实）、P-t836 描述超断言（出界/寿命消散与 updateFishing 镜像路径零覆盖）。

---

## 中危 findings

### 中-1 t847 草丛放置预检与失撑掉落链不对称（四族中恰缺新增的那族）

> **已修（R19.13 收尾批 fix(review-r1913-final)）**：新增 Core 谓词 `BlockRegistry::isGroundPlant`（草丛/花/蘑菇族成员单一权威），放置预检（playercontroller）与失撑钩子（World::checkFlowerMushroomOnEdit，四 setBlock 钩子入口全覆盖）两面共用——加族必两面齐动，杜绝再漂移。t847 探针补失撑列（破草丛下泥土 → 清 Air + dropId 掉落，运行期读表）。附带同族口径（支撑被置换不掉）按指示**登记不扩**：花/蘑菇自 t507 起同只对 id==Air 生效，保持同族一致优先，world.cpp 钩子头注释已记。

- **提交**：7437d5a
- **位置**：
  - 预检侧 `src/Game/playercontroller.cpp:4409-4417`（TallGrass/DeadBush/花/蘑菇并入 plantGroundBlock 统一预检）
  - 谓词 `src/Core/blockregistry.cpp:1201-1212`（草丛→泥土/草）
  - 失撑侧 `src/World/world.cpp:2371-2393`（checkFlowerMushroomOnEdit 只扫 `isFlower(above) || isMushroom(above)`）、`world.cpp:2344`（checkDeadBushOnEdit 只扫 DeadBush）、`world.cpp:480-494` 钩子清单（Cactus/DeadBush/Flower/Mushroom/PressurePlate/Sugarcane/SnowLayer/Gravity/Rail/TrapdoorDoor/Painting——**无 TallGrass**）
- **问题**：t847 把草丛从"完全无预检"收紧为泥土/草限定，但 World 失撑族没有任何钩子清 TallGrass。挖掉草丛（worldgen 生成或创造放置）下方的泥土/草方块 → 草丛悬空永存。四族统一进 plantGroundBlock 的家族里，恰好只有本任务新增的 TallGrass 没有失撑半边；MC 1.0 草丛失支撑即消失。dev-plan ✅✅ 记录"地面集与失撑掉落链（checkFlowerMushroomOnEdit / dropUnsupportedCropsAround …）互为表里不漂移"**言过其实**（对花/蘑菇/枯灌木成立，对草丛不成立）。
- **附带同族缺口**（次级）：失撑钩子全部只对 `id == Air` 的编辑生效——支撑被**置换**成非合法着地面时不掉（如锄把蘑菇下方泥土变耕地：plantGroundBlock(Mushroom, Farmland)=false 但无触发）。花/蘑菇/枯灌木共有的既有行为，t847 未引入也未修。
- **触发场景**：挖草丛下方泥土 → 草丛悬浮；锄蘑菇下方泥土 → 蘑菇站耕地上。
- **建议**：World 增 checkTallGrassOnEdit，或更优——把 checkFlowerMushroomOnEdit 复检谓词改为 `plantGroundBlock(above, 新下方格 id)` 单一权威（一并覆盖"支撑被置换"与全部四族 + 草丛）；探针 t847 补失撑列；dev-plan t847 记录改口。

### 中-2 P-t836 一条探针锁五个行为面：描述超断言 + updateFishing 镜像路径零执行

> **已修（R19.13 收尾批 fix(review-r1913-final)）**：补 (f) 出界消散**真断言**（XZ 飞越 8 tick 内出界 + y<0 首 tick 槽释放，与 180s 寿命路径区分）；补 (g) 两半边——行为半边（探针可达）：clearAll 清浮标后 ① 镜像惰性（fishing 态不塌）② 收竿走 valid=false 干净收场（无获物/无耐久/fishing 复位）；自动收竿半边（pc.tick 驱动 updateFishing）因 tickImpl 的 captured 门在无窗测试二进制**不可达**（直调 pc.tick 先走 !m_captured 早退分支的 cancelFishing，会掩盖镜像路径本体）→ 按预案改**源序钉**（t836(e) 手法：滤注释锁 updateFishing 函数体内 aliveAt/kindAt 双查 + m_fishing=false + emit fishingChanged 语句面），取舍在探针注释与 PASS 文案声明。另补 d2 垂死 mob 收竿不扣耐久断言（随 B-L2）。PASS 文案与断言已对齐（"out-of-bounds despawn ASSERTED"）。

- **提交**：0a1acb5
- **位置**：`tools/redstone_matrix_test.cpp:9968-10320`；PASS 文案 `:10308-10319` 明言 "out-of-bounds despawn"，但 (a)-(e) 无任何对应断言。
- **已锁住的五面**（确认足够扎实的部分）：(a) 抛物/落水浮定位（含 bit 级精确 settle）/陆上冻结；(b) 确定性等待公式两端恰可达 + 600 序号分布带 + **行为级 ±1 tick 按预计算等待值咬钩**（公式即驱动的真锁）；(c) 窗内收=获物+耐久-1+载荷四元组（池内 id/浮标位/朝玩家点积>0.9/弹速镜像）/窗过=escaped+重等+空收零消耗；(d) 真甩竿钩猪→拉拽位移朝玩家+耐久-5+血量不变+零 fishCaught；(e) 熟鱼 kSmelt+kSmeltXp 双表+食用值+名+源码钉。断言强度总体好（相对基线、时间语义、无恒真）。
- **差在哪（逐面列出）**：
  1. **浮标出界 / 180s 寿命消散**：文案声称、零断言（Review24 #9/#10 刚修"断言强度"病，此处在文案层复发——描述 > 断言）。
  2. **updateFishing 状态机从未执行**：探针只直调 `useFishingRod()` + `ents.tick()`，从不跑 `pc.tick()`/`updateFishing`——实体失效自动收竿（aliveAt/kindAt 双查→fishing 翻 false）、换槽 cancelFishing、bobberPosition/hasBite 每 tick 镜像（值变才 emit）全部未驱动。
  3. 钓竿耐久归零清槽（-5 跨零）与 Unbreaking 交互未测（探针用素竿）。
  4. fishCaught → QML spawnItemAt 路由（呈现层，矩阵范围外，可豁免）。
  5. 0.5s 咬钩窗断言带宽 0.6s（>0.5 即过）——窗长若漂到 0.55 仍过，仅靠镜像常量补偿（可接受，列出备查）。
- **修复建议**：补 (f) 小节——spawnBobber 后 removeEntityAt 模拟消散 + 直调等价 updateFishing 驱动（或暴露最小 tick 路径），断言 `fishing()` 翻 false 零 fishCaught；换槽（setSelectedSlot 持非钓竿）断言 cancelFishing 收浮标；文案与断言对齐（删 "out-of-bounds despawn" 或补出界 spawn 断言）。

---

## 低危 findings

### 低-1 t823 tripwire 单向灵敏：逮得住权威侧漂移，逮不住 QML 副本单侧漂移

- **提交**：65636e4；`tools/redstone_matrix_test.cpp:3896-3912`。
- 冻结镜像 vs World 权威五组态对比（净空 0/0、贴身环带 0/0、单层满环 16/15 有意分歧钉死、堵两角 -2、双层叠 2/2）设计合理，能逮 `countBookshelvesAround` 改规则不同步；但 EnchantGlyphFlow.qml / EnchantRunes.qml 自身被改坏时探针照 PASS（镜像代表 QML 但不读 QML）。已文档化此局限。t836(e) 已示范源码钉手法（读源文件滤注释查语句）——同法可钉两 QML 副本的 `Math.trunc`/±2 环带表达式与 95/94 字面量，建议下批补齐。清场好公民（clearRing 复原）已做到。

### 低-2 t835 疾跑采样门（m_moveState == Sprint）无行为级锁

- **提交**：f28ff44；`src/Game/playercontroller.cpp:3241`。
- 探针 (e) 只锁物理落距比与镜像常量（P18 已文档化"Game 层本地 constexpr 探针不可达"）；掷出分支的 Sprint 门本身零覆盖——门条件写反/枚举改值只能靠人工目视⑤兜底。建议至少加源码钉（滤注释查 `m_moveState == Sprint ? kPearlSprintFactor` 语句）。采样时序本身正确（掷出瞬间直读 live 态，右键事件内读当帧 step 已更新的 m_moveState）。

### 低-3 spawnPrimedTnt 尾日志读 moved-from 对象

- `src/Entities/entitymanager.cpp:4492-4493`：`acquireSlot(std::move(e))` 之后 `qCInfo` 读 `e.fuse`。Entity 为 POD-ish 结构实践无害、仅日志；建议 fuse 先存局部再 move（顺带项，该日志行早于本窗口，t856 只动了函数前半）。

### 低-4 t836(e) 源码钉强度弱

- `tools/redstone_matrix_test.cpp:9956/9966`：`t2.contains("int(RecipeRegistry::CookedFishId)")` 匹配任意出现（hotbar.cpp 恰有两处且都有意义，但钉不住"创造 tab 排生鱼旁"的邻接关系——该声称仅人工目视）。建议钉相邻行（RawFishId 行后紧随 CookedFishId）。

### 低-5 t847 ✅✅ 记录 "250 PASS（基线 242+8）" 口径易误读

- `docs/dev-plan.md:3015`：+8 是杂项组批合计（t815/t838 探针 1 + t837×6 + t847×1），t847 本身 +1。数字自洽（242→+8=250→t823+1=251→t836+1=252 全链对上），但单条记录读起来像 t837/t847 各自 +8。建议补一句构成说明。

---

## 通过项（简列）

1. **§2-A RHI 囚笼**：src/Game 零 QRhi/QShader 命中（唯一命中为注释提及 GPU 计时设想）；无 Renderer/UI/ViewModels 上行 include；playercontroller.cpp include 全部向下（Game/World/Core/Qt）。
2. **t835 applyEnderPearlTeleport 碰撞盒口径**（playercontroller.cpp:1949-1996）：无碰撞盒格（轨/火把/草丛）可立入（探针钉 y=84 立格内）、薄盒（压力板）立其顶（y=85）、水格立入走游泳链、全列无立位 abort 有 qInfo 白耗语义、y 越上界安全（越界空盒=开放）、lx/lz 钳制、骑乘先下坐骑、m_vel/m_knockback 清零 + m_peakY 重置防误摔伤、伤害仅 Survival 恰一次 (5, EnderPearlTp)。扫描终止性正确（yy 每迭代递减；窄缝续扫不重入）。极端情形按题目核验：落点全列无碰撞盒（草丛顶等）→ 沿列下扫到首个碰撞支撑立其上一格，草丛格可站（探针 (a) 轨/火把柱即此退化的等价构造）。
3. **t856 TNT 分支**（playercontroller.cpp:5365-5378）：dropper 排除双保险（首分支结构性排除 + 显式 `!isDropper` 条件自文档）；标准引信（fuseSec=-1 → kPrimedTntFuseSec 5s，无新发明引信）；定向初速走 spawnPrimedTnt 新可选尾参（默认 0，机关/电力/链式既有路径零变，entitymanager.cpp:4485 有零值跳过守卫）；消耗走共享尾段恒扣（3→2）+ dispenserFired 埋点、无挥手（机关无手，同箭/雪球/剑分支先例）、冷却 2s 由 fireDispenserAt 复用、与 firePowerTnt 原地引爆的两路径边界注释即契约。发射面朝向恒水平四向（chestFrontFace 解码）→ `x+int(dir.x())` 邻格定位精确；朝墙发射时 PrimedTnt 嵌墙 spawn 后照常倒计引爆（halfW=0 无碰撞，可接受）。
4. **t847 谓词面**：plantGroundBlock 真值表 17 断言含域守卫（非植物恒 false、ground=Water 拒）；水下拒绝取"目标格==Air"（含水格拒）与甘蔗 t547③ 同口径（非邻水口径，正确——贴水放置应放行）；拒绝=不挥不耗同仙人掌/铁轨先例；花/蘑菇/枯灌木三族集合与旧内联判定一致（无放宽/收紧，仅统一）。
5. **t836 Game 侧收竿三态**（playercontroller.cpp:2411-2476）：分派完整且优先级正确（hooked > bite > 空收；快照三查询后再 removeEntityAt；valid 四重守卫 idx/count/alive/kind 防槽复用误绑）；valid=false（消散）→ 清态零结算；甩竿失败（kCap 槽满）不进钓鱼态；m_fishCastSerial 确定性错峰（PLAN §2-K 合规——hashVoxel 非 RNG）。**出界/寿命镜像检测每 tick 成本 O(1)**：updateFishing 全部为索引读（selectedItemId/count/aliveAt/kindAt/posAt/bobberHasBiteAt），无扫描；值变才 emit。cancelFishing 收口三处（respawn:407 / loadSaved:459 / 失焦暂停:530,857）。
6. **damageSelectedItem(times)**（hotbar.cpp:1574-1578）：缺省 times=1 旧行为逐字不变，既有 19 处调用点零改动（grep 全核）；times 包装逐点独立 Unbreaking 掷骰 + 破损清槽后空槽自然 no-op 不溢出。
7. **CookedFishId=0x25B 全链**：染料段 0x24B..0x25A 之上首个空闲号，无冲突（recipe.h 全段核对）；kSmelt+kSmeltXp 双表都接（smelting.cpp:48/:105，t788 教训吸收）；食用 +4 走 foodHungerAmount 单一权威（playercontroller.cpp:92，已升 public 供探针直调）；豹猫 gate RawFishId 不受影响——0x25B > DyeBlackId(0x25A) 亦不误入染羊分支；hotbar 调色板（:577 排 RawFishId 后）+ 名"熟鱼"（:1066）+ QML MaterialIcon case 0x25B + pack 映射（compat 键，itemFilenameMap 既有模式）。
8. **t828-t833 Game 侧**（5f9a40c）：染料染羊分支（段门 DyeIdBase..DyeBlackId、woolIdx=delta 换算、dyeSheep 越界/非羊拒则不消耗、挥手+CD 齐全；Entities 侧 dyeSheep 有 0..15 越界守卫 + 长回重掷清 sheepWoolDyed 单一权威 rollNaturalSheepWool）；空手坐站补狼（:3442-3451，与猫分支对称，fall-through 语义一致）；受伤驯服宠物 healTamedPet(4) 优先于繁殖（狼 :3364 / 豹猫 :3408 两处对称）；全部与骨头/肉/生鱼/剪刀分支先例模式一致（Air 守卫前分流、独立 findMobHit、消耗/挥手/m_lastPlaceMs 链）。
9. **画作钩子迁移收尾**（7437d5a Game 侧）：dropUnsupportedPaintingsAround 已删净，finishMiningAt 直调 World::removePaintingAt 单一权威，无陈旧引用残留。
10. **IP 门（§9）**：本窗口 Game 层源码标识符/用户可见字符串无 MC 专有名词（用户可见全中文原创名；Shambler/Bones/Stalker/Nightwalker/Emberling 均 §9 改名）；"cooked_cod.png"/"cooked_fish" 为 pack 映射 / 冶炼配方 compat 键（既有 itemFilenameMap / smelt-key 模式，非用户可见资产）。EnderPearl/Blaze 词根为既有遗留（R19.12 已登记"MC 词根改名批"开放项），本窗口未新增词根类。注释中"机制等价 MC 1.0 …"为合法设计参照。
11. **探针计数交叉核对**（审查点 6）：f28ff44 +1（P-t835 单 PASS 位）、15a1000 +1（P-t856）、7437d5a +8（t815/t838 1 + t837 6 + t847 1，t837 六个 PASS 位 :9817-9892 逐一对上）、65636e4 +1、0a1acb5 +1 → 242→250→251→252 与各 ✅✅ 声称一致。静态 PASS 位 125 个 + 组合循环运行期展开（如 :231 源×接收器组合表在循环内打印）= 运行时总数，结构自洽（未执行二进制，逐位运行时数不可静态复核，已注明）。
12. **探针质量总评（Review24 #9/#10 防复发）**：本窗口新增探针无恒真断言、相对基线齐备（t835 落距带 vs 旧物理、sinkW>sinkL 比较、dmg 恰 N 次；t856 冷却内"信号确发的零发射"差分）、时间语义到位（±1 tick 行为级、引信满值 vs 链式 0.24 的判别式、fuse 递减复采）；唯二弱点即中-2（描述超断言）与低-1/低-2（单向灵敏/镜像常量无行为锁），已列整改。

---

## 复审建议（优先级序）

1. 中-1：World 侧补 TallGrass 失撑（连带把失撑谓词统一到 plantGroundBlock 覆盖"支撑置换"族）+ t847 探针补列 + dev-plan 记录改口。
2. 中-2：P-t836 补 (f) 消散/换槽/镜像驱动小节，PASS 文案与断言对齐。
3. 低-1/低-2：t823 QML 副本源码钉、t835 Sprint 门源码钉（顺手，同一次探针文件提交）。

---

## R19.13 收尾批处置记录（fix(review-r1913-final)）

- **中-1 已修**：`isGroundPlant` 单一权威两面共用 + t847 探针失撑列（详见 finding 处标记）；"支撑置换"附带口径登记不扩（同族一致优先）。dev-plan t847 记录未改动——修复后"地面集与失撑链互为表里"陈述已成立（草丛对称缺口即本次闭合项）。
- **中-2 已修**：(f) 真断言 + (g) 行为半边 + 源序钉取舍声明（详见 finding 处标记）。
- **低-1 至低-5 登记不修**（C 组 5 低危）：t823 QML 副本源码钉、t835 Sprint 门源码钉、spawnPrimedTnt 尾日志 moved-from、t836(e) 源码钉邻接强化、t847 ✅✅ 记录口径注——均探针强度/文档口径类，无行为缺陷，留探针增强批。
- 验证：全量重编零警告（dxcompiler.dll 提示豁免）；矩阵 252 PASS / 0 FAIL（t836/t847 原地加强，无新 PASS 位）；exe 冒烟 14s 存活零 QML 错误。
