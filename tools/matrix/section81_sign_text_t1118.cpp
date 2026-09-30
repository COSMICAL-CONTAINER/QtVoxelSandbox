#include "matrix_helpers.h"

#include "signboardfont.h" // t1118 牌板面字模/映射探针（Game 层纯函数直调）

#include <QFileInfoList> // 负面钉：textures 全目录字体资产扫面

// t1118 牌板面文字渲染探针段(3 腿;filter 词 r2088;矩阵 887→890)。置尾先例沿用(接 section80,
//   runAll 末执行,rig 世界零接触——纯像素/源钉腿,零世界零 rig)。
//
// ── 现状核实裁定表(t1113 关单登记「文本存 SignStore、瓦面=空白刻线板」候选池清偿;派工三件逐一实读)──
//   ① 呈现面选型(实读留痕):图集文本瓦烘焙不可行——牌子文本 per-sign 动态无界,terrain 图集是共享
//     静态资源(compositeAtlas 程序图集+包覆盖),无 per-sign 动态瓦区可分配,且 revision 变更须整
//     chunk 网格重建才可见;Quick 3D 字体路径(系统字体)违 §9 零字体文件+原创点阵字模纪律;故取
//     paintingHost QML delegate 同门(世界内异形呈现层先例——per-sign 独立视觉、随 revision 刷新
//     不经网格重建)。牌子世界内几何 = partialblockgeometry.cpp StandingSign/WallSign 双 case 板面盒
//     (signBoardBoxes 同源编码)。
//   ② 字形工程(§9):原创 9 行点阵字模(signboardfont.cpp lookupGlyph——7 主体行+2 下伸行、5 列宽、
//     步进 6),85 字模在册(大写 26+小写 26+数字 10+基础标点 22 含空格);覆盖面外(含 CJK/扩充标点
//     [ ] { } | \ ~ ^)→ 缺字框占位。零字体文件(textures 全目录零 *font* 资产负面钉)+零 MC 资产名。
//   ③ 文本→板面映射:行带对齐(sign_board 瓦[209]四行淡文本带瓦行 5/7/9/11 × 6px/瓦px = 画布行
//     30/42/54/66 顶,节距 12px);站牌画布 96×96(板面 12/16×12/16)/挂墙牌 128×96(满格宽×12/16),
//     1 方块 = 128px 口径;行内水平居中;空行零墨(空文本牌子 = 既有空白刻线板不变——呈现层对无
//     文本牌子不挂 delegate)。刷新 = signStore.revision 入 Texture URL 失效键(mapstore revision-URL
//     同门),文本变更不经 delegate 重建。
//   ④ 存档/机制面零触碰:SignStore 仅加 active() 静态桥(main.cpp provider 拉取面,MapStore::active
//     同门)+析构注销——存储形状/落盘/机制零改动;文本写入/截断/round-trip 面 r2083c 既钉原样。
//
// ── NEG 面与豁免设计(恰红归因先于腿文)──
//   NEG-1 = 摘 src/Game/signboardfont.cpp renderBoard 内「img.setPixel(px, py, kInkColor);」整行
//     (守卫行幸存→编译零警告仍绿;C++ 面)→ 恰红 = {r2088a}(行为腿:已知文本墨像素计数归零 = 映射
//     写点失效;r2088b 幸存——其行为面只读画布几何尺寸不读墨;r2088c 幸存——零重叠,该行不入钉面)。
//   NEG-2 = 摘 src/ui/Main.qml signHost 的「function onSignChanged() { signHost.reconcileVis() }」
//     整行(QML 面,不进 C++ 构建 = 编译仍绿;Connections 块余壳合法)→ 恰红 = {r2088b}(接线腿 raw
//     钉该行;r2088a 行为腿不涉源钉 = 幸存;r2088c 零重叠 = 幸存)。
//   (双摘面互不重叠:NEG-1 在 Game 层像素写点、NEG-2 在 QML 接线行;各由本段专权腿持有。)

namespace {

// 源钉根路径(section80 同款:applicationDirPath/../)。
inline QString repoRootForSignTextPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absolutePath();
}

// 原始读含(QML 接线锚/负面面——pinSet 剥注释会失配,section76 同款)。
inline bool rawContainsSignText(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 墨像素计数(alpha>0 计数;r2088a/r2088b 行为面)。
inline int countInkPixelsSignText(const QImage &img)
{
    int n = 0;
    for (int yy = 0; yy < img.height(); ++yy)
        for (int xx = 0; xx < img.width(); ++xx)
            if (qAlpha(img.pixel(xx, yy)) > 0)
                ++n;
    return n;
}

// 行 i 的墨行区间 [kFirstLineTop + i*kLinePitch, +kGlyphRows)。
inline int signTextLineTop(int line)
{
    return SignBoardFont::kFirstLineTop + line * SignBoardFont::kLinePitch;
}

} // namespace

void MatrixRun::section81_sign_text_t1118()
{
    // ── r2088a:字模/映射柱(NEG-1 敏感面 = renderBoard 墨写点行)────────────────────────────
    //   程序字模存在面(85 在册全覆盖扫 + 覆盖面外反判 + 缺字框形态钉)+ 文本→像素映射正确性
    //   (已知文本 → 墨非空 + 行序正确[行 0/行 2 各单行文本墨行区间互斥且各落其带] + 行内水平
    //   居中) + 空文本恒空白面(4 空行 → 全透明,双形态)。
    runLeg("r2088a glyph and mapping column (the procedural bitmap table answers all eighty"
        " five registered glyphs with the out of coverage chars rejected to the missing box"
        " mask, known text renders non empty ink on both board canvases with the line order"
        " pinned by mutually exclusive ink row bands and horizontal centering, and the all"
        " empty text keeps a fully transparent face on both shapes)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 程序字模存在面:85 在册全覆盖(A-Z a-z 0-9 + 基础标点 22)+ 覆盖面外反判。
        {
            QString table;
            for (int c = 'A'; c <= 'Z'; ++c) table += QChar(c);
            for (int c = 'a'; c <= 'z'; ++c) table += QChar(c);
            for (int c = '0'; c <= '9'; ++c) table += QChar(c);
            table += QStringLiteral(" !\"'(),-./:;?+=_<>#%&*@");
            int missingInTable = 0;
            for (const QChar &ch : table)
                if (!SignBoardFont::hasGlyph(ch.unicode()))
                    ++missingInTable;
            const bool coverOk = table.size() == 85 && missingInTable == 0;
            ok = ok && coverOk;
            if (!coverOk) diag += QStringLiteral("[cover n=%1 miss=%2]")
                .arg(table.size()).arg(missingInTable);
            const bool outOk = !SignBoardFont::hasGlyph(u'\x4E2D') // CJK 覆盖面外(缺字框占位)
                && !SignBoardFont::hasGlyph(u'[')
                && !SignBoardFont::hasGlyph(u'~')
                && !SignBoardFont::hasGlyph(u'{');
            ok = ok && outOk;
            if (!outOk) diag += QStringLiteral("[outcov]");
        }
        // (2) 缺字框形态钉(覆盖面外 → 空心框+内点行,掩码逐行钉——hasGlyph 反判的形态学根据)。
        {
            quint8 rows[SignBoardFont::kGlyphRows];
            SignBoardFont::glyphRows(u'\x4E2D', rows);
            const quint8 kBox[SignBoardFont::kGlyphRows] = {
                0x0E, 0x11, 0x0A, 0x11, 0x0A, 0x11, 0x0E, 0x00, 0x00 };
            const bool boxOk = std::memcmp(rows, kBox, SignBoardFont::kGlyphRows) == 0;
            ok = ok && boxOk;
            if (!boxOk) diag += QStringLiteral("[box]");
        }
        // (3) 已知文本 → 墨非空(双形态画布;承载「写入已知文本→读回映射面非空」)。
        {
            const QString lines[SignBoardFont::kLineCount] = {
                QStringLiteral("Shop"), QStringLiteral("- wheat 3"),
                QString(), QString() };
            const int inkWall = countInkPixelsSignText(SignBoardFont::renderBoard(lines, true));
            const int inkStd = countInkPixelsSignText(SignBoardFont::renderBoard(lines, false));
            const bool inkOk = inkWall > 0 && inkStd > 0;
            ok = ok && inkOk;
            if (!inkOk) diag += QStringLiteral("[ink w=%1 s=%2]").arg(inkWall).arg(inkStd);
        }
        // (4) 行序正确(单行文本墨行区间互斥且各落其带:行 0 墨全落行 0 带零溅行 1 带;行 2 同理)。
        {
            const QString l0[SignBoardFont::kLineCount] = {
                QStringLiteral("ABCDEF"), QString(), QString(), QString() };
            const QString l2[SignBoardFont::kLineCount] = {
                QString(), QString(), QStringLiteral("ABCDEF"), QString() };
            const QImage i0 = SignBoardFont::renderBoard(l0, false);
            const QImage i2 = SignBoardFont::renderBoard(l2, false);
            const int b0t = signTextLineTop(0), b0b = b0t + SignBoardFont::kGlyphRows;
            const int b1t = signTextLineTop(1), b1b = b1t + SignBoardFont::kGlyphRows;
            const int b2t = signTextLineTop(2), b2b = b2t + SignBoardFont::kGlyphRows;
            int i0InB0 = 0, i0InB1 = 0, i2InB2 = 0, i2InB0 = 0;
            for (int yy = 0; yy < i0.height(); ++yy) {
                const bool inB0 = yy >= b0t && yy < b0b;
                const bool inB1 = yy >= b1t && yy < b1b;
                const bool inB2 = yy >= b2t && yy < b2b;
                for (int xx = 0; xx < i0.width(); ++xx) {
                    if (qAlpha(i0.pixel(xx, yy)) > 0) {
                        if (inB0) ++i0InB0;
                        if (inB1) ++i0InB1;
                    }
                    if (qAlpha(i2.pixel(xx, yy)) > 0) {
                        if (inB2) ++i2InB2;
                        if (inB0) ++i2InB0;
                    }
                }
            }
            const bool orderOk = i0InB0 > 0 && i0InB1 == 0 && i2InB2 > 0 && i2InB0 == 0;
            ok = ok && orderOk;
            if (!orderOk) diag += QStringLiteral("[order i0=%1/%2 i2=%3/%4]")
                .arg(i0InB0).arg(i0InB1).arg(i2InB2).arg(i2InB0);
            // 行内水平居中(单字 A 于站牌画布:墨 x 中点 = 画布中心 ±2px 容差——5px 字模对 96px 居中)。
            const QString one[SignBoardFont::kLineCount] = {
                QStringLiteral("A"), QString(), QString(), QString() };
            const QImage ia = SignBoardFont::renderBoard(one, false);
            int xMin = ia.width(), xMax = -1;
            for (int yy = 0; yy < ia.height(); ++yy)
                for (int xx = 0; xx < ia.width(); ++xx)
                    if (qAlpha(ia.pixel(xx, yy)) > 0) { xMin = std::min(xMin, xx); xMax = std::max(xMax, xx); }
            const bool centerOk = xMax >= 0
                && std::abs((xMin + xMax) / 2 - ia.width() / 2) <= 2;
            ok = ok && centerOk;
            if (!centerOk) diag += QStringLiteral("[center %1..%2 w=%3]")
                .arg(xMin).arg(xMax).arg(ia.width());
        }
        // (5) 空文本恒空白面(4 空行 → 全透明,双形态——既有空白刻线板不变的行为面)。
        {
            const QString empty[SignBoardFont::kLineCount] = {
                QString(), QString(), QString(), QString() };
            const int inkWall = countInkPixelsSignText(SignBoardFont::renderBoard(empty, true));
            const int inkStd = countInkPixelsSignText(SignBoardFont::renderBoard(empty, false));
            const bool emptyOk = inkWall == 0 && inkStd == 0;
            ok = ok && emptyOk;
            if (!emptyOk) diag += QStringLiteral("[empty w=%1 s=%2]").arg(inkWall).arg(inkStd);
        }
        // (6) 行数互钉(本类 kLineCount == SignStore::kLinesPerSign——映射面行数单源对表)。
        {
            const bool linesOk = SignBoardFont::kLineCount == SignStore::kLinesPerSign
                && SignBoardFont::kLineCount == 4;
            ok = ok && linesOk;
            if (!linesOk) diag += QStringLiteral("[lines]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2088a glyph and mapping column (the procedural bitmap table answers all"
               " eighty five registered glyphs with the out of coverage chars rejected to the"
               " missing box mask, known text renders non empty ink on both board canvases"
               " with the line order pinned by mutually exclusive ink row bands and"
               " horizontal centering, and the all empty text keeps a fully transparent face"
               " on both shapes)"
            << (ok ? QString() : diag);
    });

    // ── r2088b:接线柱(NEG-2 敏感面 = Main.qml onSignChanged 对账行)────────────────────────
    //   双形态几何读回(挂墙 128×96 / 站牌 96×96 非空——尺寸行为面,不读墨 = NEG-1 幸存)+
    //   revision 失效键钉(Texture URL 携 signStore.revision raw 钉 + provider 注册钉)+
    //   SignStore::active 桥行为面(构造注册/最后构造者生效)+ QML 接线 raw 钉族(对账行/实例行/
    //   cleanup 兜底行——NEG-2 摘面行在本腿钉)。
    runLeg("r2088b wiring column (the dual shape canvases read back one twenty eight by"
        " ninety six for the wall board and ninety six square for the standing board, the"
        " texture url carries the sign store revision invalidation key with the provider"
        " registered in the app glue, the active store bridge answers the last constructed"
        " instance, and the qml reconcile and cleanup wiring rows are pinned on file)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 双形态几何读回(尺寸行为面;挂墙满格宽 128 / 站牌 12/16 宽 96,高同 96)。
        {
            const QString lines[SignBoardFont::kLineCount] = {
                QStringLiteral("Hi"), QString(), QString(), QString() };
            const QImage wallImg = SignBoardFont::renderBoard(lines, true);
            const QImage stdImg = SignBoardFont::renderBoard(lines, false);
            const bool dimOk = !wallImg.isNull() && !stdImg.isNull()
                && wallImg.width() == SignBoardFont::kCanvasWallW
                && wallImg.height() == SignBoardFont::kCanvasH
                && stdImg.width() == SignBoardFont::kCanvasStandingW
                && stdImg.height() == SignBoardFont::kCanvasH;
            ok = ok && dimOk;
            if (!dimOk) diag += QStringLiteral("[dim %1x%2 %3x%4]")
                .arg(wallImg.width()).arg(wallImg.height())
                .arg(stdImg.width()).arg(stdImg.height());
        }
        // (2) revision 失效键钉(Texture URL 携 signStore.revision——mapstore revision-URL 同门;
        //     raw 钉:URL 构造行含 scheme 与 revision 尾段)。
        {
            const QString qml = QDir(repoRootForSignTextPins() + QStringLiteral("/src")).absoluteFilePath(QStringLiteral("ui/Main.qml"));
            const bool urlOk = rawContainsSignText(qml, QStringLiteral("image://signboard/"))
                && rawContainsSignText(qml, QStringLiteral("\"/\" + signRoot.wall + \"/\" + signStore.revision"));
            ok = ok && urlOk;
            if (!urlOk) diag += QStringLiteral("[url]");
        }
        // (3) provider 注册钉(app 胶水层注册行 + provider 类行)。
        {
            const QString mainCpp = repoRootForSignTextPins() + QStringLiteral("/main.cpp");
            const bool provOk = rawContainsSignText(mainCpp,
                    QStringLiteral("addImageProvider(QStringLiteral(\"signboard\")"))
                && rawContainsSignText(mainCpp, QStringLiteral("class SignBoardAtlasProvider"));
            ok = ok && provOk;
            if (!provOk) diag += QStringLiteral("[prov]");
        }
        // (4) SignStore::active 桥行为面(构造注册 = 最后构造者生效;析构注销回退前一态——
        //     MapStore::active 同门语义)。
        {
            SignStore first;
            SignStore *pFirst = &first;
            SignStore second;
            const bool bridgeOk = SignStore::active() == &second
                && SignStore::active() != pFirst;
            ok = ok && bridgeOk;
            if (!bridgeOk) diag += QStringLiteral("[bridge]");
        }
        // (5) QML 接线 raw 钉族(**NEG-2 摘面行在本腿钉**;a/c 腿零重叠 = 恰红归因唯一):
        //     onSignChanged 对账行 + signStore 实例行 + onWorldChanged cleanup 兜底行。
        {
            const QString qml = QDir(repoRootForSignTextPins() + QStringLiteral("/src")).absoluteFilePath(QStringLiteral("ui/Main.qml"));
            const bool wiringOk = rawContainsSignText(qml,
                    QStringLiteral("function onSignChanged() { signHost.reconcileVis() }")) // NEG-2 摘面行
                && rawContainsSignText(qml, QStringLiteral("SignStore { id: signStore }"))
                && rawContainsSignText(qml, QStringLiteral("signHost.cleanupVis()"));
            ok = ok && wiringOk;
            if (!wiringOk) diag += QStringLiteral("[wiring]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2088b wiring column (the dual shape canvases read back one twenty eight"
               " by ninety six for the wall board and ninety six square for the standing"
               " board, the texture url carries the sign store revision invalidation key"
               " with the provider registered in the app glue, the active store bridge"
               " answers the last constructed instance, and the qml reconcile and cleanup"
               " wiring rows are pinned on file)"
            << (ok ? QString() : diag);
    });

    // ── r2088c:结构钉族(NEG 双摘面全豁免 = 值面/源钉对照腿)────────────────────────────────
    //   源钉族(字模表头/映射常量/渲染签名/active 桥/构造注册行/CMake 双行——NEG-1 摘面行
    //   [img.setPixel]与 NEG-2 摘面行[onSignChanged]均不入本族)+ 负面钉(零字体引擎零字体文件
    //   零 MC 资产名)。
    runLeg("r2088c structure pin family (the glyph table head and the mapping constants and"
        " the render signature and the active bridge rows and the cmake rows are pinned on"
        " file with both negative lesion faces exempt, and no font engine call no font file"
        " asset and no mc font asset name exist anywhere in the glyph source)", [&]() {
        bool ok = true;
        QString diag;
        const QString root = repoRootForSignTextPins();
        // (C1) 字模/映射源钉(pinSet 剥注释后仍命中=真实语句锚;t1118 沿革锚走 raw 含)。
        const QStringList missFont = pinSet(root + QStringLiteral("/src/Game/signboardfont.h"), {
            SrcPin("glyph cols const", "static constexpr int kGlyphCols = 5;", 1),
            SrcPin("glyph rows const", "static constexpr int kGlyphRows = 9;", 1),
            SrcPin("advance const", "static constexpr int kAdvance = 6;", 1),
            SrcPin("line pitch const", "static constexpr int kLinePitch = 12;", 1),
            SrcPin("first line top const", "static constexpr int kFirstLineTop = 30;", 1),
            SrcPin("has glyph decl", "static bool hasGlyph(char32_t ch);", 1),
            SrcPin("render decl", "static QImage renderBoard(const QString *lines, bool wall);", 1),
        });
        ok = ok && missFont.isEmpty();
        if (!missFont.isEmpty())
            diag += QStringLiteral("[fh %1]").arg(missFont.join(QLatin1Char(',')));
        const QStringList missFontCpp = pinSet(root + QStringLiteral("/src/Game/signboardfont.cpp"), {
            SrcPin("lookup head", "bool lookupGlyph(char32_t ch, quint8 out[SignBoardFont::kGlyphRows])", 1),
            SrcPin("table A row", "case char32_t('A'):", 1),
            SrcPin("table z row", "case char32_t('z'):", 1),
            SrcPin("table at row", "case char32_t('@'):", 1),
            SrcPin("missing box default", "return false;", 1),
            SrcPin("impl has glyph", "bool SignBoardFont::hasGlyph(char32_t ch)", 1),
            SrcPin("impl render", "QImage SignBoardFont::renderBoard(const QString *lines, bool wall)", 1),
        });
        ok = ok && missFontCpp.isEmpty();
        if (!missFontCpp.isEmpty())
            diag += QStringLiteral("[fc %1]").arg(missFontCpp.join(QLatin1Char(',')));
        // (C2) active 桥源钉(声明行 + 构造注册行 + 析构注销行——机制/存储面零触碰的呈现桥三行)。
        const QStringList missStore = pinSet(root + QStringLiteral("/src/Game/signstore.h"), {
            SrcPin("active decl", "static SignStore *active() { return s_active; }", 1),
        });
        ok = ok && missStore.isEmpty();
        if (!missStore.isEmpty())
            diag += QStringLiteral("[sh %1]").arg(missStore.join(QLatin1Char(',')));
        const QStringList missStoreCpp = pinSet(root + QStringLiteral("/src/Game/signstore.cpp"), {
            SrcPin("active member", "SignStore *SignStore::s_active = nullptr;", 1),
            SrcPin("ctor register row", "s_active = this;", 1),
            SrcPin("dtor unregister row", "if (s_active == this)", 1),
        });
        ok = ok && missStoreCpp.isEmpty();
        if (!missStoreCpp.isEmpty())
            diag += QStringLiteral("[sc %1]").arg(missStoreCpp.join(QLatin1Char(',')));
        // (C3) CMake 双行钉(app 源行 + 本段注册行——分属 fix/test 两提交,合入后同在)。
        const QStringList missCm = pinSet(root + QStringLiteral("/CMakeLists.txt"), {
            SrcPin("cmake app source", "src/Game/signboardfont.cpp", 2),
            SrcPin("cmake section81", "tools/matrix/section81_sign_text_t1118.cpp", 1),
        });
        ok = ok && missCm.isEmpty();
        if (!missCm.isEmpty())
            diag += QStringLiteral("[cm %1]").arg(missCm.join(QLatin1Char(',')));
        // (C4) 负面钉:零字体引擎调用(QFont/QRawFont/QFontDatabase 全无——程序点阵字模纪律面)。
        for (const QString &needle : { QStringLiteral("QFont"), QStringLiteral("QRawFont"),
                                       QStringLiteral("QFontDatabase") }) {
            const bool absent = !rawContainsSignText(
                root + QStringLiteral("/src/Game/signboardfont.cpp"), needle);
            ok = ok && absent;
            if (!absent) diag += QStringLiteral("[fonteng %1]").arg(needle);
        }
        // (C5) 负面钉:零字体文件资产(textures 全目录零 *font* 文件)。
        {
            const QDir texDir(root + QStringLiteral("/textures"));
            const int fontFiles = texDir.entryInfoList(
                QStringList { QStringLiteral("*font*") }, QDir::Files).size();
            const bool noFontAsset = fontFiles == 0;
            ok = ok && noFontAsset;
            if (!noFontAsset) diag += QStringLiteral("[fontfile n=%1]").arg(fontFiles);
        }
        // (C6) 负面钉:零 MC 资产名(MC 字体图集名 ascii.png 不入字模源——§9 名词/资产禁区面)。
        {
            const bool noMcName = !rawContainsSignText(
                root + QStringLiteral("/src/Game/signboardfont.cpp"), QStringLiteral("ascii.png"));
            ok = ok && noMcName;
            if (!noMcName) diag += QStringLiteral("[mcname]");
        }
        // (C7) 沿革锚(批头注锚在盘;注释体锚走 raw 含)。
        {
            const bool lineageOk = rawContainsSignText(
                root + QStringLiteral("/src/Game/signboardfont.cpp"), QStringLiteral("t1118"));
            ok = ok && lineageOk;
            if (!lineageOk) diag += QStringLiteral("[lineage]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2088c structure pin family (the glyph table head and the mapping constants"
               " and the render signature and the active bridge rows and the cmake rows are"
               " pinned on file with both negative lesion faces exempt, and no font engine"
               " call no font file asset and no mc font asset name exist anywhere in the"
               " glyph source)"
            << (ok ? QString() : diag);
    });
}
