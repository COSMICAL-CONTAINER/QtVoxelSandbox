import QtQuick
import QtQuick3D
// t458 滚动条（ScrollBar）来自 QtQuick.Controls。纯 QML 模块不经 C++ 链接（同 Inventory.qml t127）——
//   CMakeLists 的 windeployqt POST_BUILD 已用 `--qmldir src/ui` 扫 import 自动部署匹配插件，无部署缺口。
import QtQuick.Controls
// t41：迁入 src/ui/ 子目录后须显式 import 自身模块以解析下方 C++ 类型（BlockCube 几何 / Hotbar VM）。
import VoxelSandbox

// qml-touch 三轮：本文件「触碰 packActive」的绑定改表达式形式（触碰值参与返回值），防 qmlcachegen AOT
//   把裸语句触碰 `packActive;` 当死代码消除 → pack 切换后图标源不刷新（机制/返回值不变）。

// t458 资源查看器 / 方块浏览器（JEI 式 3D 预览面板）。
//
// 入口：设置面板（ESC 菜单 → 设置）顶部「资源查看器」按钮触发（host Main.qml resourceBrowserOpen）。
// 用户诉求：「找不到入口」浏览所有可用方块 / 物品的样貌，尤其 pack 开启时看实际贴图效果。
//
// 布局（JEI 式）：
//   左：可滚动全物品网格（创造调色板全集 = 方块段 + 工具段 + 材料段 + 护甲段；复用 Hotbar VM 的
//       creativeBlocks/Tools/Materials/Armor，单一权威，UI 不另持副本）；
//   右：选中物预览：
//       - 整立方方块段 → 内嵌 View3D 旋转 BlockCube（复用既有几何 + 共享图集；lighting:NoLighting，
//         渲染可见性铁律见 lessons-learned「渲染盲区静态化」）；
//       - 工具段 / 材料段 / 护甲段 / cross / partial / 火把 → 大图标（复用 iconSourceForBlock /
//         ToolIcon / MaterialIcon，与背包槽同渲染路由，§9a 自绘原创）。
//   底：选中物中文名 + id + 类别标签（§9 override (b) 通用词）。
//
// 复用既有渲染：方块预览的 BlockCube 几何与掉落实体 / 手持立方同一条已验证可见路径
// （BlockCube + voxelAtlas + PrincipledMaterial.NoLighting）。图集 source 由 host 注入（resourcePack.atlasSource），
// pack 切换即时刷新（file:// ↔ qrc:/）；网格图标走 hotbar.iconSourceForBlock（t745 双态路由：pack 开 = 运行期
//   pack 图集渲染 / 2D pack 立绘；pack 关 = 程序原生手绘或程序图集重渲）。
//
// 分层（PLAN §2）：本组件属 UI 呈现层，只读 Hotbar VM（ViewModel 读 BlockRegistry / ToolRegistry），
// 不反向写栅格 / 槽位；3D 几何属 Renderer，向下依赖合规。零 MC 专名 / 资产（§9）。
//
// 指针态：由设置面板触发（已 !captured，光标可见可点格）；关闭（closed 信号 / Esc）→ 回设置面板（仍 !captured），
// 不做 grab/release（设置面板接管指针态）。背景遮罩仅吸收点击（§9 lessons「全屏遮罩 onClicked 会误关」→ 无 close 语义）。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（提供 creativeBlocks/Tools/Materials/Armor + iconSourceForBlock /
    //   nameForBlock + isTool/isMaterial/isPartialBlock/isCrossBlock 路由谓词 + toolTier/toolType）。
    property Hotbar hotbar
    // 图集贴图源 URL（host 传 resourcePack.atlasSource：active → file:/// 合成图集；否则 qrc 默认）。
    // 预览 View3D 的 BlockCube 材质 baseColorMap 绑此 → pack 切换即时刷新（同主场景 voxelAtlas）。
    property string atlasSource
    // 资源包是否启用（host 传 resourcePack.active）。网格图标 source 绑定触碰它 → pack 切换图标刷新。
    property bool packActive
    // 宿主注入：ResourcePackManager 整实例。生物图鉴 / 生物蛋预览需调 Q_INVOKABLE mobTextureSource(mobType)
    //   （非 Q_PROPERTY，不能经 atlasSource/packActive 字符串传递），故整实例注入；读 .active → pack 切换即时刷新。
    property var resourcePack

    // 请求宿主关闭（回设置面板）。
    signal closed()

    // 调色板全集（方块 + 工具 + 材料 + 护甲；不加创造背包那种尾部空槽占位 —— 浏览器只列实物）。
    // root.hotbar 由 null→对象 时重新求值（host 注入时机）。
    readonly property var paletteModel: root.hotbar
        ? root.hotbar.creativeBlocks().concat(root.hotbar.creativeTools())
                                .concat(root.hotbar.creativeMaterials())
                                .concat(root.hotbar.creativeArmor())
        : []

    // ── 生物图鉴（feat）：左「生物」段列 mob，选中 → 右侧 View3D 旋转显示 MobModel 3D 模型（替代大图标平图）；
    //   选中「生物蛋」材料（0x20F..0x216/0x22C/0x22E + t785 补全 0x246/0x247/0x249/0x24A）同样直接显示对应 mob 模型。机制等价 MC 1.0 mob 形态，
    //   名称 §9 区隔（Shambler↔zombie / Bones↔skeleton / Stalker↔creeper）。雪傀儡/铁傀儡条目 I3 追加——
    //   本任务已接入：加 mobType 12/13 两行 + mobPreviewCentY + mobFallbackColor 分支（mobPreviewScale 12/13→0.75
    //   既存）。pack 命中 snow_golem.png / iron_golem.png → View3D 显带 pack 纹理的雪块身 / 铁块身 MobModel；
    //   pack 关 → 纯色雪白 / 铁灰（mobFallbackColor）。t598：雪/铁傀儡南瓜头补独立 BlockCube Model（同 Main.qml
    //   t582 游戏内方案；此前预览漏头 = 雪傀儡「无头」）。眼 overlay 不在图鉴预览（聚焦 pack 贴图）。
    readonly property var mobModel: [
        { mobType: 1, name: "猪" }, { mobType: 2, name: "牛" }, { mobType: 3, name: "羊" },
        { mobType: 4, name: "蹒跚者" }, { mobType: 5, name: "骷髅弓箭手" }, { mobType: 6, name: "潜行者" },
        { mobType: 7, name: "蜘蛛" }, { mobType: 8, name: "鸡" }, { mobType: 9, name: "鱿鱼" },
        { mobType: 10, name: "狼" }, { mobType: 11, name: "豹猫" },
        { mobType: 12, name: "雪傀儡" }, { mobType: 13, name: "铁傀儡" }, { mobType: 14, name: "蠹虫" },
        { mobType: 16, name: "夜行者" }, // t727 末影人→夜行者（§9 改名；生物图鉴条目 + 生物蛋 0x246 映射）
        { mobType: 17, name: "燃烬者" }, // t728 烈焰人→燃烬者（§9 改名；生物图鉴条目 + 生物蛋 0x247 映射）
        { mobType: 19, name: "小蹒跚者" } // t952 幼体僵尸（§9 区隔命名；生物图鉴条目 + 生物蛋 0x25D 映射；生成时概率组成小鸡骑士）
        // t751 条目合并：t663 ⑥ 曾把剪毛变体拆成独立条目（羊（剪毛后）/雪傀儡（剪头后）——同 mobType 双条
        //   仅靠名字区分）。现改为**每生物单条**+预览区下沿悬浮变体面板（见 previewArea 内 variantPanel，
        //   t783 ② 迁入）：羊 = 剪毛/未剪 toggle + 毛色 swatch（仅羊有颜色变体）、雪傀儡 = 戴头/剪头 toggle。
        //   变体态存组件级属性（sheepSheared / snowGolemSheared / sheepWoolIndex），切换即时刷新预览。
    ]
    // 生物段选中（mobType；-1 = 未选）。与物品选中互斥（点物品格清空、点生物格不改 selectedId）。
    //   t751：剪毛变体条目已合并（每生物单条），selectedMobName 仅作显示名伴选（不再承担 sheared 判据——
    //   变体态改由下方组件级属性承载，预览区悬浮变体面板切换）。
    property int selectedMobFromSection: -1
    property string selectedMobName: ""
    // ── t751 变体状态（组件级；预览区悬浮变体面板读写，预览绑定消费 → 切换即时刷新）──
    // 羊剪毛态（false=毛茸羊毛形态 / true=裸肤残毛形态；镜像游戏内 shearedAt 双态）。
    property bool sheepSheared: false
    // 雪傀儡剪头态（false=戴南瓜头 / true=纯雪头 + 刻面五官；镜像游戏内 snowGolemShearedAt 双态）。
    property bool snowGolemSheared: false
    // 羊毛色索引（0=白色不着色；1..15=染色 tint）。调色板与游戏内羊毛方块 16 色**同源**
    //   （tools/build_wool.py WOOL_COLORS，白色=默认羊毛色 → 预览零 tint）。t789 起游戏内羊实体已有毛色
    //   字段（EntityManager sheepWool / 自然权重 kSheepNaturalWeights：白主导 + 粉/灰/浅灰/棕/黑少数；
    //   毛层 tint 走 sheepWoolTintForIndex 同值色板 + 剪/杀掉对应色羊毛）→ 本预览着色 = 游戏内观感
    //   的所见即所得（三处色板镜像：build_wool.py / EntityManager kSheepWoolTints / 此处；矩阵测试
    //   t789 探针钉死同值契约）。
    property int sheepWoolIndex: 0
    readonly property var woolPalette: [
        { name: "白色", tint: "#ffffff" }, // 白=默认毛色，不着色（乘白恒等）
        { name: "橙色", tint: "#de781e" }, { name: "品红", tint: "#b94ba5" },
        { name: "淡蓝", tint: "#4696d2" }, { name: "黄色", tint: "#d2b428" },
        { name: "柠绿", tint: "#5faf2d" }, { name: "粉红", tint: "#e191af" },
        { name: "灰色", tint: "#464650" }, { name: "淡灰", tint: "#9b9ba0" },
        { name: "青色", tint: "#418791" }, { name: "紫色", tint: "#823ca5" },
        { name: "蓝色", tint: "#3746a5" }, { name: "棕色", tint: "#734b2d" },
        { name: "绿色", tint: "#468237" }, { name: "红色", tint: "#962828" },
        { name: "黑色", tint: "#1e1e26" }
    ]
    // t920 狼（10）/豹猫（11）驯服态预览（变体面板两段切换）：第一段 驯服/未驯服（镜像游戏内 wolfTamed /
    //   ocelotTamed）；第二段仅驯服后可切 站立/坐下（镜像 wolfSitting / ocelotSitting——野狼/野豹猫不可命令坐，
    //   机制等价 MC 1.0；坐姿几何走 MobModel sitPose（t878② 已就绪），狼驯服态视觉 = 红项圈 overlay（t831），
    //   豹猫驯服态 = 家猫贴图（mob_cat_*，游戏内 3 变体随机，图鉴取变体 0 棕虎斑代表——t963 家猫
    //   花纹返修：旧全黑档退役，用户口径「驯服=家猫花纹，非全黑」）。蛋路径不设变体（同羊）。
    property bool mobTamedPreview: false
    property bool mobSitPreview: false
    // 生物段选中是否狼/豹猫（t920 驯服态面板门控；蛋路径 selectedMobFromSection<0 恒 false）。
    readonly property bool selectedMobTameable: root.selectedMobFromSection === 10
        || root.selectedMobFromSection === 11
    // 预览当前驯服激活态（面板第一段「已驯服」且选中狼/豹猫）。
    readonly property bool mobTamedActive: root.selectedMobTameable && root.mobTamedPreview
    // 生物蛋材料 id → mobType（t785 起与 RecipeRegistry::mobTypeForSpawnEgg 单一权威表同源镜像——Core 层
    //   QML 不能引 Game 头，字面量 + 注释互指；矩阵测试 t785 探针对 C++ 权威表全蛋断言防漂移）。
    //   pig=1/cow=2/sheep=3/shambler=4/bones=5/stalker=6/spider=7/chicken=8/squid=9/wolf=10/ocelot=11/
    //   nightwalker=16/emberling=17。非蛋 id → -1（无映射）。
    function mobTypeForEgg(id) {
        switch (id) {
            case 0x20F: return 1; case 0x210: return 2; case 0x211: return 3;
            case 0x213: return 4; case 0x214: return 5; case 0x215: return 6;
            case 0x216: return 7; case 0x22C: return 8; case 0x22E: return 9;
            case 0x246: return 16; // t727 夜行者生物蛋（SpawnEggNightwalkerId；与 PlayerController placeBlock 同源）
            case 0x247: return 17; // t728 燃烬者生物蛋（SpawnEggEmberlingId；与 PlayerController placeBlock 同源）
            case 0x249: return 10; // t785 狼生物蛋（SpawnEggWolfId；右键 → 生成野生狼）
            case 0x24A: return 11; // t785 豹猫生物蛋（SpawnEggOcelotId；右键 → 生成野生豹猫）
            case 0x25D: return 19; // t952 小蹒跚者生物蛋（SpawnEggBabyShamblerId；右键 → 生成幼体僵尸，生成时掷小鸡骑士组合骰）
        }
        return -1
    }
    // pack 关时程序生成贴图（build_mob.py 产物，§9a 原创；与 Main.qml mobHost delegate 同源）。返回空串 →
    //   纯色回退（bones/stalker/spider 无程序贴图 → baseColorMap:null + 纯色 baseColor，同 Main.qml 潜行者模式）。
    function mobFallbackTexture(t) {
        switch (t) {
            case 1: return "qrc:/textures/mob_pig.png"
            case 2: return "qrc:/textures/mob_cow.png"
            case 3: return "qrc:/textures/mob_sheep.png"
            case 4: return "qrc:/textures/mob_shambler.png"
            case 19: return "qrc:/textures/mob_baby_shambler.png" // t952 小蹒跚者程序生成亮黄绿幼体贴图（§9 原创；无 pack 分流同源）
            case 8: return "qrc:/textures/mob_chicken.png"
            case 9: return "qrc:/textures/mob_squid.png"
            case 10: return "qrc:/textures/mob_wolf.png"
            case 11: return "qrc:/textures/mob_ocelot.png"
            case 14: return "qrc:/textures/mob_silverfish.png"
            case 16: return "qrc:/textures/mob_nightwalker.png" // t727 夜行者程序生成暗紫黑影贴图（§9 原创）
            case 17: return "qrc:/textures/entity_emberling.png" // t728 燃烬者黄焰头贴图（与 Main.qml mobHost head 同源；build_entities_pack.py）
        }
        return ""
    }
    function mobFallbackColor(t) {
        switch (t) {
            case 5: return "#d8d8d0"  // Bones 骨白（与 Main.qml 蛋生成色同源）
            case 6: return "#3a5a3a"  // Stalker 暗绿
            case 7: return "#2a1a1a"  // Spider 暗黑
            case 12: return "#f0f4f8" // SnowGolem 雪白（与 Main.qml 雪块身纯色同源）
            case 13: return "#7d848c" // IronGolem 铁灰（与 Main.qml 铁块身纯色同源）
            case 16: return "#2a1f2a" // Nightwalker 暗紫黑（与 Main.qml 蛋生成色 / 程序贴图同源；t727）
            case 17: return "#e8b030" // Emberling 燃焰黄（与 Main.qml 蛋生成色同源；t728）
        }
        return "#ffffff"
    }
    // 预览模型缩放：多数 mob 1.0；傀儡（几何高 1.2-1.4，撑满 3.2 镜头）0.75；蠹虫（0.44×0.30 小虫）1.6 放大可辨。
    function mobPreviewScale(t) {
        if (t === 12 || t === 13) return 0.75
        if (t === 14) return 1.6
        if (t === 16) return 0.55 // t781 夜行者细肢人形高 2.70（[-1.40,1.30]）→ 缩到镜头内全身可见
        if (t === 17) return 1.1 // t782/t818 燃烬者头+4棒全模型 1.10 高 ×1.14 宽（[-0.58,0.52]/半径 0.57；t818 头 0.88³→0.7³ + 轨道 0.62→0.52）→ 1.1 撑满可辨
        if (t === 19) return 1.5 // t952 小蹒跚者全模型 ~0.97 高（[-0.45,0.515]）幼体小 → 放大可辨（同蠹虫放大口径）
        if (t === 6) return 0.85 // t894 潜行者游戏内视觉体格 0.85 同源（图鉴所见即游戏内比例）
        return 1.0
    }
    // 预览模型垂直居中微调：几何局部原点 = 碰撞中心，mob 身体偏向 -Y → 上提让主体在镜头居中。
    function mobPreviewCentY(t) {
        switch (t) {
            case 1: return 0.16   // 猪 [-0.58, 0.27]
            case 2: return 0.10   // 牛 [-0.55, 0.37]
            case 3: return 0.06   // 羊 [-0.44, 0.33]
            case 4: return 0.06   // 蹒跚者 [-0.90, 0.79]
            case 19: return -0.03 // t952 小蹒跚者 [-0.45, 0.515]（体心 -0.03 → 下压居中主体）
            case 5: return 0.08   // 骸骨 [-0.90, 0.75]
            case 6: return 0.05   // 潜行者 [-0.77, 0.69]（t894 0.85 视觉缩放后跨度；缩放前 [-0.90, 0.81]——
                                  //   中心偏移差 <0.05 格视觉不可辨，0.05 沿用 t616 值；review27 #24 注释勘误）
            case 7: return 0.08   // 蜘蛛 [-0.30, 0.13]
            case 8: return 0.01   // 鸡 [-0.40, 0.38]
            case 9: return 0.07   // 鱿鱼 [-0.46, 0.32]（t778 删尖顶后 pack 开关两态统一；全跨度中心 -0.07 → 上提居中）
            case 10: return 0.05  // 狼 [-0.42, 0.37]
            case 11: return 0.05  // 豹猫 [-0.40, 0.33]
            case 12: return 0.0   // 雪傀儡 [-0.90, 0.90]（对称居中，无需上提）
            case 13: return 0.30 // 铁傀儡 [-1.20, 0.58]（偏 -Y，上提 0.30 居中主体）
            case 14: return -0.30 // 蠹虫 [-0.15, 0.14]（t750 分节重做：背脊甲板顶 0.14；小虫贴地 → 下压 0.30 进镜头中心）
            case 16: return 0.06 // t781 夜行者 [-1.40, 1.30]（偏 -Y 0.05；×0.55 缩后上提居中主体）
            case 17: return 0.02 // t782 头+棒跨 [-0.58, 0.54]（体心 -0.02 → 上提居中；棒对称近居中微调）
        }
        return 0
    }

    // 选中 mobType 单一权威：生物段选中优先；否则选中物是生物蛋 → 蛋 → mobType 映射。
    readonly property int selectedMobType: root.selectedMobFromSection >= 0 ? root.selectedMobFromSection
        : (root.hotbar ? root.mobTypeForEgg(root.selectedId) : -1)
    // 是否「生物预览」态（生物段 / 生物蛋选中 → View3D 显 MobModel 3D 模型，替代大图标）。
    readonly property bool selectedIsMob: root.selectedMobType >= 0
    // pack entity 贴图源（active 且映射命中 → file:///...；否则空串 → 程序生成 / 纯色回退）。
    readonly property string selectedMobPackSrc: root.selectedMobType >= 0 && root.resourcePack && root.resourcePack.active
        ? root.resourcePack.mobTextureSource(root.selectedMobType) : ""
    // 最终贴图源：pack 命中 → pack；否则程序生成 mob_*.png（无 → 空串走纯色）。
    //   t920 例外：已驯服豹猫 → 恒程序家猫贴图 mob_cat_tabby（变体 0 棕虎斑代表，t963 花纹返修；
    //   demo 包无驯服猫 PNG，Main.qml
    //   游戏内同款「驯服猫恒程序贴图」口径——pack 豹猫贴图只覆盖野生形态）。触碰驯服态属性即时刷新。
    readonly property string selectedMobTexSource: root.selectedMobFromSection === 11 && root.mobTamedPreview
        ? "qrc:/textures/mob_cat_tabby.png"
        : (root.selectedMobPackSrc !== "" ? root.selectedMobPackSrc
                                          : root.mobFallbackTexture(root.selectedMobType))
    // 选中 mob 显示名：生物段选中 → selectedMobName + 变体后缀（t751：剪毛/剪头/毛色态随预览区悬浮
    //   变体面板切换刷新）；否则按 mobType 反查 mobModel 表（生物蛋路径，t751 合并后每型单条恒得常规形态名）。
    readonly property string selectedMobDisplay: (root.selectedMobFromSection >= 0 && root.selectedMobName !== ""
        ? root.selectedMobName : mobNameForType(root.selectedMobType)) + root.mobVariantSuffix
    // mobType → mobModel 表名（t751 条目合并后每型单条，直接命中）。
    function mobNameForType(t) {
        for (let i = 0; i < root.mobModel.length; ++i)
            if (root.mobModel[i].mobType === t)
                return root.mobModel[i].name
        return ""
    }
    // t751 变体态派生（预览各渲染分支单一判据；变体面板切换 → 本属性 NOTIFY → 头/贴图/毛色绑定即时刷新）。
    //   生物蛋路径（selectedMobFromSection<0）恒 false = 常规形态（蛋只孵常规形态，机制对齐游戏内）。
    readonly property bool selectedMobSheared:
        root.selectedMobFromSection === 3 ? root.sheepSheared
        : (root.selectedMobFromSection === 12 ? root.snowGolemSheared : false)
    // t751 变体面板当前两段 toggle 的激活段（0=常规 / 1=剪后；羊与雪傀儡共用 toggle 控件，本属性供激活态高亮）。
    readonly property bool mobVariantSheared: root.selectedMobFromSection === 3 ? root.sheepSheared
        : root.snowGolemSheared
    // t751 变体后缀（选中名行标注当前变体；毛色后缀仅毛茸态显示——裸肤不染色，着色不生效时不标注）。
    readonly property string mobVariantSuffix: {
        if (root.selectedMobFromSection === 3) {
            if (root.sheepSheared) return "（剪毛后）"
            return root.sheepWoolIndex > 0 ? " · " + root.woolPalette[root.sheepWoolIndex].name + "羊毛" : ""
        }
        if (root.selectedMobFromSection === 12) return root.snowGolemSheared ? "（剪头后）" : ""
        // t920 狼/豹猫驯服态后缀（未驯服无后缀 = 常规形态名）。
        if (root.selectedMobTameable)
            return root.mobTamedPreview ? (root.mobSitPreview ? "（已驯服 · 坐下）" : "（已驯服）") : ""
        return ""
    }
    // t749 剪毛羊 pack 本体层源（pack 启用且包内有 sheep/sheep.png → file:///；否则空串走程序贴图）。
    //   与 mobTextureSource(3) 的毛层合成路径分开——剪毛态要裸身 + 真脸（本体层），不要毛身。触碰 packActive。
    readonly property string sheepBodyPackSrc: {
        const _r = root.packActive
        return _r >= 0 && root.resourcePack ? root.resourcePack.entitySource("sheep_body") : ""
    }
    // t777 ② pack 真脸判据（羊眼 overlay 显隐的例外）：毛茸态合成贴图生效（resourcePack.sheepWoolFaceActive
    //   单一权威，镜像 Main.qml 游戏内修法——毛身 + 本体层头区真脸在身）或剪毛态本体层命中
    //   （sheepBodyPackSrc）→ 贴图自带真脸，overlay 眼隐（对齐牛等「pack 自带脸则隐眼」语义）；否则
    //   （pack 关 / 毛层 miss / 合成失败回退毛层无脸）眼恒显（程序贴图无脸，眼是唯一脸）。依赖
    //   selectedMobPackSrc / sheepBodyPackSrc / selectedMobSheared（任一变 → 重算；invokable 无 NOTIFY，
    //   刷新由这些带 NOTIFY 的上游属性驱动）。
    readonly property bool sheepPreviewPackFace: root.selectedMobSheared
        ? root.sheepBodyPackSrc !== ""
        : (root.selectedMobPackSrc !== "" && root.resourcePack
           && root.resourcePack.sheepWoolFaceActive)
    // t876 羊头分离 subset 判据（t816 脸罩退役）：毛茸态羊预览 MobModel 开 sheepSkinHead → 几何输出
    //   subset 0（躯干+腿毛层）+ subset 1（头盒），materials[1] 换绑本体层头区纹理源（pack 开 →
    //   mobPrevTex 合成贴图头区真脸 / pack 关 → mobSheepHeadTex 程序羊头贴图）且**不吃毛色 tint**
    //   （机制等价 MC 羊头 = skin 层恒自然色，毛色 tint 只落躯干毛层）。剪毛态 / 非羊不分离（单材质
    //   整模，零回归）。
    readonly property bool sheepSkinHeadActive:
        root.selectedMobFromSection === 3 && !root.sheepSheared
    // t880 「有 3D 模型的物品」3D 预览判据（家族 = Main.qml isItem3DFamily 掉落物家族 ∪ **楼梯三族
    //   16/59/110**——查看器楼梯**上 3D**、掉落物按 MC 平贴语义保留 billboard，两处差集即楼梯三族；
    //   review27 #19①：旧版只并了木楼梯 16，圆石楼梯 59 / 石砖楼梯 110 仍 2D 大图标——同为 ShapeStairs
    //   几何，一并补进（掉落物侧楼梯三族都不进 isItem3DFamily，两侧家族表差集 = {16,59,110}））：
    //   活板门（20/136）/ 火把（13）/ 台阶四族（15/87/58/109）/ 楼梯三族（16/59/110）/ 雪层（44）/
    //   草丛（24）/ 附魔台（94，带书）→ ItemShapeGeometry 真实 3D 形状旋转预览（替代大图标平面图）。
    //   t925 二批扩面（凡右键可放置的方块都应有 3D 模型）：枯灌木（43）/ 小麦作物（25，成熟态瓦片）/
    //   栅栏族（木 17 / 圆石墙 60 / 云杉 88，柱 + 双档满连形态）/ 门族（木 19 / 铁 135 / 云杉 89，薄板 +
    //   同族基材薄边）/ 白蘑菇（115）/ 红蘑菇（48）/ 蜘蛛网（102）/ 红石火把（129）——cross 族走
    //   ItemShapeGeometry 交叉双面片；拉杆（112）/ 木·石按钮（113/114）——mechBoxes 单一几何源。
    //   字面量 = BlockRegistry id（QML 不 import C++ 枚举；两侧家族表注释互指——加族员须同步两处；
    //   掉落物排除清单沿用 t880：铁轨平贴族 / 楼梯三族 billboard）。
    //   t965 两处修订：① 门族「铁 71」系**错 id**（71 = WoolCyan 青色羊毛——旧版把青羊毛暗路由进
    //   ItemShapeGeometry 满格兜底〔观感凑巧同 BlockCube 掩盖〕、铁门 135 反而拿不到 3D 门预览/掉落
    //   薄板形态）→ 订正 135；② 动力铁轨（127）**查看器侧**新入 3D（贴地薄板 quad，形态按钮组
    //   未激活/激活亮金轨贴图差需真 3D 预览承载）——掉落物侧排除清单不变（Main.qml isItem3DFamily
    //   不收 127，轨道掉落仍 billboard；两侧家族表差集 = {16,59,110,127}）。
    readonly property bool selectedIsItem3D: root.selectedId === 13 || root.selectedId === 20
        || root.selectedId === 136 || root.selectedId === 15 || root.selectedId === 87
        || root.selectedId === 58 || root.selectedId === 109 || root.selectedId === 16
        || root.selectedId === 59 || root.selectedId === 110
        || root.selectedId === 44 || root.selectedId === 24 || root.selectedId === 94
        || root.selectedId === 43 || root.selectedId === 25 // t925：枯灌木 / 小麦
        || root.selectedId === 17 || root.selectedId === 60 || root.selectedId === 88 // 栅栏族
        || root.selectedId === 19 || root.selectedId === 135 || root.selectedId === 89 // 门族（t965：铁门订正 135，原误 71=青色羊毛）
        || root.selectedId === 115 || root.selectedId === 48 // 白 / 红蘑菇
        || root.selectedId === 102 || root.selectedId === 129 // 蛛网 / 红石火把
        || root.selectedId === 112 || root.selectedId === 113 || root.selectedId === 114 // 拉杆 / 按钮
        || root.selectedId === 127 // t965：动力铁轨（查看器限定 3D；形态态变贴地薄板预览）
    readonly property string selectedMobCategory: {
        if (root.selectedMobFromSection >= 0) return "生物 / mobType " + root.selectedMobFromSection
        const t = root.hotbar ? root.mobTypeForEgg(root.selectedId) : -1
        if (t >= 0) return "生物蛋 / 0x" + root.selectedId.toString(16).toUpperCase()
        return ""
    }

    // 当前选中物 id（默认首个；Component.onCompleted 兜底）。
    property int selectedId: 0
    // ── t965 形态按钮组（预览区下沿悬浮面板 formPanel）──
    // 支持表单一权威 = Hotbar::blockFormStates（Game 层；按钮 index → state 值表，空表=不支持）。
    // 本侧零状态字面量：只消费表 + 维护选中钮 + 把 state 写进预览几何（BlockCube / ItemShapeGeometry
    // 的 blockState 属性 → BlockRegistry::stateTileOverride 态变瓦片/开合几何——Core 单一权威）。
    readonly property var selectedFormStates: root.hotbar && root.selectedId > 0
        ? root.hotbar.blockFormStates(root.selectedId) : []
    // 形态按钮组当前选中钮（0 = 表首 = 最普通/放置缺省形态——耕地干 / 门活板门合 / 草丛矮 /
    //   红石火把亮 / 动力轨未激活 / 末地框无眼 / 作物初始阶段，用户「默认最普通形态」口径）。
    property int selectedFormIndex: 0
    // 换选物品即回默认形态（防上一件的钮位串味到新一件——state 表随 id 换，钮位语义不跨 id）。
    onSelectedIdChanged: root.selectedFormIndex = 0
    // 预览驱动 state（越界/非支持防御回 0 = 缺省形态）。
    readonly property int selectedFormState: root.selectedFormIndex > 0
        && root.selectedFormIndex < root.selectedFormStates.length
        ? root.selectedFormStates[root.selectedFormIndex] : 0
    // t617 悬浮窗（同创造背包 t94 tooltip 模式）：hover 格写 hoveredName + hoveredTipPos（格顶中心，panel
    //   坐标系），离开按名守卫清除（防相邻格进出竞态互清）。tooltip 名 + 简述（类别 / mobType）。
    //   t633 ① 修「hover 名字空白」：①两 HoverHandler 补 hotbar 空守卫（hotbar 注入前 hover → 旧版
    //   root.hotbar.nameForBlock 抛 TypeError 被信号处理器吞 → hoveredName 恒空 → tooltip 永不出现 = 用户
    //   观感「名字全空白」的根因路径）；②简述行独立派生属性（旧版 text 只依赖 hoveredName —— 名不变而
    //   hoveredId 变（同名物品格 ↔ 生物格）时简述不刷新）。
    property string hoveredName: ""
    property point hoveredTipPos: Qt.point(0, 0)
    // hover 物 id（类别简述反查用；mob 格 = -1 哨兵 → 类别「生物」）。与 hoveredName 同步写；离开同步清。
    property int hoveredId: -1
    // 类别简述（物品 → hoveredCategory 谓词；mob → 生物）。依赖 hoveredId（换格即重算，名相同也刷新）。
    //   t663 拆分「材料 / 护甲」混串：isMaterial 是渲染路由谓词（含护甲段），类别标签须先判 isArmor 再判
    //   isMaterial —— 护甲显示「护甲」、纯材料显示「材料」（此前玻璃/种子/床全标「材料 / 护甲」根因）。
    readonly property string hoveredSuffixText: {
        // t677/t663 hover 名去后缀（用户点名「煤矿石」而非「煤矿石·方块」）：物品 / mob 段均恒空。
        return ""
    }
    // 名字是否 mob 段（hoveredId=-1 时按名反查 mobModel 表）。
    function isMobName(name) {
        for (let i = 0; i < root.mobModel.length; ++i)
            if (root.mobModel[i].name === name) return true
        return false
    }
    // tooltip 简述行：t677/t663 hover 名**不带类别后缀**（用户点名「煤矿石」而非「煤矿石·方块」）→ 恒空串
    //   （保留函数防 hoveredSuffixText 绑定链断裂）。
    function hoveredCategory(id) {
        return ""
    }

    // 旋转角度（预览方块绕 Y 自转；仅 selectedIsCube 时跑）。t617：拖拽写 0..360 取模；自转动画 to=from+360
    //   可越 360（eulerRotation 角度语义等价）—— 统一不改写（动画运行期 DragHandler 不会同时写）。
    property real spinAngle: 0
    // t599 鼠标拖拽旋转态：dragging = DragHandler 活动中（暂停自转）；userPitch = 拖拽累计俯仰角偏移
    //   （叠加在 -22° 基倾上，Y 拖上/下看顶/底——t877 恢复 t599 原方向；**用户确认方向，勿再改**：
    //   t820 曾按「推球面」直觉取反符号，用户两轮实测均判反 → 本符号为用户定稿）。松手 resume
    //   动画把 spinAngle lerp 回自转相位（无跳变）。yaw 由 spinAngle 本身承载（拖拽水平位移直接写入
    //   spinAngle，自转从松手角度继续）。
    property bool previewDragging: false
    property real userPitch: 0
    // t922 预览滚轮缩放（3D 预览态）：>1 放大 / <1 缩小，钳 [0.5, 3.0]（防贴脸穿 clipNear / 缩成芝麻）；
    //   由 cubeView 相机距离承载（z = 3.2 / zoom，全 3D 分支共享——方块/床/异形/生物同缩放）。大图标态不缩放
    //   （非 3D 预览无镜头概念）。重置按钮（预览区右下角）回 1.0。
    property real previewZoom: 1.0

    // t599 松手回自转说明：yaw 由 NumberAnimation on spinAngle 重启从当前值续跑（无跳变）；pitch 归零走
    //   resumePitchAnim（见预览区 DragHandler 处）。

    // 选中物是否「整立方方块」（走 View3D 旋转预览）。路由谓词与 Main.qml 掉落实体 / 手持立方同源：
    //   排除 火把(13) / 异形段(isPartialBlock) / cross 段(isCrossBlock) / 工具段(isTool) / 材料·护甲段(isMaterial)。
    //   这些非整立方走大图标分支（iconSourceForBlock / ToolIcon / MaterialIcon）。
    readonly property bool selectedIsCube: root.hotbar && root.selectedId !== 0 && root.selectedId !== 13
        && !root.hotbar.isPartialBlock(root.selectedId)
        && !root.hotbar.isCrossBlock(root.selectedId)
        && !root.hotbar.isTool(root.selectedId)
        && !root.hotbar.isMaterial(root.selectedId)

    // t784 选中物是否「床」（isBed 单一权威谓词，覆盖既存 8 色 0x20..0x27 + 补齐 8 色 0x4E..0x55 两段）。
    //   床在世界内是双格横置低 3D 异形（ShapeBed）——isPartialBlock/isCrossBlock/isMaterial 均否 → 旧版被
    //   selectedIsCube 路由当整立方渲成「满格 BlockCube 六面同贴被面瓦片」的旋转立方（= 任务「仍是老模型」
    //   根因）。本属性把床从整立方分支摘出，改走下方 BedModelGeometry 低 3D 床分支（几何与游戏内
    //   partialblockgeometry 床 case 同源 bedHalfBoxes；16 色变体联动：调色板床条目各持独立 id →
    //   blockId 绑 selectedId，选色即换被面瓦片，t751 变体联动同族——床色无共用模型控件故不设 variantPanel）。
    readonly property bool selectedIsBed: root.hotbar && root.hotbar.isBed(root.selectedId)

    // 选中物类别标签（§9 通用词；§2 分层：谓词经 Hotbar VM）。t663 拆分「材料 / 护甲」混串：isMaterial 是
    //   渲染路由谓词（含护甲段 0x300..），类别标签须先判 isArmor → 护甲显「护甲」、纯材料显「材料」
    //   （此前玻璃 / 小麦种子 / 床全被 isMaterial 吞进「材料 / 护甲」混标，用户点名拆开）。
    readonly property string selectedCategory: {
        if (!root.hotbar || root.selectedId === 0) return ""
        if (root.hotbar.isTool(root.selectedId)) return "工具"
        if (root.hotbar.isArmor(root.selectedId)) return "护甲"
        if (root.hotbar.isMaterial(root.selectedId)) return "材料"
        if (root.hotbar.isPartialBlock(root.selectedId)) return "不完整方块"
        if (root.hotbar.isCrossBlock(root.selectedId)) return "植物 / cross"
        if (root.selectedId === 13) return "光源"
        return "方块"
    }

    Component.onCompleted: {
        // 默认选首个调色板项（grass）；空集兜底 0（预览区空白安全）。
        if (root.selectedId === 0 && root.paletteModel.length > 0)
            root.selectedId = root.paletteModel[0]
    }

    // 预览自转动画：8s 一圈，仅在面板可见且选中整立方 / 生物、且未在拖拽时跑（省 GPU；t599 拖拽时暂停）。
    // t617 修「松手跳变」：旧 `NumberAnimation on spinAngle { from: 0 }` 重启时 from 恒 0 → 拖拽把 spinAngle
    //   拖到任意角度后松手，running 翻 true 动画从 0 起播 = 视角瞬间跳回 0 再转（跳变根因）。改独立
    //   NumberAnimation（target/property 显式，不再 `on spinAngle`）：start 前 from 钉当前 spinAngle、to =
    //   from+360（同向续转无回绕跳变）；onPreviewDraggingChanged 显式 start/stop（t492 教训：running 绑定在
    //   状态切换瞬间不可靠，显式 start 最稳）。pitch 归零仍走 resumePitchAnim（400ms OutCubic，无跳变）。
    NumberAnimation {
        id: spinAnim
        target: root; property: "spinAngle"
        from: 0; to: 360; duration: 8000; loops: Animation.Infinite
    }
    // 自转启停统一入口：不拖拽 + 面板可见 + 3D 预览态 → start（from 钉当前 spinAngle，无跳变）；否则 stop。
    //   供 previewDragging / visible / selectedIsCube / selectedIsMob 四个变化源共用（提为具名函数，勿把
    //   signal handler 当函数调 —— onXxxChanged 带函数体后不可再被外部调用，运行期 TypeError）。
    function restartSpinIfIdle() {
        if (!previewDragging && visible && (selectedIsCube || selectedIsMob || selectedIsItem3D)) {
            spinAnim.from = spinAngle          // 锚当前拖拽角度（无跳变核心）
            spinAnim.to = spinAngle + 360      // 同向续转（值域可 >360，eulerRotation 角度语义等价）
            spinAnim.start()
        } else {
            spinAnim.stop()
        }
    }
    onPreviewDraggingChanged: restartSpinIfIdle()
    // 面板可见 / 选中物变化（cube↔mob↔icon 三态切换停启动画）也走同一「from 锚当前」入口。
    onVisibleChanged: restartSpinIfIdle()
    onSelectedIsCubeChanged: restartSpinIfIdle()
    onSelectedIsMobChanged: restartSpinIfIdle()
    // t599 松手后 pitch 平滑归零（回标准 -22° 3/4 视角；yaw 已由自转从当前角度续转承接）。
    NumberAnimation {
        id: resumePitchAnim
        target: root; property: "userPitch"; to: 0
        duration: 400; easing.type: Easing.OutCubic
    }

    // 生物预览贴图（MobModel baseColorMap）：source 随选中 mob 切换（pack → pack entity 贴图；否则程序生成
    //   mob_*.png / 空）。空 source（bones/stalker/spider pack 关）→ baseColorMap:null + 纯色（同 Main.qml 潜行者模式）。
    Texture {
        id: mobPrevTex
        source: root.selectedMobTexSource
        generateMipmaps: false
    }
    // t749 剪毛羊预览贴图（双态）：pack 命中本体层 sheep/sheep.png（裸身 + 真脸 box-UV 布局）；否则程序生成
    //   mob_sheep_sheared.png（裸肤 + 残羊毛块全脸 UV，build_mob.py t749 新增）。替代旧纯色 #d6b890——
    //   用户「被剪羊毛的羊贴图纯色错误，应有裸皮 + 残毛造型」。
    Texture {
        id: mobShearedTex
        source: root.sheepBodyPackSrc !== "" ? root.sheepBodyPackSrc
                                             : "qrc:/textures/mob_sheep_sheared.png"
        generateMipmaps: false
    }
    // t876 羊头贴图（自然羊头色：裸肤脸 + 头顶羊毛帽 + 吻部暗带；build_mob.py 程序生成，镜像 Main.qml
    //   mobSheepHeadTex）：毛茸态羊预览 sheepSkinHead subset 1（头盒）的 pack 关态纹理源，不吃毛色 tint。
    Texture {
        id: mobSheepHeadTex
        source: "qrc:/textures/mob_sheep_head.png"
        generateMipmaps: false
    }
    // t880 附魔台 3D 预览悬浮书贴图（两态，镜像 Main.qml enchantBookTex/enchantBookPackTex）：pack 命中
    //   entity/enchant_book → 包书（布局 1，宽 >64 实测分区）；否则 qrc 程序书（布局 0 左右对半）。
    Texture {
        id: enchantBookTexBrowser
        source: "qrc:/textures/entity_enchant_book.png"
        generateMipmaps: false
    }
    Texture {
        id: enchantBookPackTexBrowser
        source: root.resourcePack && root.resourcePack.active
            ? root.resourcePack.entitySource("enchant_book") : ""
        generateMipmaps: false
    }
    // t750 夜行者眼睛发光层两态贴图（镜像 Main.qml mobNightwalkerEyesTex / nightwalkerEyesPackTex）：
    //   pack 命中 enderman_eyes → 包内竖眼层；否则程序生成 mob_nightwalker_eyes（透明底 + 紫白竖眼，
    //   Mask 裁透明底只显竖眼）。图鉴预览头前眼层 Model 用（修复④「黑影无五官」）。
    Texture {
        id: mobNwEyesTex
        source: "qrc:/textures/mob_nightwalker_eyes.png"
        generateMipmaps: false
    }
    Texture {
        id: nwEyesPackTex
        source: root.resourcePack && root.resourcePack.active
            ? root.resourcePack.entitySource("nightwalker_eyes") : ""
        generateMipmaps: false
    }

    // ── 尺寸常量 ──
    // t591：paletteCols 9 → 8 —— 物品网格 8×42+7×4=364 ≤ 左区视口 398 − 滚动条 6（当时 cellSize=42；9 列 410
    //   > 398，右列被 clip 截掉 + 滚动条再遮 = 用户「物品被遮挡」根因）。8 列与上方生物段同宽（对齐），
    //   滚动条 6px 落在网格右侧空隙（"右 padding 补滚动条宽度"：内容不被遮）。
    // t616：cellSize 42 → 44 —— t591 留的右侧空隙 ~34px 观感「滚动条和方块间隔太大」；44 后内容
    //   8×44+7×4=380，滚动条 6px 贴右侧轨道（内容距视口右缘 ~12px）→ 间距收紧且滚动条不遮内容
    //   （380+6=386 < 视口 398）。生物段 mobGrid 同读 cellSize → 两段同步对齐。
    readonly property int paletteCols: 8
    readonly property int cellSize: 44

    // 半透遮罩：仅吸收点击（防穿透到背后设置面板），不关闭（只能 Esc / 返回按钮关）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.65)
        MouseArea { anchors.fill: parent; onClicked: {} }
    }

    // 面板：深色圆角，居中。
    Rectangle {
        id: panel
        width: 780; height: 500
        anchors.centerIn: parent
        radius: 14
        color: "#1b1f24"
        border.color: "#3a444f"; border.width: 1

        // 兜底吸收面板内空点击（防穿透到遮罩）。
        TapHandler { acceptedButtons: Qt.LeftButton }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            // 标题行：左标题，右关闭提示。
            Item {
                width: parent.width; height: 26
                Text {
                    text: "资源查看器"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "点击左侧浏览 · [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 主体 Row：左网格 + 右预览。
            Item {
                width: parent.width; height: parent.height - 26 - 10 - footerCol.height - 10
                Row {
                    anchors.fill: parent
                    spacing: 12

                    // ── 左：可滚动全物品网格 ──
                    Rectangle {
                        width: parent.width - 322 - 12
                        height: parent.height
                        radius: 8
                        color: "#15191e"
                        border.color: "#2a323b"; border.width: 1
                        Flickable {
                            id: gridFlick
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            contentWidth: grid.width
                            // t591：contentHeight 底部 +8 padding —— 末行物品滚动到底不被 Flickable 底边
                            //   裁剪（"底 padding 补滚动条宽度"）；滚动条用 DarkScrollBar（暗色细条，UI 统一样式）。
                            contentHeight: mobCol.height + 8
                            flickableDirection: Flickable.VerticalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: DarkScrollBar {}

                            // 「生物」段（图鉴）+「物品」段（原调色板全集）上下排列（Column）。选中互斥：
                            //   点生物格设 selectedMobFromSection（右侧显 3D 模型）；点物品格清空它回物品预览。
                            Column {
                                id: mobCol
                                width: grid.width
                                spacing: 8

                                Text {
                                    text: "生物"
                                    color: "#7fae7f"; font.pixelSize: 13; font.bold: true
                                }
                                // 生物图鉴网格（静态 mob 表）：每格 = 体色 swatch + 中文名。选中 → 右侧 View3D
                                //   旋转 MobModel 3D 模型（pack 开 → pack entity 贴图；关 → 程序生成 / 纯色回退）。
                                Grid {
                                    id: mobGrid
                                    columns: 8
                                    spacing: 4
                                    Repeater {
                                        model: root.mobModel
                                        delegate: Item {
                                            width: root.cellSize; height: root.cellSize
                                            // 凹陷斜面槽框（同物品格风格）。
                                            Rectangle { anchors.fill: parent; color: "#222831" }
                                            Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
                                            Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
                                            Rectangle { color: "#5a5a5a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
                                            Rectangle { color: "#5a5a5a"; width: 1; height: parent.height; anchors.right: parent.right }
                                            Column {
                                                anchors.centerIn: parent
                                                spacing: 2
                                                // t633 ② 生物头像：pack 命中 → mobHeadIconSource 裁的头部 2D
                                                //   PNG；miss → 回退链。Image source 触碰 packActive（表达式形式
                                                //   防 AOT 死代码消除）。
                                                // t749 七张空白头像补齐回退链（鱿鱼9/狼10/豹猫11/蠹虫14/夜行者16/
                                                //   燃烬者17）：pack 关 / 头区裁剪 miss → 改显程序 mob 贴图全脸图
                                                //   （mobFallbackTexture，对齐 mobTextureSource 的「pack 命中→程序
                                                //   回退」双态语义）——旧态这些 mobType 不在 mobHeadRegions 表 =
                                                //   mobHeadIconSource 恒空串 → 白色 swatch 被读作「空白」的根因。
                                                //   雪傀儡（12）pack 关无程序实体贴图 → 维持 swatch（雪白块）；
                                                //   其余 10 mob 维持 swatch 回退不变（原行为零回归）。
                                                Item {
                                                    width: 26; height: 26
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    property string headSrc: {
                                                        const _r = root.packActive
                                                        const t = modelData.mobType
                                                        if (_r >= 0 && root.resourcePack) {
                                                            const s = root.resourcePack.mobHeadIconSource(t)
                                                            if (s !== "")
                                                                return s
                                                        }
                                                        const hasProgAvatar = t === 9 || t === 10 || t === 11
                                                              || t === 14 || t === 16 || t === 17
                                                        return hasProgAvatar ? root.mobFallbackTexture(t) : ""
                                                    }
                                                    Image {
                                                        anchors.fill: parent
                                                        visible: parent.headSrc !== ""
                                                        source: parent.headSrc
                                                        fillMode: Image.PreserveAspectFit
                                                        smooth: false // 像素锐利（HD 原图缩放）
                                                    }
                                                    Rectangle {
                                                        anchors.fill: parent
                                                        radius: 4
                                                        visible: parent.headSrc === ""
                                                        color: root.mobFallbackColor(modelData.mobType)
                                                        border.color: "#4a5a4a"; border.width: 1
                                                    }
                                                }
                                                Text {
                                                    text: modelData.name
                                                    color: "#eaf2ea"; font.pixelSize: 10
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                }
                                            }
                                            // 选中态高亮（金边）+ hover 高亮（绿边）。
                                            Rectangle {
                                                anchors.fill: parent; color: "transparent"; radius: 2
                                                border.color: root.selectedMobFromSection === modelData.mobType ? "#ffd76a"
                                                              : (mobHover.hovered ? "#7fe57f" : "transparent")
                                                border.width: 2
                                            }
                                            HoverHandler {
                                                id: mobHover
                                                onHoveredChanged: {
                                                    // t617 tooltip：进入写名 + 格顶中心（panel 坐标系）+ mob 哨兵 id；
                                                    //   离开按名守卫清（t633 ①：id 一并清，防陈旧 id 影响后续简述）。
                                                    if (hovered) {
                                                        root.hoveredName = modelData.name
                                                        root.hoveredId = -1
                                                        const p = parent.mapToItem(panel, parent.width / 2, 0)
                                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                                    } else if (root.hoveredName === modelData.name) {
                                                        root.hoveredName = ""
                                                        root.hoveredId = -1
                                                    }
                                                }
                                            }
                                            // 选中生物段条目（t751 条目合并后每型单条；selectedMobName 仅作
                                            //   显示名伴选，变体不再拆条目——改预览区悬浮变体面板切换）。
                                            TapHandler { onTapped: { root.selectedMobFromSection = modelData.mobType; root.selectedMobName = modelData.name } }
                                        }
                                    }
                                }

                                Text {
                                    text: "物品"
                                    color: "#7fae7f"; font.pixelSize: 13; font.bold: true
                                }
                                Grid {
                                    id: grid
                                    columns: root.paletteCols
                                    spacing: 4
                                    Repeater {
                                        model: root.paletteModel
                                        delegate: Item {
                                            width: root.cellSize; height: root.cellSize
                                            // 凹陷斜面槽框（同创造背包槽位风格）。
                                            Rectangle { anchors.fill: parent; color: "#222831" }
                                            Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
                                            Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
                                            Rectangle { color: "#5a5a5a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
                                            Rectangle { color: "#5a5a5a"; width: 1; height: parent.height; anchors.right: parent.right }

                                            // 物品图标（路由同创造背包 delegate：方块 Image / 工具 ToolIcon / 材料·护甲 MaterialIcon）。
                                            Item {
                                                anchors.centerIn: parent
                                                width: 30; height: 30
                                                Image {
                                                    anchors.fill: parent
                                                    visible: !root.hotbar.isTool(modelData) && !root.hotbar.isMaterial(modelData)
                                                    // 触碰 packActive → pack 切换图标刷新（t745 双态路由：pack 开 = 运行期 pack 图集渲染 / 2D pack 立绘；pack 关 = 程序原生）。
                                                    source: { const _r = root.packActive; return _r >= 0 ? (root.hotbar.iconSourceForBlock(modelData)) : "" }
                                                    fillMode: Image.PreserveAspectFit
                                                    smooth: true
                                                }
                                                ToolIcon {
                                                    anchors.fill: parent
                                                    visible: root.hotbar.isTool(modelData)
                                                    tier: root.hotbar.toolTier(modelData)
                                                    toolType: root.hotbar.toolType(modelData)
                                                }
                                                MaterialIcon {
                                                    anchors.fill: parent
                                                    visible: root.hotbar.isMaterial(modelData)
                                                    materialId: modelData
                                                }
                                            }
                                            // 选中态高亮（金边）+ hover 高亮（绿边）。
                                            Rectangle {
                                                anchors.fill: parent; color: "transparent"; radius: 2
                                                border.color: root.selectedId === modelData ? "#ffd76a"
                                                              : (cellHover.hovered ? "#7fe57f" : "transparent")
                                                border.width: 2
                                            }
                                            HoverHandler {
                                                id: cellHover
                                                onHoveredChanged: {
                                                    // t617 tooltip：进入写名 + 格顶中心（panel 坐标系）+ 物品 id；
                                                    //   离开按名守卫清（t633 ①：hotbar 空守卫防注入前 hover 抛错吞掉
                                                    //   信号 → hoveredName 恒空 = 「名字全空白」根因；id 一并清）。
                                                    if (hovered) {
                                                        root.hoveredName = root.hotbar ? root.hotbar.nameForBlock(modelData) : ""
                                                        root.hoveredId = modelData
                                                        const p = parent.mapToItem(panel, parent.width / 2, 0)
                                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                                    } else if (root.hoveredName === (root.hotbar ? root.hotbar.nameForBlock(modelData) : "")) {
                                                        root.hoveredName = ""
                                                        root.hoveredId = -1
                                                    }
                                                }
                                            }
                                            // 点物品格 → 选中该物品 + 清空生物段选中（互斥；生物蛋 id 经
                                            //   mobTypeForEgg 映射回 mob → 右侧仍显 3D 模型，但类别标签走「生物蛋」）。
                                            TapHandler { onTapped: { root.selectedId = modelData; root.selectedMobFromSection = -1; root.selectedMobName = "" } }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── 右：选中物预览（View3D 旋转立方 / 大图标）+ 名 + 类别 ──
                    Rectangle {
                        width: 322; height: parent.height
                        radius: 8
                        color: "#0e1115"
                        border.color: "#2a323b"; border.width: 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            // 预览区（View3D 与大图标互斥）。t783 ②：变体切换控件（羊剪毛/毛色、雪傀儡剪头）
                            //   悬浮于本预览区下沿内侧（见区内尾部 variantPanel），不再挂右列 Column 底部。
                            Item {
                                id: previewArea
                                width: parent.width; height: 300

                                // t599 3D 预览鼠标拖拽旋转（用户「一直自动旋转，能不能拖拽看」）：在自动旋转基础上
                                //   加 DragHandler —— 按住拖时暂停自转（previewDragging → NumberAnimation running=false），
                                //   水平位移增量写 spinAngle（yaw，度；1px = 0.6° 手感系数）、垂直位移增量累计
                                //   userPitch（pitch，度；上拖看顶 / 下拖看底，限 ±60° 防过翻）；松手 pitch 由
                                //   resumePitchAnim 平滑归零（400ms OutCubic 回标准 -22° 3/4 视角），yaw 由自转从当前
                                //   角度无缝续转（NumberAnimation on spinAngle 重启从当前值推进，无跳变）。
                                //   t877 恢复 t599 原符号（userPitch - dy*0.6）：t820 曾按「推球面」直觉取反
                                //   （+dy），用户两轮实测均判「上下反了」→ 回退原方向并**写死定稿**——
                                //   【用户确认方向，勿再改】（任何方向直觉推导都不得再翻转此符号；左右 yaw
                                //   未动）。方块与生物 3D 预览共用（同一 spinAngle/userPitch）；enabled 限定
                                //   3D 预览可见时（大图标态不抢手势；左侧网格在其外不受影响）。translation
                                //   是只读累计值 → lastX/lastY 记上次值取增量（拖拽结束归零基准，下次拖从 0 差起）。
                                DragHandler {
                                    id: previewDrag
                                    target: null // 不拖动对象本身，只读位移（增量驱动旋转）
                                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen
                                    enabled: cubeView.visible
                                    property real lastX: 0
                                    property real lastY: 0
                                    onActiveChanged: {
                                        if (active) {
                                            root.previewDragging = true
                                            resumePitchAnim.stop() // 拖拽开始即接管 pitch（防归零动画抢写）
                                        } else {
                                            root.previewDragging = false
                                            lastX = 0; lastY = 0 // 松手归零基准（translation 重新累计）
                                            resumePitchAnim.restart() // 松手：pitch 平滑归零，yaw 自转续跑
                                        }
                                    }
                                    onTranslationChanged: {
                                        if (!active) return
                                        const dx = translation.x - lastX
                                        const dy = translation.y - lastY
                                        lastX = translation.x
                                        lastY = translation.y
                                        root.spinAngle = (root.spinAngle + dx * 0.6 + 360) % 360
                                        // t877 用户定稿符号（t599 原方向）：上拖看顶 / 下拖看底。
                                        //   【用户确认方向，勿再改】——t820 的 +dy 取反已按用户实测回退。
                                        // t921 背面相位补偿（与 t877 契约的关系，钉死防误改回）：t877 定稿的是
                                        //   **正面相位**（显示 yaw 未过 ±90°）的符号——本行不改该符号，只在
                                        //   自转把背面转向相机时（显示 yaw = spinAngle-35 的 cos < 0，pitch 铰链
                                        //   轴在屏幕上的投影反号）把**增量**乘 -1，使「上拖看顶」的屏幕感知在
                                        //   背面相位与正面一致（用户定位的规律：转到底面朝人时上下拖拽反了）。
                                        //   两态同向 = t877 契约保持并扩到全程，非翻转既有方向。
                                        const yawRad = (root.spinAngle - 35) * Math.PI / 180
                                        const faceSign = Math.cos(yawRad) >= 0 ? 1 : -1
                                        root.userPitch = Math.max(-60, Math.min(60, root.userPitch - dy * 0.6 * faceSign))
                                    }
                                }
                                // t922 滚轮缩放（3D 预览态）：滚上放大 / 滚下缩小（每档 ×1.1，钳 [0.5, 3.0]）。
                                //   enabled 限定 cubeView 可见（大图标态不抢滚轮——左侧网格 Flickable 不受影响，
                                //   WheelHandler 在 previewArea 内独占消费）。target:null 只读滚轮增量不拖对象。
                                //   缩放由相机距离承载（PerspectiveCamera z = 3.2/zoom）——各 3D 分支零改动共享。
                                WheelHandler {
                                    acceptedDevices: PointerDevice.Mouse
                                    enabled: cubeView.visible
                                    onWheel: (wheel) => {
                                        const factor = wheel.angleDelta.y > 0 ? 1.1 : 1.0 / 1.1
                                        let z = root.previewZoom * factor
                                        if (z < 0.5) z = 0.5
                                        if (z > 3.0) z = 3.0
                                        root.previewZoom = z
                                    }
                                }
                                // t922 重置缩放按钮（预览区右下角）：回默认大小 1.0。仅 3D 预览且已缩放时显示
                                //   （默认态自隐，不占预览面）；变体面板已收窄让位（width -12 → -58）防叠。
                                // t964 永远最前（z 序契约）：本按钮声明在 cubeView（View3D，锚满预览区）**之前**
                                //   —— QML 兄弟层缺省按声明序绘制，后声明的视口恒绘在按钮上层；且按钮仅在已
                                //   缩放态显示（zoom≠1）= 恰逢模型投影最大、像素铺进视口右下角的时刻 → 放大即
                                //   被盖（用户第五轮「滚轮放大后遮住重置按钮」根因；headless 不可见，探针走源码
                                //   层级钉 P-t964）。修 = 显式 z: 10 提到视口（缺省 z 0）之上——variantPanel 同款
                                //   「显式 z 防后人插层翻序」约定。【层级契约】预览区悬浮控件层（变体面板 /
                                //   本按钮 / t965 后续形态·分类按钮组）一律显式 z ≥ 10 = 浮层永远最前于视口；
                                //   与变体面板同高但不相交（面板已收窄让位 58px），互不遮。
                                Rectangle {
                                    visible: cubeView.visible && Math.abs(root.previewZoom - 1.0) > 0.001
                                    z: 10
                                    width: 52; height: 24; radius: 6
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: 6
                                    anchors.bottomMargin: 6
                                    color: zoomResetArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                                    border.color: "#3a5a7a"; border.width: 1
                                    Text { anchors.centerIn: parent; text: "重置"; color: "#7fb0e5"; font.pixelSize: 11 }
                                    MouseArea {
                                        id: zoomResetArea
                                        anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.previewZoom = 1.0
                                    }
                                }
                                // 整立方方块 → 内嵌 View3D 旋转 BlockCube。
                                // 渲染可见性铁律（lessons-learned）：clipNear≈0.05（默认 10 会剪掉单位立方）+
                                //   PrincipledMaterial.NoLighting（默认 lit 在本工程不渲染）+ alphaMode Mask（leaves 等带
                                //   alpha 贴图 cutout 正确）。BlockCube 复用既有几何（同掉落实体 / 手持立方已验证路径）。
                                View3D {
                                    id: cubeView
                                    anchors.fill: parent
                                    visible: root.selectedIsCube || root.selectedIsMob || root.selectedIsItem3D
                                    // View3D 默认 Offscreen 渲染（FBO 合成），嵌面板预览正确。
                                    PerspectiveCamera {
                                        position: Qt.vector3d(0, 0, 3.2 / root.previewZoom) // t922 滚轮缩放（距离承载；1.0 = 默认 3.2）
                                        clipNear: 0.05
                                        clipFar: 100
                                        fieldOfView: 45
                                    }
                                    Model {
                                        // 仅整立方方块时显示（选中 mob / 生物蛋 → 只显 MobModel；选中床 → 只显
                                        //   BedModelGeometry 低 3D 床；t880 异形物品 → 只显 ItemShapeGeometry，
                                        //   多模型互斥不叠渲染）。
                                        visible: root.selectedIsCube && !root.selectedIsMob && !root.selectedIsBed && !root.selectedIsItem3D // review27 #4：附魔台 94 不在 isPartialBlock → selectedIsCube 对 94 仍 true，与下方 ItemShapeGeometry 预览叠渲 z-fight（同 Main.qml 掉落物侧修法）；家族互斥钉死
                                        // blockId 绑选中物；不设 world → BlockCube 顶点色恒白（全亮，无天光遮蔽，预览纯净）。
                                        // t965：blockState 绑形态按钮组（耕地干/湿顶面、末地框无眼/有眼顶面——
                                        //   BlockRegistry::stateTileOverride Core 权威；非态变方块 state 0 零漂移）。
                                        geometry: BlockCube { blockId: root.selectedId; blockState: root.selectedFormState }
                                        // 固定 -22° X 基倾（见顶面）+ userPitch 拖拽俯仰（t599）+ Y 自转
                                        //   （spinAngle，拖拽时由 DragHandler 写入）；-35° 基偏给 3/4 视角。
                                        eulerRotation: Qt.vector3d(-22 + root.userPitch, root.spinAngle - 35, 0)
                                        materials: PrincipledMaterial {
                                            lighting: PrincipledMaterial.NoLighting
                                            baseColorMap: Texture { source: root.atlasSource; generateMipmaps: false }
                                            // Mask + 0.5：leaves 等带 alpha 的整立方贴图 cutout 正确（同地形 terrain 段）；
                                            //   其余不透明方块贴图 alpha=1 不受影响。
                                            alphaMode: PrincipledMaterial.Mask
                                            alphaCutoff: 0.5
                                        }
                                    }
                                    // t784 床预览：游戏内低 3D 床模型（BedModelGeometry 双格拼装：床尾半 + 床头半，
                                    //   盒布局复用游戏内 bedHalfBoxes 单一权威）替代旧满格 BlockCube 立方。贴图同源共享
                                    //   图集（atlasSource → pack 开 = pack 床瓦片即时刷新）；腿/架/板贴 planks、枕头贴白
                                    //   wool、床垫贴床色被面瓦片——与游戏内逐瓦片一致。scale 1.15：双格床长 2（对角投影
                                    //   ~2.4）撑满镜头仍整床可见（单格高立方 1.0 的对比基准）；旋转/拖拽与方块分支共用
                                    //   spinAngle/userPitch（床仍在 cubeView 内，DragHandler 手势不变）。
                                    Model {
                                        visible: root.selectedIsBed
                                        geometry: BedModelGeometry { blockId: root.selectedId }
                                        scale: Qt.vector3d(1.15, 1.15, 1.15)
                                        eulerRotation: Qt.vector3d(-22 + root.userPitch, root.spinAngle - 35, 0)
                                        materials: PrincipledMaterial {
                                            lighting: PrincipledMaterial.NoLighting
                                            // 床瓦片（planks/wool/被面）全不透明 → 无需 Mask（同 bed 盒贴图约定）。
                                            baseColorMap: Texture { source: root.atlasSource; generateMipmaps: false }
                                        }
                                    }
                                    // t880 异形物品 3D 预览：活板门/火把/台阶/木楼梯/雪层/草丛/附魔台 →
                                    //   ItemShapeGeometry 真实 3D 形状（几何与 World partialblockgeometry 形状
                                    //   同源、形心居中），替代大图标平面图。旋转/拖拽与方块分支共用
                                    //   spinAngle/userPitch（DragHandler 手势不变）。Mask+0.5：活板门孔 / cross
                                    //   透明底 / 火把窗 cutout 正确；不透明瓦片不受影响（同方块分支契约）。
                                    //   火把（13）形状细小（2/16 柱）→ scale 1.6 放到近立方视觉量级；其余 1.0
                                    //   （参数视觉钉死，待用户目视确认）。
                                    Node {
                                        visible: root.selectedIsItem3D
                                        // t880 火把细柱 1.6 放大先例 → t925 小体型族同款放大（近立方视觉
                                        //   量级可辨）：机关小体（拉杆/按钮 6/16 见方）×1.8、蘑菇剪影 / 红石
                                        //   火把 ×1.5、栅栏横档细臂 ×1.2；其余 1.0（参数视觉钉死，待目视）。
                                        property real item3DScale: {
                                            if (root.selectedId === 13) return 1.6
                                            if (root.selectedId === 112 || root.selectedId === 113
                                                || root.selectedId === 114) return 1.8
                                            if (root.selectedId === 48 || root.selectedId === 115
                                                || root.selectedId === 129) return 1.5
                                            if (root.selectedId === 17 || root.selectedId === 60
                                                || root.selectedId === 88) return 1.2
                                            return 1.0
                                        }
                                        scale: Qt.vector3d(item3DScale, item3DScale, item3DScale)
                                        eulerRotation: Qt.vector3d(-22 + root.userPitch, root.spinAngle - 35, 0)
                                        Model {
                                            // t965：blockState 绑形态按钮组（门/活板门开合几何、草丛高度、
                                            //   作物/红石火把/动力轨态变瓦片——ItemShapeGeometry 显式 state 路径；
                                            //   auto 旧默认仅掉落物侧消费）。
                                            geometry: ItemShapeGeometry { blockId: root.selectedId; blockState: root.selectedFormState }
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                alphaMode: PrincipledMaterial.Mask
                                                alphaCutoff: 0.5
                                                baseColorMap: Texture { source: root.atlasSource; generateMipmaps: false }
                                            }
                                        }
                                        // t880 附魔台预览**上面要有书**：台顶（形心居中系 y=+0.375）叠静态
                                        //   敞开书（EnchantBookBox 两页 V 形——纸页 + 镜像纸页，机制等价放置态
                                        //   bookDelegate 静息造型；贴图两态 pack 命中（entitySource("enchant_book")
                                        //   宽 >64 → 布局 1 包书分区）/ qrc 程序书（布局 0），Main.qml bookPackHit
                                        //   同判据）。页 0.38 宽 × 0.46 深 × 0.03 厚，各绕 Z 外倾 ±22° 成 V，
                                        //   书心 y=+0.46（台顶上浮 ~0.08「悬浮书」观感）。
                                        Node {
                                            id: etBookNode
                                            visible: root.selectedId === 94
                                            position: Qt.vector3d(0, 0.46, 0)
                                            property bool etBookPackHit: enchantBookPackTexBrowser.source.toString().length > 0
                                                                         && root.resourcePack
                                                                         && root.resourcePack.entityTextureWidth("enchant_book") > 64
                                            Model { // 左页（纸页镜像 piece 4；-22° 外缘下倾）
                                                geometry: EnchantBookBox { piece: 4; layout: etBookNode.etBookPackHit ? 1 : 0 }
                                                position: Qt.vector3d(-0.176, 0.045, 0)
                                                eulerRotation: Qt.vector3d(0, 0, -22)
                                                scale: Qt.vector3d(0.38, 0.03, 0.46)
                                                materials: PrincipledMaterial {
                                                    lighting: PrincipledMaterial.NoLighting
                                                    baseColorMap: etBookNode.etBookPackHit ? enchantBookPackTexBrowser : enchantBookTexBrowser
                                                }
                                            }
                                            Model { // 右页（纸页 piece 1；+22° 镜像成 V）
                                                geometry: EnchantBookBox { piece: 1; layout: etBookNode.etBookPackHit ? 1 : 0 }
                                                position: Qt.vector3d(0.176, 0.045, 0)
                                                eulerRotation: Qt.vector3d(0, 0, 22)
                                                scale: Qt.vector3d(0.38, 0.03, 0.46)
                                                materials: PrincipledMaterial {
                                                    lighting: PrincipledMaterial.NoLighting
                                                    baseColorMap: etBookNode.etBookPackHit ? enchantBookPackTexBrowser : enchantBookTexBrowser
                                                }
                                            }
                                        }
                                    }
                                    // 生物预览（生物段 / 生物蛋选中）：MobModel 3D 模型替代大图标平图。
                                    //   pack 命中（selectedMobPackSrc 非空）→ packTextured（几何 T 字 UV 展开进 pack
                                    //   entity 贴图）+ baseColorMap = pack 贴图；pack 关 → 全脸 UV + 程序生成 mob_*.png /
                                    //   纯色（mobFallback*；bones/stalker/spider 无程序贴图 → baseColorMap:null）。
                                    //   NoLighting（渲染可见性铁律）。scale 1.0（I3 雪/铁傀儡 0.75）+ 垂直居中微调。
                                    // t598 雪/铁傀儡南瓜头：两傀儡 MobModel 几何只含块身（头是独立 Model，同 Main.qml
                                    //   游戏内方案）—— 图鉴预览此前漏了头 → 雪傀儡「无头」。把游戏内方案带过来：
                                    //   雪傀儡（12）叠 BlockCube{blockId:100}（南瓜方块）+ 共享图集（atlasSource，
                                    //   pack 激活即 HD 南瓜瓦片，机制等价 MC 1.0 雪傀儡戴刻面南瓜）。
                                    //   铁傀儡（13）review L14 改镜像游戏内：纯橙 UnitCube + 刻面双眼（非南瓜贴图）。
                                    //   头随父 Model 同转（自转/拖拽）。
                                    Node {
                                        visible: root.selectedIsMob
                                        position: Qt.vector3d(0, root.mobPreviewCentY(root.selectedMobType), 0)
                                        scale: Qt.vector3d(root.mobPreviewScale(root.selectedMobType),
                                                          root.mobPreviewScale(root.selectedMobType),
                                                          root.mobPreviewScale(root.selectedMobType))
                                        eulerRotation: Qt.vector3d(-22 + root.userPitch, root.spinAngle - 35, 0)
                                        Model {
                                            geometry: MobModel {
                                                // Review 2026-08-24 #6：selectedMobType 在未选生物/生物蛋时
                                                //   是 -1（合法「无选择」哨兵——mobPreviewCentY/Scale 同把 -1 当
                                                //   预期输入优雅返 0）。本 Node 只 visible 门控（对象恒实例化、
                                                //   绑定恒求值），裸传 -1 会让 setMobType 的越界 qWarning
                                                //   （review #35 诊断信号）在每次选非生物条目时误报「接线 bug」，
                                                //   污染真信号。钳到 1（Pig，同 setMobType 越界兜底）——不可见态
                                                //   几何无观感；取 Loader active 门控的代价是 delegate 常驻变
                                                //   按需重建（开图鉴翻条目更重），故取一行钳制。
                                                mobType: Math.max(1, root.selectedMobType)
                                                // t876 羊头分离 subset：毛茸态羊 → 头盒独立 subset（materials[1]
                                                //   换绑本体层头区、不吃毛色 tint）；剪毛态 / 非羊 → false 单段绘制。
                                                sheepSkinHead: root.sheepSkinHeadActive
                                                // t920 坐姿几何：狼/豹猫驯服态 + 面板「坐下」段 → MobModel
                                                //   sitPose 分支（t878②；与 Main.qml 游戏内 delegate 同一几何
                                                //   源，非浏览器侧复刻）。未驯服不可坐（机制等价 MC 野狼/野豹猫
                                                //   不可命令）；切换即时重建。
                                                sitPose: root.mobTamedActive && root.mobSitPreview
                                                // t749 剪毛羊 pack 本体层是 box-UV 布局 → 同样开 T 字展开
                                                //   （程序 mob_sheep_sheared 是全脸 UV → 保持 false）。
                                                // t949 贴图源 × UV 模式同源钉：驯服豹猫例外（t920 驯服猫 → 程序
                                                //   mob_cat_* **全脸**贴图）必须同时关 box-UV——「贴图源」与「UV
                                                //   模式」是两个独立开关，须同一条件门。旧版 packTextured 只看
                                                //   pack 命中（mobType 11 开包恒命中）→ 驯服态预览几何以 box-UV
                                                //   窗采程序猫贴图任意像素 = 混入狼样灰斑（用户第五轮「3D 贴图
                                                //   混入狼的灰色贴图」根因；游戏内 delegate 的 ocelotPackHit 自带
                                                //   !ocatTamed 故游戏内无此病——两消费端门条件现逐字同源）。
                                                packTextured: (root.selectedMobPackSrc !== ""
                                                               && !(root.selectedMobFromSection === 11 && root.mobTamedPreview))
                                                    || (root.selectedMobSheared && root.selectedMobType === 3
                                                        && root.sheepBodyPackSrc !== "")
                                                // t782 燃烬者棒组公转（度；头+4棒共享几何）：仅选燃烬者时给动画角
                                                //   （其余型恒 0——绑定时表达式结果不变 → 不触发 rebuild，无逐帧开销；
                                                //   时钟恒跑属零成本 NumberAnimation，2.2s/圈同游戏内转速）。
                                                property real rodClock: 0
                                                rodSpin: root.selectedMobType === 17 ? rodClock : 0
                                                NumberAnimation on rodClock {
                                                    from: 0; to: 360; duration: 2200; loops: Animation.Infinite
                                                }
                                            }
                                            // t876 双材质（仅羊毛茸态有 subset 1，其余 mobType 单段用 [0]）：
                                            //   [0] = 身体（毛层 × 毛色 tint）；[1] = 羊头 subset（本体层头区自然色）。
                                            materials: [
                                                PrincipledMaterial {
                                                    lighting: PrincipledMaterial.NoLighting
                                                    // pack 关且无程序贴图（bones/stalker/spider）→ null + 纯色 baseColor。
                                                    // t597 修：渲染 = baseColorMap × baseColor —— pack 贴图在身时 baseColor 用白
                                                    //   （贴图原色完整透出，同 Main.qml t597 修法）；mobFallbackColor 是 pack 关的
                                                    //   纯色体色（stalker #3a5a3a / spider #2a1a1a 均暗色），乘上 pack 贴图会把
                                                    //   贴图压暗近黑（图鉴预览同样「暗淡/无贴图」观感）。
                                                    // t663 ⑥ → t749 改：剪毛羊变体去**纯色**改贴图（pack 本体层 / 程序
                                                    //   mob_sheep_sheared 裸肤 + 残羊毛块），贴图在身 → baseColor 白（同 t597）。
                                                    // ── t751 不变式（剪头雪傀儡「下半身错误」修复结论）── 身体（MobModel）
                                                    //   的贴图/颜色路由 = f(mobType, pack 态)，**与剪/戴变体无关**：唯一带
                                                    //   selectedMobSheared 的身体分支是羊（3）的裸肤贴图切换（机制等价游戏内
                                                    //   剪羊毛换裸皮）；雪傀儡（12）剪头仅切头 Model（下方南瓜 ↔ 纯雪头），
                                                    //   身体两态逐位一致。历史根因：t663 旧版把 baseColorMap 写成
                                                    //   `有贴图 && !selectedMobSheared` —— 任何剪后变体（含雪傀儡）在 pack 开时
                                                    //   身体贴图被一并剥成纯白，与戴头形态（snow_golem 贴图有纹）并排对比即
                                                    //   「下半身模型错误」（参照物戴头形态正确 = 贴图路由未受损）；t749 重写该
                                                    //   绑定已恢复路由，t751 变体切换化后以本不变式钉死防回归。
                                                    baseColorMap: root.selectedMobSheared && root.selectedMobType === 3 ? mobShearedTex
                                                        : (root.selectedMobTexSource !== "" ? mobPrevTex : null)
                                                    baseColor: {
                                                        // t751/t876 羊毛色预览着色：毛茸态 + 非白色 → 染色 tint 乘贴图
                                                        //   （白底羊毛贴图 × 染色 = 染色羊毛，调色板与羊毛方块 16 色同源）。
                                                        //   诚实边界：游戏内无染色羊机制，图鉴侧仅预览着色；t876 起 tint 只落
                                                        //   subset 0 躯干毛层（头 subset 独立材质本体层自然色、腿 = t777
                                                        //   四腿皮肤罩，同为 skin 层不 tint），对齐 MC 染色羊脸/腿不随毛色染
                                                        //   （t816 脸罩方案退役——头盒直接换绑纹理源，非遮盖）；
                                                        //   裸肤（剪毛后）不染色。生物蛋路径不 tint（变体仅生物段浏览）。
                                                        if (root.selectedMobFromSection === 3 && !root.sheepSheared
                                                            && root.sheepWoolIndex > 0)
                                                            return root.woolPalette[root.sheepWoolIndex].tint
                                                        return (root.selectedMobSheared && root.selectedMobType === 3)
                                                            || root.selectedMobTexSource !== "" ? "#ffffff"
                                                            : root.mobFallbackColor(root.selectedMobType)
                                                    }
                                                    // t663 ⑥ 羊毛层 Mask（图鉴羊「不对」回归修复）：合成贴图（t749）全不透明 →
                                                    //   Mask 对它无影响；仅 pack 命中且非剪毛态保留（防御异形包毛层镂空）。
                                                    // t781 夜行者：pack enderman 头前透明下巴（底色 RGB 黄）→ pack 命中时
                                                    //   Mask 裁（Main.qml 实体 delegate / 刷怪笼迷你态同款；程序贴图全不透明）。
                                                    alphaMode: (root.selectedMobType === 3 && root.selectedMobPackSrc !== "" && !root.selectedMobSheared)
                                                               || (root.selectedMobType === 16 && root.selectedMobPackSrc !== "")
                                                               ? PrincipledMaterial.Mask : PrincipledMaterial.Opaque
                                                    alphaCutoff: 0.5
                                                },
                                                PrincipledMaterial {
                                                    // t876 subset 1 羊头：本体层头区自然色，不吃毛色 tint（镜像
                                                    //   Main.qml 游戏内侧）。pack 开 → mobPrevTex 合成贴图头区（box-UV
                                                    //   head(0,0)6×6×8 直采本体层真脸）；pack 关 → mobSheepHeadTex
                                                    //   程序羊头贴图（全脸 UV）。无 subset 1 的 mobType / 剪毛态本材质
                                                    //   不被消费（materials 多于 subset → 多余材质忽略）。
                                                    lighting: PrincipledMaterial.NoLighting
                                                    baseColor: "#ffffff" // 贴图在身 → 白透原色（t597）
                                                    // review26 #18：判据改 selectedMobPackSrc（与下行 alphaMode 同源）——
                                                    //   selectedMobTexSource 在 pack 关时也非空（= 回退 qrc 贴图）→ 旧三目
                                                    //   恒走 mobPrevTex，注释声明的「pack 关 → mobSheepHeadTex」落空 = 程序
                                                    //   羊头贴图死代码（图鉴侧从未激活）。
                                                    baseColorMap: root.selectedMobPackSrc !== "" ? mobPrevTex : mobSheepHeadTex
                                                    alphaMode: root.selectedMobType === 3 && root.selectedMobPackSrc !== ""
                                                               ? PrincipledMaterial.Mask : PrincipledMaterial.Opaque
                                                    alphaCutoff: 0.5
                                                }
                                            ]
                                        }
                                        // t663 ⑥ 羊眼 overlay（镜像 Main.qml t633 ③：sheep_fur.png 毛层头前无脸 →
                                        //   眼恒显；Main.qml 颈枢 Node 绑 headPitch，图鉴静态 0 → 直立，直接定位）。
                                        //   裸羊变体同显（裸肤色无脸）。
                                        // t777 ① 修「预览眼埋进头内不显」：旧 z=-0.35 是把 Main.qml 颈枢**相对**坐标
                                        //   （颈枢 (0,0.10,-0.29) + 眼相对 z -0.35）当绝对坐标用——头盒 z∈[-0.61,-0.29]
                                        //   把眼整个包住 → 被头面遮挡恒不可见。烘焙正确绝对位：白眼底 z=-0.64（凸出
                                        //   头前面 -0.61 外 0.03 无 z-fight）/ 黑瞳 z=-0.65（叠白眼底前），y=0.10。
                                        // t777 ② pack 真脸门控：贴图自带脸（sheepPreviewPackFace）→ 隐 overlay 眼
                                        //   （防两双眼，镜像 Main.qml 游戏内修法 + 牛等既有语义）。t876：t816 脸罩
                                        //   退役（头 subset 独立材质自然色），眼显隐回归单一判据、无罩例外分支。
                                        Model {
                                            visible: root.selectedMobType === 3 && !root.sheepPreviewPackFace
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.055, 0.10, -0.64)
                                            scale: Qt.vector3d(0.055, 0.055, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3 && !root.sheepPreviewPackFace
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.055, 0.10, -0.64)
                                            scale: Qt.vector3d(0.055, 0.055, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3 && !root.sheepPreviewPackFace
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.055, 0.10, -0.65)
                                            scale: Qt.vector3d(0.028, 0.028, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3 && !root.sheepPreviewPackFace
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.055, 0.10, -0.65)
                                            scale: Qt.vector3d(0.028, 0.028, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        // t777 ① 羊腿 skin 层：MobModel 单材质把羊毛贴图（pack 毛层 / 程序 mob_sheep
                                        //   全脸）与 t789 毛色 tint 铺满全身**含四腿** → 用户观感「脚被羊毛完全覆盖」。
                                        //   语义：羊毛只覆躯干/头，腿是 skin 层 → 四腿位叠独立纯色羊皮 Model
                                        //   （#d6b890，t749 前剪毛羊裸肤同源色）罩住毛贴图腿。图鉴静态（walkPhase
                                        //   恒 0 → 四腿轴对齐，中心 (±0.18,-0.28,±0.26)、全长 (0.18,0.32,0.18)，
                                        //   镜像 mobmodel.cpp 羊分支 addLegs 实参）→ 静态盒可精确罩合；尺寸外扩
                                        //   0.01/0.02 防共面 z-fight（顶沿没入躯干 / 底沿探出 0.01，预览无地面）。
                                        //   独立材质不吃毛色 tint（有色羊腿仍羊皮色，t789 协同：tint 只乘毛层）；
                                        //   剪毛态同罩（裸肤腿读作平滑皮肤）。鸡腿 t616 同款「几何外独立腿 Model」先例。
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.18, -0.28, -0.26)
                                            scale: Qt.vector3d(0.19, 0.34, 0.19)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#d6b890" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.18, -0.28, -0.26)
                                            scale: Qt.vector3d(0.19, 0.34, 0.19)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#d6b890" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.18, -0.28, 0.26)
                                            scale: Qt.vector3d(0.19, 0.34, 0.19)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#d6b890" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.18, -0.28, 0.26)
                                            scale: Qt.vector3d(0.19, 0.34, 0.19)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#d6b890" }
                                        }
                                        // t663 ⑤ 蠹虫眼（2 颗黑点贴头前；镜像 Main.qml t487 delegate 位
                                        //   (±0.05,0.00,-0.35) scale 0.03——头心 (0,0,-0.24) 半 (0.14,0.11,0.10)）。
                                        Model {
                                            visible: root.selectedMobType === 14
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.05, 0.00, -0.35)
                                            scale: Qt.vector3d(0.03, 0.03, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#101010" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 14
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.05, 0.00, -0.35)
                                            scale: Qt.vector3d(0.03, 0.03, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#101010" }
                                        }
                                        // ── t750 图鉴 3D 预览对齐游戏内模型（六模型修复）──
                                        // 抉择「共享组件 vs 浏览器复刻」：游戏内正确拼装深嵌 Main.qml mobHost
                                        //   delegate（绑 entityManager 索引族 walkPhase/hurtFlash/rage/sit…），
                                        //   抽共享组件须把十余条实体绑定参数化且回归面覆盖全部 17 种 mob——成本 /
                                        //   风险远超收益；按任务行「评估成本」走**浏览器侧 1:1 复刻**（同 t598 傀儡
                                        //   头 / t616 弓 + 鸡腿 / t663 羊眼先例），各块注明 Main.qml 锚点互指。几何级
                                        //   差异（蠹虫分节；鱿鱼尖顶 t778 已随「单一生物模型」终态删除）已下沉
                                        //   MobModel 共享层（mobmodel.cpp，两侧同源无双份维护）。
                                        // t778 鱿鱼眼层整删 + 尖顶删（镜像 Main.qml squid delegate 同源修）：
                                        //   原 t750① 给图鉴补过 2 颗黑点几何眼（pack 关态显；程序贴图 mob_squid
                                        //   不画眼 → 眼靠几何盒）——与「鱿鱼=单一生物模型（去小鱿鱼、去眼睛）」
                                        //   终态冲突 → 删。眼只来自 pack 贴图前脸纹素（pack 命中态自带眼）；程序
                                        //   贴图态无脸纹（纯斑纹软体观感）。「头顶小鱿鱼」叠加层根源 = mobmodel.cpp
                                        //   pack 态尖顶小盒（复用 mantle texOffs → 六面各显 mantle 对应面缩图含
                                        //   前脸眼纹），t750 只删了 pack 关态、pack 态残留 → 本任务几何层整删，
                                        //   图鉴 / 游戏内 / 刷怪笼迷你态三处共享单源一并修复（见 mobmodel.cpp
                                        //   t778 注释）。
                                        // t750 ② 狼尾（修复「像兔子」——缺尾缺眼的灰身立耳四足读作兔；镜像
                                        //   Main.qml wolfTailPivot：尾根 (0,0.16,0.38) + 竖细盒毛色 0.55 灰；图鉴
                                        //   静态取满血竖起 35°（游戏内随血量 35°..140°）。
                                        //   t946 坐姿随移（Main.qml t878②→t946 成对契约）：尾根 = 站姿位绕坐姿根锚
                                        //   (-0.14,0.36) 旋 18° = (0,0.139,0.472) + 垂尾搭地 35°+75°=110°
                                        //   （机制等价 MC 坐狼垂尾）。
                                        Node {
                                            visible: root.selectedMobType === 10
                                            position: root.mobTamedActive && root.mobSitPreview
                                                      ? Qt.vector3d(0, 0.14, 0.47) : Qt.vector3d(0, 0.16, 0.38)
                                            eulerRotation.x: root.mobTamedActive && root.mobSitPreview ? 110 : 35
                                            Model {
                                                geometry: UnitCube {}
                                                position: Qt.vector3d(0, 0.10, 0)
                                                scale: Qt.vector3d(0.06, 0.20, 0.06)
                                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8c8c8c" }
                                            }
                                        }
                                        // t920 驯服狼红项圈（t831 驯服态视觉；镜像 Main.qml 游戏内 collar
                                        //   Model：站姿颈根 (0,0.16,-0.30) / 坐姿位 = 站姿绕坐姿根锚旋 18° (0,0.349,-0.175)
                                        //   （t946 链派生成对契约），横扁环带 x 微出躯干侧缘读作环颈）。未驯服不显；图鉴不做
                                        //   昼夜灰阶 / 受击红闪（纯色预览，同其他 overlay 眼/腿约定）。
                                        Model {
                                            visible: root.selectedMobType === 10 && root.mobTamedPreview
                                            geometry: UnitCube {}
                                            position: root.mobTamedActive && root.mobSitPreview
                                                      ? Qt.vector3d(0, 0.35, -0.175) : Qt.vector3d(0, 0.16, -0.30)
                                            scale: Qt.vector3d(0.42, 0.06, 0.07)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                baseColor: "#c22828" // 驯服项圈红（Main.qml rgba(0.76,0.16,0.16) 同值）
                                            }
                                        }
                                        // t963 驯服猫红项圈（用户第五轮「驯服后没看到项圈」；镜像 Main.qml 游戏
                                        //   内 t963 猫项圈 + t920 狼项圈先例：站姿颈根 (0,0.14,-0.30) / 坐姿位 =
                                        //   站姿绕豹猫坐姿根锚 (-0.12,0.32) 旋 18° = (0,0.319,-0.189)（t946 链派生
                                        //   成对契约，豹猫颈围镜像数值系 0.36/0.05/0.06）。未驯服不显；门挂
                                        //   mobTamedPreview（与 t920 贴图切换同一驯服拨杆位——单源）。图鉴不做
                                        //   昼夜灰阶 / 受击红闪（纯色预览，同狼项圈 overlay 约定）。
                                        Model {
                                            visible: root.selectedMobType === 11 && root.mobTamedPreview
                                            geometry: UnitCube {}
                                            position: root.mobTamedActive && root.mobSitPreview
                                                      ? Qt.vector3d(0, 0.32, -0.19) : Qt.vector3d(0, 0.14, -0.30)
                                            scale: Qt.vector3d(0.36, 0.05, 0.06)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                baseColor: "#c22828" // 驯服项圈红（狼项圈同值）
                                            }
                                        }
                                        // t750 ② 狼眼（2 颗深点；镜像 Main.qml wolf delegate：头心
                                        //   (0,0.12,-0.42) 半 (0.14,0.15,0.18) → 前脸 z=-0.60 → 眼贴头前
                                        //   (±0.08,0.16,-0.61)（t819 头后移贴胸，眼随移）。
                                        //   t780：pack 命中 → box-UV 贴图头前脸自带双瞳 → overlay 隐（t777 双眼教训）。
                                        //   t946 坐姿眼随移（Main.qml t878②→t946 成对契约）：坐姿头心 (0,0.324,-0.308)
                                        //   + 眼偏移净 10° 随头旋 → (±0.08, 0.40, -0.49)。
                                        Model {
                                            visible: root.selectedMobType === 10 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: root.mobTamedActive && root.mobSitPreview
                                                      ? Qt.vector3d(-0.08, 0.40, -0.49) : Qt.vector3d(-0.08, 0.16, -0.61)
                                            scale: Qt.vector3d(0.04, 0.05, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 10 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: root.mobTamedActive && root.mobSitPreview
                                                      ? Qt.vector3d(0.08, 0.40, -0.49) : Qt.vector3d(0.08, 0.16, -0.61)
                                            scale: Qt.vector3d(0.04, 0.05, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        // t750 ③ 豹猫眼（修复「没有脸」；镜像 Main.qml ocelot delegate：头心
                                        //   (0,0.12,-0.38) 半 (0.11,0.12,0.14) → 前脸 z=-0.52 → 眼贴头前
                                        //   (±0.07,0.15,-0.53)（t819 头后移贴胸，眼随移）。
                                        //   t780：pack 命中 → 贴图头前脸自带眼点 → overlay 隐（同上）。
                                        //   t920：已驯服豹猫贴图恒程序家猫（无脸纹）→ 眼恒显（镜像 Main.qml
                                        //   「驯服猫不走 pack 判据」）；t946 坐姿眼随移（与狼同修）：坐姿头心
                                        //   (0,0.306,-0.276) + 眼偏移净 10° 随头旋 → (±0.07, 0.36, -0.42)。
                                        Model {
                                            visible: root.selectedMobType === 11
                                                     && (root.selectedMobPackSrc === ""
                                                         || (root.selectedMobFromSection === 11 && root.mobTamedPreview))
                                            geometry: UnitCube {}
                                            position: root.mobTamedActive && root.mobSitPreview
                                                      ? Qt.vector3d(-0.07, 0.36, -0.42) : Qt.vector3d(-0.07, 0.15, -0.53)
                                            scale: Qt.vector3d(0.035, 0.04, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 11
                                                     && (root.selectedMobPackSrc === ""
                                                         || (root.selectedMobFromSection === 11 && root.mobTamedPreview))
                                            geometry: UnitCube {}
                                            position: root.mobTamedActive && root.mobSitPreview
                                                      ? Qt.vector3d(0.07, 0.36, -0.42) : Qt.vector3d(0.07, 0.15, -0.53)
                                            scale: Qt.vector3d(0.035, 0.04, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        // t750 ④ / t781 夜行者头前五官层（修复「黑影无脸」；镜像 Main.qml nwHead：
                                        //   细肢人形几何本体走共享 MobModel mobType 16（t781 头心 0.975 半 0.28 →
                                        //   前脸 z=-0.28），图鉴此前漏此层 = 无脸黑影）：
                                        //   眼发光层头前 (0,1.00,-0.30) 铺竖眼贴图（pack 命中 enderman_eyes 切包内
                                        //   竖眼；Mask 裁透明底）+ 嘴非激怒态淡显暗唇（opacity 0.15，同游戏内静态）。
                                        //   t781：pack 命中后身体贴图头前脸 row12 自带灰白双眼 → 隐 overlay 防四眼
                                        //   （狼/豹猫 t780「pack 自带脸则隐」同规；程序贴图无脸纹 → 恒显）。
                                        Model {
                                            visible: root.selectedMobType === 16 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0, 1.00, -0.30)
                                            scale: Qt.vector3d(0.34, 0.13, 0.03)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                baseColor: "#e8dcff" // 紫白魅眼底色（贴图缺失兜底，同游戏内）
                                                baseColorMap: nwEyesPackTex.source.toString().length > 0 ? nwEyesPackTex : mobNwEyesTex
                                                alphaMode: PrincipledMaterial.Mask
                                                alphaCutoff: 0.5
                                            }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 16
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0, 0.72, -0.29)
                                            scale: Qt.vector3d(0.18, 0.05, 0.03)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                opacity: 0.15
                                                baseColor: "#140f18" // 近黑紫（嘴缝/口腔）
                                            }
                                        }
                                        // t782 燃烬者环绕棒组移入 MobModel 共享几何（mobType 17 分支：头 + 4 棒
                                        //   径向 90° 分布 + rodSpin 公转，上 rodClock 动画驱动）——旧 t750 ⑤ 手搓
                                        //   4 根 UnitCube 纯色 Repeater 删除（无贴图且与游戏内各持一份易漂移；
                                        //   「单悬浮头 + 旋转贴图棒」观感同游戏内 t728/t782 标志形态）。悬浮 bob
                                        //   属游戏内游动动画，图鉴自转 + 棒组公转已给动态 → 不复刻。
                                        // t616 骷髅弓箭手持弓（用户「能不能拿上弓箭」；同 t598 傀儡头补法——图鉴预览
                                        //   此前只显 MobModel，游戏内弓（Main.qml 肩枢 Node）漏显 = 无弓骷髅）：Bones 时在
                                        //   垂手旁挂 MobBowGeometry（静态持弓位 drawAmount=0，同 Main.qml t616 游戏内方案；
                                        //   木褐色 #6b4526 独立于骨白体色）。MobBowGeometry 是 Renderer 层已注册 QML 类型
                                        //   （import VoxelSandbox 解析），NoLighting 红线。
                                        // review L13：z 由 -0.02 对齐游戏内合成位（肩枢+握把 z = -0.10）——旧值弓半埋臂内。
                                        Model {
                                            visible: root.selectedMobType === 5
                                            geometry: MobBowGeometry { drawAmount: 0 }
                                            position: Qt.vector3d(0.24, -0.37, -0.10)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                baseColor: "#6b4526" // 木褐色（同 Main.qml 骨骼弓配色）
                                            }
                                        }
                                        // t616 鸡细黄腿（用户「应该是细小的黄色腿」；同 Main.qml 游戏内方案——t598 让
                                        //   几何腿共用 body texOffs 采到毛绒区 → 腿已从 MobModel 移除，本处补纯色细黄腿
                                        //   #e8c53a 粗 0.06；图鉴静态（无 walkPhase），双腿直立）。
                                        Model {
                                            visible: root.selectedMobType === 8
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.07, -0.225, 0)
                                            scale: Qt.vector3d(0.06, 0.35, 0.06)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8c53a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 8
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.07, -0.225, 0)
                                            scale: Qt.vector3d(0.06, 0.35, 0.06)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8c53a" }
                                        }
                                        // t598 傀儡南瓜头（雪傀儡 mobType 12；同 Main.qml t582 游戏内头方案：
                                        //   BlockCube{blockId:100} + 图集瓦片 per-face 采 pumpkin_side/top/face）。
                                        //   位置/尺寸与 Main.qml 游戏内 delegate 一致（雪：头心 y=1.14 宽 0.50 ——
                                        //   碰撞中心局部坐标，随父 Node scale 缩放）。
                                        //   t663 ⑥ → t751：剪头变体（悬浮面板「已剪头」toggle → selectedMobSheared）
                                        //   → 南瓜头隐藏、下方纯雪头接管；身体两态不变（t751 不变式见材质注释）。
                                        Model {
                                            visible: root.selectedMobType === 12 && !root.selectedMobSheared
                                            geometry: BlockCube { blockId: 100 } // 100 = BlockRegistry::Pumpkin（QML 不 import C++ 静态类故字面量，同 Main.qml 约定）
                                            position: Qt.vector3d(0, 1.14, 0)
                                            scale: Qt.vector3d(0.50, 0.50, 0.50)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                baseColorMap: Texture { source: root.atlasSource; generateMipmaps: false }
                                                alphaMode: PrincipledMaterial.Mask
                                                alphaCutoff: 0.5
                                            }
                                        }
                                        // t663 ⑥/⑦ → t751 剪头后纯雪头 + 柔灰刻面眼嘴（悬浮面板「已剪头」toggle 触发；
                                        //   镜像 Main.qml t663 ⑦ 游戏内形态：
                                        //   纯色雪白 #f0f4f8 同身体 + #4a5568 柔灰刻面五官——非近黑「骷髅」刻痕）。
                                        Model {
                                            visible: root.selectedMobType === 12 && root.selectedMobSheared
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0, 1.14, 0)
                                            scale: Qt.vector3d(0.50, 0.50, 0.50)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#f0f4f8" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 12 && root.selectedMobSheared
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.13, 1.19, -0.27)
                                            scale: Qt.vector3d(0.10, 0.11, 0.04)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#4a5568" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 12 && root.selectedMobSheared
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.13, 1.19, -0.27)
                                            scale: Qt.vector3d(0.10, 0.11, 0.04)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#4a5568" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 12 && root.selectedMobSheared
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0, 1.07, -0.27)
                                            scale: Qt.vector3d(0.26, 0.06, 0.04)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#4a5568" }
                                        }
                                        // 铁傀儡头（纯橙 + 刻面双眼，镜像 Main.qml 游戏内 delegate；t635 pack 命中隐藏——
                                        //   MobModel 贴图头接管。t663 ④ 头心 0.95→0.905 / 眼 1.00→0.955 消头-身缝）。
                                        Model {
                                            visible: root.selectedMobType === 13 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0, 0.905, 0)
                                            scale: Qt.vector3d(0.72, 0.66, 0.72)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8821e" } // 橙（同游戏内；图鉴预览不调昼夜灰阶）
                                        }
                                        Model {
                                            visible: root.selectedMobType === 13 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.14, 0.955, -0.38)
                                            scale: Qt.vector3d(0.09, 0.11, 0.03)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a0e04" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 13 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.14, 0.955, -0.38)
                                            scale: Qt.vector3d(0.09, 0.11, 0.03)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a0e04" }
                                        }
                                    }
                                }

                                // 非整立方 / 非生物 → 大图标（路由同网格：方块段 iconSourceForBlock / 工具 ToolIcon / 材料·护甲 MaterialIcon）。
                                Item {
                                    anchors.centerIn: parent
                                    width: 200; height: 200
                                    visible: !root.selectedIsCube && !root.selectedIsMob && !root.selectedIsBed
                                             && !root.selectedIsItem3D // t880 异形物品 3D 家族走 cubeView 预览
                                    // 大图标背景圆角板（与 View3D 区视觉分隔）。
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 180; height: 180; radius: 10
                                        color: "#171b21"
                                        border.color: "#2a323b"; border.width: 1
                                        z: -1
                                    }
                                    Image {
                                        anchors.centerIn: parent
                                        width: 150; height: 150
                                        visible: root.hotbar && !root.hotbar.isTool(root.selectedId) && !root.hotbar.isMaterial(root.selectedId)
                                        source: { const _r = root.packActive; return _r >= 0 ? (root.hotbar ? root.hotbar.iconSourceForBlock(root.selectedId) : "") : "" }
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                    }
                                    ToolIcon {
                                        anchors.centerIn: parent
                                        width: 150; height: 150
                                        visible: root.hotbar && root.hotbar.isTool(root.selectedId)
                                        tier: root.hotbar ? root.hotbar.toolTier(root.selectedId) : 1
                                        toolType: root.hotbar ? root.hotbar.toolType(root.selectedId) : 1
                                    }
                                    MaterialIcon {
                                        anchors.centerIn: parent
                                        width: 150; height: 150
                                        visible: root.hotbar && root.hotbar.isMaterial(root.selectedId)
                                        materialId: root.selectedId
                                    }
                                }

                                // ── t783 ② 变体切换悬浮面板（预览区下沿内侧，悬浮于 3D 动态图像上）──
                                //   原 t751 面板挂右列 Column 底部：右列内容总高（预览 300 + 名 + 类别 + 变体面板
                                //   ~86px）超视口 366px → Column 不裁剪溢出子项，变体面板纵向溢出越过预览矩形 /
                                //   面板底缘压进 footer 行——footer 是主 Column 后声明兄弟（z 更高），返回按钮
                                //   矩形盖住「已剪毛」toggle 右半 + 其 MouseArea 吃掉点击 = t783 ①「剪羊毛按钮
                                //   点不了且与返回按钮重叠」根因；毛色圆点行 / 边界注则被推出面板底缘残显。
                                //   修法：整组控件迁入预览区作锚定悬浮面板。锚定不参与 Column 布局 → 显隐
                                //   不影响其余 mob 预览（原 Column 跳过 visible:false 语义等价）；右列回归
                                //   300 + 名 + 类别 ≈ 360 ≤ 视口 366 恒不溢出（新增预览侧控件一律走本悬浮
                                //   模式，勿再挂右列底部）。半透明底板（Rectangle rgba alpha，非 Item opacity
                                //   ——后者连带淡化子控件）+ 描边圆角 = t790「浮层与主 UI 视觉区分」同款语言；
                                //   z 10 悬浮于 View3D（后声明兄弟本就在上，显式 z 防后人插层翻序），只占预览
                                //   区下沿 ~1/3，模型中心不盖死（底板半透模型下沿透出）。hover/点击态原样
                                //   保留（toggle hover 底色 / swatch tooltip / 金框选中）。
                                Rectangle {
                                    id: variantPanel
                                    visible: root.selectedMobFromSection === 3 || root.selectedMobFromSection === 12
                                             || root.selectedMobTameable // t920 狼/豹猫驯服态面板
                                    z: 10
                                    width: parent.width - 58 // t922 收窄 58：右下角让位重置缩放按钮（防叠；两段 104px + 8 间距仍容纳）
                                    height: variantCol.implicitHeight + 10
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 6
                                    radius: 8
                                    color: Qt.rgba(0.059, 0.078, 0.102, 0.85) // #0f141a 半透明：模型下沿透出不死黑
                                    border.color: "#3a444f"; border.width: 1
                                    Column {
                                        id: variantCol
                                        width: parent.width - 10
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.top: parent.top
                                        anchors.topMargin: 5
                                        spacing: 5

                                        Text {
                                            text: "变体切换"
                                            color: "#7fae7f"; font.pixelSize: 11; font.bold: true
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                        // 两段 toggle（激活段金边金字 = 选中格高亮同款 #ffd76a；非激活 = 返回按钮蓝字风）。
                                        Row {
                                            visible: root.selectedMobFromSection === 3 || root.selectedMobFromSection === 12
                                            spacing: 8
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            Repeater {
                                                // 段标签随选中生物分流（羊=剪毛两态 / 雪傀儡=戴头两态）。
                                                model: [ root.selectedMobFromSection === 3 ? "未剪羊毛" : "戴南瓜头",
                                                         root.selectedMobFromSection === 3 ? "已剪毛" : "已剪头" ]
                                                delegate: Rectangle {
                                                    width: 104; height: 26; radius: 6
                                                    color: variantSegHover.hovered ? "#2a3a4a" : "#1a2a3a"
                                                    border.color: (index === 1) === root.mobVariantSheared ? "#ffd76a" : "#3a5a7a"
                                                    border.width: (index === 1) === root.mobVariantSheared ? 2 : 1
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData
                                                        color: (index === 1) === root.mobVariantSheared ? "#ffd76a" : "#7fb0e5"
                                                        font.pixelSize: 12
                                                    }
                                                    MouseArea {
                                                        id: variantSegHover
                                                        anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            // t751 写变体态（预览绑定 + 名行后缀即时刷新）。
                                                            if (root.selectedMobFromSection === 3)
                                                                root.sheepSheared = (index === 1)
                                                            else
                                                                root.snowGolemSheared = (index === 1)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        // t920 狼/豹猫驯服态两段（同款金边 toggle 语言）：第一段 驯服/未驯服
                                        //   （镜像 wolfTamed/ocelotTamed）；第二段仅驯服后显示 站立/坐下
                                        //   （镜像 wolfSitting/ocelotSitting——野生态不可命令坐，机制等价 MC 1.0；
                                        //   切「未驯服」时坐段随隐 = 回野生站姿）。
                                        Row {
                                            visible: root.selectedMobTameable
                                            spacing: 8
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            Repeater {
                                                model: [ "未驯服", "已驯服" ]
                                                delegate: Rectangle {
                                                    width: 104; height: 26; radius: 6
                                                    color: tameSegHover.hovered ? "#2a3a4a" : "#1a2a3a"
                                                    border.color: (index === 1) === root.mobTamedPreview ? "#ffd76a" : "#3a5a7a"
                                                    border.width: (index === 1) === root.mobTamedPreview ? 2 : 1
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData
                                                        color: (index === 1) === root.mobTamedPreview ? "#ffd76a" : "#7fb0e5"
                                                        font.pixelSize: 12
                                                    }
                                                    MouseArea {
                                                        id: tameSegHover
                                                        anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.mobTamedPreview = (index === 1)
                                                    }
                                                }
                                            }
                                        }
                                        Row {
                                            visible: root.selectedMobTameable && root.mobTamedPreview
                                            spacing: 8
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            Repeater {
                                                model: [ "站立", "坐下" ]
                                                delegate: Rectangle {
                                                    width: 104; height: 26; radius: 6
                                                    color: sitSegHover.hovered ? "#2a3a4a" : "#1a2a3a"
                                                    border.color: (index === 1) === root.mobSitPreview ? "#ffd76a" : "#3a5a7a"
                                                    border.width: (index === 1) === root.mobSitPreview ? 2 : 1
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: modelData
                                                        color: (index === 1) === root.mobSitPreview ? "#ffd76a" : "#7fb0e5"
                                                        font.pixelSize: 12
                                                    }
                                                    MouseArea {
                                                        id: sitSegHover
                                                        anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.mobSitPreview = (index === 1)
                                                    }
                                                }
                                            }
                                        }
                                        // t920 驯服猫形态注（仅豹猫驯服态）：游戏内三变体随机，图鉴取棕虎斑代表
                                        //   （t963 家猫花纹返修——旧全黑代表退役）。
                                        Text {
                                            visible: root.selectedMobFromSection === 11 && root.mobTamedPreview
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: "驯服后为家猫形态（棕虎斑变体代表 · 游戏内三变体随机）"
                                            color: "#7fae7f"; font.pixelSize: 9
                                        }
                                        // 毛色 swatch 行（仅羊）：16 色与游戏内羊毛方块调色板同源（build_wool.py
                                        //   WOOL_COLORS + 白）；选中格金框（同选中高亮语言）。
                                        Row {
                                            spacing: 2
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            visible: root.selectedMobFromSection === 3
                                            Repeater {
                                                model: root.woolPalette
                                                delegate: Rectangle {
                                                    width: 15; height: 15; radius: 3
                                                    color: modelData.tint
                                                    border.color: root.sheepWoolIndex === index ? "#ffd76a" : "#3a444f"
                                                    border.width: root.sheepWoolIndex === index ? 2 : 1
                                                    HoverHandler {
                                                        cursorShape: Qt.PointingHandCursor
                                                        onHoveredChanged: {
                                                            // 复用格 tooltip 通道（格顶中心 + 名字守卫清除，同 mob 格模式）。
                                                            const nm = "羊毛颜色 · " + modelData.name
                                                            if (hovered) {
                                                                root.hoveredName = nm
                                                                root.hoveredId = -1
                                                                const p = parent.mapToItem(panel, parent.width / 2, 0)
                                                                root.hoveredTipPos = Qt.point(p.x, p.y)
                                                            } else if (root.hoveredName === nm) {
                                                                root.hoveredName = ""
                                                                root.hoveredId = -1
                                                            }
                                                        }
                                                    }
                                                    TapHandler { onTapped: root.sheepWoolIndex = index }
                                                }
                                            }
                                        }
                                        // 毛色诚实边界注（仅羊）：图鉴侧预览着色，游戏内羊染色机制未实现。
                                        Text {
                                            visible: root.selectedMobFromSection === 3
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: "毛色为图鉴预览着色 · 游戏内羊染色待后续"
                                            color: "#7fae7f"; font.pixelSize: 9
                                        }
                                    }
                                }

                                // ── t965 形态切换按钮组悬浮面板（预览区下沿内侧；与 variantPanel 同款
                                //   悬浮语言/同层 z 约定）──
                                //   支持方块「状态」形态切换：耕地干/湿、门+活板门未激活/激活、草丛矮/中/高、
                                //   红石火把亮/灭、动力轨未激活/激活、末地框无眼/有眼、作物生长阶段（Hotbar::
                                //   blockFormStates 单一权威，空表=不支持 → 面板不出现）。编号钮 1 2 3…，
                                //   钮 1 = 表首 = 最普通/放置缺省形态（默认选中）。选中物切换回钮 1
                                //   （onSelectedIdChanged 重置）；预览网格经 blockState → Core 态变即时刷新。
                                //   生物段/生物蛋选中（selectedMobFromSection ≥ 0）不出现（变体归 variantPanel）。
                                //   z 序：预览区浮层永远最前契约（t964 登记）→ 显式 z: 10 同 variantPanel。
                                Rectangle {
                                    id: formPanel
                                    visible: root.selectedMobFromSection < 0 && root.selectedFormStates.length > 0
                                    // t965 预览区浮层最前（同 variantPanel t783 / 重置按钮 t964 约定；
                                    //   独立 z 行 = P-t964(b) 约定钉的计数形态，勿并注释入行）。
                                    z: 10
                                    width: parent.width - 58 // 同 variantPanel 收窄 58 让位右下角重置按钮（两面板互斥显隐恒不叠）
                                    height: formCol.implicitHeight + 10
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 6
                                    radius: 8
                                    color: Qt.rgba(0.059, 0.078, 0.102, 0.85) // #0f141a 半透明（variantPanel 同色）
                                    border.color: "#3a444f"; border.width: 1
                                    Column {
                                        id: formCol
                                        width: parent.width - 10
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.top: parent.top
                                        anchors.topMargin: 5
                                        spacing: 5

                                        Text {
                                            text: "形态切换"
                                            color: "#7fae7f"; font.pixelSize: 11; font.bold: true
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                        // 编号钮组（1 2 3…＝形态序；激活钮金边金字 = 选中格高亮同款语言）。
                                        //   8 钮（作物阶段）最多：8×26+7×4 = 236 ≤ 面板内容宽 254 恒不溢出。
                                        Row {
                                            spacing: 4
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            Repeater {
                                                model: root.selectedFormStates
                                                delegate: Rectangle {
                                                    width: 26; height: 26; radius: 5
                                                    color: formSegHover.hovered ? "#2a3a4a" : "#1a2a3a"
                                                    border.color: index === root.selectedFormIndex ? "#ffd76a" : "#3a5a7a"
                                                    border.width: index === root.selectedFormIndex ? 2 : 1
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: index + 1
                                                        color: index === root.selectedFormIndex ? "#ffd76a" : "#7fb0e5"
                                                        font.pixelSize: 12
                                                    }
                                                    MouseArea {
                                                        id: formSegHover
                                                        anchors.fill: parent; hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: root.selectedFormIndex = index
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // 选中名（中文 §9b）+ 类别标签 + id。
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                color: "#f2f2f2"; font.pixelSize: 18; font.bold: true
                                text: root.selectedIsMob && root.selectedMobDisplay !== "" ? root.selectedMobDisplay
                                    : (root.hotbar && root.selectedId !== 0 ? root.hotbar.nameForBlock(root.selectedId) : "—")
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                color: "#9fb0c0"; font.pixelSize: 12
                                // 生物段 → 「生物 / mobType N」；生物蛋 → 「生物蛋 / 0x…」；其余 → 方块类别 + id。
                                text: root.selectedIsMob && root.selectedMobCategory !== "" ? root.selectedMobCategory
                                    : (root.selectedCategory + "    id: 0x" + (root.selectedId >= 0 ? root.selectedId.toString(16).toUpperCase() : "0"))
                            }

                            // t783 ①②：t751 变体切换面板（剪毛/剪头 toggle + 16 色毛色圆点）原挂本列底部——
                            //   右列内容总高超视口 366px，Column 不裁剪溢出子项 → 面板纵向溢出压进 footer，
                            //   返回按钮（后声明兄弟 z 更高）盖住「已剪毛」toggle 吃掉点击 + 毛色行被推出
                            //   面板底缘。已整组迁入预览区作下沿悬浮面板（见 previewArea 内 variantPanel）；
                            //   右列现内容 = 预览 300 + 名 + 类别 ≈ 360 ≤ 366 恒不溢出——后续新增预览侧
                            //   控件一律走预览区悬浮模式，勿再挂本列（布局不变式）。
                        }
                    }
                }
            }

            // 底部：返回按钮（t617：删悬停名提示条——名字改悬浮窗 tooltip（hover 显示名+简述，同创造背包
            //   t94 模式）；返回按钮保留）。
            Item {
                id: footerCol
                width: parent.width; height: 36
                Rectangle {
                    width: 120; height: 32; radius: 6
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    color: backArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                    border.color: "#3a5a7a"; border.width: 1
                    Text { anchors.centerIn: parent; text: "返回"; color: "#7fb0e5"; font.pixelSize: 13 }
                    MouseArea {
                        id: backArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closed()
                    }
                }
            }
        }

        // t617 悬浮窗 tooltip（同 Inventory t94 itemTip 模式；删底部提示条后名字的唯一呈现）：hover 格顶
        //   上方居中小黑框，显「名字 · 类别简述」；panel 坐标系（hoveredTipPos 由各格 mapToItem(panel)），
        //   边界钳制（左/右不出 panel，顶不足翻到格下方）。
        Rectangle {
            id: hoverTip
            visible: root.hoveredName !== ""
            z: 1000
            width: tipLabel.implicitWidth + 14
            height: tipLabel.implicitHeight + 8
            color: "#101216"
            opacity: 0.94
            border.color: "#3a444f"; border.width: 1
            radius: 3
            x: {
                let px = root.hoveredTipPos.x - width / 2
                if (px < 2) px = 2
                const maxX = panel.width - width - 2
                if (px > maxX) px = maxX
                return px
            }
            y: {
                let py = root.hoveredTipPos.y - height - 6
                if (py < 2) py = root.hoveredTipPos.y + 6 // 顶部不足 → 翻到格下方
                return py
            }
            Text {
                id: tipLabel
                anchors.centerIn: parent
                // 名字 · 类别简述（mob 格类别 = 生物段；物品格 = hoveredCategory 谓词路由；名字对不上物品
                //   段（mob 格）时类别留空防误配）。名字经 hoveredName 单一来源，简述走 hoveredSuffixText
                //   （t633 ①：派生属性依赖 hoveredId —— 名不变而格变时简述也刷新）。
                text: root.hoveredName !== "" ? root.hoveredName + root.hoveredSuffixText : ""
                color: "#f2f2f2"
                font.pixelSize: 12
            }
        }
    }
}
