// R20.03 测试分层：main 只剩 app 构造 + 套件运行 + 返回码（原 L227 / L47997-47998）。
// 运行方式不变：build/redstone_matrix_test.exe，全过 exit 0（headless 须 QT_QPA_PLATFORM=offscreen）。
#include "matrix_helpers.h"

int main(int argc, char *argv[])
{
    // t814：QCoreApplication → QGuiApplication —— Game 层 PlayerController 直编（QQuickItem 派生，
    //   构造需 Gui 平台集成；无窗口创建，探针纯对象交互）。
    QGuiApplication app(argc, argv);

    // R20.03 目标 B：--filter <substring> —— 腿名含子串才执行，未命中计 SKIP；
    // 不加该参数时行为与原先逐位一致（全 545 跑）。
    MatrixRun run;
    for (int i = 1; i < argc; ++i) {
        if (QLatin1String(argv[i]) == QLatin1String("--filter") && i + 1 < argc)
            run.legFilter = QString::fromUtf8(argv[++i]);
    }
    run.runAll();
    return run.totalFail == 0 ? 0 : 1;
}
