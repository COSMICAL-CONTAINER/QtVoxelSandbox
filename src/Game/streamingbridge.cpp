#include "streamingbridge.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

#include "chunkstore.h" // stream_worlds 元数据域载体（独立命名连接开-用-关；worldstore 零涉）

// ── §29.5-W5b 流式 UI 桥实现（r2028；语义与选型立证见头文件类头注）────────────────────────

StreamingBridge *StreamingBridge::instance()
{
    static StreamingBridge inst; // 进程全局唯一（会话状态面；QML create 同对象——BuildInfo 同款）
    return &inst;
}

// saves/ 目录三级解析（worldstore::savesDir 镜像——登记的镜像面，一致性由 r2028b 真链腿锚定）。
QString StreamingBridge::savesDir()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/../saves")).absolutePath(),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
             + QStringLiteral("/saves")).absolutePath()
    };
    for (const QString &dir : candidates) {
        if (!dir.isEmpty() && QDir().mkpath(dir))
            return dir;
    }
    return exeDir + QStringLiteral("/saves"); // 兜底（mkpath 在 exe 同级仍可能成功）
}

QString StreamingBridge::resolveSavePath(const QString &file)
{
    // 绝对路径直用（矩阵临时库先例——openWorld 绝对路径同门）；相对名落 saves/（镜像解析）。
    return QDir(savesDir()).absoluteFilePath(file);
}

bool StreamingBridge::readMetaFlag(const QString &savePath, const QString &worldId,
                                   StreamWorldMeta &out)
{
    if (savePath.isEmpty())
        return false;
    ChunkStore probe; // 独立临时 store：纯读面（readStreamWorldMeta 不建表不写库）
    probe.bind(savePath);
    probe.setStreamWorldId(worldId);
    return probe.readStreamWorldMeta(out);
}

bool StreamingBridge::flagNewWorldStreaming(const QString &file, int coreWidth, int coreDepth)
{
    const QString savePath = resolveSavePath(file);
    ChunkStore store;
    store.bind(savePath);
    store.setStreamWorldId(file);
    // D2 创建与 D3 转换同一落点（后端语义原样：置位 + core dims + 代次锚——新库首锚 = 0 = 时序真值）。
    return store.markStreamingWorld(coreWidth, coreDepth);
}

bool StreamingBridge::saveIsStreaming(const QString &file) const
{
    StreamWorldMeta meta;
    if (!readMetaFlag(resolveSavePath(file), file, meta))
        return false; // 无行 / 库缺席 / 读败 → fixed 语义（诚实降级面见类头注）
    return meta.streaming;
}

bool StreamingBridge::convertSaveToStreaming(const QString &file, int coreWidth, int coreDepth)
{
    const QString savePath = resolveSavePath(file);
    // 幂等守卫：已转换（含翻回期残留行 streaming=0 除外——那正是再转换的合法入口）零重锚。
    // 读败（锁/病）→ false 上报不谎报（不动旧行，marker 同门）。
    StreamWorldMeta meta;
    if (readMetaFlag(savePath, file, meta) && meta.streaming)
        return true;
    ChunkStore store;
    store.bind(savePath);
    store.setStreamWorldId(file);
    // D3 同一落点复用（置位 + core dims + 代次锚；零数据搬迁零 chunk_edits 触碰——§29.5.3 选型 2）。
    return store.markStreamingWorld(coreWidth, coreDepth);
}

bool StreamingBridge::enterWorld(World *world, WorldStore *store, WorldClock *clock,
                                 PlayerController *player, const QString &file, int seed)
{
    if (!world || !store) {
        qWarning() << "StreamingBridge::enterWorld: null world/store - refusing";
        return false; // 防御（QML 装配恒双全；不炸不进——caller 走 fixed 链）
    }
    if (store->world() != world) {
        qWarning() << "StreamingBridge::enterWorld: store not bound to this world"
                   << "- rebind required (r2010d discipline)";
        return false; // r2010d rebind 纪律（loadChunks 写入面 = store.world；QML 装配恒同——防御面）
    }

    const QString savePath = resolveSavePath(file);
    StreamWorldMeta meta;
    const bool known = readMetaFlag(savePath, file, meta);

    if (!known || !meta.streaming) {
        // fixed 存档（或标志读败 = 诚实降级走 fixed，qWarning 留痕见 readMetaFlag 消费面）：
        // 清流式残留 + 空网格归位 → caller 走既有 fixed 进入链（逐字节原样 = 默认关零变化墙）。
        detachWorld(); // 旧会话先于模式迁移消亡（线程 join 有界——析构序承重选择）
        if (world->isSparse()) {
            // 上一局流式世界跨世界残留：归位 fixed 空网格（调用序契约——caller 随后 beginLoad /
            //   regenerate 全量卫生重置，见 world.h reinitializeAsFixed 头注）。dims 取宿主当前值
            //   （QML 装配 = worldChunksPerSide 派生，运行期常量——归位精确；sparse core 域即进入时
            //   所取同源 dims）。
            world->reinitializeAsFixed(world->width(), world->depth(), world->height());
        }
        return false;
    }

    // ── 流式进入链（D2 新世界与 D3 转换世界同门）：sparse 重构 → 会话通电 → 绑定 → overlay 读档
    //    → 泵拍 / 位置沿挂钩。五拍全在主线程同步完成（返回即「进入即通电」事实）。──────────────
    detachWorld(); // 跨世界切换：旧会话（含线程件）先于世界重构消亡
    World::SparseWorldParams sp;
    sp.seed = seed;
    sp.coreWidth = meta.coreW > 0 ? meta.coreW : world->width(); // 行缺席 dims 的防御回退（正常行恒有）
    sp.coreDepth = meta.coreD > 0 ? meta.coreD : world->depth();
    sp.height = world->height();   // y 域随宿主（app theWorld 装配值；两模式同构仍有限高）
    sp.spawnPreGenerateRadius = 2; // W1 默认（D4 首屏下限；P5 调参缝不在此——P1 单一来源）
    world->reinitializeAsSparse(sp);

    m_session = std::make_unique<GameSession>(*world); // isSparse() 门内全量通电（W2/W3/W4 三缝）
    if (!m_session->bindChunkEditsStore(savePath) || !m_session->setStreamWorldId(file))
        qWarning() << "StreamingBridge::enterWorld: chunk-edits store bind failed for" << file
                   << "- continuing without persist domain (fail-safe: eviction aborts,"
                   << " all-generate reload)"; // 无冲洗域 fail-safe 两面保守（W3 语义承接）
    store->setWorld(world);                    // r2010d rebind（幂等——QML 装配恒同，防御面）
    m_session->loadStreamingWorld(*store);     // overlay 合并（行回灌 + blob 物化 + 代次仲裁；
                                               //   返回值入会话观测账面，失败诚实降级可玩）
    ensurePumpHook(clock);                     // ticked → pumpTick（与 QML tick 桥同拍同源）
    ensureFeedHook(player);                    // playerChunkChanged → notePlayerChunk（W2 生产链）
    return true;
}

bool StreamingBridge::flushForSave()
{
    if (!m_session)
        return true; // 非流式会话：无冲洗域 → 放行（fixed 三写链逐字节原样 = 默认关零变化）
    if (!m_session->isStreamingWorld())
        return true; // 会话在但本存档未登记流式（fixed 残留边缘）→ 同上放行（零标志零活动同门）
    // 流式：冲洗成败原样穿透（驻留 dirty 落附加表 + 保存代次推进；失败上报不谎报——caller 门三写，
    // 重试重放 = 已落盘行同键盖写无副作用[marker 同门]）。
    return m_session->flushResidentEditsForSave();
}

void StreamingBridge::pumpTick()
{
    if (m_session)
        m_session->pumpStreamingFrame(); // tick 尾流式收割拍（fixed/无会话 = 零动作墙，D2 同门）
}

void StreamingBridge::detachWorld()
{
    m_session.reset(); // 线程件随会话析构 join 有界（gamesession.h 退出语义）；幂等
}

void StreamingBridge::ensurePumpHook(WorldClock *clock)
{
    if (!clock || m_pumpClock == clock)
        return; // 未提供 / 已挂同一源 = 零动作（幂等）
    if (m_pumpConn)
        QObject::disconnect(m_pumpConn); // 换源防御（QML 装配单 clock，生产不走到）
    m_pumpClock = clock;
    // 单时钟权威：与 QML 既有 tick 桥同一 ticked 沿（泵拍 = tick 尾语义的桥侧对位；无第二计时器）。
    m_pumpConn = QObject::connect(clock, &WorldClock::ticked, this, [this](qreal) { pumpTick(); });
}

void StreamingBridge::ensureFeedHook(PlayerController *player)
{
    if (!player || m_feedPlayer == player)
        return; // 未提供 / 已挂同一源 = 零动作（幂等）
    if (m_feedConn)
        QObject::disconnect(m_feedConn);
    m_feedPlayer = player;
    // W2 位置源生产链原样：PlayerController 移动沿（floorDiv16 换格在 C++ 侧）直连会话喂入钩子。
    m_feedConn = QObject::connect(player, &PlayerController::playerChunkChanged, this,
                                  [this](int cx, int cz) {
                                      if (m_session)
                                          m_session->notePlayerChunk(cx, cz);
                                  });
}
