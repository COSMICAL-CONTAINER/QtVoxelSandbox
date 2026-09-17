#ifndef CHUNKGEOMETRY_H
#define CHUNKGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QVector3D>

#include <QtQml/qqml.h>

#include "world.h" // Q_PROPERTY(World*) + 路由（World 层只读）
#include "worldfacade.h" // R20.08 WorldFacade：chunk 脏门/存在门查询走收窄面（不再直取 Chunk*）

class QElapsedTimer; // §29.5-W4 applyChunkMeshData 形参（const ref——前向声明足够）

// 体素区块几何（纯视图，per-chunk，t03）：每个 ChunkGeometry 负责一个 chunk（cx,cz）的
// 局部 culled meshing。从注入的 World（经 blockAt 跨 chunk 路由）取体素 + 邻居判定，
// 只生成「邻居为空气」的可见面。顶点为 chunk 局部坐标；QML 把 Model 摆到 chunk 世界起点
// (cx*16, 0, cz*16) 完成世界定位。
//
// dirty 驱动重建（dev-spec t03 验收）：setBlock 经 ChunkManager 标目标 + 边界邻接 chunk 脏；
// worldChanged → onWorldChanged() 检 chunkDirty()（R20.08 前为 myChunk()->dirty()），**仅脏 chunk 重建并清脏**，非脏
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
    // PLAN §2-H 夜间火把发光修复（R19 B6）：昼夜天光乘子 dayMul（QML 端 terrainLight(skyLight) ∈ [minLight,1]）
    //   **只乘天光分量**、**绝不乘方块光分量**。机制：方块光（火把/熔炉 flood-fill）时间不变；昼夜只应调制
    //   天光。旧实现把昼夜乘子留在 QML baseColor（baseColor×vertexColor）→ 它会同时压暗 block 通道 →
    //   夜间（skyLight=0 → dayMul=minLight=0.4）满光火把 0.93 被压到 0.37 = 火把夜间不发光。修复把 dayMul
    //   注入 mesher，烘进 sky*(1-软影) 项（5 处烘焙点 + BlockCube 同步），block 项保持原样、取 max 后钳制 →
    //   火把光池任何 dayMul 下都全亮。地形材质 baseColor 改白（不再承担昼夜）。
    //   分层（PLAN §2）：只接收裸 float（不 include worldclock.h），保持 Renderer→向下。
    //   性能：dayPhaseChanged（10Hz）会推本属性，但 setDayMul 走 sunRebuildDue 同款量化门（dayMul 累计变超
    //   kDayMulThresh=0.03 才重建，与 sun-step 门合并），故昼夜过渡按 ~每 12s（最陡段）一步量化、不 10Hz 全量重建。
    Q_PROPERTY(float dayMul READ dayMul WRITE setDayMul NOTIFY dayMulChanged)
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
    // **t860（R19.14）折叠**：t442 起 terrain 段材质已带 alphaMode:Mask + alphaCutoff:0.5（leaves cutout
    //   实证生效），与本段材质逐字相同 → cross/门/活板门顶点并入 terrain 段 mesh（buildMesh PASS 1 路由
    //   不再跳过），QML 停建 cutout 段 Model（每 chunk 6 段 → 5 段）。降级杠杆 = **cutoutFolded 显式开关**
    //   （review28 #4；Main.qml window.cutoutSegmentRestored 单开关联动 terrain 路由 / cutout 段实例化 /
    //   segmentsPerChunk 三处——旧「只恢复 createObject 一行即回 6 段」的注释路径会两段同发 z-fighting，
    //   已废），折叠行为由矩阵 t860 探针钉死（terrain 段 mesh 计入 cross 顶点 + 开关行为级两态断言）。
    Q_PROPERTY(bool cutoutOnly READ cutoutOnly WRITE setCutoutOnly NOTIFY cutoutOnlyChanged)
    // review28 #4（t860 降级杠杆显式开关化）：cutoutFolded=true（默认）→ terrain 段 PASS 1 路由**全收**
    //   cross/门/活板门（t860 折叠态，QML 停建 cutout 段）。false → terrain 段恢复 t860 **前**的跳过清单
    //   （cross/门/活板门让位并行存在的独立 cutout 段——两段同发同几何同材质 = 逐顶点重合 z-fighting，
    //   即 review28 #4 指出的「照旧注释只恢复 createObject 一行」事故路径）。Main.qml 以
    //   window.cutoutSegmentRestored **单开关**联动三处（本属性绑定 + crossChunkComp 条件实例化 +
    //   segmentsPerChunk），恢复 = 翻一个属性（构建期置位），结构上不可能只恢复一半。
    Q_PROPERTY(bool cutoutFolded READ cutoutFolded WRITE setCutoutFolded NOTIFY cutoutFoldedChanged)
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
    // t468 冰渲染分流（机制等价 waterOnly / lavaOnly / glassOnly 的「半透独立段」）：iceOnly=true → 本几何只
    //   网格化冰族方块（Ice / PackIce / BlueIce，isIce 谓词；透明整立方，独立半透段，Main.qml 用 opacity≈0.7
    //   + NoLighting + Blend 材质渲染 → 半透冰可见背后的方块 / 实体，机制等价 MC 1.0 半透冰）；iceOnly=false
    //   （默认）→ 地形段跳过冰族（避免与冰段重复绘制 + 被当不透明地形）。一个 chunk 由六个 ChunkGeometry 实例
    //   渲染（地形 + 水 + cutout + 岩浆 + 玻璃 + 冰），各自绑 QML Model + 材质。iceOnly 段走 culled/greedy 整立方面
    //   路径（满格立方；冰族 solid=false → 相邻实体不剔面 → 透过半透冰可见背后方块；邻实体剔 / 邻冰剔 / 邻空气
    //   画，见 buildMesh，同 glass 模式）。冰非流体（无 state 液面），不走水的变高水面路径。dirty 时序：六段
    //   onWorldChanged 皆同步槽，World emit worldChanged() 内全部重建完才 clearAllDirty。
    Q_PROPERTY(bool iceOnly READ iceOnly WRITE setIceOnly NOTIFY iceOnlyChanged)
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
    // t1023（R19.20）AO 环境光遮蔽开关（平滑光照调研的首批小步单项）：true → 地形段逐格 culled
    //   路径每面 4 角点做经典 MC AO 三探针（面邻格基 + 两侧 + 对角，实体遮挡计数、双侧钳 3），
    //   遮挡因子（VoxelLight::kAoFactor 曲线，单一权威）乘顶点色 → 墙根 / 拐角接触阴影暗角。
    //   **默认 false（出厂关）**——t1023 弹性口径：平滑光照全量版（角点 4 格光场平均）改造面大
    //   （greedy 合并键冲突 / 视觉全场景回归 / ~2-4× 光采样），登记 docs/perf-batch3-research-2026-09.md
    //   路线不硬做；AO 单项先行，待用户实机目视确认后另单翻默认。值变 → buildMesh 重网格化
    //   （同 setGreedyMeshing 的 Dirty 路径 + t472 视距门控）。
    //   范围（钉死）：仅地形段逐格 culled 路径（greedy 合并键不含 AO、流体水面接触阴影观感未验、
    //   异形/cross 段走 PartialLightCtx 上下文——均不采样，登记后续）。乘法语义：AO 乘在光场钳制后
    //   （接触阴影允许低过 kVcMin 地板——暗角即压暗语义，非光场分量）。
    Q_PROPERTY(bool aoEnabled READ aoEnabled WRITE setAoEnabled NOTIFY aoEnabledChanged)
    // t472 性能：chunk 是否在玩家渲染距离内（视距门控，修 t470 视距盲点 + 砍 mesh 重建风暴）。
    //   由所在段 Model.chunkInRange 绑定注入（Main.qml `_refreshChunkVisibility` 据玩家所在 chunk +
    //   renderDistance 切比雪夫半径切换）。**false = 远端 chunk**：sun 步进（setSunDir）/ 水翻页
    //   （setWaterAnimPhase）/ 阴影开关（setShadowsEnabled）/ greedy 开关（setGreedyMeshing）/ 编辑
    //   （onWorldChanged）的 setter **仍更新内部值**（保 catch-up 时值已是最新），但**跳过 buildMesh**
    //   （远 chunk 不绘制 → 不需要重建；这是「600→154 段零 FPS 提升」真因的根治：t470 只门控了
    //   Model.visible，未门控绑在 Model 内部 ChunkGeometry 上的 sun/water 重建，所有 600 段每 ~3.3s/
    //   800ms 无视距全成本重建）。
    //   false→true 转变（玩家走近远 chunk 进视野）触发**一次** buildMesh(Sun) catch-up：把离开视野期间
    //   错过的 sun/water/shadow/greedy 变化一并应用（远 chunk 重新进视野时贴图 / 光照非陈旧）。
    //   true→false 不重建（远 chunk 不绘制，下次回 true 再 catch up）。默认 true（首帧 mesh 未绑前按近程
    //   处理，启动期 worldChanged 触发首次构建后再被 _refreshChunkVisibility 切换）。
    //   分层（PLAN §2）：纯呈现层门控信号（bool），不依赖 Game 层。
    //   **t972 语义收窄**：本属性只承担「CPU 何时重建」的门控，**不再承担「GPU 是否绘制」**——
    //   Model.visible 只由 vertexCount>0 决定（有限世界全幅渲染，机制对标 MC 1.0 有限地图边到边可见；
    //   t470 实测「600→154 段」的绘制剔除本就零 FPS 收益，绘制侧放开零成本，重建窗口经济学无损保留）。
    //   窗外段错过重建不再靠 catch-up 兜底可见性：错过的**内容**重建记入 m_deferredRebuild、错过的
    //   **光照**重烘记入 m_lightStale，由呈现层渐进同步队列（Main.qml kickWorldMeshSync / _meshSyncTimer）
    //   以近→远、每帧限量节奏排空（refreshMesh 走与编辑同一条 buildMesh 链）——进入世界时窗外区域
    //   秒级渐进填满，而非永久留白到玩家走近 / 编辑。
    Q_PROPERTY(bool chunkInRange READ chunkInRange WRITE setChunkInRange NOTIFY chunkInRangeChanged)
    // t223/tXXX 水贴图动画 phase（flipbook 帧索引 0/1）：**历史遗留属性**。tXXX 水动画重建消除——
    //   flipbook 翻页换帧（2s 一次全量水段 buildMesh，Swamp 场景 261 段/次）是 mesh 重建风暴第二根因；
    //   2 帧 UV 子区换帧不必重建整段 → 现改**静态水**（mesher 恒用 phase 0 帧：静水 tile 19 / 流水 tile 23 +
    //   烘死的空间涟漪），本 setter 只记录值**不再触发 buildMesh**（API 稳定 + 将来 material 级动画
    //   （UV offset / shader 位移）落地后复用；静态化视觉损失可接受，见 tXXX 验收）。值变早退同旧。
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

    // PLAN §2-H dayMul（昼夜天光乘子，仅乘天光分量；见 Q_PROPERTY 注释）。
    float dayMul() const { return m_dayMul; }
    void setDayMul(float m);

    // t148 水渲染分流（见 Q_PROPERTY 注释）：true=只网格化 Water 段。
    bool waterOnly() const { return m_waterOnly; }
    void setWaterOnly(bool on);
    // t326 cross cutout 分流（见 Q_PROPERTY 注释）：true=只网格化 cross 广告牌方块（草丛 / 作物 / 树苗）。
    bool cutoutOnly() const { return m_cutoutOnly; }
    void setCutoutOnly(bool on);
    // review28 #4 cutout 折叠开关（见 Q_PROPERTY 注释）：true=terrain 段全收 cutout 族（t860 折叠态）；
    // false=恢复独立 cutout 段时代的跳过清单。值变 → 重网格化（两态 PASS 1 选块不同）。
    bool cutoutFolded() const { return m_cutoutFolded; }
    void setCutoutFolded(bool on);
    // t343 岩浆分流（见 Q_PROPERTY 注释）：true=只网格化 Lava 段（满格立方，独立近不透暖色材质）。
    bool lavaOnly() const { return m_lavaOnly; }
    void setLavaOnly(bool on);
    // t405 玻璃分流（见 Q_PROPERTY 注释）：true=只网格化 Glass 段（透明整立方，独立半透材质）。
    bool glassOnly() const { return m_glassOnly; }
    void setGlassOnly(bool on);
    // t468 冰分流（见 Q_PROPERTY 注释）：true=只网格化冰族段（Ice/PackIce/BlueIce 透明整立方，独立半透材质）。
    bool iceOnly() const { return m_iceOnly; }
    void setIceOnly(bool on);
    // t166b 阴影开关（false → sunShadowAt 返 0，关 PCF 软影，meshing 提速）。
    bool shadowsEnabled() const { return m_shadowsEnabled; }
    void setShadowsEnabled(bool on);
    // t178 贪婪网格化开关（true=greedy 合并同面；false=逐格 culled）。值变 → 重网格化。
    bool greedyMeshing() const { return m_greedyMeshing; }
    void setGreedyMeshing(bool on);
    // t1023 AO 环境光遮蔽开关（见 Q_PROPERTY 注释）。值变 → 重网格化（地形段 culled 路径角点重烘）。
    bool aoEnabled() const { return m_aoEnabled; }
    void setAoEnabled(bool on);
    // t223 水贴图动画 phase（0/1；仅水段使用）。值变 → 水段 buildMesh(Water)（地形段早退）。
    int waterAnimPhase() const { return m_waterAnimPhase; }
    void setWaterAnimPhase(int phase);

    // t972 载入世界空白区修复——呈现层渐进同步三入口（重建链仍单点 buildMesh，不另起第二套构建）：
    //   clearMesh()   世界内容换代（enterWorld 的 beginLoad/regenerate）后由呈现层对**窗外段**调：
    //                 旧世界 / 上局残 mesh 立即清空（vertexCount→0 → Model.visible 绑定自动隐，
    //                 防「换世界后窗外显上一世界地形」的陈旧错景），并清两项错过重建欠账。
    //   refreshMesh() 渐进同步队列的排空动作：无条件走 buildMesh(Dirty)（与编辑即时重建同一条链，
    //                 同因由计数），把当前世界数据烘成 mesh。承载「载入完成→近→远逐帧限量重建」。
    //   deferredRebuildPending() / lightStale()：稳态期（游玩中）窗外段错过的**内容**重建（编辑 /
    //                 流体写被 onWorldChanged 窗口门控跳过，dirty 随 clearAllDirty 清）与**光照**
    //                 重烘（setSunDir/setDayMul 窗口静默跟随）的欠账查询——呈现层低频扫描把欠账段
    //                 排进同一同步队列，使远处可见地形的光照 / 内容最终一致（否则夜晚远处仍显正午
    //                 亮度 = t972 放开绘制后必须补的最终一致性）。空段（vertexCount==0）无光可烘，
    //                 呈现层对 lightStale 段先查顶点数跳过，避免零顶点重建刷日志 / 计数。
    Q_INVOKABLE void clearMesh();
    Q_INVOKABLE void refreshMesh();
    Q_INVOKABLE bool deferredRebuildPending() const { return m_deferredRebuild; }
    Q_INVOKABLE bool lightStale() const { return m_lightStale; }
    // t472 视距门控（见 Q_PROPERTY 注释）：true=近程（重建启用）；false=远端（setter 跳过 buildMesh）。
    bool chunkInRange() const { return m_chunkInRange; }
    void setChunkInRange(bool inRange);

    // 网格统计（t10 F3 叠层）：上次 buildMesh 产出的顶点 / 三角面数。
    int vertexCount() const { return m_vertexCount; }
    int triangleCount() const { return m_triangleCount; }

signals:
    void worldChanged();
    void cxChanged();
    void czChanged();
    void sunInputChanged(); // t123：sunDir 变（太阳量化跨步）；驱动呈现层 / 未来光场刷新
    void dayMulChanged();   // PLAN §2-H：昼夜天光乘子变（仅乘天光分量，方块光时间不变）
    void waterOnlyChanged(); // t148：水段开关变（QML 改 waterOnly → 重建，水段 / 地形段重网格化）
    void cutoutOnlyChanged(); // t326：cutout 段开关变（QML 改 cutoutOnly → 重建，cross 段 / 地形段重网格化）
    void cutoutFoldedChanged(); // review28 #4：折叠杠杆开关变（QML 恢复 cutout 段 → terrain 段退回跳过清单并重建）
    void lavaOnlyChanged(); // t343：岩浆段开关变（QML 改 lavaOnly → 重建，岩浆段 / 地形段重网格化）
    void glassOnlyChanged(); // t405：玻璃段开关变（QML 改 glassOnly → 重建，玻璃段 / 地形段重网格化）
    void iceOnlyChanged(); // t468：冰段开关变（QML 改 iceOnly → 重建，冰段 / 地形段重网格化）
    void shadowsEnabledChanged(); // t166b：阴影开关变（→ buildMesh 重算顶点光 PCF）
    void greedyMeshingChanged();  // t178：贪婪网格化开关变（→ buildMesh 重网格化）
    void aoEnabledChanged();      // t1023：AO 环境光遮蔽开关变（→ 地形段 culled 角点重烘 AO）
    void waterAnimPhaseChanged(); // t223/tXXX：水贴图动画 phase 变（历史遗留；tXXX 起**不再触发 buildMesh**，静态水单帧，见 Q_PROPERTY 注释）
    void chunkInRangeChanged();   // t472：视距门控变（false→true 触发一次 catch-up buildMesh）
    // buildMesh 完成（顶点 / 三角面数已更新；t10 F3 叠层据此刷新汇总）。
    void meshRebuilt();

private:
    void onWorldChanged();            // worldChanged 槽：仅 dirty chunk 才重建（编辑即时，同步于 setBlock）
    // R20.13 MeshBuilder：网格算法单一权威已抽出（meshbuilder.{h,cpp}）——本方法收敛为
    //   「采集稠密快照 → MeshBuilder::build（owning ChunkMeshData）→ 灌 QQuick3D 几何」。旧私有
    //   网格件（tileFor / farmlandHydrBrightMul / sunShadowAt / blockAtWorld / stateAtWorld 与
    //   buildMesh 体）自本类**搬移**（非复制）进 meshbuilder（快照访问器同名同语义承接数据来源
    //   替换）；Q_PROPERTY/信号面零变化（验收④：旧 QtQuick3DAdapter 消费形态保持）。
    //   §29.5-W4（r2026）bake→worker 网格化：buildMesh 升级为「路径选择器」——通电世界先走
    //   异步提交（定格快照 → 提交 → 返回，网格由收割拍交付应用）；提交被拒（队列满载 /
    //   执行器已停 / 桥未就绪）→ 同步内联回退（计数可见）。通电条件 = World::chunkMeshAsync
    //   Active()（sink 已绑定 [sparse 流式会话] 或 fixed 使能异步烘培[§29.7 t1060——生产挂点
    //   = StreamingBridge fixed 进入分支，收割拍 = StreamingBridge::pumpTick 薄收割槽]）。
    //   未通电世界（裸 fixed / env 回退 QTVOXEL_SYNC_BAKE≠0）→ 同步内联路径逐位原样（零变化
    //   墙——本方法同步分支体零改动）。
    void buildMesh(RebuildReason reason);
    // §29.5-W4 异步提交半边（主线程 Only）：采集稠密快照（与同步路径同一采集调用）→
    //   requestId 派生 → 上一在途作业显式注销（latest-snapshot-wins 路由层半边）→ 经 World 桥
    //   提交。被接受 → true（m_pendingRequestId 在途，网格等收割拍交付）；被拒 / 空快照
    //  （无 chunk——构建产物恒空，无作业必要）→ false（调用方走同步内联回退；拒绝面计数可见）。
    //   **通电条件**：World::chunkMeshAsyncActive()（sink 已绑定 [⟺ sparse 流式会话在] 或
    //   fixed 世界已使能异步烘培[§29.7 t1060——World 惰性执行器 + StreamingBridge::pumpTick
    //   收割拍]）由调用方先行判定；本方法内 world 空指针防御返回 false。
    bool submitMeshJobAsync(RebuildReason reason);
    // §29.5-W4 应用半边（同步/异步两路共用的收尾链，禁两份灌注逻辑并存）：统计镜像 → 灌
    //   QQuick3D（文档序逐行原样）→ 烘焙账本（bakedSunDir/bakedDayMul = **采集时刻定格值**——
    //   异步路径采集与应用之间 sun/dayMul 可能已前移，账本必须记「实际烘进顶点色的值」，否则
    //   sun 量化门会误判已烘）→ vo.render 观测 + meshRebuilt 通知。同步路径传当前成员值
    //  （= 采集时刻值，与旧行为逐位一致）。
    void applyChunkMeshData(ChunkMeshData mesh, RebuildReason reason,
                            const QVector3D &bakedSunDir, float bakedDayMul,
                            const QElapsedTimer &bt);
    // §29.5-W4 requestId 派生（(cx,cz,段) 基座 + 提交代次，选型头注释立证于 .cpp 定义处）。
    static quint64 deriveMeshJobRequestId(int cx, int cz, int segIndex, quint32 submitGen);
    // 本几何的段位（六段路由折叠：terrain=0 / water=1 / lava=2 / glass=3 / ice=4 / cutout=5）——
    //   同 chunk 的各段是相互独立的网格作业（快照烘焙参数互异），requestId 基座须含段位。
    int segmentIndex() const
    {
        return m_waterOnly ? 1 : m_lavaOnly ? 2 : m_glassOnly ? 3 : m_iceOnly ? 4
            : m_cutoutOnly ? 5 : 0;
    }
    // tXXX sun-step 粗量化门：判定本次 sunDir / dayMul 变化是否需要重烘顶点色（否则只更新值不重建）。
    //   重烘事件：影淡入/淡出带穿越（y 跨 kSunMin/kSunMax）| 仰角/方位角累计变超阈值 | dayMul 累计变超
    //   kDayMulThresh | 距上次重烘超硬顶。昼夜天光（dayMul）现烘进顶点色天空分量（PLAN §2-H），故 dayMul 累计
    //   变化超阈值亦触发重烘（保持昼夜过渡可见、但不 10Hz 全量重建）。
    bool sunRebuildDue(const QVector3D &dir, float dayMul) const;
    // R20.08 WorldFacade 门查询（示范迁移点）：本几何负责的 chunk 的存在/脏/流体专用脏三门，
    //   经 WorldFacade 收窄面查询（worldfacade.h chunkExistsAt/chunkDirtyAt/chunkFluidOnlyDirtyAt
    //   —— 委托 World 层同一路由，语义与旧 myChunk() 直取 Chunk* 逐位一致）。**每次现查、不缓存
    //   指针**（旧 myChunk 同款防悬空口径：world recreate 后拿到的是新 chunk 状态）；world 未设
    //   → 恒 false（同旧 myChunk() 返 nullptr 的门语义）。迁移后本类零 Chunk*（渲染侧唯一
    //   Chunk 内部指针消费点消除——R20.08 验收①）。
    bool chunkExists(int cx, int cz) const
    {
        return m_world && WorldFacade(*m_world).chunkExistsAt(cx, cz);
    }
    bool chunkDirty(int cx, int cz) const
    {
        return m_world && WorldFacade(*m_world).chunkDirtyAt(cx, cz);
    }
    bool chunkFluidOnlyDirty(int cx, int cz) const
    {
        return m_world && WorldFacade(*m_world).chunkFluidOnlyDirtyAt(cx, cz);
    }
    // R20.13：世界坐标查询帮手（blockAtWorld/stateAtWorld）随网格本体迁入 ChunkMeshSnapshot
    //   快照访问器（同名同 OOB 语义）——本类消费世界面收敛为「存在/脏三门（Facade）+ 采集一次」。

    World *m_world = nullptr;
    int m_cx = -1; // -1 = 未赋值（chunkExists 门恒 false，待 QML 赋 cx/cz 后才建）
    int m_cz = -1;
    QVector3D m_sunDir{0.f, 1.f, 0.f}; // t123 太阳方向（单位向量；默认天顶正午，QML 绑 WorldClock.sunDir）
    float m_dayMul = 1.0f; // PLAN §2-H：昼夜天光乘子（仅乘天光分量；默认 1.0=正午全日照，QML 绑 terrainLight(skyLight)）
    bool m_waterOnly = false; // t148：true=只网格化 Water 段（透明水）；false=只网格化非水地形段
    bool m_cutoutOnly = false; // t326：true=只网格化 cross 段（草丛/作物/树苗 cutout）；false=不网格化 cross
    bool m_cutoutFolded = true; // review28 #4：true=t860 折叠态（terrain 段全收 cutout 族，默认）；false=恢复跳过清单（cutout 段并行时的互斥路由）
    bool m_lavaOnly = false; // t343：true=只网格化 Lava 段（满格立方近不透暖色）；false=地形段跳 Lava
    bool m_glassOnly = false; // t405：true=只网格化 Glass 段（透明整立方半透）；false=地形段跳 Glass
    bool m_iceOnly = false; // t468：true=只网格化冰族段（Ice/PackIce/BlueIce 透明整立方半透）；false=地形段跳冰族
    bool m_shadowsEnabled = true; // t166b：PCF 软影开关（false → sunShadowAt 返 0，跳过 per-vertex 采样）
    bool m_greedyMeshing = false; // t178/t183：贪婪网格化开关（true=合并同面但贴图拉伸；false=逐格 culled 贴图清晰，t183 默认）
    bool m_aoEnabled = false;     // t1023：AO 环境光遮蔽开关（默认关出厂；地形段 culled 路径角点接触阴影）
    int m_waterAnimPhase = 0; // t223/tXXX：水贴图动画 phase（**历史遗留**，静态水后恒 0；setter 不重建，见 Q_PROPERTY 注释）
    bool m_chunkInRange = true; // t472：视距门控（false=远端跳过 buildMesh；false→true catch-up 一次）
    // tXXX sun-step 粗量化：上次「实际烘进顶点色的太阳方向」与时刻（buildMesh 末尾更新）。setSunDir 据此
    //   判是否值得重烘（方向变太小 / 未穿影带 → 只更新 m_sunDir 不重建；影边 PCF 软 → 粗更新无亮度跳变）。
    QVector3D m_lastBakedSunDir{0.f, 1.f, 0.f};
    float m_lastBakedDayMul = 1.0f; // PLAN §2-H：上次「实际烘进顶点色的 dayMul」（sunRebuildDue 据此判是否重烘昼夜）
    qint64 m_lastSunBakeNs = 0;
    int m_vertexCount = 0;   // 上次 buildMesh 的顶点数（t10 F3 叠层汇总）
    int m_triangleCount = 0; // 上次 buildMesh 的三角面数（idx.size()/3）
    // t972 窗外欠账标记（呈现层渐进同步队列的排空依据；buildMesh / clearMesh 双清）：
    bool m_deferredRebuild = false; // onWorldChanged 窗外跳过时 chunk 确为脏 → 内容重建欠账
    bool m_lightStale = false;      // setSunDir/setDayMul 窗外静默跟随且 sunRebuildDue 判定该烘 → 光照欠账
    // §29.5-W4 异步网格作业在途账（主线程 Only）：最新提交的作业键（0 = 无在途——交付回调
    //   的 latest-wins 判据锚）+ 提交代次（同段连发互异在途键的低位源；mod 8192 回绕周期 ≫
    //   在途上界 128，选型论证见 deriveMeshJobRequestId 定义处头注释）。
    quint64 m_pendingRequestId = 0;
    quint32 m_meshSubmitGen = 0;
};

#endif // CHUNKGEOMETRY_H
