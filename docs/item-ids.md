# 物品 ID 参考表（voxelsandbox）

> 本文件列出引擎内**所有**注册过的物品 id → 名称映射，供 `/give` 调试聊天命令（t314）与开发参考。
> 引擎对齐 **Minecraft Java 1.0.0 机制**（PLAN §4），但所有标识符 / 用户可见字符串均使用**通用词**，
> 零 MC 专有名词 / 资产（PLAN §9）。生物名采用项目重命名（Shambler / Bones / Stalker / Spider）。
> 右侧「MC 1.0.0」列仅作 sanity 对照（同机制等价的 MC Java 1.0.0 数字 id；「—」= 该物品在 MC 1.0.0 不存在，
> 「block」= MC 1.0.0 中以方块形式存在而非可拾取物品）。

---

## MC 1.0 对齐策略（t348：资源包前置 + 存档向后兼容）

**结论：不重排引擎 id，改以「映射层」对齐 MC 1.0。**

引擎 id（方块段 `0..30` / 工具段 `0x100..0x110` / 材料段 `0x200..0x21F`）是**存档权威**：`.sqlite` 的 chunk blob
按方块引擎 id 落盘、`player_state` JSON（背包 / hotbar）按物品引擎 id 落盘、配方表的 `pattern/outputId` 引用引擎
id。重排这些常量会让所有旧存档读出乱码（旧 `1`=grass 读成新 `1`=stone）且无救——故**不做 id renumber**。

对齐靠**映射表**：每个段在其所属注册表里持一份「引擎 id → MC 1.0 数字 id」单向查找表，未来资源包加载器据此把引擎
方块 / 物品翻译成 MC 1.0 `terrain.png` / `items.png` 的贴图槽（MC 1.0 按 block/item 数字 id 在图集中定位 tile）。
**单一权威**（PLAN §2）：本文件各表的「MC 1.0.0」列与代码映射函数须一致（改一处同步另一处）。

| 段 | 代码映射函数 | 文件 | 分层 |
|---|---|---|---|
| 方块段 | `BlockRegistry::mcBlockId(engineId)` | `src/Core/blockregistry.{h,cpp}` | Core |
| 工具段 | `ToolRegistry::mcToolId(engineToolId)` | `src/Game/toolregistry.{h,cpp}` | Game |
| 材料段 | `RecipeRegistry::mcMaterialId(engineMaterialId)` | `src/Game/recipe.{h,cpp}` | Game |

**无 MC 1.0 等价 → -1**：映射函数返回 -1（木板台阶 WoodSlab——1.0 仅石台阶 44；铜矿 / 铜锭——1.17+；铁 / 金原矿与
锭——1.0 直接掉矿石方块），资源包加载器见 -1 即回退引擎过程化贴图。**生物蛋**：MC 1.0 是单一 spawn egg id `383`
+ metadata 分 mob 变体，故引擎全部 `spawn_egg_*`（猪 / 牛 / 羊 / 蹒跚者 / 骸骨 / 潜行者 / 蜘蛛）→ `383`。

**存档向后兼容**：因未重排引擎 id，chunk blob / player_state JSON 的语义不变 → **无需 user_version 迁移**（schema
亦未变，`kSchemaVersion` 不动）。若将来确需 renumber（如批量改方方块 id），届时再 `kSchemaVersion` +1 + 加
load-time remap（读旧 id 经映射表换新 id）；本任务的映射层（`mcBlockId` 等）已为此 remap 预置「翻译」基础设施。

## id 段总览

| 段 | 范围 | 说明 |
|---|---|---|
| 方块段 | `0 .. Count-1`（`0 .. 39`） | 可放置的体素方块（含 air=0）。空气恒不可拾取 / 不可放置。 |
| 工具段 | `0x100 .. 0x10F`（`256 .. 271`） | 工具 / 武器。单件不可堆叠（`maxStack=1`），带耐久（`maxDurability`）。 |
| 材料段 | `0x200 .. 0x21F`（`512 .. 543`） | 非方块可堆叠物品（合成材料 / 食物 / 桶 / 树苗物品 / mob 掉落 / 生物蛋）。默认可堆叠 64；铁桶单件。 |

基址常量：`RecipeRegistry::MaterialIdBase = 0x200`，`ToolRegistry::ToolIdBase = 0x100`。
方块段上界：`BlockRegistry::Count = 40`。

## `/give` 用法

```
/give <id> [count] [durability]
```

- `id` — 任一段已注册 id（十进制整数；如 `3`=石头、`270`=铁剑、`530`=钻石）。
- `count` — 数量，缺省 `1`。工具段每槽单件（9 hotbar 槽 + 27 主栏槽，工具段填满后多出部分丢弃）。
- `durability` — 仅对工具段有意义（缺省 = 满耐久；非工具恒 0，写入被忽略）。

**这是 debug 命令，无视游戏模式**（创造 / 生存 / 观察者都可调；不属玩法经济，仅测试 / 调试用）。

聊天栏（默认 `T` 或 `Enter` 打开）输入示例：

```
/give 3 64          # 给予 64 个石头（方块段）
/give 270 1 100     # 给予 1 把铁剑，耐久 100（工具段，显式耐久）
/give 270           # 给予 1 把满耐久铁剑
/give 530           # 给予 1 个钻石（材料段）
/give 512 64        # 给予 64 根木棒
```

成功回显：`给予 <名> ×<数>`（如 `给予 铁剑 ×1`）。失败：`未知物品 id: <输入>` / `用法: /give <id> [count] [durability]` / 背包满时尾追 `（背包已满，弃 <数>）`。

---

## 1. 方块段（id `0 .. 39`）

| id (hex) | id (dec) | 内部 key | 中文名 | MC 1.0.0 |
|---|---|---|---|---|
| 0x00 | 0 | `air` | 空气（不可拾取） | 0 |
| 0x01 | 1 | `grass` | 草方块 | 2 |
| 0x02 | 2 | `dirt` | 泥土 | 3 |
| 0x03 | 3 | `stone` | 石头 | 1 |
| 0x04 | 4 | `cobble` | 圆石 | 4 |
| 0x05 | 5 | `log` | 橡木原木 | 17 |
| 0x06 | 6 | `planks` | 橡木木板 | 5 |
| 0x07 | 7 | `leaves` | 橡树树叶 | 18 |
| 0x08 | 8 | `sand` | 沙子 | 12 |
| 0x09 | 9 | `crafting_table` | 工作台 | 58 |
| 0x0A | 10 | `furnace` | 熔炉 | 61 |
| 0x0B | 11 | `coal_ore` | 煤矿石 | 16 |
| 0x0C | 12 | `iron_ore` | 铁矿石 | 15 |
| 0x0D | 13 | `torch` | 火把 | 50 |
| 0x0E | 14 | `bedrock` | 基岩（不可破坏） | 7 |
| 0x0F | 15 | `wood_slab` | 木板台阶 | —（1.0.0 仅石台阶 id 44） |
| 0x10 | 16 | `wood_stairs` | 木板楼梯 | 53 |
| 0x11 | 17 | `wood_fence` | 木栅栏 | 85 |
| 0x12 | 18 | `wood_pressure_plate` | 木板压力板 | 72 |
| 0x13 | 19 | `wood_door` | 木板门 | 64 / 物品 324 |
| 0x14 | 20 | `wood_trapdoor` | 木活板门 | 96 |
| 0x15 | 21 | `water` | 水（不可拾取，桶舀取） | 8 / 9 |
| 0x16 | 22 | `chest` | 箱子 | 54 |
| 0x17 | 23 | `farmland` | 耕地 | 60 |
| 0x18 | 24 | `tall_grass` | 草丛 | 31 |
| 0x19 | 25 | `wheat_crop` | 小麦作物 | 59 |
| 0x1A | 26 | `diamond_ore` | 钻石矿石 | 56 |
| 0x1B | 27 | `wool` | 羊毛（方块） | 35 |
| 0x1C | 28 | `sapling` | 橡树树苗（方块） | 6 |
| 0x1D | 29 | `copper_ore` | 铜矿石 | —（1.17+） |
| 0x1E | 30 | `gold_ore` | 金矿石 | 14 |
| 0x1F | 31 | `lava` | 岩浆（不可拾取，桶舀取） | 10 / 11 |
| 0x20 | 32 | `bed_red` | 红色床（配方产物） | 26 |
| 0x21 | 33 | `bed_orange` | 橙色床 | 26 |
| 0x22 | 34 | `bed_yellow` | 黄色床 | 26 |
| 0x23 | 35 | `bed_green` | 绿色床 | 26 |
| 0x24 | 36 | `bed_cyan` | 青色床 | 26 |
| 0x25 | 37 | `bed_blue` | 蓝色床 | 26 |
| 0x26 | 38 | `bed_magenta` | 品红色床 | 26 |
| 0x27 | 39 | `bed_black` | 黑色床 | 26 |

> 注：方块段 `0x28 (40) = Count` 是哨兵上界，不是合法 id。床方块（bed）8 色变体机制等价 MC 1.0 床（bed），
> 简化为单格整立方（spec t387）；MC 1.0 床颜色由 metadata 分（统一 id 26），本工程用独立 id 表达色变
> （物品系统 id 驱动，无 place-state）。配方 planks+wool → 红床（默认色）；其余色变体创造调色板取用。

---

## 2. 工具段（id `0x100 .. 0x10F`）

工具 / 武器：单件不可堆叠（`maxStack=1`），带耐久（每次生存挖掘 / 攻击 / 耕地 -1；归零破损消失；创造模式不消耗耐久）。

耐久值（同 tier 共享）：木 59 / 石 131 / 铁 250；弓 384。

| id (hex) | id (dec) | 内部 key | 中文名 | tier | 耐久 | MC 1.0.0 |
|---|---|---|---|---|---|---|
| 0x100 | 256 | `pickaxe_wood` | 木镐 | 1 | 59 | 270 |
| 0x101 | 257 | `pickaxe_stone` | 石镐 | 2 | 131 | 274 |
| 0x102 | 258 | `pickaxe_iron` | 铁镐 | 3 | 250 | 257 |
| 0x103 | 259 | `hoe_wood` | 木锄 | 1 | 59 | 290 |
| 0x104 | 260 | `hoe_stone` | 石锄 | 2 | 131 | 291 |
| 0x105 | 261 | `hoe_iron` | 铁锄 | 3 | 250 | 292 |
| 0x106 | 262 | `axe_wood` | 木斧 | 1 | 59 | 271 |
| 0x107 | 263 | `axe_stone` | 石斧 | 2 | 131 | 275 |
| 0x108 | 264 | `axe_iron` | 铁斧 | 3 | 250 | 258 |
| 0x109 | 265 | `shovel_wood` | 木铲 | 1 | 59 | 269 |
| 0x10A | 266 | `shovel_stone` | 石铲 | 2 | 131 | 273 |
| 0x10B | 267 | `shovel_iron` | 铁铲 | 3 | 250 | 256 |
| 0x10C | 268 | `sword_wood` | 木剑 | 1 | 59 | 272 |
| 0x10D | 269 | `sword_stone` | 石剑 | 2 | 131 | 276 |
| 0x10E | 270 | `sword_iron` | **铁剑** | 3 | 250 | 267 |
| 0x10F | 271 | `bow` | 弓 | 1 | 384 | 261 |

> 例：`/give 270 1 100` → 给予 1 把耐久 100 的铁剑。

---

## 3. 材料 / 物品段（id `0x200 .. 0x227`，非 mob 蛋 / 非 mob 掉落部分）

非方块可堆叠物品（默认 `maxStack=64`；铁桶 / 装水铁桶单件不可堆叠）。

| id (hex) | id (dec) | 内部 key | 中文名 | MC 1.0.0 |
|---|---|---|---|---|
| 0x200 | 512 | `stick` | 木棒 | 280 |
| 0x201 | 513 | `coal` | 煤炭 | 263 |
| 0x202 | 514 | `iron_ore_drop` | 铁原矿 | —（1.0.0 直接掉铁矿石方块；原矿物品 1.17+） |
| 0x203 | 515 | `iron_ingot` | 铁锭 | 265 |
| 0x204 | 516 | `glass` | 玻璃 | 20（block） |
| 0x205 | 517 | `charcoal` | 木炭 | 263（metadata 1） |
| 0x206 | 518 | `bucket_empty` | 铁桶（空） | 325 |
| 0x207 | 519 | `water_bucket` | 装水铁桶 | 326 |
| 0x208 | 520 | `seed` | 小麦种子 | 295 |
| 0x209 | 521 | `wheat` | 小麦 | 296 |
| 0x20A | 522 | `bread` | 面包 | 297 |
| 0x212 | 530 | `diamond` | 钻石 | 264 |
| 0x21A | 538 | `arrow` | 箭 | 262 |
| 0x21B | 539 | `sapling_item` | 橡树树苗（物品） | 6（block） |
| 0x21C | 540 | `copper_ore_drop` | 铜原矿 | —（1.17+） |
| 0x21D | 541 | `copper_ingot` | 铜锭 | —（1.17+） |
| 0x21E | 542 | `gold_ore_drop` | 金原矿 | —（1.0.0 直接掉金矿石方块；原矿物品 1.17+） |
| 0x21F | 543 | `gold_ingot` | 金锭 | 266 |
| 0x220 | 544 | `lava_bucket` | 装岩浆铁桶 | 327 |
| 0x221 | 545 | `cooked_porkchop` | 熟猪排 | 320 |
| 0x222 | 546 | `cooked_beef` | 熟牛肉 | 364 |
| 0x223 | 547 | `cooked_mutton` | 熟羊肉 | —（1.8+） |
| 0x224 | 548 | `redstone` | 红石粉 | 331 |
| 0x225 | 549 | `saddle` | 马鞍 | 329 |
| 0x226 | 550 | `name_tag` | 命名牌 | —（1.6+） |
| 0x227 | 551 | `enchanted_book` | 附魔书占位 | —（1.4+；占位无真附魔） |

> t393：`redstone` / `saddle` / `name_tag` / `enchanted_book` 为战利品表（loot table）专用物品 —— 地牢箱首开
> 填充 + 预留钓鱼（t401）共用 `LootTable`（src/Game/loottable.*）。生存非合成获得（仅地牢战利品 / 创造调色板）。

> 注：羊毛在两个段都有 —— 方块 `0x1B (27)` = 可放置的羊毛方块；材料 `0x20E (526)` = 杀羊掉落的羊毛物品（见下方「mob 掉落」）。橡树树苗同理：方块 `0x1C (28)` 可放置生长，物品 `0x21B (539)` 是种植原料（破叶 / 破树苗方块掉此物品）。

---

## 4. 生物蛋段（id `0x20F .. 0x216`）

创造模式物品，右键地面 → 生成对应 mob。可堆叠 64。

> §9 重命名对照（项目内名 → MC 1.0.0 等价机制）：Shambler / Bones / Stalker / Spider 为项目原创名，对齐 MC 1.0.0 四种敌对 mob 的**机制**；被动 mob（猪 / 牛 / 羊）沿用通用词。本表右列只标 MC 1.0.0 spawn egg 统一 id，不引用 MC mob 名。

| id (hex) | id (dec) | 内部 key | 中文名 | MC 1.0.0 |
|---|---|---|---|---|
| 0x20F | 527 | `spawn_egg_pig` | 生物蛋（猪） | 383（mob 元数据变体） |
| 0x210 | 528 | `spawn_egg_cow` | 生物蛋（牛） | 383（mob 元数据变体） |
| 0x211 | 529 | `spawn_egg_sheep` | 生物蛋（羊） | 383（mob 元数据变体） |
| 0x213 | 531 | `spawn_egg_shambler` | 生物蛋（蹒跚者） | 383（mob 元数据变体） |
| 0x214 | 532 | `spawn_egg_bones` | 生物蛋（骸骨） | 383（mob 元数据变体） |
| 0x215 | 533 | `spawn_egg_stalker` | 生物蛋（潜行者） | 383（mob 元数据变体） |
| 0x216 | 534 | `spawn_egg_spider` | 生物蛋（蜘蛛） | 383（mob 元数据变体） |

> 注：id `0x212 (530)` 是钻石（材料段，见上节），夹在生物蛋段间 —— 材料段是连续空间，生物蛋只是其中一段子集而非独立段。

---

## 5. mob 死亡掉落段（id `0x20B..0x20E` + `0x217..0x219`）

杀 mob 掉落（可堆叠 64；非方块 → 右键不放置）。被动 mob（猪 / 牛 / 羊）与敌对 mob（骸骨 / 蹒跚者 / 蜘蛛）掉落物。

| id (hex) | id (dec) | 内部 key | 中文名 | 来源 mob | MC 1.0.0 |
|---|---|---|---|---|---|
| 0x20B | 523 | `raw_porkchop` | 生猪排 | 猪 | 319 |
| 0x20C | 524 | `raw_beef` | 生牛肉 | 牛 | 363 |
| 0x20D | 525 | `leather` | 皮革 | 牛 | 334 |
| 0x20E | 526 | `wool` | 羊毛（物品） | 羊 | 35（block） |
| 0x217 | 535 | `bone` | 骨头 | 骸骨 | 352 |
| 0x218 | 536 | `rotten_flesh` | 腐肉 | 蹒跚者 | 367 |
| 0x219 | 537 | `string` | 线 | 蜘蛛 | 287 |

---

## 备注

- **铁剑 id 示例**：`0x10E (270)`。`/give 270` → 给予 1 把满耐久（250）铁剑；`/give 270 1 100` → 给予 1 把耐久 100 的铁剑。
- **方块 id 范围**：`0 .. 39`（`Count=40`，哨兵上界不可用）。
- **材料 id 基址**：`MaterialIdBase = 0x200 (512)`；当前上界 `0x21F (543)`。
- **工具 id 基址**：`ToolIdBase = 0x100 (256)`；当前上界 `0x10F (271)`。
- **铁桶 / 装水铁桶**：归材料段（`isMaterial=true`，MaterialIcon 自绘桶图标），但 `maxStack=1`（同工具段语义，单件功能性物品）。
- **不可堆叠的工具段物品 + `count > 1`**：addStack 把每件放进单独空槽（hotbar 9 + 主栏 27 = 最多 36 件），多出部分丢弃并在聊天回显尾追 `（背包已满，弃 <数>）`。
