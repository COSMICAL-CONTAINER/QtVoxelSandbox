#include "streamingbridge.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

#include "chunkstore.h" // stream_worlds 元数据域载体（独立命名连接开-用-关；worldstore 零涉）
#include "entitystagestore.h" // t1142 E1：暂存域行族常量（kStageKindMob——Game→World 向下合规）
#include "savebridge.h" // t1129：生产冲洗缝登记宿主（Game 层同域——桥间单点装配）
#include "entitymanager.h" // t1142 E1 生产装配：entityManager 属性注入读口（QML 注入的运行期
                           //   依赖——PLAN §2 Game→Entities 向下合规，PlayerController 同门先例）
#include "itementitymanager.h" // t1142 E1 生产装配：itemEntities 属性注入读口（同上）

// ── §29.5-W5b 流式 UI 桥实现（r2028；语义与选型立证见头文件类头注）────────────────────────

StreamingBridge *StreamingBridge::instance()
{
    static StreamingBridge inst; // 进程全局唯一（会话状态面；QML create 同对象——BuildInfo 同款）
    // t1129 SAVE-01：生产冲洗缝一次性登记（幂等守卫——首次 instance 即装配，main.cpp 零触碰 =
    //   r2031d 生产零挂载反探幸存面）。两钩成对：flush = 保存事务内把驻留编辑落 chunk_edits
    //   （外部连接直用）；commit = 提交成功尾清本批未落盘账（回滚面绝不调 = 账面收敛）。
    //   【t1129 修订留痕】旧形态 = Main.qml runExitSave 前置行调 flushForSave()（保存链外独立
    //   事务）——那在「冲洗已提交、四面未提交」中断窗留混合代次（新地形旧玩家）；现冲洗随保存
    //   事务原子（saveViaCoordinator 钩内调度），Main.qml 前置行退役（r2028c/r2031d 面钉 lawful
    //   修订：StreamingBridge. 计数 2→1 携沿革注）。
    static bool flushSeamRegistered = false;
    if (!flushSeamRegistered) {
        flushSeamRegistered = true;
        SaveBridge *sb = SaveBridge::instance();
        sb->setFlushHook([](QSqlDatabase &db) { return instance()->flushForSaveOn(db); });
        sb->setFlushCommitHook([]() {
            if (instance()->m_session)
                instance()->m_session->commitFlushResidentEditsOn();
        });
        // t1142 E1：暂存 mob 行载荷合并 provider（审查「实体暂存面与区块暂存同一保存点语义」
        //   ——卸载中实体随保存事务的 entities 段进已提交快照；writeEntitiesPart DELETE+INSERT
        //   全量快照序 = 步⑥b 在冲洗钩后，故合并只能走载荷装配面，事务内直插会被全量重写清掉）。
        //   钩空（非流式会话/无暂存）= 零追加，既有腿零扰动。kind 门 = mob 行归 entities 段
        //   （掉落物行走冲洗钩内 item_entities 快照拍——行族分派单一权威 = kStageKind* 常量）。
        sb->setStagedMobProvider([]() {
            StreamingBridge *b = instance();
            QVariantList out;
            if (!b->m_session || !b->m_session->isStreamingWorld())
                return out;
            const QVariantList staged = b->m_session->stagedEntityRowsForSave();
            for (const QVariant &v : staged)
                if (v.toMap().value(QStringLiteral("kind")).toInt() == kStageKindMob)
                    out.append(v);
            return out;
        });
    }
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
        m_fixedWorld = nullptr; // §29.7：收割宿主引用随态复位（下方 fixed 分支重设）
        if (world->isSparse()) {
            // 上一局流式世界跨世界残留：归位 fixed 空网格（调用序契约——caller 随后 beginLoad /
            //   regenerate 全量卫生重置，见 world.h reinitializeAsFixed 头注）。dims 取宿主当前值
            //   （QML 装配 = worldChunksPerSide 派生，运行期常量——归位精确；sparse core 域即进入时
            //   所取同源 dims）。
            world->reinitializeAsFixed(world->width(), world->depth(), world->height());
        }
        // ── §29.7 t1060 fixed 世界异步烘培使能 + 收割拍挂钩（C1 完全体；QML 零改动）─────────
        // 生产使能点 = 本 fixed 进入分支（W5b 进入链既有 C++ 面在内侧，Main.qml 分流逐字节
        //   原样）。env 回退（QTVOXEL_SYNC_BAKE≠0）在使能缝拒绝 → 世界保持全同步内联（旧路径
        //   逐位 = 实机自救面）。收割宿主 = 本桥既有泵拍：ensurePumpHook 复用与流式泵同一
        //   ticked 连接——pumpTick 无会话（fixed）时拍内薄收割 World::harvestBuiltChunkMeshes
        //   （单拍应用有界 = 风暴摊平；10Hz、非阻塞、延迟 ≤ 一拍）。引用持 QPointer（世界随
        //   QML/矩阵腿存亡——销毁后悬垂自动归零，pumpTick 静默跳过）。
        world->enableFixedAsyncBake();
        ensurePumpHook(clock);
        m_fixedWorld = world;
        return false;
    }

    // ── 流式进入链（D2 新世界与 D3 转换世界同门）：sparse 重构 → 会话通电 → 绑定 → overlay 读档
    //    → 泵拍 / 位置沿挂钩。五拍全在主线程同步完成（返回即「进入即通电」事实）。──────────────
    detachWorld(); // 跨世界切换：旧会话（含线程件）先于世界重构消亡
    m_fixedWorld = nullptr; // §29.7：流式态收割宿主让位（拍内收割归会话 pumpStreamingFrame 独占）
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
    if (!m_session->bindEntityStageStore(savePath))
        qWarning() << "StreamingBridge::enterWorld: entity stage store bind failed for" << file
                   << "- entity unload staging disabled (fail-safe: live entities stay)"; // t1142 E1
    store->setWorld(world);                    // r2010d rebind（幂等——QML 装配恒同，防御面）
    m_session->loadStreamingWorld(*store);     // overlay 合并（行回灌 + blob 物化 + 代次仲裁；
                                               //   返回值入会话观测账面，失败诚实降级可玩）。
                                               //   t1142：会话开启拍（暂存域截断 + 代次升格 +
                                               //   上界守卫）在此一并生效。

    // ── t1142 E1 实体卸载生命周期生产装配（审查 E1 原文「用真实 StreamingBridge 入口，不在
    //    测试中手动补生产缺钩」的接线点）────────────────────────────────────────────────────
    // 两管理器经 player 的 QML 属性注入读口取得（Main.qml itemEntities:/entityManager: 注入——
    //   运行期依赖零构造序耦合，PlayerController 同门）。三缝：驱逐沿 = 先序列化入暂存域后
    //   释放活体（卸载 ≠ 销毁——暂存行随会话回访恰一次恢复 / 随保存事务进已提交快照；暂存写
    //   失败 = 早退不释放 = 宁驻留不误删同门）；落位恢复 = take 暂存行注入两族恢复面（恰一次
    //   = SELECT+DELETE 同拍）；快照 LIVE 源 = 掉落物族全量导出（保存事务内 item_entities 拍）。
    //   管理器缺席（无 QML 注入的裸会话）= 三缝不装配 = 旧 despawnless 语义（诚实降级面）。
    EntityManager *prodMobs = player ? player->entityManager() : nullptr;
    ItemEntityManager *prodItems = player ? player->itemEntities() : nullptr;
    if (prodMobs && prodItems) {
        GameSession *sess = m_session.get();
        sess->setEvictionEntitySink([prodMobs, prodItems, sess](int cx, int cz) {
            const QVariantList mobRows = prodMobs->exportPersistedInChunk(cx, cz);
            const QVariantList itemRows = prodItems->exportPersistedInChunk(cx, cz);
            QVariantList rows = mobRows;
            rows += itemRows;
            if (!sess->stageEntitiesForChunk(cx, cz, rows))
                return; // 暂存域写失败：活体留守驻留 chunk（驱逐沿此刻已被 persist 中止，双保守）
            prodMobs->despawnInChunk(cx, cz);
            prodItems->despawnInChunk(cx, cz);
        });
        sess->setEntityRestoreSink([prodMobs, prodItems, sess](int cx, int cz) -> int {
            const QVariantList rows = sess->takeStagedEntitiesForChunk(cx, cz);
            return prodMobs->restorePersistedEntities(rows)
                + prodItems->restorePersistedRows(rows);
        });
        sess->setLiveItemExportSink([prodItems]() { return prodItems->exportPersistedRows(); });
    }

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

bool StreamingBridge::flushForSaveOn(QSqlDatabase &db)
{
    // t1129 同事务版 flushForSave：守卫同门（无会话 / 未登记流式 = 放行），执行体换外部连接
    //   变体且不清账（提交面才清——GameSession::flushResidentEditsForSaveOn 头注立证）。
    if (!m_session)
        return true;
    if (!m_session->isStreamingWorld())
        return true;
    return m_session->flushResidentEditsForSaveOn(db);
}

void StreamingBridge::pumpTick()
{
    if (m_session) {
        m_session->pumpStreamingFrame(); // tick 尾流式收割拍（W4 ⑤ 段排干语义不变，r2026）
        return;
    }
    // §29.7 t1060 fixed 收割拍（C1 完全体）：无会话（fixed 世界已使能异步烘培）→ 薄收割槽
    //   World::harvestBuiltChunkMeshes（单拍应用有界——黎明星空→白天 dayMul 跨门重烘风暴分帧
    //   摊平；未使能世界该拍恒零动作返回）。世界已亡（矩阵腿间/退出世界）/未进入 → QPointer
    //   空 = 零动作（无悬垂收割面）。
    if (m_fixedWorld)
        m_fixedWorld->harvestBuiltChunkMeshes();
}

void StreamingBridge::detachWorld()
{
    m_session.reset(); // 线程件随会话析构 join 有界（gamesession.h 退出语义）；幂等
    // ── t1076：挂钩随会话一并退役（悬垂源指针 × 栈地址复用的假幂等收口）─────────────────────
    // 两钩的再挂点只在 enterWorld 内（下方 ensurePumpHook / ensureFeedHook 的全部生产调用面）
    // → 挂钩生命周期本就 = 会话生命周期。此前 detachWorld 只拆会话不清挂钩：被挂源对象
    //（WorldClock / PlayerController）先于本桥消亡的场景（矩阵腿间、换世界换对象接线）下，
    // Qt 连接随发送者析构自动断开，而 m_pumpClock / m_feedPlayer 记忆指针悬垂——之后落在
    // 复用栈地址上的新源会被 ensureHook 的「已挂同一源 = 零动作」早退误判为已挂钩（真实连接
    // 已死）→ clock.ticked 泵拍 / playerChunkChanged 位置沿静默断链 → 会话在活却零提交零采用
    // 零驱逐（t1076 复现腿 r2049a/b 进入 2 的病灶签名，t1074 实机「负向走查不收敛」的根因）。
    // 断连 + 句柄与记忆指针清零 = 生命周期对齐的最小收口；「同源重进」路径不经本记忆依赖
    // （enterWorld 先拆后挂全链），幂等语义不破。
    if (m_pumpConn) {
        QObject::disconnect(m_pumpConn);
        m_pumpConn = QMetaObject::Connection();
    }
    m_pumpClock = nullptr;
    if (m_feedConn) {
        QObject::disconnect(m_feedConn);
        m_feedConn = QMetaObject::Connection();
    }
    m_feedPlayer = nullptr;
}

void StreamingBridge::ensurePumpHook(WorldClock *clock)
{
    // t1076：幂等守卫 = 同源**且连接仍活**（bool(Connection) 对发送者已亡的连接为 false）。
    // detachWorld 的挂钩退役清零是悬垂假幂等的主收口，本联言是同源场景的防御半边（源对象
    // 在两次进入之间被销毁重建且同址复用时，不再被「已挂同一源」早退吞掉重挂）。
    if (!clock || (m_pumpClock == clock && m_pumpConn))
        return; // 未提供 / 已挂同一源且连接仍活 = 零动作（幂等）
    if (m_pumpConn)
        QObject::disconnect(m_pumpConn); // 换源防御（QML 装配单 clock，生产不走到）
    m_pumpClock = clock;
    // 单时钟权威：与 QML 既有 tick 桥同一 ticked 沿（泵拍 = tick 尾语义的桥侧对位；无第二计时器）。
    m_pumpConn = QObject::connect(clock, &WorldClock::ticked, this, [this](qreal) { pumpTick(); });
}

void StreamingBridge::ensureFeedHook(PlayerController *player)
{
    // t1076：幂等守卫 = 同源**且连接仍活**（bool(Connection) 对发送者已亡的连接为 false）。
    // detachWorld 的挂钩退役清零是悬垂假幂等的主收口，本联言是同源场景的防御半边（源对象
    // 在两次进入之间被销毁重建且同址复用时，不再被「已挂同一源」早退吞掉重挂）。
    if (!player || (m_feedPlayer == player && m_feedConn))
        return; // 未提供 / 已挂同一源且连接仍活 = 零动作（幂等）
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
