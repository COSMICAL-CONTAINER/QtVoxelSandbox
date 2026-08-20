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
// pack 切换即时刷新（file:// ↔ qrc:/）；网格图标走 hotbar.iconSourceForBlock（pack 启用时对 LapisOre / 床等映射内方块返 pack item 贴图；工作台 / 熔炉 t537 回退到 t492 二轮的 2D pack 图，pack 启用即覆盖 3D 立方体图标）。
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
    //   选中「生物蛋」材料（0x20F..0x216/0x22C/0x22E）同样直接显示对应 mob 模型。机制等价 MC 1.0 mob 形态，
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
        // t663 ⑥ 剪毛变体条目（同 mobType 异外观）：选中 → 右侧预览显对应剪后形态（shearedAt=true 的游戏内
        //   外观镜像——羊 = 裸肤色 #d6b890、雪傀儡 = 纯雪头（无南瓜））。「所有做好的生物形态都在图鉴里」。
        { mobType: 3, name: "羊（剪毛后）", sheared: true },
        { mobType: 12, name: "雪傀儡（剪头后）", sheared: true }
    ]
    // 生物段选中（mobType；-1 = 未选）。与物品选中互斥（点物品格清空、点生物格不改 selectedId）。
    //   t663 ⑥：selectedMobName 伴选（剪毛变体条目同 mobType 3/12 → 仅 mobType 不可区分；名字作
    //   sheared 判据，selectedMobSheared 派生供预览切换裸羊 / 纯雪头形态）。
    property int selectedMobFromSection: -1
    property string selectedMobName: ""
    // 生物蛋材料 id → mobType（与 PlayerController::placeBlock 生物蛋分流同源；entitymanager.h MobType 同值）。
    //   pig=1/cow=2/sheep=3/shambler=4/bones=5/stalker=6/spider=7/chicken=8/squid=9。非蛋 id → -1（无映射）。
    function mobTypeForEgg(id) {
        switch (id) {
            case 0x20F: return 1; case 0x210: return 2; case 0x211: return 3;
            case 0x213: return 4; case 0x214: return 5; case 0x215: return 6;
            case 0x216: return 7; case 0x22C: return 8; case 0x22E: return 9;
            case 0x246: return 16; // t727 夜行者生物蛋（SpawnEggNightwalkerId；与 PlayerController placeBlock 同源）
            case 0x247: return 17; // t728 燃烬者生物蛋（SpawnEggEmberlingId；与 PlayerController placeBlock 同源）
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
        if (t === 16) return 0.55 // t727 夜行者细长人形高 ~2.6（[-1.40,1.17]）→ 缩到镜头内全身可见
        if (t === 17) return 1.6 // t728 燃烬者悬浮火球头盒 ~0.45（[-0.225,0.225]）→ 放大 1.6 可辨（同蠹虫小体型）
        return 1.0
    }
    // 预览模型垂直居中微调：几何局部原点 = 碰撞中心，mob 身体偏向 -Y → 上提让主体在镜头居中。
    function mobPreviewCentY(t) {
        switch (t) {
            case 1: return 0.16   // 猪 [-0.58, 0.27]
            case 2: return 0.10   // 牛 [-0.55, 0.37]
            case 3: return 0.06   // 羊 [-0.44, 0.33]
            case 4: return 0.06   // 蹒跚者 [-0.90, 0.79]
            case 5: return 0.08   // 骸骨 [-0.90, 0.75]
            case 6: return 0.05   // 潜行者 [-0.90, 0.81]（t616 拉高 ~1.7 格后近对称 → 微上提居中）
            case 7: return 0.08   // 蜘蛛 [-0.30, 0.13]
            case 8: return 0.01   // 鸡 [-0.40, 0.38]
            case 9: return 0.0    // 鱿鱼 [-0.46, 0.58]（mantle 对称居中）
            case 10: return 0.05  // 狼 [-0.42, 0.37]
            case 11: return 0.05  // 豹猫 [-0.40, 0.33]
            case 12: return 0.0   // 雪傀儡 [-0.90, 0.90]（对称居中，无需上提）
            case 13: return 0.30 // 铁傀儡 [-1.20, 0.58]（偏 -Y，上提 0.30 居中主体）
            case 14: return -0.30 // 蠹虫 [-0.15, 0.11]（小虫贴地 → 下压 0.30 进镜头中心）
            case 16: return 0.13 // t727 夜行者 [-1.40, 1.17]（偏 -Y 0.12；×0.55 缩后上提居中主体）
            case 17: return 0.0  // t728 燃烬者悬浮头盒 [-0.225, 0.225]（近对称居中，无需调整）
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
    readonly property string selectedMobTexSource: root.selectedMobPackSrc !== "" ? root.selectedMobPackSrc
        : root.mobFallbackTexture(root.selectedMobType)
    // 选中 mob 显示名：生物段选中 → selectedMobName（伴选态；剪毛变体带「（剪毛后）」后缀）；否则按
    //   mobType 反查 mobModel 表（生物蛋路径，首条命中 = 常规形态名）。t663 ⑥ 同 mobType 双条目可区分。
    readonly property string selectedMobDisplay: root.selectedMobFromSection >= 0 && root.selectedMobName !== ""
        ? root.selectedMobName
        : mobNameForType(root.selectedMobType)
    // mobType → mobModel 表首条命中名（同 mobType 双条目时常规形态在前，剪毛变体在后 → 生物蛋路径
    //   反查恒得常规形态名）。t663 ⑥。
    function mobNameForType(t) {
        for (let i = 0; i < root.mobModel.length; ++i)
            if (root.mobModel[i].mobType === t)
                return root.mobModel[i].name
        return ""
    }
    // t663 ⑥ 剪毛变体态（选中条目带 sheared 标记——羊裸肤色 / 雪傀儡纯雪头；生物蛋路径恒 false）。
    readonly property bool selectedMobSheared: {
        if (root.selectedMobFromSection < 0) return false
        for (let i = 0; i < root.mobModel.length; ++i)
            if (root.mobModel[i].mobType === root.selectedMobFromSection
                && root.mobModel[i].name === root.selectedMobName)
                return root.mobModel[i].sheared === true
        return false
    }
    readonly property string selectedMobCategory: {
        if (root.selectedMobFromSection >= 0) return "生物 / mobType " + root.selectedMobFromSection
        const t = root.hotbar ? root.mobTypeForEgg(root.selectedId) : -1
        if (t >= 0) return "生物蛋 / 0x" + root.selectedId.toString(16).toUpperCase()
        return ""
    }

    // 当前选中物 id（默认首个；Component.onCompleted 兜底）。
    property int selectedId: 0
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
    //   （叠加在 -22° 基倾上，Y 拖上/下看顶/底）；松手 resume 动画把 spinAngle lerp 回自转相位（无跳变）。
    //   yaw 由 spinAngle 本身承载（拖拽水平位移直接写入 spinAngle，自转从松手角度继续）。
    property bool previewDragging: false
    property real userPitch: 0

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
        if (!previewDragging && visible && (selectedIsCube || selectedIsMob)) {
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
                                                //   PNG（pack 关 / 无该 mob 头区 → 空串 → 回退体色 swatch）。
                                                //   Image source 触碰 packActive（表达式形式防 AOT 死代码消除）。
                                                Item {
                                                    width: 26; height: 26
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    // 头像命中 → 显 Image；否则显体色方块（原 swatch）。
                                                    property string headSrc: {
                                                        const _r = root.packActive
                                                        return (_r && root.resourcePack) ? root.resourcePack.mobHeadIconSource(modelData.mobType) : ""
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
                                            // t663 ⑥：伴选名字（同 mobType 的剪毛变体条目靠名字区分）。
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
                                                    // 触碰 packActive → pack 切换图标刷新（iconSourceForBlock 对 pack 映射内方块返 pack item 贴图；工作台 / 熔炉 t537 回退到 2D pack 图，pack 启用即覆盖 3D 立方体）。
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

                            // 预览区（View3D 与大图标互斥）。
                            Item {
                                width: parent.width; height: 300

                                // t599 3D 预览鼠标拖拽旋转（用户「一直自动旋转，能不能拖拽看」）：在自动旋转基础上
                                //   加 DragHandler —— 按住拖时暂停自转（previewDragging → NumberAnimation running=false），
                                //   水平位移增量写 spinAngle（yaw，度；1px = 0.6° 手感系数）、垂直位移增量累计
                                //   userPitch（pitch，度；上拖看顶 / 下拖看底，限 ±60° 防过翻）；松手 pitch 由
                                //   resumePitchAnim 平滑归零（400ms OutCubic 回标准 -22° 3/4 视角），yaw 由自转从当前
                                //   角度无缝续转（NumberAnimation on spinAngle 重启从当前值推进，无跳变）。
                                //   方块与生物 3D 预览共用（同一 spinAngle/userPitch）；enabled 限定 3D 预览可见时
                                //   （大图标态不抢手势；左侧网格在其外不受影响）。translation 是只读累计值 →
                                //   lastX/lastY 记上次值取增量（拖拽结束归零基准，下次拖从 0 差起）。
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
                                        root.userPitch = Math.max(-60, Math.min(60, root.userPitch - dy * 0.6))
                                    }
                                }
                                // 整立方方块 → 内嵌 View3D 旋转 BlockCube。
                                // 渲染可见性铁律（lessons-learned）：clipNear≈0.05（默认 10 会剪掉单位立方）+
                                //   PrincipledMaterial.NoLighting（默认 lit 在本工程不渲染）+ alphaMode Mask（leaves 等带
                                //   alpha 贴图 cutout 正确）。BlockCube 复用既有几何（同掉落实体 / 手持立方已验证路径）。
                                View3D {
                                    id: cubeView
                                    anchors.fill: parent
                                    visible: root.selectedIsCube || root.selectedIsMob
                                    // View3D 默认 Offscreen 渲染（FBO 合成），嵌面板预览正确。
                                    PerspectiveCamera {
                                        position: Qt.vector3d(0, 0, 3.2)
                                        clipNear: 0.05
                                        clipFar: 100
                                        fieldOfView: 45
                                    }
                                    Model {
                                        // 仅整立方方块时显示（选中 mob / 生物蛋 → 只显 MobModel，两模型互斥不叠渲染）。
                                        visible: root.selectedIsCube && !root.selectedIsMob
                                        // blockId 绑选中物；不设 world → BlockCube 顶点色恒白（全亮，无天光遮蔽，预览纯净）。
                                        geometry: BlockCube { blockId: root.selectedId }
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
                                                mobType: root.selectedMobType
                                                packTextured: root.selectedMobPackSrc !== ""
                                            }
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                // pack 关且无程序贴图（bones/stalker/spider）→ null + 纯色 baseColor。
                                                // t597 修：渲染 = baseColorMap × baseColor —— pack 贴图在身时 baseColor 用白
                                                //   （贴图原色完整透出，同 Main.qml t597 修法）；mobFallbackColor 是 pack 关的
                                                //   纯色体色（stalker #3a5a3a / spider #2a1a1a 均暗色），乘上 pack 贴图会把
                                                //   贴图压暗近黑（图鉴预览同样「暗淡/无贴图」观感）。
                                                // t663 ⑥ 剪毛羊变体：选「羊（剪毛后）」→ 去贴图改裸肤色 #d6b890
                                                //   （同 Main.qml 裸羊 delegate；pack 贴图是毛层，裸羊不走它）。
                                                // t663 ⑥ 羊毛层 Mask（图鉴羊「不对」回归修复）：sheep_fur.png 头前/体侧
                                                //   有透明镂空（毛层透出下层），Main.qml t633 ③ 已加 Mask + 0.5 但图鉴预览
                                                //   漏带 → pack 开时图鉴羊透明区渲成黑块（= 用户「羊模型 wrong now」根因）。
                                                //   仅 pack 命中时 Mask（程序 mob_sheep.png 全不透明，Mask 对它无影响）。
                                                baseColorMap: root.selectedMobTexSource !== "" && !root.selectedMobSheared ? mobPrevTex : null
                                                baseColor: root.selectedMobSheared && root.selectedMobType === 3 ? "#d6b890"
                                                    : (root.selectedMobTexSource !== "" ? "#ffffff" : root.mobFallbackColor(root.selectedMobType))
                                                alphaMode: root.selectedMobType === 3 && root.selectedMobPackSrc !== "" && !root.selectedMobSheared
                                                           ? PrincipledMaterial.Mask : PrincipledMaterial.Opaque
                                                alphaCutoff: 0.5
                                            }
                                        }
                                        // t663 ⑥ 羊眼 overlay（镜像 Main.qml t633 ③：sheep_fur.png 毛层头前无脸 →
                                        //   眼恒显；颈枢 Node 绑 headPitch（图鉴静态 0 → 直立即可，直接定位））。
                                        //   裸羊变体同显（裸肤色无脸）。
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.055, 0.10, -0.35)
                                            scale: Qt.vector3d(0.055, 0.055, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.055, 0.10, -0.35)
                                            scale: Qt.vector3d(0.055, 0.055, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.055, 0.10, -0.36)
                                            scale: Qt.vector3d(0.028, 0.028, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 3
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.055, 0.10, -0.36)
                                            scale: Qt.vector3d(0.028, 0.028, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
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
                                        //   t663 ⑥：剪头变体（selectedMobSheared）→ 南瓜头隐藏、下方纯雪头接管。
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
                                        // t663 ⑥/⑦ 剪头后纯雪头 + 柔灰刻面眼嘴（镜像 Main.qml t663 ⑦ 游戏内形态：
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
                                    visible: !root.selectedIsCube && !root.selectedIsMob
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
