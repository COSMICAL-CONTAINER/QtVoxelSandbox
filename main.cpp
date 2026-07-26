#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QTimer>

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

int main(int argc, char *argv[])
{
    qputenv("QSG_INFO", "1"); // 启动时打印所选 RHI 后端（走 scenegraph 日志 -> 文件）

    QGuiApplication app(argc, argv);

    static QFile logFile;
    logFile.setFileName(QCoreApplication::applicationDirPath() + "/voxelsandbox.log");
    logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
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
