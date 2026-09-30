#ifndef SIGNBOARDFONT_H
#define SIGNBOARDFONT_H

#include <QtGlobal> // quint8 / quint32
#include <QString>
#include <QImage>

// 牌板面文字像素渲染（Game 层纯函数工具；t1118——t1113 候选池清偿「SignStore 四行文本 → 板面像素」）。
//
// 呈现链（分层 PLAN §2）：SignStore（Game 存储）→ 本类（文本 → 板面像素 QImage）→ main.cpp 胶水
//   SignBoardAtlasProvider（image://signboard/…，MapAtlasProvider 同门——QtQuick 依赖留 app 胶水层）
//   → Main.qml signDelegate（paintingHost 同门世界内呈现层）贴板面 quad。本类零 QtQuick / 零 World
//   依赖（纯函数：4 行文本 + 板面形态 → 像素），故矩阵可密闭直调。
//
// 呈现面选型实读留痕（t1118 派工三候选定夺）：图集文本瓦烘焙不可行——牌子文本 per-sign 动态无界，
//   terrain 图集是共享静态资源（compositeAtlas 程序图集 + 包覆盖），无 per-sign 动态瓦区可分配，且
//   revision 变更须整 chunk 网格重建才可见（文本编辑不该牵动网格化）；Quick 3D 字体路径（Text 节点 /
//   系统字体）违 §9 零字体文件 + 原创点阵字模纪律（系统字体是外部资产且字形非原创）；故取 paintingHost
//   QML delegate 同门（世界内异形呈现层先例——per-sign 独立视觉、随 revision 刷新不经网格重建）。
//
// 字形工程（§9）：**原创 9 行点阵字模**——0..6 主体行 + 7..8 下伸行，5 列宽、横向步进 6（5+1 字距）。
//   字形逐格手工原创（本文件内编码），零字体文件、零 MC 资产、零现成位图字体拷贝。覆盖面实读定夺：
//   ASCII 大写 26 + 小写 26 + 数字 10 + 基础标点 22 = 85 字模在册（编辑面板可输入的中文等非 ASCII →
//   缺字框占位，如实登记简化）。
//
// 板面几何（与 partialblockgeometry.cpp StandingSign/WallSign case + blockregistry.cpp signBoardBoxes
//   同源编码镜像——改编码两处同步）：站牌板面 12/16 宽 × 12/16 高（画布 96×96）、挂墙牌板面满格宽 ×
//   12/16 高（画布 128×96）；1 方块 = 128px 口径。行带对齐：sign_board 瓦[209]四行淡文本带位于瓦行
//   5/7/9/11（16px 瓦 → 96px 画布 = 6px/瓦px），带 0 顶 = 画布行 30、带节距 2 瓦px = 12px——四行字身
//   顶骑在其淡带上（行 i 字身顶 = 30 + 12×i），未写满时淡带仍显「空行刻线」。
class SignBoardFont
{
public:
    static constexpr int kCanvasWallW = 128;    // 挂墙牌板面画布宽（宽向满贯格）
    static constexpr int kCanvasStandingW = 96; // 站牌板面画布宽（12/16）
    static constexpr int kCanvasH = 96;         // 板面画布高（12/16，双形态同值）
    static constexpr int kLineCount = 4;        // 行数（== SignStore::kLinesPerSign，矩阵互钉）
    // 字模几何（原创 9 行点阵：7 主体行 + 2 下伸行）。
    static constexpr int kGlyphCols = 5; // 字模宽 5px
    static constexpr int kGlyphRows = 9; // 字模高 9 行
    static constexpr int kAdvance = 6;   // 横向步进 6px（5 + 1 字距）
    // 行布局（带对齐常量，见类注）。
    static constexpr int kLinePitch = 12;    // 行节距（= 淡带节距 2 瓦px）
    static constexpr int kFirstLineTop = 30; // 行 0 字身顶（= 淡带 0 顶：瓦行 5 × 6px）
    // 墨色 ARGB（暗棕黑——木底上读作深色刻字；§9a 原创色，非取自任何 MC 资产）。
    static constexpr quint32 kInkColor = 0xFF2E2416;

    // 覆盖面谓词：在册 = 大写 26 + 小写 26 + 数字 10 + 基础标点 22（space ! " ' ( ) , - . / : ; ?
    //   ? + = _ < > # % & * @）= 85 字模；不在册（CJK 等非 ASCII 与扩充标点 [ ] { } | \ ~ ^）→ false
    //   （渲染走缺字框占位）。
    static bool hasGlyph(char32_t ch);
    // 单字模 9 行位掩码（每行 5 位，bit4 = 最左列；不在册 → 缺字框掩码——空心框 + 内点）。
    static void glyphRows(char32_t ch, quint8 out[kGlyphRows]);
    // 渲染 kLineCount 行文本到板面画布（透明底 + 墨字）。wall=true → 挂墙牌 128×96 / false → 站牌
    //   96×96。行内水平居中；行 i 字身顶 = kFirstLineTop + i×kLinePitch。**全空 4 行 → 全透明画布**
    //   （空文本牌子 = 既有空白刻线板不变：呈现层对无文本牌子不挂 delegate，栅格瓦原样）。
    static QImage renderBoard(const QString *lines, bool wall);

private:
    SignBoardFont() = delete; // 纯静态工具，无实例。
};

#endif // SIGNBOARDFONT_H
