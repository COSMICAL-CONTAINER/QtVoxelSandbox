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
// pack 切换即时刷新（file:// ↔ qrc:/）；网格图标走 hotbar.iconSourceForBlock（pack 启用时对 LapisOre 等映射内方块返 pack item 贴图；t492 后工作台 / 熔炉已不进该映射，恒走 3D 立方体图标）。
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
    //   本任务仅保结构可扩展（加一行 + mobFallback* 分支 + mobEntityMap 条目即接入）。
    readonly property var mobModel: [
        { mobType: 1, name: "猪" }, { mobType: 2, name: "牛" }, { mobType: 3, name: "羊" },
        { mobType: 4, name: "蹒跚者" }, { mobType: 5, name: "骸骨" }, { mobType: 6, name: "潜行者" },
        { mobType: 7, name: "蜘蛛" }, { mobType: 8, name: "鸡" }
    ]
    // 生物段选中（mobType；-1 = 未选）。与物品选中互斥（点物品格清空、点生物格不改 selectedId）。
    property int selectedMobFromSection: -1
    // 生物蛋材料 id → mobType（与 PlayerController::placeBlock 生物蛋分流同源；entitymanager.h MobType 同值）。
    //   pig=1/cow=2/sheep=3/shambler=4/bones=5/stalker=6/spider=7/chicken=8/squid=9。非蛋 id → -1（无映射）。
    function mobTypeForEgg(id) {
        switch (id) {
            case 0x20F: return 1; case 0x210: return 2; case 0x211: return 3;
            case 0x213: return 4; case 0x214: return 5; case 0x215: return 6;
            case 0x216: return 7; case 0x22C: return 8; case 0x22E: return 9;
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
        }
        return ""
    }
    function mobFallbackColor(t) {
        switch (t) {
            case 5: return "#d8d8d0"  // Bones 骨白（与 Main.qml 蛋生成色同源）
            case 6: return "#3a5a3a"  // Stalker 暗绿
            case 7: return "#2a1a1a"  // Spider 暗黑
        }
        return "#ffffff"
    }
    // 预览模型缩放：本任务 8 种 mob 均 1.0；I3 雪/铁傀儡（几何高 1.2-1.4，撑满 3.2 镜头）需 0.75。
    function mobPreviewScale(t) {
        if (t === 12 || t === 13) return 0.75
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
            case 6: return 0.05   // 潜行者 [-0.90, 0.81]
            case 7: return 0.08   // 蜘蛛 [-0.30, 0.13]
            case 8: return 0.01   // 鸡 [-0.40, 0.38]
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
    readonly property string selectedMobName: {
        for (let i = 0; i < root.mobModel.length; ++i)
            if (root.mobModel[i].mobType === root.selectedMobType)
                return root.mobModel[i].name
        return ""
    }
    readonly property string selectedMobCategory: {
        if (root.selectedMobFromSection >= 0) return "生物 / mobType " + root.selectedMobFromSection
        const t = root.hotbar ? root.mobTypeForEgg(root.selectedId) : -1
        if (t >= 0) return "生物蛋 / 0x" + root.selectedId.toString(16).toUpperCase()
        return ""
    }

    // 当前选中物 id（默认首个；Component.onCompleted 兜底）。
    property int selectedId: 0
    // 当前悬停物中文名（网格 hover 时更新；§9 override (b) 中文通用词）。
    property string hoveredName: ""

    // 旋转角度（预览方块绕 Y 自转；仅 selectedIsCube 时跑）。
    property real spinAngle: 0

    // 选中物是否「整立方方块」（走 View3D 旋转预览）。路由谓词与 Main.qml 掉落实体 / 手持立方同源：
    //   排除 火把(13) / 异形段(isPartialBlock) / cross 段(isCrossBlock) / 工具段(isTool) / 材料·护甲段(isMaterial)。
    //   这些非整立方走大图标分支（iconSourceForBlock / ToolIcon / MaterialIcon）。
    readonly property bool selectedIsCube: root.hotbar && root.selectedId !== 0 && root.selectedId !== 13
        && !root.hotbar.isPartialBlock(root.selectedId)
        && !root.hotbar.isCrossBlock(root.selectedId)
        && !root.hotbar.isTool(root.selectedId)
        && !root.hotbar.isMaterial(root.selectedId)

    // 选中物类别标签（§9 通用词；§2 分层：谓词经 Hotbar VM）。
    readonly property string selectedCategory: {
        if (!root.hotbar || root.selectedId === 0) return ""
        if (root.hotbar.isTool(root.selectedId)) return "工具"
        if (root.hotbar.isMaterial(root.selectedId)) return "材料 / 护甲"
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

    // 预览方块自转动画：8s 一圈，仅在面板可见且选中整立方 / 生物时跑（省 GPU）。
    NumberAnimation on spinAngle {
        from: 0; to: 360; duration: 8000; loops: Animation.Infinite
        running: root.visible && (root.selectedIsCube || root.selectedIsMob)
    }

    // 生物预览贴图（MobModel baseColorMap）：source 随选中 mob 切换（pack → pack entity 贴图；否则程序生成
    //   mob_*.png / 空）。空 source（bones/stalker/spider pack 关）→ baseColorMap:null + 纯色（同 Main.qml 潜行者模式）。
    Texture {
        id: mobPrevTex
        source: root.selectedMobTexSource
        generateMipmaps: false
    }

    // ── 尺寸常量 ──
    readonly property int paletteCols: 9
    readonly property int cellSize: 42

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
                            contentHeight: mobCol.height
                            flickableDirection: Flickable.VerticalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

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
                                                // 体色 swatch：pack 关纯色回退的视觉提示（有程序贴图的 mob 用主色近似）。
                                                Rectangle {
                                                    width: 22; height: 22; radius: 4
                                                    color: root.mobFallbackColor(modelData.mobType)
                                                    border.color: "#4a5a4a"; border.width: 1
                                                    anchors.horizontalCenter: parent.horizontalCenter
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
                                                    if (hovered) root.hoveredName = modelData.name
                                                    else if (root.hoveredName === modelData.name) root.hoveredName = ""
                                                }
                                            }
                                            TapHandler { onTapped: root.selectedMobFromSection = modelData.mobType }
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
                                                    // 触碰 packActive → pack 切换图标刷新（iconSourceForBlock 对 pack 映射内方块返 pack item 贴图；t492 工作台 / 熔炉已移出，恒 3D 立方体）。
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
                                                    if (hovered) root.hoveredName = root.hotbar.nameForBlock(modelData)
                                                    else if (root.hoveredName === root.hotbar.nameForBlock(modelData)) root.hoveredName = ""
                                                }
                                            }
                                            // 点物品格 → 选中该物品 + 清空生物段选中（互斥；生物蛋 id 经
                                            //   mobTypeForEgg 映射回 mob → 右侧仍显 3D 模型，但类别标签走「生物蛋」）。
                                            TapHandler { onTapped: { root.selectedId = modelData; root.selectedMobFromSection = -1 } }
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
                                        // 固定 -22° X 倾（见顶面）+ Y 自转（spinAngle）；-35° 基偏给 3/4 视角。
                                        eulerRotation: Qt.vector3d(-22, root.spinAngle - 35, 0)
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
                                    Model {
                                        visible: root.selectedIsMob
                                        geometry: MobModel {
                                            mobType: root.selectedMobType
                                            packTextured: root.selectedMobPackSrc !== ""
                                        }
                                        position: Qt.vector3d(0, root.mobPreviewCentY(root.selectedMobType), 0)
                                        scale: Qt.vector3d(root.mobPreviewScale(root.selectedMobType),
                                                          root.mobPreviewScale(root.selectedMobType),
                                                          root.mobPreviewScale(root.selectedMobType))
                                        eulerRotation: Qt.vector3d(-22, root.spinAngle - 35, 0)
                                        materials: PrincipledMaterial {
                                            lighting: PrincipledMaterial.NoLighting
                                            // pack 关且无程序贴图（bones/stalker/spider）→ null + 纯色 baseColor。
                                            baseColorMap: root.selectedMobTexSource !== "" ? mobPrevTex : null
                                            baseColor: root.mobFallbackColor(root.selectedMobType)
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
                                text: root.selectedIsMob && root.selectedMobName !== "" ? root.selectedMobName
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

            // 底部：悬停名提示 + 返回按钮。
            Item {
                id: footerCol
                width: parent.width; height: 36
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    color: "#7d8893"; font.pixelSize: 12
                    text: root.hoveredName !== "" ? "悬停：" + root.hoveredName : "提示：左侧网格点击任一物品即可预览"
                }
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
    }
}
