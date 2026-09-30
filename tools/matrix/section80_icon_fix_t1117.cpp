#include "matrix_helpers.h"

#include <QFileInfo> // t1117 全目录扫面(entryInfoList 像素扫描)
#include <QImage>

// t1117 t1112 三空白图标修复探针段(2 腿;filter 词 r2087;矩阵 885→887)。置尾先例沿用(接 section79,
//   runAll 末执行,rig 世界零接触——纯资产/源钉腿,零世界零 rig)。
//
// ── 现状核实裁定表(t1116 现场新发现清偿;派工三件逐一 python 逐张 alpha 实测 + 全仓 grep 实读)──
//   ① 三图标空白:全仓 147 张 git 跟踪 icon_*.png 逐张 python PNG 解码 alpha 扫描——零不透明像素
//     恰 3 张 = icon_fence_gate / icon_glass_pane / icon_cake(各 64×64 ct6 4096 像素全透明;
//     stone_button / wood_button 136 opaque 属正常细杆形状,无第四张空白)。根因:t1112 只登记
//     PARTIALS_3D_T1112 表未落 render_partial_3d 的 fence_gate / pane / cake shape 分支 → 循环
//     调 render_partial_3d 落 else 提前返回**空画布**。交付 = 生成器三分支补齐(PartialBlock-
//     Geometry 同构盒集:栅栏门合态沿 Z 端柱双盒+双横档 / 玻璃板满连十字柱板 4/16 截面 / 蛋糕
//     bites=0 内缩 1px 矮盒 8/16 高)+ 三 PNG 单图标点名重生成(模块级直调,禁全量重烘——t1113
//     119 张事故纪律)。在盘非空自证(重生成当轮 python 实测):fence_gate 1428/4096、
//     glass_pane 1952/4096、cake 1733/4096 不透明像素。
//   ② 零行为变更面(如实核实留痕):FenceGate / GlassPane / Cake 三块**均不在** isPackDerivedIconFamily
//     族、resourcepackmanager.cpp 无 ShapeFenceGate / ShapeGlassPane / ShapeCake atlasIconSpecForBlock
//     特型 case(负面钉三针)→ 三块 qrc icon_X.png 是 iconSourceForBlock 回退链的**唯一图标源**
//     (区别 t1116 IronBars:族内先答程序图集重渲,qrc 稿是降级兜底层)→ 空白 PNG = 图标全场景空白,
//     PNG 重生成即全修复;C++ 侧行零触碰(iconFileForBlock 三 case 行 / CMake 资源行 / 家族行原样)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文)──
//   NEG-1 = 摘 tools/build_cube_icons.py 的 fence_gate shape 分支整块(elif 行 + boxes 表 + y_min
//     行整块删除;工具 .py 不进 C++ 构建 = 编译仍绿)→ 恰红 = {r2087b}(elif 分支行源钉归零 = 腿级
//     FAIL;r2087a 不含该行任何形态源钉——图标在盘非空钉读的是 PNG 本体,摘生成器分支不触达在盘
//     资产 = 幸存)。pane / cake 分支行不在摘面 = 幸存。
//   (本单 NEG ×1;无 C++ 摘面——零行为变更面即本批交付语义,无 C++ 面可摘。)
namespace {

// 源钉根路径(section79 同款:applicationDirPath/../;r2086a 图标路径同门)。
inline QString repoRootForIconFixPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absolutePath();
}

// 原始读含(注释体锚/负面面——pinSet 剥注释会失配,section75..79 同款)。
inline bool rawContainsIconFix(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 图标在盘非空钉(QImage 逐像素 alpha 扫描;r2086a 同款钉法升级为逐像素计数——非空 = 至少 1 像素
//   alpha>0,返回实际计数供 diag;零不透明像素 = t1112 三空白图标缺口本体面)。
inline int countNonTransparentPixelsIconFix(const QImage &icon)
{
    int n = 0;
    for (int yy = 0; yy < icon.height(); ++yy)
        for (int xx = 0; xx < icon.width(); ++xx)
            if (qAlpha(icon.pixel(xx, yy)) > 0)
                ++n;
    return n;
}

} // namespace

void MatrixRun::section80_icon_fix_t1117()
{
    // ── r2087a:空白图标修复柱(交付本体面)──────────────────────────────────────────────────────
    //   三 PNG 在盘非空(逐像素 alpha 扫描 ×3)+ 全目录零空白扫面(顺手核查固化:icon_*.png 全集
    //   无第四张零不透明)+ 运行期 iconSourceForBlock 三行非空(回退链唯一图标源=本 PNG)+
    //   调色板三行在册。
    runLeg("r2087a blank icon repair column (the three repaired icons answer real non"
        " blank images on disk with per pixel alpha scans, the icon directory sweeps"
        " clean of zero opaque images, the runtime icon sources for the three palette"
        " rows stay resolvable, and the palette carries all three entries)",
        [&]() {
        bool ok = true;
        QString diag;
        // (1) 三 PNG 在盘非空(逐像素 alpha 计数 >0;重生成资产本体面)。
        const QString root = repoRootForIconFixPins();
        const QStringList names = {
            QStringLiteral("icon_fence_gate.png"),
            QStringLiteral("icon_glass_pane.png"),
            QStringLiteral("icon_cake.png"),
        };
        for (const QString &name : names) {
            const QImage icon(root + QStringLiteral("/textures/") + name);
            const int nonEmpty = icon.isNull() ? 0 : countNonTransparentPixelsIconFix(icon);
            const bool iconOk = !icon.isNull() && nonEmpty > 0;
            ok = ok && iconOk;
            if (!iconOk)
                diag += QStringLiteral("[%1 null=%2 nz=%3]")
                    .arg(name).arg(icon.isNull() ? 1 : 0).arg(nonEmpty);
        }
        // (2) 全目录零空白扫面(顺手核查面固化:icon_*.png 全集逐张扫,零不透明像素 = FAIL)。
        {
            const QDir iconDir(root + QStringLiteral("/textures"));
            int swept = 0;
            bool sweepOk = true;
            const QFileInfoList files = iconDir.entryInfoList(
                QStringList { QStringLiteral("icon_*.png") }, QDir::Files);
            for (const QFileInfo &fi : files) {
                const QImage icon(fi.absoluteFilePath());
                const int nonEmpty = icon.isNull() ? 0 : countNonTransparentPixelsIconFix(icon);
                ++swept;
                if (nonEmpty == 0) {
                    sweepOk = false;
                    diag += QStringLiteral("[blank %1]").arg(fi.fileName());
                }
            }
            sweepOk = sweepOk && swept >= 140; // 目录面在册下界(147 张实测口径——防目录移位假绿)
            ok = ok && sweepOk;
            if (!sweepOk) diag += QStringLiteral("[sweep n=%1]").arg(swept);
        }
        // (3)(4) 运行期非空钉 ×3 + 调色板行在册钉 ×3(三块 qrc PNG = 回退链唯一图标源——
        //     非空即调色板行可解析;空白 PNG 时代本行恒空串)。
        {
            Hotbar hb;
            const QList<int> ids = { int(BR::FenceGate), int(BR::GlassPane), int(BR::Cake) };
            for (const int id : ids) {
                const bool srcOk = !hb.iconSourceForBlock(id).isEmpty();
                ok = ok && srcOk;
                if (!srcOk) diag += QStringLiteral("[src %1]").arg(id);
                const bool palOk = hb.creativeBlocks().contains(QVariant(id));
                ok = ok && palOk;
                if (!palOk) diag += QStringLiteral("[pal %1]").arg(id);
            }
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2087a blank icon repair column (the three repaired icons answer real"
               " non blank images on disk with per pixel alpha scans, the icon directory"
               " sweeps clean of zero opaque images, the runtime icon sources for the"
               " three palette rows stay resolvable, and the palette carries all three"
               " entries)"
            << (ok ? QString() : diag);
    });

    // ── r2087b:结构钉族(NEG-1 敏感面 = 生成器 fence_gate 分支行)────────────────────────────
    //   生成器三分支源钉(fence_gate 分支行 = NEG-1 摘面,本腿专权;pane/cake 不在摘面)+ 名册表
    //   三行钉 + CMake 段行钉 + iconFileForBlock 三 case 行幸存钉 + 家族行钉(IronBars 双形态
    //   minCount=2)+ 三 shape 无 atlas spec 负面钉 ×3(qrc 唯一图标源核实的留痕形态)+ 生成器
    //   沿革锚。
    runLeg("r2087b structure pin family (the generator carries the three shape branches"
        " and the roster table rows on file, the cmake section row and the icon case"
        " rows and the family row are pinned, and the three repaired blocks stay"
        " outside the atlas icon family with no atlas spec cases on file)", [&]() {
        bool ok = true;
        QString diag;
        const QString root = repoRootForIconFixPins();
        // (B1) 生成器三分支源钉(NEG-1 摘 fence_gate 分支 → 零命中 = 恰红;pane/cake 幸存)+
        //     名册表三行钉(表-分支收敛面)。
        const QStringList missPy = pinSet(root + QStringLiteral("/tools/build_cube_icons.py"), {
            SrcPin("fence_gate branch row", "elif shape == \"fence_gate\":", 1),
            SrcPin("pane branch row", "elif shape == \"pane\":", 1),
            SrcPin("cake branch row", "elif shape == \"cake\":", 1),
            SrcPin("roster fence_gate row", "(\"fence_gate\", \"fence_gate\", \"default_wood\"", 1),
            SrcPin("roster pane row", "(\"glass_pane\", \"pane\",       \"default_glass\"", 1),
            SrcPin("roster cake row", "(\"cake\",       \"cake\",       \"default_cake_top\"", 1),
        });
        ok = ok && missPy.isEmpty();
        if (!missPy.isEmpty())
            diag += QStringLiteral("[py %1]").arg(missPy.join(QLatin1Char(',')));
        // (B2) CMake 段行钉(本段注册面)。
        const QStringList missCm = pinSet(root + QStringLiteral("/CMakeLists.txt"), {
            SrcPin("cmake section80", "tools/matrix/section80_icon_fix_t1117.cpp", 1)});
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        // (B3) iconFileForBlock 三 case 行幸存钉(零行为变更面:本批零 C++ 触碰,行原样)+
        //     家族行钉(IronBars 双形态:iconFileForBlock case 行 + isPackDerivedIconFamily 入族行,
        //     剥注释后同前缀——minCount=2 锁两形态在位)。
        const QStringList missHb = pinSet(root + QStringLiteral("/src/Game/hotbar.cpp"), {
            SrcPin("gate icon case", "case BlockRegistry::FenceGate:        return \"icon_fence_gate.png\";", 1),
            SrcPin("pane icon case", "case BlockRegistry::GlassPane:        return \"icon_glass_pane.png\";", 1),
            SrcPin("cake icon case", "case BlockRegistry::Cake:             return \"icon_cake.png\";", 1),
            SrcPin("iron bars family rows", "case BlockRegistry::IronBars:", 2),
        });
        ok = ok && missHb.isEmpty();
        if (!missHb.isEmpty())
            diag += QStringLiteral("[hb %1]").arg(missHb.join(QLatin1Char(',')));
        // (B4) 三 shape 无 atlas spec 负面钉 ×3(qrc 唯一图标源核实的留痕形态:resourcepackmanager
        //     零命中 → 三块不走程序图集重渲 → 空白 PNG 时代图标恒空白、PNG 在盘非空即全修复)。
        const QStringList atlasNeg = {
            QStringLiteral("ShapeFenceGate"),
            QStringLiteral("ShapeGlassPane"),
            QStringLiteral("ShapeCake"),
        };
        for (const QString &needle : atlasNeg) {
            const bool absent = !rawContainsIconFix(
                root + QStringLiteral("/src/Core/resourcepackmanager.cpp"), needle);
            ok = ok && absent;
            if (!absent)
                diag += QStringLiteral("[atlas %1]").arg(needle);
        }
        // (B5) 生成器沿革锚(t1117 批头注锚注在盘;注释体锚走 raw 含)。
        const bool lineageOk = rawContainsIconFix(
            root + QStringLiteral("/tools/build_cube_icons.py"), QStringLiteral("t1117"));
        ok = ok && lineageOk;
        if (!lineageOk) diag += QStringLiteral("[lineage]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2087b structure pin family (the generator carries the three shape"
               " branches and the roster table rows on file, the cmake section row and"
               " the icon case rows and the family row are pinned, and the three repaired"
               " blocks stay outside the atlas icon family with no atlas spec cases on"
               " file)"
            << (ok ? QString() : diag);
    });
}
