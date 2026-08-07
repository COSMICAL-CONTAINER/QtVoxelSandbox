#ifndef CHUNKGEOMETRY_H
#define CHUNKGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QVector3D>

#include <QtQml/qqml.h>

#include "world.h" // Q_PROPERTY(World*) + chunks() 路由（World 层只读）

// 体素区块几何（纯视图，per-chunk，t03）：每个 ChunkGeometry 负责一个 chunk（cx,cz）的
// 局部 culled meshing。从注入的 World（经 blockAt 跨 chunk 路由）取体素 + 邻居判定，
// 只生成「邻居为空气」的可见面。顶点为 chunk 局部坐标；QML 把 Model 摆到 chunk 世界起点
// (cx*16, 0, cz*16) 完成世界定位。
//
// dirty 驱动重建（dev-spec t03 验收）：setBlock 经 ChunkManager 标目标 + 边界邻接 chunk 脏；
// worldChanged → onWorldChanged() 检 myChunk()->dirty()，**仅脏 chunk 重建并清脏**，非脏
// chunk 不重建（rebuild 次数 = dirty chunk 数）。跨 chunk 边界面剔除走 world.blockAt
//（相邻两 chunk 实体→共边面剔除无夹层；一侧空气→画出；越界=空气）→ 3×3 无缝。
//
// 分层（PLAN §2）：本类属 Renderer，**只读** World/ChunkManager（blockAt/isSolid + dirty 标记），
// 不反向写栅格。不变量 B 形：mesh 数据 own/move-only/不可变（为后续线程化留形）。
//
// 方块 id 与每面瓦片映射见 BlockRegistry（单一权威）；本类只读它做网格化。
// 图集瓦片顺序（须与 tools/build_atlas.py 一致）：
//   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
//   5=cobble 6=log_top 7=log_side 8=planks 9=leaves（N=10）
class ChunkGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChunkGeometry)
public:
    // t155 重建触发源（可观测性）：区分「编辑即时重建」与「太阳步进重建」，供日志核对破/放后是否
    //   <1 帧即时刷新（dev-spec t155 验收「破块贴图立刻消失，无 3-4s 残留」）。dirty = 编辑 / 初次
    //   加载触发（onWorldChanged，dirty-gated，**同步**于 setBlock）；sun = 太阳跨步触发（setSunDir，
    //   绕 dirty 全量重算顶点光，t155 编辑活跃期被 WorldClock 节流跳过）；water = 水段开关切换。
    enum class RebuildReason { Dirty, Sun, Water };
    Q_ENUM(RebuildReason)
    Q_PROPERTY(World *world READ world WRITE setWorld NOTIFY worldChanged)
    Q_PROPERTY(int cx READ cx WRITE setCx NOTIFY cxChanged)
    Q_PROPERTY(int cz READ cz WRITE setCz NOTIFY czChanged)
    // t123 动态太阳光照（太阳方向单位向量，由 WorldClock 派生、经 QML 绑定注入）。t151 真光场后顶点色
    //   基底改采 per-voxel flood-fill 光场；t153 PCF 软影复用本 sunDir —— sunShadowAt 据此沿太阳水平方向
    //   步进采样 heightmap 正交深度图、压暗天光分量。设值触发 buildMesh（顶点色 PCF 软影需随太阳重算）。
    //   分层（PLAN §2）：只接收「裸 QVector3D」（不 include worldclock.h、不依赖 Game 层时间源），保持
    //   Renderer→向下 依赖方向。
    Q_PROPERTY(QVector3D sunDir READ sunDir WRITE setSunDir NOTIFY sunInputChanged)
    // t148 水渲染分流：waterOnly=true → 本几何只网格化 Water 方块（独立透明段，Main.qml 用 opacity=0.7
    //   材质渲染）；waterOnly=false（默认）→ 只网格化非水方块（地形 / 异形 / ...，跳过 Water，避免与水段
    //   重复绘制 + 水被当不透明地形误渲）。两段共用同一 culled meshing 主体 + 顶点色光照管线，仅：
    //   (a) 选块（水 vs 非水）；(b) 邻居面剔除规则不同（水段额外剔 nb==Water，水-水面互剔；见 .cpp）。
    //   一个 chunk 由两个 ChunkGeometry 实例渲染（地形段 + 水段），各自绑 QML Model + 材质。
    Q_PROPERTY(bool waterOnly READ waterOnly WRITE setWaterOnly NOTIFY waterOnlyChanged)
    // t326 cross cutout 渲染分流（机制等价 waterOnly 的「半透独立段」）：cutoutOnly=true → 本几何只网格化
    //   cross 广告牌方块（草丛 / 小麦作物 / 树苗，isCrossBillboard），跳过 partial 盒体与立方面。
    //   必要性：cross 贴图带 alpha 透明底（草叶 / 树苗本体 alpha=255、底 alpha=0），须 alpha-test cutout 才
    //   显透明间隙。但 PrincipledMaterial 在本 D3D11 后端 **alphaCutoff 仅在 opacity<1（透明通道）下生效**
    //   （见 lessons-learned alpha 契约条 / crack 材质注释）；地形段材质 opacity=1（不透明）→ alpha 被忽略 →
    //   透明底当不透明显 → cross 显成两片实心板（用户「草丛挡住视线」）。地形段不能整体降 opacity（全地形
    //   半透 + 透明通道无深度写 = 灾难性 z-fight），故把 cross 拆进独立段、配 opacity:0.99+alphaCutoff:0.5
    //   材质（沿用 torch/crack/MaterialIcon 的 alpha-test 契约）。一个 chunk 由三个 ChunkGeometry 实例渲染
    //   （地形段 + 水段 + cutout 段），各自绑 QML Model + 材质。cutout 段无邻居面剔除（cross 透明装饰，不挡
    //   邻居；PASS 1 只发 cross 顶点、PASS 2 立方面跳过）。dirty 时序不受影响（三段 onWorldChanged 皆同步槽，
    //   World emit worldChanged() 内全部重建完才 clearAllDirty，见 world.cpp setBlock）。
    Q_PROPERTY(bool cutoutOnly READ cutoutOnly WRITE setCutoutOnly NOTIFY cutoutOnlyChanged)
    // t343 岩浆渲染分流（机制等价 waterOnly 的「独立段」，复用 culled/greedy 立方面路径而非水的变高水面）：
    //   lavaOnly=true → 本几何只网格化 Lava 方块（独立段，Main.qml 用 opacity≈0.95 + NoLighting 暖色 baseColor 材质
    //   显近不透岩浆 + 自发光感）；lavaOnly=false（默认）→ 地形段跳过 Lava（避免与岩浆段重复绘制）。岩浆段满格立方
    //   + 自剔 nb==Lava + 邻实体剔面（同 culled 地形约定），不效仿水的变高水面（岩浆浓稠、满格观感即可）。一个 chunk
    //   由四个 ChunkGeometry 实例渲染（地形段 + 水段 + cutout 段 + 岩浆段），各自绑 QML Model + 材质。
    Q_PROPERTY(bool lavaOnly READ lavaOnly WRITE setLavaOnly NOTIFY lavaOnlyChanged)
    // t405 玻璃渲染分流（机制等价 waterOnly / lavaOnly 的「半透独立段」）：glassOnly=true → 本几何只网格化
    //   Glass 方块（透明整立方，独立半透段，Main.qml 用 opacity≈0.45 + NoLighting 材质渲染 → 透过玻璃可见背后
    //   的方块 / 实体）；glassOnly=false（默认）→ 地形段跳过 Glass（避免与玻璃段重复绘制 + 被当不透明地形）。
    //   一个 chunk 由五个 ChunkGeometry 实例渲染（地形 + 水 + cutout + 岩浆 + 玻璃），各自绑 QML Model + 材质。
    //   glassOnly 段走 culled/greedy 整立方面路径（满格立方；Glass solid=false → 相邻实体不剔面 → 透过玻璃可见
    //   背后方块；邻实体剔 / 邻 Glass 剔 / 邻空气画，见 buildMesh）。玻璃非流体（无 state 液面），不走水的变高
    //   水面路径。dirty 时序：五段 onWorldChanged 皆同步槽，World emit worldChanged() 内全部重建完才 clearAllDirty。
    Q_PROPERTY(bool glassOnly READ glassOnly WRITE setGlassOnly NOTIFY glassOnlyChanged)
    // t166b 阴影开关（用户「卡顿疑似阴影所致，加开关测」）：false → sunShadowAt 直接返 0（跳过 PCF per-vertex
    //   heightmap 采样 → meshing 大幅省时，sun-step 50 chunk 重建更快）+ 顶点光基底只剩 flood-fill 光场（无软影）。
    //   ESC 设置面板开关绑 window.shadowsEnabled → 全 chunk 实例。默认 true（保留软影）；关掉即诊断 / 提速。
    Q_PROPERTY(bool shadowsEnabled READ shadowsEnabled WRITE setShadowsEnabled NOTIFY shadowsEnabledChanged)
    // t178 贪婪网格化开关（PLAN §4 性能打磨）：true → buildMesh 走 greedy meshing（按 6 面方向逐层 2D mask
    //   合并同 (tile, 邻格天光, 邻格方光) 的共面连续格为单个矩形）；false → 回退逐格 culled meshing（每可见面 4 顶点）。
    //   greedy 大幅降顶点 / 三角 / 索引数（平坦地面 16×16=256 quad → 1），F3 叠层据此可观测 meshing 吞吐改善。
    //   ⚠️ 贴图在合并 quad 上**拉伸**铺满：图集走 CLAMP 采样，UV 超 [u0,u1] 会采到**相邻瓦片**而非同瓦片重复 →
    //   无法用「UV>1 + REPEAT」做逐格平铺；把合并 quad 细分成 per-block 子格则输出与逐格 culled 完全一致（无顶点
    //   收益、徒增复杂度——相邻 cell 共边处 UV 不连续，不可共享顶点）。真正的「逐格清晰 + 顶点预算」需纹理数组
    //   （per-face tileIndex 顶点属性 + sampler2DArray，UV 在 [0,1] 内 per-layer REPEAT）= 自研 RHI 路径（PLAN §2-I
    //   顶点格式 / dev-plan 偏差 1/2），属推迟项。**t183 默认 false**（用户实测拉伸不可接受）；ESC 设置面板仍可手动
    //   打开 greedy（性能对比 / 纹理数组落地后切回）。值变 → buildMesh 重网格化。
    Q_PROPERTY(bool greedyMeshing READ greedyMeshing WRITE setGreedyMeshing NOTIFY greedyMeshingChanged)
    // t223 水贴图动画 phase（flipbook 帧索引 0/1）：仅水段（waterOnly=true）使用。mesher 据本值在
    //   静水 tile {19,24}（水源 state=0）与流水 tile {23,25}（流水 state>0）间选帧——phase 0 用 {19,23}，
    //   phase 1 用 {24,25}。两帧 flipbook 慢速切换（Main.qml Timer ~800ms 切 0/1，spec「勿快」）→ 静水轻微
    //   荡漾、流水斜纹呈「向右下流动」动势（机制等价 MC still/flowing_water flipbook，§9 原创）。
    //   非水段（地形）不引用水 tile，phase 变化对其无视觉影响 → setter 仅在 waterOnly 时触发 buildMesh
    //   （避免地形段无谓重建）。值变 → 水段 buildMesh(Water) 重网格化（顶点 UV 子区换帧）。
    //   分层（PLAN §2）：本属 Renderer（mesher），只接收裸 int phase（不依赖 Game 层时间源 / Timer），保持
    //   Renderer→向下 依赖方向。
    Q_PROPERTY(int waterAnimPhase READ waterAnimPhase WRITE setWaterAnimPhase NOTIFY waterAnimPhaseChanged)
    // 网格统计（t10 F3 调试叠层，PLAN §2-F）：buildMesh 完成后暴露本 chunk 的顶点 / 三角面数，
    // 供 F3 叠层汇总诊断 meshing 吞吐与帧抖根因（§2-F 明言「没有 F3 叠层，帧率验收无法诊断帧抖」）。
    // 仅在 buildMesh 末尾经 meshRebuilt 通知；呈现层只读、不反向写。三角面 = idx/3（实际索引计数
    // 派生，不依赖内部「每可见面 4 顶点 + 6 索引」约定，将来换贪婪网格化仍正确）。
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY meshRebuilt)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY meshRebuilt)

public:
    explicit ChunkGeometry(QQuick3DObject *parent = nullptr);

    World *world() const { return m_world; }
    void setWorld(World *w);

    int cx() const { return m_cx; }
    void setCx(int cx);
    int cz() const { return m_cz; }
    void setCz(int cz);

    // t123 太阳方向（mesher 据此烘顶点光方向调制 + 投影阴影）。
    QVector3D sunDir() const { return m_sunDir; }
    void setSunDir(const QVector3D &dir);

    // t148 水渲染分流（见 Q_PROPERTY 注释）：true=只网格化 Water 段。
    bool waterOnly() const { return m_waterOnly; }
    void setWaterOnly(bool on);
    // t326 cross cutout 分流（见 Q_PROPERTY 注释）：true=只网格化 cross 广告牌方块（草丛 / 作物 / 树苗）。
    bool cutoutOnly() const { return m_cutoutOnly; }
    void setCutoutOnly(bool on);
    // t343 岩浆分流（见 Q_PROPERTY 注释）：true=只网格化 Lava 段（满格立方，独立近不透暖色材质）。
    bool lavaOnly() const { return m_lavaOnly; }
    void setLavaOnly(bool on);
    // t405 玻璃分流（见 Q_PROPERTY 注释）：true=只网格化 Glass 段（透明整立方，独立半透材质）。
    bool glassOnly() const { return m_glassOnly; }
    void setGlassOnly(bool on);
    // t166b 阴影开关（false → sunShadowAt 返 0，关 PCF 软影，meshing 提速）。
    bool shadowsEnabled() const { return m_shadowsEnabled; }
    void setShadowsEnabled(bool on);
    // t178 贪婪网格化开关（true=greedy 合并同面；false=逐格 culled）。值变 → 重网格化。
    bool greedyMeshing() const { return m_greedyMeshing; }
    void setGreedyMeshing(bool on);
    // t223 水贴图动画 phase（0/1；仅水段使用）。值变 → 水段 buildMesh(Water)（地形段早退）。
    int waterAnimPhase() const { return m_waterAnimPhase; }
    void setWaterAnimPhase(int phase);

    // 网格统计（t10 F3 叠层）：上次 buildMesh 产出的顶点 / 三角面数。
    int vertexCount() const { return m_vertexCount; }
    int triangleCount() const { return m_triangleCount; }

signals:
    void worldChanged();
    void cxChanged();
    void czChanged();
    void sunInputChanged(); // t123：sunDir 变（太阳量化跨步）；驱动呈现层 / 未来光场刷新
    void waterOnlyChanged(); // t148：水段开关变（QML 改 waterOnly → 重建，水段 / 地形段重网格化）
    void cutoutOnlyChanged(); // t326：cutout 段开关变（QML 改 cutoutOnly → 重建，cross 段 / 地形段重网格化）
    void lavaOnlyChanged(); // t343：岩浆段开关变（QML 改 lavaOnly → 重建，岩浆段 / 地形段重网格化）
    void glassOnlyChanged(); // t405：玻璃段开关变（QML 改 glassOnly → 重建，玻璃段 / 地形段重网格化）
    void shadowsEnabledChanged(); // t166b：阴影开关变（→ buildMesh 重算顶点光 PCF）
    void greedyMeshingChanged();  // t178：贪婪网格化开关变（→ buildMesh 重网格化）
    void waterAnimPhaseChanged(); // t223：水贴图动画 phase 变（→ 水段 buildMesh(Water) 换帧）
    // buildMesh 完成（顶点 / 三角面数已更新；t10 F3 叠层据此刷新汇总）。
    void meshRebuilt();

private:
    void onWorldChanged();            // worldChanged 槽：仅 dirty chunk 才重建（编辑即时，同步于 setBlock）
    void buildMesh(RebuildReason reason); // 局部 culled mesh + 写入 QQuick3DGeometry + 清脏（编辑路径）
    int tileFor(quint8 block, int face, quint8 state) const;
    // t153 PCF 软影（方案③：t151 顶点光基底 + heightmap 正交深度图）：给定世界空间顶点，沿 sunDir 水平
    //   方向步进 kMaxShadow 格、2×2 PCF 采样路径列 heightmap，返回 [0,1] 软影因子（0=全亮、1=全影）。
    //   mesher 据此把天光分量乘 (1-sh)，火把方光取 max 保留。只读 World::heightmapAt + 裸 sunDir（不依赖
    //   Game 层 WorldClock，保持 Renderer→向下）。
    float sunShadowAt(float wx, float wy, float wz) const;
    Chunk *myChunk() const;           // 本几何负责的 chunk（world/cx/cz 无效 → nullptr）
    // 世界坐标查询（跨 chunk 经 world.blockAt 路由 → 边界面剔除正确）
    quint8 blockAtWorld(int wx, int wy, int wz) const {
        return m_world ? m_world->blockAt(wx, wy, wz) : quint8(0);
    }
    // t133：世界坐标 state 查询（异形方块朝向/开合；经 world.stateAt 跨 chunk 路由）。
    //   常规方块 / 越界 → 0。PartialBlockGeometry::append 据此选朝向变体。
    quint8 stateAtWorld(int wx, int wy, int wz) const {
        return m_world ? m_world->stateAt(wx, wy, wz) : quint8(0);
    }

    World *m_world = nullptr;
    int m_cx = -1; // -1 = 未赋值（myChunk 返回 nullptr，待 QML 赋 cx/cz 后才建）
    int m_cz = -1;
    QVector3D m_sunDir{0.f, 1.f, 0.f}; // t123 太阳方向（单位向量；默认天顶正午，QML 绑 WorldClock.sunDir）
    bool m_waterOnly = false; // t148：true=只网格化 Water 段（透明水）；false=只网格化非水地形段
    bool m_cutoutOnly = false; // t326：true=只网格化 cross 段（草丛/作物/树苗 cutout）；false=不网格化 cross
    bool m_lavaOnly = false; // t343：true=只网格化 Lava 段（满格立方近不透暖色）；false=地形段跳 Lava
    bool m_glassOnly = false; // t405：true=只网格化 Glass 段（透明整立方半透）；false=地形段跳 Glass
    bool m_shadowsEnabled = true; // t166b：PCF 软影开关（false → sunShadowAt 返 0，跳过 per-vertex 采样）
    bool m_greedyMeshing = false; // t178/t183：贪婪网格化开关（true=合并同面但贴图拉伸；false=逐格 culled 贴图清晰，t183 默认）
    int m_waterAnimPhase = 0; // t223：水贴图动画 phase（0/1；仅水段使用，flipbook 在 {19,24}/{23,25} 间选帧）
    int m_vertexCount = 0;   // 上次 buildMesh 的顶点数（t10 F3 叠层汇总）
    int m_triangleCount = 0; // 上次 buildMesh 的三角面数（idx.size()/3）
};

#endif // CHUNKGEOMETRY_H
