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
        { mobType: 17, name: "燃烬者" } // t728 烈焰人→燃烬者（§9 改名；生物图鉴条目 + 生物蛋 0x247 映射）
        // t751 条目合并：t663 ⑥ 曾把剪毛变体拆成独立条目（羊（剪毛后）/雪傀儡（剪头后）——同 mobType 双条
        //   仅靠名字区分）。现改为**每生物单条**+右侧底部变体切换按钮（见预览区下方 variantPanel）：
        //   羊 = 剪毛/未剪 toggle + 毛色 swatch（仅羊有颜色变体）、雪傀儡 = 戴头/剪头 toggle。
        //   变体态存组件级属性（sheepSheared / snowGolemSheared / sheepWoolIndex），切换即时刷新预览。
    ]
    // 生物段选中（mobType；-1 = 未选）。与物品选中互斥（点物品格清空、点生物格不改 selectedId）。
    //   t751：剪毛变体条目已合并（每生物单条），selectedMobName 仅作显示名伴选（不再承担 sheared 判据——
    //   变体态改由下方组件级属性承载，底部变体面板切换）。
    property int selectedMobFromSection: -1
    property string selectedMobName: ""
    // ── t751 变体状态（组件级；底部变体面板读写，预览绑定消费 → 切换即时刷新）──
    // 羊剪毛态（false=毛茸羊毛形态 / true=裸肤残毛形态；镜像游戏内 shearedAt 双态）。
    property bool sheepSheared: false
    // 雪傀儡剪头态（false=戴南瓜头 / true=纯雪头 + 刻面五官；镜像游戏内 snowGolemShearedAt 双态）。
    property bool snowGolemSheared: false
    // 羊毛色索引（0=白色不着色；1..15=染色 tint）。调色板与游戏内羊毛方块 16 色**同源**
    //   （tools/build_wool.py WOOL_COLORS，白色=默认羊毛色 → 预览零 tint）。诚实边界：游戏内**没有**
    //   染色羊机制（EntityManager 羊实体无毛色字段），图鉴侧做**预览着色**（贴图 tint 乘色），
    //   游戏内羊毛染色留待后续（下方毛色行注明）。
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
            case 9: return 0.0    // 鱿鱼 [-0.46, 0.58]（mantle 对称居中；t750 起 pack 关省略尖顶 → 顶 0.32，mantle 仍近对称）
            case 10: return 0.05  // 狼 [-0.42, 0.37]
            case 11: return 0.05  // 豹猫 [-0.40, 0.33]
            case 12: return 0.0   // 雪傀儡 [-0.90, 0.90]（对称居中，无需上提）
            case 13: return 0.30 // 铁傀儡 [-1.20, 0.58]（偏 -Y，上提 0.30 居中主体）
            case 14: return -0.30 // 蠹虫 [-0.15, 0.14]（t750 分节重做：背脊甲板顶 0.14；小虫贴地 → 下压 0.30 进镜头中心）
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
    // 选中 mob 显示名：生物段选中 → selectedMobName + 变体后缀（t751：剪毛/剪头/毛色态随底部变体面板
    //   切换刷新）；否则按 mobType 反查 mobModel 表（生物蛋路径，t751 合并后每型单条恒得常规形态名）。
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
        return ""
    }
    // t749 剪毛羊 pack 本体层源（pack 启用且包内有 sheep/sheep.png → file:///；否则空串走程序贴图）。
    //   与 mobTextureSource(3) 的毛层合成路径分开——剪毛态要裸身 + 真脸（本体层），不要毛身。触碰 packActive。
    readonly property string sheepBodyPackSrc: {
        const _r = root.packActive
        return _r >= 0 && root.resourcePack ? root.resourcePack.entitySource("sheep_body") : ""
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
    // t749 剪毛羊预览贴图（双态）：pack 命中本体层 sheep/sheep.png（裸身 + 真脸 box-UV 布局）；否则程序生成
    //   mob_sheep_sheared.png（裸肤 + 残羊毛块全脸 UV，build_mob.py t749 新增）。替代旧纯色 #d6b890——
    //   用户「被剪羊毛的羊贴图纯色错误，应有裸皮 + 残毛造型」。
    Texture {
        id: mobShearedTex
        source: root.sheepBodyPackSrc !== "" ? root.sheepBodyPackSrc
                                             : "qrc:/textures/mob_sheep_sheared.png"
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
                                            //   显示名伴选，变体不再拆条目——改底部变体面板切换）。
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
                                                // t749 剪毛羊 pack 本体层是 box-UV 布局 → 同样开 T 字展开
                                                //   （程序 mob_sheep_sheared 是全脸 UV → 保持 false）。
                                                packTextured: root.selectedMobPackSrc !== ""
                                                    || (root.selectedMobSheared && root.selectedMobType === 3
                                                        && root.sheepBodyPackSrc !== "")
                                            }
                                            materials: PrincipledMaterial {
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
                                                    // t751 羊毛色预览着色：毛茸态 + 非白色 → 染色 tint 乘贴图
                                                    //   （白底羊毛贴图 × 染色 = 染色羊毛，调色板与羊毛方块 16 色同源）。
                                                    //   诚实边界：游戏内无染色羊机制，图鉴侧仅预览着色（近整模乘色，
                                                    //   脸区随乘为近似）；裸肤（剪毛后）不染色（对齐 MC 剪后裸肤无色）。
                                                    //   生物蛋路径不 tint（变体仅生物段浏览，见 sheepWoolIndex 注）。
                                                    if (root.selectedMobFromSection === 3 && !root.sheepSheared
                                                        && root.sheepWoolIndex > 0)
                                                        return root.woolPalette[root.sheepWoolIndex].tint
                                                    return (root.selectedMobSheared && root.selectedMobType === 3)
                                                        || root.selectedMobTexSource !== "" ? "#ffffff"
                                                        : root.mobFallbackColor(root.selectedMobType)
                                                }
                                                // t663 ⑥ 羊毛层 Mask（图鉴羊「不对」回归修复）：合成贴图（t749）全不透明 →
                                                //   Mask 对它无影响；仅 pack 命中且非剪毛态保留（防御异形包毛层镂空）。
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
                                        // ── t750 图鉴 3D 预览对齐游戏内模型（六模型修复）──
                                        // 抉择「共享组件 vs 浏览器复刻」：游戏内正确拼装深嵌 Main.qml mobHost
                                        //   delegate（绑 entityManager 索引族 walkPhase/hurtFlash/rage/sit…），
                                        //   抽共享组件须把十余条实体绑定参数化且回归面覆盖全部 17 种 mob——成本 /
                                        //   风险远超收益；按任务行「评估成本」走**浏览器侧 1:1 复刻**（同 t598 傀儡
                                        //   头 / t616 弓 + 鸡腿 / t663 羊眼先例），各块注明 Main.qml 锚点互指。几何级
                                        //   差异（鱿鱼尖顶 / 蠹虫分节）已下沉 MobModel 共享层（mobmodel.cpp t750，
                                        //   两侧同源无双份维护）。
                                        // t750 ① 鱿鱼眼（2 颗黑点；镜像 Main.qml squid delegate t730 终审 L5：
                                        //   pack 命中时隐藏——包贴图前脸自带眼纹素，几何黑点再叠成双层眼；pack 关
                                        //   显示（程序贴图 mob_squid 不画眼，眼全靠几何盒））。「头顶小鱿鱼」叠加层
                                        //   已在几何层修（pack 关省略尖顶盒，见 mobmodel.cpp t750 注释）。
                                        Model {
                                            visible: root.selectedMobType === 9 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.10, 0.10, -0.29)
                                            scale: Qt.vector3d(0.03, 0.03, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 9 && root.selectedMobPackSrc === ""
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.10, 0.10, -0.29)
                                            scale: Qt.vector3d(0.03, 0.03, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        // t750 ② 狼尾（修复「像兔子」——缺尾缺眼的灰身立耳四足读作兔；镜像
                                        //   Main.qml wolfTailPivot：尾根 (0,0.16,0.38) + 竖细盒毛色 0.55 灰；图鉴
                                        //   静态取满血竖起 35°（游戏内随血量 35°..140°）。
                                        Node {
                                            visible: root.selectedMobType === 10
                                            position: Qt.vector3d(0, 0.16, 0.38)
                                            eulerRotation.x: 35
                                            Model {
                                                geometry: UnitCube {}
                                                position: Qt.vector3d(0, 0.10, 0)
                                                scale: Qt.vector3d(0.06, 0.20, 0.06)
                                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8c8c8c" }
                                            }
                                        }
                                        // t750 ② 狼眼（2 颗深点；镜像 Main.qml wolf delegate：头心
                                        //   (0,0.12,-0.52) 半 (0.14,0.15,0.18) → 眼贴头前 (±0.08,0.16,-0.71)）。
                                        Model {
                                            visible: root.selectedMobType === 10
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.08, 0.16, -0.71)
                                            scale: Qt.vector3d(0.04, 0.05, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 10
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.08, 0.16, -0.71)
                                            scale: Qt.vector3d(0.04, 0.05, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        // t750 ③ 豹猫眼（修复「没有脸」；镜像 Main.qml ocelot delegate：头心
                                        //   (0,0.12,-0.46) 半 (0.11,0.12,0.14) → 眼贴头前 (±0.07,0.15,-0.61)）。
                                        Model {
                                            visible: root.selectedMobType === 11
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(-0.07, 0.15, -0.61)
                                            scale: Qt.vector3d(0.035, 0.04, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        Model {
                                            visible: root.selectedMobType === 11
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0.07, 0.15, -0.61)
                                            scale: Qt.vector3d(0.035, 0.04, 0.02)
                                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                        }
                                        // t750 ④ 夜行者头前五官层（修复「黑影无脸」；镜像 Main.qml nwHead：
                                        //   细长人形几何本体走共享 MobModel mobType 16，图鉴此前漏此层 = 无脸黑影）：
                                        //   眼发光层头前 (0,0.95,-0.21) 铺竖眼贴图（pack 命中 enderman_eyes 切包内
                                        //   竖眼；Mask 裁透明底）+ 嘴非激怒态淡显暗唇（opacity 0.15，同游戏内静态）。
                                        Model {
                                            visible: root.selectedMobType === 16
                                            geometry: UnitCube {}
                                            position: Qt.vector3d(0, 0.95, -0.21)
                                            scale: Qt.vector3d(0.30, 0.12, 0.03)
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
                                            position: Qt.vector3d(0, 0.78, -0.20)
                                            scale: Qt.vector3d(0.16, 0.05, 0.03)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial.NoLighting
                                                opacity: 0.15
                                                baseColor: "#140f18" // 近黑紫（嘴缝/口腔）
                                            }
                                        }
                                        // t750 ⑤ 燃烬者环绕竖棒（修复缺棒「多余白色身体」观感；1:1 镜像
                                        //   Main.qml emberRods：4 根烟灰橙竖棒半径 0.52、2200ms/圈绕 Y 匀速旋转
                                        //   ——单悬浮头 + 旋转棒是游戏内 t728 标志形态）。悬浮 bob 属游戏内游动
                                        //   动画，图鉴自转已给动态 → 不复刻（观感锚点是旋转棒）。
                                        Node {
                                            visible: root.selectedMobType === 17
                                            property real spin: 0
                                            NumberAnimation on spin { from: 0; to: 360; duration: 2200; loops: Animation.Infinite }
                                            eulerRotation.y: spin
                                            Repeater {
                                                model: 4
                                                Model {
                                                    geometry: UnitCube {}
                                                    property real ang: index * 90
                                                    position: Qt.vector3d(Math.cos(ang * 0.0174533) * 0.52, 0,
                                                                          Math.sin(ang * 0.0174533) * 0.52)
                                                    scale: Qt.vector3d(0.09, 1.15, 0.09)
                                                    materials: PrincipledMaterial {
                                                        lighting: PrincipledMaterial.NoLighting
                                                        baseColor: "#e8b030" // 烟灰橙黄（同游戏内 / 蛋生成色）
                                                    }
                                                }
                                            }
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
                                        //   t663 ⑥ → t751：剪头变体（底部「已剪头」toggle → selectedMobSheared）
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
                                        // t663 ⑥/⑦ → t751 剪头后纯雪头 + 柔灰刻面眼嘴（底部「已剪头」toggle 触发；
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

                            // ── t751 变体切换面板（右侧浏览界面底部；仅选中羊(3)/雪傀儡(12)生物段条目时显示）──
                            //   羊：剪毛/未剪两段 toggle + 16 色毛色 swatch（仅羊有颜色变体）；雪傀儡：戴南瓜头/
                            //   剪头两段 toggle。QML Column 布局器跳过 visible:false 子项 → 选其他 mob 时本面板
                            //   不占位（其余 mob 的右侧预览观感零变化）。变体态见组件级 sheepSheared/
                            //   snowGolemSheared/sheepWoolIndex（切换 → selectedMobSheared NOTIFY → 预览即时刷新）。
                            Column {
                                width: parent.width
                                spacing: 6
                                visible: root.selectedMobFromSection === 3 || root.selectedMobFromSection === 12

                                Text {
                                    text: "变体切换"
                                    color: "#7fae7f"; font.pixelSize: 11; font.bold: true
                                    anchors.horizontalCenter: parent.horizontalCenter
                                }
                                // 两段 toggle（激活段金边金字 = 选中格高亮同款 #ffd76a；非激活 = 返回按钮蓝字风）。
                                Row {
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
