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

#include "resourcepackmanager.h"

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
    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
        auto *frames = new int(0);
        QObject::connect(win, &QQuickWindow::frameSwapped, win, [frames]() { ++(*frames); });
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
