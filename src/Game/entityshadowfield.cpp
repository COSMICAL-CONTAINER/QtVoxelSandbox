#include "entityshadowfield.h"

#include "worldfacade.h" // 收窄查询面（supportTopYAt / chunkExistsAt——新代码统一走本面契约）
#include "mathtypes.h"   // floorDiv（负坐标 chunk 键——Core 单一权威，禁自行换算）

#include <cmath>
#include <cstring>
#include <vector>

// t1075 blob 软阴影采样权威（Game 层）——选型/落选/分层/贴地语义头注释见 .h。
// 本 .cpp 只做四件事：空转门、采样拍体、中心列扫描、指纹差分下发。
// QML 现行玩法路径零迁移：呈现面只追加「一 Model + 五条指针接线」。

EntityShadowField::EntityShadowField(QObject *parent)
    : QObject(parent)
{
    m_ticker.setTimerType(Qt::PreciseTimer);
    m_ticker.setInterval(kSampleIntervalMs);
    connect(&m_ticker, &QTimer::timeout, this, &EntityShadowField::onTick);
}

void EntityShadowField::setWorld(World *w)
{
    if (m_world == w)
        return;
    m_world = w;
    emit worldChanged();
    refreshTicker(); // 空转门重估（world 缺席 → 停钟；新 world → 启动沿立即采样）
}

void EntityShadowField::setPlayer(PlayerController *p)
{
    if (m_player == p)
        return;
    m_player = p;
    emit playerChanged();
    update(); // 玩家注入沿立即采样（确定性首拍，无 tick 依赖）
}

void EntityShadowField::setMobs(EntityManager *m)
{
    if (m_mobs == m)
        return;
    m_mobs = m;
    emit mobsChanged();
    refreshTicker();
}

void EntityShadowField::setGeometry(BlobShadowGeometry *g)
{
    if (m_geometry == g)
        return;
    m_geometry = g;
    emit geometryChanged();
    update(); // 几何注入沿立即下发（确定性首拍，无 tick 依赖）
}

void EntityShadowField::setRunning(bool r)
{
    if (m_running == r)
        return;
    m_running = r;
    emit runningChanged();
    refreshTicker(); // 空转门重估（false → 停钟；true → 启动沿立即采样）
}

void EntityShadowField::refreshTicker(){
    // 空转门（t1032 模式：构造不启钟，按「总闸 ∧ 数据源在位」启/停；启动沿立即采样兜底防丢帧）。
    const bool active = m_running && m_world != nullptr;
    if (active && !m_ticker.isActive()) {
        m_ticker.start();
        update();
    } else if (!active && m_ticker.isActive()) {
        m_ticker.stop();
    }
}

void EntityShadowField::update()
{
    std::vector<ShadowBlobQuad> quads;
    quads.reserve(8);

    if (m_world) {
        const WorldFacade facade(*m_world);
        ShadowBlobQuad quad;
        if (m_player) {
            const QVector3D feet = m_player->feetPosition();
            if (sampleQuad(facade, feet.x(), feet.y(), feet.z(), kPlayerRadius, quad))
                quads.push_back(quad);
        }
        if (m_mobs) {
            const int n = m_mobs->count();
            for (int i = 0; i < n; ++i) {
                if (!m_mobs->aliveAt(i))
                    continue;
                if (m_mobs->kindAt(i) != EntityManager::Mob)
                    continue;
                const QVector3D p = m_mobs->posAt(i);
                const float halfW = m_mobs->radiusAt(i);
                const float halfH = m_mobs->halfHeightAt(i);
                const float radius = std::clamp(halfW * kMobRadiusFactor,
                                                kMobRadiusMin, kMobRadiusMax);
                if (sampleQuad(facade, p.x(), p.y() - halfH, p.z(), radius, quad))
                    quads.push_back(quad);
            }
        }
    }

    // 指纹差分下发：与已下发基线逐位比较——零变化零重传零沿（类头注选型）。
    bool changed = quads.size() != m_pushed.size();
    if (!changed && !quads.empty())
        changed = std::memcmp(quads.data(), m_pushed.constData(),
                              sizeof(ShadowBlobQuad) * quads.size()) != 0;
    if (!changed)
        return;
    m_pushed.clear();
    if (!quads.empty())
        m_pushed = QVector<ShadowBlobQuad>(quads.cbegin(), quads.cend());
    if (m_geometry)
        m_geometry->setQuads(m_pushed.constData(), int(m_pushed.size()));
    ++m_shadowRevision;
    emit shadowRevisionChanged();
}

bool EntityShadowField::sampleQuad(const WorldFacade &facade, float x, float feetY, float z,
                                   float radius, ShadowBlobQuad &out)
{
    // 未物化区不投影（chunk 生命周期门：对象在位 ∧ 态可查询；fixed 全物化恒过门 → 两模式语义
    //   一致，派工不变量④）。floorDiv 负坐标安全（Core mathtypes 单一权威）。
    const int cx = floorDiv(int(std::floor(x)), int(Chunk::kSize));
    const int cz = floorDiv(int(std::floor(z)), int(Chunk::kSize));
    if (!facade.chunkExistsAt(cx, cz))
        return false;

    // 中心列向下找最近承载面（World::supportTopYAt 碰撞 sub-AABB 真顶单一权威；无碰撞族 -1
    //   穿透——水/火/轨/花草不作落点，与 mob 落地扫描同语义）。起点 = 脚位所在格（脚位恰在
    //   支撑顶时 floor = 支撑格上方空气格，多探一格无碍；脚位在半砖/薄层顶时 floor = 支撑格
    //   本体，首探即中）。
    const int startCell = int(std::floor(feetY));
    int cellY = startCell;
    float ground = -1.0f;
    for (int k = 0; k < kProbeCells && cellY >= 0; ++k, --cellY) {
        const float top = facade.supportTopYAt(int(std::floor(x)), cellY, int(std::floor(z)));
        if (top >= 0.0f) {
            ground = top;
            break;
        }
    }
    if (ground < 0.0f)
        return false; // 窗口内无承载 = 悬空格不投影（悬崖边缘语义：中心列语义天然给出）

    // 腾空高度衰减：h=0 贴地满档；h ≥ 窗口缺席。负 h（脚位嵌入支撑顶以下的异常位形）钳 0——
    //   衰减面只在 [0, 窗) 生效，嵌入不放大 alpha。
    const float h = std::max(0.0f, feetY - ground);
    if (h >= kShadowMaxHeight)
        return false;
    const float alpha = kBaseAlpha * (1.0f - h / kShadowMaxHeight);

    out.x = x;
    out.y = ground + kGroundLift;
    out.z = z;
    out.radius = radius;
    out.alpha = alpha;
    return true;
}
