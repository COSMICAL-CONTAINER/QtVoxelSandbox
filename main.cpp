#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include "frameprofiler.h"
#include "resourcepackmanager.h"
// t813 构建版本戳（Core 叶子）：启动日志自报家门 —— 每份 logs/voxelsandbox.log 首段即可
//   核对「该日志由哪个构建产出」，与主菜单角落 / F3 显示同源（BuildInfo 单一权威出口）。
#include "buildinfo.h"

// t414 image provider：把 ResourcePackManager 合成的运行期图集（默认图集 + 包覆盖）以
//   image://rp/atlas 暴露给 QML 的 terrain Texture。QtQuick 依赖留在此 app 胶水层（Core 不沾 QtQuick）。
//   合成图集构建幂等（首调解析包 + 覆盖瓦片并缓存），无包时返回程序生成默认图集像素。
class ResourcePackAtlasProvider : public QQuickImageProvider
{
public:
    ResourcePackAtlasProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        Q_UNUSED(id);
        Q_UNUSED(requestedSize);
        QImage img = ResourcePackManager::compositeAtlas();
        if (size)
            *size = img.size();
        return img;
    }
};

// --- 日志系统（内联；后续抽成 Core/Logger 模块，PLAN §2 不变量 F）---
static QFile *g_log = nullptr;
static void logHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    static QMutex m;
    QMutexLocker lock(&m);
    if (!g_log || !g_log->isOpen())
        return;
    const char *lvl = "?";
    switch (type) {
    case QtDebugMsg:    lvl = "DBG"; break;
    case QtInfoMsg:     lvl = "INF"; break;
    case QtWarningMsg:  lvl = "WRN"; break;
    case QtCriticalMsg: lvl = "CRT"; break;
    case QtFatalMsg:    lvl = "FTL"; break;
    }
    QTextStream s(g_log);
    s << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << ' ' << lvl << ' '
      << (ctx.category ? ctx.category : "app") << " | " << msg << '\n';
    s.flush();
}

// 日志文件路径解析（dev-spec t48）：日志移出 build/。
//   1) 开发期 exe 在 <工程根>/build/ 下 → <exeDir>/../logs 解析为 <工程根>/logs，
//      开发者一眼能找到（不再埋进 build/）。
//   2) 部署期（exe 装在 Program Files 等）上面那路径无写权限 → 降级到系统 AppLocalData。
//   3) 都不行则兜底回 exe 同级（旧行为，至少不崩）。
// mkpath 既是「确保目录存在」也是「写权限探针」：不可写时创建失败 → 跳到下一个候选。
static QString resolveLogFilePath()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/../logs")).absolutePath(),                  // 1) <工程根>/logs
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)      // 2) 部署降级
    };
    for (const QString &dir : candidates) {
        if (!dir.isEmpty() && QDir().mkpath(dir))
            return QDir(dir).absoluteFilePath(QStringLiteral("voxelsandbox.log"));
    }
    return exeDir + QStringLiteral("/voxelsandbox.log");                            // 3) 兜底
}

int main(int argc, char *argv[])
{
    qputenv("QSG_INFO", "1"); // 启动时打印所选 RHI 后端（走 scenegraph 日志 -> 文件）

    QGuiApplication app(argc, argv);

    static QFile logFile;
    logFile.setFileName(resolveLogFilePath());
    // open() 带 [[nodiscard]]，必须检查返回值。失败则降级：logHandler 在 !isOpen() 时
    // 静默丢弃消息，应用仍可运行（PLAN §2-E 错误模型：保持运行而非崩溃）。
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning("无法打开日志文件 %s（%s）；运行期日志将被丢弃。",
                 qPrintable(logFile.fileName()),
                 qPrintable(logFile.errorString()));
    }
    g_log = &logFile;
    qInstallMessageHandler(logHandler);
    // 收紧：关掉最啰嗦的 debug category，只留警告（日志从 ~1.2MB 降到几 KB）。
    QLoggingCategory::setFilterRules(
        "qt.qpa.*=false\n"
        "qt.scenegraph.general=false\n"
        "qt.scenegraph.renderloop=false\n"
        "qt.scenegraph.time.*=false\n");

    qInfo() << "=== voxelsandbox start ===";
    // t813 构建版本戳：构建时间 + git 短哈希落日志（用户发来日志即可判新旧 exe）。
    qInfo() << "build:" << BuildInfo::instance()->full();
    qInfo() << "log file:" << logFile.fileName();
    qInfo() << "graphics api (enum):" << int(QQuickWindow::graphicsApi());

    QQmlApplicationEngine engine;
    // t414：注册资源包图集 image provider（image://rp/atlas）。必须在 loadFromModule 之前注册，
    //   供 Main.qml 的 terrain Texture（voxelAtlas）按需拉取合成图集。engine 接管 provider 生命周期。
    engine.addImageProvider(QStringLiteral("rp"), new ResourcePackAtlasProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { qCritical("QML objectCreationFailed"); QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("VoxelSandbox", "Main");
    qInfo() << "root objects after load:" << engine.rootObjects().size();
    if (engine.rootObjects().isEmpty())
        return -1;

    // FPS：frameSwapped（GUI 线程，每帧）+1，每秒写到 QML 的 fps 属性。
    // perf-t520 帧时间分解桶（main_total / render_cpu）：frameSwapped 间隔 ≈ GUI 线程帧周期
    //   （含 sim + QML binding/scenegraph update + 同步等待）；beforeRendering → afterRendering
    //   ≈ 渲染线程 CPU 侧编码 + GPU 提交阻塞（非真 GPU 时间，QtQuick3D 路径无公开 GPU 计时查询）。
    //   两者推入 FrameProfiler 单例，1s 窗口 ÷ frames 得 ms/frame → F3 / logs 报告区分主线程 vs 渲染线程瓶颈。
    //   threaded render loop 诊断公式：frame ≈ max(main_total, render_cpu)。
    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
        auto *frames = new int(0);
        // main_total：本帧 frameSwapped 与上帧 frameSwapped 的间隔（ns）。首帧 lastNs=0 跳过（无基准）。
        // t904：swap 时先结清 idleB（afterSynchronizing→frameSwapped，见上方四段归因注释）。
        // t934：swap 时顺带结清 fPresent（afterRendering[渲染线程]→frameSwapped[GUI 收到]；见下方 t934 注释）。
        auto *lastSwapNs = new qint64(0);
        auto *afterSyncNs = new qint64(0);
        auto *afterRenderDoneNs = new qint64(0); // t934：渲染线程 afterRendering 时刻（0 = 未发 / 已结清）
        QObject::connect(win, &QQuickWindow::frameSwapped, win, [frames, lastSwapNs, afterSyncNs, afterRenderDoneNs]() {
            ++(*frames);
            const qint64 now = FrameProfiler::nowNs();
            if (*afterSyncNs > 0)
                FrameProfiler::instance()->addSampleMs(QStringLiteral("fIdleB"), double(now - *afterSyncNs) / 1e6);
            *afterSyncNs = 0;
            if (*afterRenderDoneNs > 0) // t934 present 段：渲染线程编完 → GUI 收到 swap 的全部时间
                FrameProfiler::instance()->addSampleMs(QStringLiteral("fPresent"),
                                                       double(now - *afterRenderDoneNs) / 1e6);
            *afterRenderDoneNs = 0;
            if (*lastSwapNs > 0) {
                const double ms = double(now - *lastSwapNs) / 1e6;
                FrameProfiler::instance()->addSampleMs(QStringLiteral("main_total"), ms);
            }
            *lastSwapNs = now;
        });
        // render_cpu：渲染线程内 beforeRendering → afterRendering 的耗时（DirectConnection = 在发射线程
        //   即渲染线程上运行 lambda）。beforeRendering / afterRendering 之间是 QtQuick3D 渲染队列的
        //   场景图 commit + draw call 编码 + RHI 提交（含 GPU stall）。仅计时，不改渲染流程。
        auto *renderStartNs = new qint64(0);
        QObject::connect(win, &QQuickWindow::beforeRendering, win, [renderStartNs]() {
            *renderStartNs = FrameProfiler::nowNs();
        }, Qt::DirectConnection);
        QObject::connect(win, &QQuickWindow::afterRendering, win, [renderStartNs, afterRenderDoneNs]() {
            if (*renderStartNs > 0) {
                const double ms = double(FrameProfiler::nowNs() - *renderStartNs) / 1e6;
                FrameProfiler::instance()->addSampleMs(QStringLiteral("render_cpu"), ms);
                *renderStartNs = 0; // 守卫：防 beforeRendering 漏一帧导致 stale 算
            }
            // t934：标记「渲染线程本帧编码完成」时刻 —— frameSwapped（GUI 收到）与此差的 fPresent 段
            //   = present/vsync 阻塞 + queued 派发延迟（threaded 循环下渲染线程 swap 完才 emit，swap 若
            //   被 GPU 排队卡住，本段吸收该阻塞）。render_cpu 小而 waitSync 大时，本段大 → GPU/present
            //   bound（配合 F3 render-side 行 gpu 真值判读）；本段也小 → 渲染线程忙在别处（prep/清理）。
            *afterRenderDoneNs = FrameProfiler::nowNs();
        }, Qt::DirectConnection);
        // qmlSync：GUI 线程 beforeSynchronizing → afterSynchronizing = QML scene-graph 同步期（Node 树 commit
        //   到渲染侧：transform/geometry/material/draw-list 重算）。mob delegate 节点爆炸 / 绑定扇出等成本藏在这里
        //   —— F3 既有桶（sim/main_total/render_cpu）都不单独覆盖此段（main_total 含它但不拆）。1s 窗口 ÷ frames
        //   报 ms/frame，与 sim 相加逼近 main_total 即证实瓶颈归因（残差 = 事件循环空闲 / 其它）。
        //   beforeSynchronizing/afterSynchronizing 在 GUI 线程发射（默认 AutoConnection = 直接调用），无需
        //   DirectConnection。**t904 起连接体并入下方四段归因三连**（beforeSynchronizing 顺带结清 waitSync、
        //   afterSynchronizing 顺带开启 idleB 计时）—— 本块只保留 syncStartNs 声明，勿在此重复 connect
        //   （双连接 = qmlSync 桶双计）。
        // t904 perf residual 四段归因：residual（= main_total − sim − qmlSync，用户实测 32.8ms/帧 @18FPS）此前
        //   是「未插桩黑盒」。GUI 线程一帧周期可按渲染管线 hook 点切成四段（全部 GUI 线程信号，与既有
        //   frameSwapped/beforeSynchronizing 同一套）：
        //     frameSwapped ──idleA──> afterAnimating ──waitSync──> beforeSynchronizing ──qmlSync(既有)──>
        //     afterSynchronizing ──idleB──> 下一 frameSwapped
        //   - idleA（frameSwapped→afterAnimating）：事件循环处理（16ms 游戏 tick = sim 计时所在 + QML 绑定求值 +
        //     其它 QML Timer）+ 纯空闲。residual 里的「未插桩主线程重活」藏此段 → 报告里再减 sim 得「非 sim 事件段」。
        //   - waitSync（afterAnimating→beforeSynchronizing）：threaded render loop 下 GUI 线程阻塞等渲染线程抵达
        //     同步屏障（渲染线程还在跑上一帧的渲染 / present-vsync）→ 大值 = 渲染侧拖慢帧节奏（vsync/提交 bound）。
        //   - idleB（afterSynchronizing→frameSwapped）：GUI 已放行、渲染线程渲染 + present；basic（单线程）渲染
        //     循环下这段 = 渲染本体在 GUI 线程跑（idleB ≈ render_cpu + present）。两机制都可由本段量值分辨。
        //   由构造 main_total ≈ idleA + waitSync + qmlSync + idleB → residual ≈ (idleA−sim) + waitSync + idleB，
        //   黑盒完全归因到命名段。frameSwapped 连接（AutoConnection）在 threaded 循环下经队列回 GUI 线程派发 →
        //   idleB 含少量派发延迟（与既有 main_total 同口径，一致性优先）。0 值守卫：漏 hook 的帧跳过该段样本
        //   （某段恒 0 = 该 hook 未发 —— 如 basic 循环不发 afterAnimating，frame2 行 evA/waitSync 恒 0 本身即判据）。
        //   afterSyncNs 与 frameSwapped 块共用同一指针（上方已声明），afterAnimNs / syncStartNs 在此声明。
        // t934 waitSync 归因（dev-plan R19.17 性能批二：用户实测 waitSync 76ms 一家独大、render_cpu ~7ms /
        //   RenderStats render ~1ms——渲染 pass 本身不慢，GUI 却在同步屏障阻塞 76ms）。机械链：waitSync 段内
        //   GUI 等渲染线程抵达屏障，而渲染线程必须先跑完**上一帧**的 [渲染 pass（render_cpu 桶）+ present/
        //   vsync（GPU 落后时 swap 阻塞，D3D11 FIFO 2 缓冲可等数个帧周期）+ 帧尾清理]。render_cpu 小而
        //   waitSync 大 → 时间去向只剩三汇，判读口（F3）：
        //     ① GPU/present bound：F3 render-side 行 gpu（RenderStats.lastCompletedGpuTime，**真 GPU ms**）
        //       同量级大 + frame2 行 present（fPresent，本任务新增：afterRendering[渲染线程]→frameSwapped
        //       [GUI 收到]）大 → 治理面 = 降 GPU 负载（renderDistance 已有 / 段折叠已有 / 透明 overdraw）。
        //     ② 渲染线程 prep/上传风暴（chunk mesh 重建 → 顶点缓冲重传 + 渲染列表重建，t930/t933 风暴的
        //       渲染侧回声）：F3 render-side 行 prep（RenderStats.renderPrepareTime）大 + win 行 mesh reb
        //       计数非 0 → 治理面 = 节流/合批（t933 已把触发面 101→2，残余是否为 0 由 act ct / reb 读出）。
        //     ③ 渲染合帧/hook 多发：frame2 行各段 (N) 样本数 > main 的 N → 四段恒等式不可加（拥塞下
        //       animation tick 每事件循环回合一拍、渲染按 vsync 合帧），是测量口径而非独立开销。
        //   fPresent 桶与 (N) 计数即本任务的插桩交付；①② 的数值面由 F3 render-side 行（Main.qml）提供。
        auto *afterAnimNs = new qint64(0);
        auto *syncStartNs = new qint64(0);
        QObject::connect(win, &QQuickWindow::afterAnimating, win, [afterAnimNs, lastSwapNs]() {
            const qint64 now = FrameProfiler::nowNs();
            if (*afterAnimNs == 0 && *lastSwapNs > 0)
                FrameProfiler::instance()->addSampleMs(QStringLiteral("fIdleA"), double(now - *lastSwapNs) / 1e6);
            *afterAnimNs = now;
        });
        QObject::connect(win, &QQuickWindow::beforeSynchronizing, win, [syncStartNs, afterAnimNs]() {
            const qint64 now = FrameProfiler::nowNs();
            if (*afterAnimNs > 0)
                FrameProfiler::instance()->addSampleMs(QStringLiteral("fWaitSync"), double(now - *afterAnimNs) / 1e6);
            *afterAnimNs = 0; // 本帧 idleA/waitSync 已结清
            *syncStartNs = now;
        });
        QObject::connect(win, &QQuickWindow::afterSynchronizing, win, [syncStartNs, afterSyncNs]() {
            const qint64 now = FrameProfiler::nowNs();
            if (*syncStartNs > 0)
                FrameProfiler::instance()->addSampleMs(QStringLiteral("qmlSync"), double(now - *syncStartNs) / 1e6);
            *syncStartNs = 0;
            *afterSyncNs = now;
        });
        auto *fpsTimer = new QTimer(win);
        fpsTimer->setInterval(1000);
        QObject::connect(fpsTimer, &QTimer::timeout, win, [win, frames]() {
            win->setProperty("fps", *frames);
            *frames = 0;
        });
        fpsTimer->start();
    }

    int code = app.exec();
    qInfo() << "app.exec returned" << code;
    return code;
}
