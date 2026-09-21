#ifndef ENTITYSHADOWFIELD_H
#define ENTITYSHADOWFIELD_H

// ── t1075 blob 软阴影采样权威（Game 层 ViewModel；玩家与生物的贴地投影采样 + 专用面下发）────
//
// 用户真机症状⑤「没有玩家的阴影」——MC 式软阴影贴片（blob shadow）：半透明暗椭圆贴片贴地
//   渲染，玩家与生物同覆盖。本类 = **对地采样权威**：按节拍对玩家脚位 + 各生物中心列做「向下
//   最近承载面」采样，把贴地 Y / 贴片半径 / 腾空衰减 alpha 算好后随专用面（BlobShadowGeometry
//   quad 缓冲）下发渲染——派工不变量①：对地采样点属模拟/权威面，C++ 侧算好下发，QML 零世界
//   查询零逐帧扫；不变量③：零 MC 资产（程序几何 + 顶点色程序渐变，blobshadowgeometry.h 选型
//   注释）。
//
// 选型与落选（设计自由度留痕）：
//   · 贴地语义 = **脚位中心列向下最近承载面**（World::supportTopYAt 碰撞 sub-AABB 真顶单一
//     权威，经 WorldFacade 收窄面读——新代码统一走本面契约）：洞穴/悬岩下列顶（columnTopSurfaceY）
//     在实体上方不能当落点，必须向下找承载。窗口 kShadowMaxHeight=8 格内无承载 → 缺席
//     （悬空格不投影）；窗口内深度 h → alpha = kBaseAlpha × (1 − h/8) 线性衰减（腾空淡出）。
//   · 未物化区不投影：WorldFacade::chunkExistsAt（生命周期门：对象在位 ∧ 态可查询）缺席即跳过
//     ——fixed/sparse 两模式语义一致（fixed 全物化恒过门；sparse 出核 = 缺席），采样 0/缺席同式。
//   · 覆盖域 = 玩家 + EntityManager::Kind::Mob 槽（落选 FallingBlock/Arrow/Bobber 等投射物族：
//     症状面是「玩家/生物没有影子」，投射物短寿命贴地噪点 > 观感收益；掉落物族不动——
//     EntityStore 单一权威契约勿动，派工不变量②）。
//   · 节拍 = **15Hz 空转门 QTimer**（interval 66ms，PreciseTimer，house style 同 t1032 先例）：
//     running（QML 绑 window.worldRunning，t884 世界锚定视觉 gate 契约）∧ world 在位才走拍，
//     启动沿立即采样兜底防丢帧（glowshellinstancing 空转门同门）。落选 QML Timer 驱动：采样
//     语义属权威面，QML 只持接线；落选每快照沿驱动：实体位移沿 20Hz 节流后仍是逐槽风暴面
//     （t500/t935 收口域），15Hz + 指纹差分已不可感（贴片 66ms 滞后远低于察觉阈）。
//   · 指纹差分 = 全值逐位比较（同源同输入同结果，无量化噪声）——零变化零重传零 NOTIFY（GPU
//     上传与沿成本只在真变时付；与 t935「指纹差分只 bump 可见态真变的槽」同门）。
//   · 贴片半径 = 玩家固定 kPlayerRadius；生物 = halfW × kMobRadiusFactor 钳 [min,max]（贴片
//     略宽于碰撞半宽 = MC blob 影观感；钳界防 tiny/huge 异形值外溢）。
//   · alpha = kBaseAlpha × 衰减 × 中心满缘 0 的径向插值（blobshadowgeometry.h 顶点色承载）。
//
// 分层（PLAN §2）：Game 层 ViewModel——向下依赖 World（WorldFacade 收窄面）+ Entities
//   （EntityManager 只读观察面）+ Renderer（BlobShadowGeometry 下发面），依赖只向下；QML 侧
//   零迁移（现行玩法路径零改动，只追加 Model + 接线），不改 EntityStore/EntityManager 模拟面
//   一行（不变量②单一权威勿动）。

#include "blobshadowgeometry.h" // ShadowBlobQuad 值类型 + 下发面（Renderer 向下合规）
#include "entitymanager.h"      // Q_PROPERTY(EntityManager*) 完整类型（Qt 6.11 moc TypeMustBeComplete，
                                //   glowshellinstancing.h 同门注释）
#include "playercontroller.h"   // Q_PROPERTY(PlayerController*) 完整类型（同上）

#include <QObject>
#include <QtQml/qqml.h>

#include <QTimer>
#include <QVector>

class World;     // 完整定义经 playercontroller.h 链入（Q_PROPERTY(World*) 完整性同门）
class WorldFacade; // 采样拍内栈上构造的收窄视图（值语义，廉价临时——worldfacade.h）

// ── 采样权威（非渲染、非模拟推进——纯观察采样 + 专用面下发）───────────────────────────
class EntityShadowField : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EntityShadowField)
    // 数据源（只读观察面）：世界 / 玩家脚位 / 生物槽。QML 接线注入（Q_PROPERTY 指针族，同
    //   PlayerController world/hotbar/entityManager 注入先例）。
    Q_PROPERTY(World *world READ world WRITE setWorld NOTIFY worldChanged)
    Q_PROPERTY(PlayerController *player READ player WRITE setPlayer NOTIFY playerChanged)
    Q_PROPERTY(EntityManager *mobs READ mobs WRITE setMobs NOTIFY mobsChanged)
    // 下发目标（Renderer 专用面；QML 实例化 BlobShadowGeometry 后把指针接进来）。
    Q_PROPERTY(BlobShadowGeometry *geometry READ geometry WRITE setGeometry NOTIFY geometryChanged)
    // 空转门总闸（QML 绑 window.worldRunning——世界锚定采样，硬暂停冻结 = t884 契约；
    //   菜单态 worldRunning 恒 false → 钟停 + 无采样）。
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    // 诊断/测试沿：任一真变（quad 集内容或条数变）时 bump。QML 现行面零消费（特征存在性
    //   由几何 quadCount 承载）；矩阵腿行为级断言面。
    Q_PROPERTY(int shadowRevision READ shadowRevision NOTIFY shadowRevisionChanged)

public:
    explicit EntityShadowField(QObject *parent = nullptr);

    // ── 采样语义常量（矩阵腿按值断言的权威面；改值须同步腿面）─────────────────────────
    static constexpr float kShadowMaxHeight = 8.0f; // 腾空衰减窗（格）：超过即缺席（悬空不投影）
    static constexpr int kProbeCells = 10;          // 列扫深度（格；> kShadowMaxHeight + 1 余量）
    static constexpr float kBaseAlpha = 0.5f;       // 贴地满档 alpha（观感标定值，待实机微调项）
    static constexpr float kGroundLift = 0.03f;     // 贴片相对承载面真顶的抬升（防 z-fight）
    static constexpr float kPlayerRadius = 0.45f;   // 玩家贴片半径（格）
    static constexpr float kMobRadiusFactor = 1.25f; // 生物贴片半径 = halfW × factor
    static constexpr float kMobRadiusMin = 0.28f;   // 生物贴片钳界（tiny 异形防外溢）
    static constexpr float kMobRadiusMax = 0.95f;   // 生物贴片钳界（huge 异形防外溢）
    static constexpr int kMaxQuads = 96;            // 下发面容量上限（玩家 1 + Mob 槽 ≤64 « 上限）
    static constexpr int kSampleIntervalMs = 66;    // 采样节拍（≈15Hz）

    World *world() const { return m_world; }
    void setWorld(World *w);
    PlayerController *player() const { return m_player; }
    void setPlayer(PlayerController *p);
    EntityManager *mobs() const { return m_mobs; }
    void setMobs(EntityManager *m);
    BlobShadowGeometry *geometry() const { return m_geometry; }
    void setGeometry(BlobShadowGeometry *g);
    bool running() const { return m_running; }
    void setRunning(bool r);
    int shadowRevision() const { return m_shadowRevision; }

    // 采样拍：玩家 + 各 Mob 槽逐列采样 → 指纹差分 → 真变才下发专用面 + bump 沿。
    //   Q_INVOKABLE 面外还给矩阵腿 headless 直调（时钟缝：矩阵不挂 QTimer，直调 update()）。
    Q_INVOKABLE void update();

    // 节拍驱动的拍体（QTimer 母钟 → 本体；与 update() 同一路径单点收口）。
    private Q_SLOTS:
    void onTick() { update(); }

Q_SIGNALS:
    void worldChanged();
    void playerChanged();
    void mobsChanged();
    void geometryChanged();
    void runningChanged();
    void shadowRevisionChanged();

private:
    void refreshTicker(); // 空转门（running ∧ world 在位 → 走拍；否则停）+ 启动沿立即采样
    // 单实体贴片采样：脚位 (x, feetY, z) 中心列向下找承载 → 命中填 out 返 true（缺席 false）。
    //   facade = 采样拍内构造的收窄视图（WorldFacade 拷贝廉价——worldfacade.h 头注）。
    bool sampleQuad(const WorldFacade &facade, float x, float feetY, float z, float radius,
                    ShadowBlobQuad &out);

    World *m_world = nullptr;
    PlayerController *m_player = nullptr;
    EntityManager *m_mobs = nullptr;
    BlobShadowGeometry *m_geometry = nullptr;
    bool m_running = false;
    int m_shadowRevision = 0;
    QTimer m_ticker;
    QVector<ShadowBlobQuad> m_pushed; // 指纹差分基线（= 已下发集合；与几何内 m_quads 同源同值）
};

#endif // ENTITYSHADOWFIELD_H
