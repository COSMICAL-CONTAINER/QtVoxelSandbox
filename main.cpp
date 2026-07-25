// Phase 0 bootstrap：最小 Qt6 Quick 应用，验证 Qt 6.11.1 工具链能编译运行。
// 下一步：在此窗口嵌入 QQuickRhiItem 空集成（PLAN §3 Phase 0 / §2 不变量 A）。
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("VoxelSandbox", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
