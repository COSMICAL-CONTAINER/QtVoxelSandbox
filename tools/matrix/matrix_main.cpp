// R20.03 测试分层：main 只剩 app 构造 + 套件运行 + 返回码（原 L227 / L47997-47998）。
// 运行方式不变：build/redstone_matrix_test.exe，全过 exit 0（headless 须 QT_QPA_PLATFORM=offscreen）。
#include "matrix_helpers.h"

int main(int argc, char *argv[])
{
    // t814：QCoreApplication → QGuiApplication —— Game 层 PlayerController 直编（QQuickItem 派生，
    //   构造需 Gui 平台集成；无窗口创建，探针纯对象交互）。
    QGuiApplication app(argc, argv);

    MatrixRun run;
    run.runAll();
    return run.totalFail == 0 ? 0 : 1;
}
