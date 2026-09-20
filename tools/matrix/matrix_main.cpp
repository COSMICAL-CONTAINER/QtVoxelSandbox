// R20.03 测试分层：main 只剩 app 构造 + 套件运行 + 返回码（原 L227 / L47997-47998）。
// 运行方式不变：build/redstone_matrix_test.exe，全过 exit 0（headless 须 QT_QPA_PLATFORM=offscreen）。
// ── 已知 flake 名单（t1071 起在主入口登记，r2045d 在场钉；复跑清协议不变：单轮挂红先复跑，
//    复红才判 FAIL）──
// 已知 flake = t882/t891/t927/t960（t882 mob 首游荡窗内甩钩 RNG；t891 火充能直燃 pumpFor 实时钟
//   敏感；t927/t960 时序/量测窗类，游走噪声同量级）。t789（羊色统计 λ 薄尾）已 t1071 信封加固退出
//   名单（绝对出现断言退役 → 确定性源钉 + 统计带，≥10 连跑全绿实证）。
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
