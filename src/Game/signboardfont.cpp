#include "signboardfont.h"

#include <cstring> // std::memcpy

// 牌板面文字像素渲染实现（声明见 .h 注——原创 9 行点阵字模 + 文本→板面像素映射；t1118）。
//
// NEG 纪律留痕：renderBoard 内「img.setPixel(px, py, kInkColor);」行 = NEG-1 摘面行（r2088a 行为红，
//   r2088b / r2088c 幸存——恰红归因见 tools/matrix/section81_sign_text_t1118.cpp 批头注）。

namespace {

// 缺字框（覆盖面外字符占位：空心框 + 内点行；行序与任何在册字模都不同——hasGlyph 据此反判）。
constexpr quint8 kMissingGlyph[SignBoardFont::kGlyphRows] = {
    0x0E, 0x11, 0x0A, 0x11, 0x0A, 0x11, 0x0E, 0x00, 0x00
};


// 原创点阵字模表查字：命中填 out（9 行掩码，bit4 = 最左列）并返 true；不命中返 false（out 不动，
//   由调用方填缺字框）。83 字模在册 = 大写 26 + 小写 26 + 数字 10 + 基础标点 21。
bool lookupGlyph(char32_t ch, quint8 out[SignBoardFont::kGlyphRows])
{
    switch (ch) {
    // ── 大写 A-M（主体行 0..6，行 7/8 恒零）─────────────────────────────────────────
    case char32_t('A'): { const quint8 g[9] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('B'): { const quint8 g[9] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }; std::memcpy(out, g, 9); return true; }
    case char32_t('C'): { const quint8 g[9] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('D'): { const quint8 g[9] = { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C }; std::memcpy(out, g, 9); return true; }
    case char32_t('E'): { const quint8 g[9] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }; std::memcpy(out, g, 9); return true; }
    case char32_t('F'): { const quint8 g[9] = { 0x1F, 0x10, 0x0E, 0x10, 0x10, 0x10, 0x10 }; std::memcpy(out, g, 9); return true; }
    case char32_t('G'): { const quint8 g[9] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }; std::memcpy(out, g, 9); return true; }
    case char32_t('H'): { const quint8 g[9] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('I'): { const quint8 g[9] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('J'): { const quint8 g[9] = { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }; std::memcpy(out, g, 9); return true; }
    case char32_t('K'): { const quint8 g[9] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('L'): { const quint8 g[9] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }; std::memcpy(out, g, 9); return true; }
    case char32_t('M'): { const quint8 g[9] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }; std::memcpy(out, g, 9); return true; }
    // ── 大写 N-Z ─────────────────────────────────────────────────────────────────────
    case char32_t('N'): { const quint8 g[9] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('O'): { const quint8 g[9] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('P'): { const quint8 g[9] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }; std::memcpy(out, g, 9); return true; }
    case char32_t('Q'): { const quint8 g[9] = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }; std::memcpy(out, g, 9); return true; }
    case char32_t('R'): { const quint8 g[9] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('S'): { const quint8 g[9] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }; std::memcpy(out, g, 9); return true; }
    case char32_t('T'): { const quint8 g[9] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t('U'): { const quint8 g[9] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('V'): { const quint8 g[9] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t('W'): { const quint8 g[9] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('X'): { const quint8 g[9] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('Y'): { const quint8 g[9] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t('Z'): { const quint8 g[9] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }; std::memcpy(out, g, 9); return true; }
    // ── 小写 a-m（x-height 行 2..6；b/f/h/k/l 升部到行 0；g/j 下伸行 7..8）────────────
    case char32_t('a'): { const quint8 g[9] = { 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F }; std::memcpy(out, g, 9); return true; }
    case char32_t('b'): { const quint8 g[9] = { 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E }; std::memcpy(out, g, 9); return true; }
    case char32_t('c'): { const quint8 g[9] = { 0x00, 0x00, 0x0F, 0x10, 0x10, 0x10, 0x0F }; std::memcpy(out, g, 9); return true; }
    case char32_t('d'): { const quint8 g[9] = { 0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F }; std::memcpy(out, g, 9); return true; }
    case char32_t('e'): { const quint8 g[9] = { 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0F }; std::memcpy(out, g, 9); return true; }
    case char32_t('f'): { const quint8 g[9] = { 0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08 }; std::memcpy(out, g, 9); return true; }
    case char32_t('g'): { const quint8 g[9] = { 0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('h'): { const quint8 g[9] = { 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('i'): { const quint8 g[9] = { 0x04, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t('j'): { const quint8 g[9] = { 0x02, 0x00, 0x02, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C }; std::memcpy(out, g, 9); return true; }
    case char32_t('k'): { const quint8 g[9] = { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12 }; std::memcpy(out, g, 9); return true; }
    case char32_t('l'): { const quint8 g[9] = { 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('m'): { const quint8 g[9] = { 0x00, 0x00, 0x1E, 0x15, 0x15, 0x15, 0x15 }; std::memcpy(out, g, 9); return true; }
    case char32_t('n'): { const quint8 g[9] = { 0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('o'): { const quint8 g[9] = { 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('p'): { const quint8 g[9] = { 0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }; std::memcpy(out, g, 9); return true; }
    case char32_t('q'): { const quint8 g[9] = { 0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01 }; std::memcpy(out, g, 9); return true; }
    case char32_t('r'): { const quint8 g[9] = { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 }; std::memcpy(out, g, 9); return true; }
    case char32_t('s'): { const quint8 g[9] = { 0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E }; std::memcpy(out, g, 9); return true; }
    case char32_t('t'): { const quint8 g[9] = { 0x08, 0x08, 0x1E, 0x08, 0x08, 0x09, 0x06 }; std::memcpy(out, g, 9); return true; }
    case char32_t('u'): { const quint8 g[9] = { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D }; std::memcpy(out, g, 9); return true; }
    case char32_t('v'): { const quint8 g[9] = { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t('w'): { const quint8 g[9] = { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A }; std::memcpy(out, g, 9); return true; }
    case char32_t('x'): { const quint8 g[9] = { 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11 }; std::memcpy(out, g, 9); return true; }
    case char32_t('y'): { const quint8 g[9] = { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x01, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('z'): { const quint8 g[9] = { 0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F }; std::memcpy(out, g, 9); return true; }
    // ── 数字 0-9 ─────────────────────────────────────────────────────────────────────
    case char32_t('0'): { const quint8 g[9] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('1'): { const quint8 g[9] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('2'): { const quint8 g[9] = { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }; std::memcpy(out, g, 9); return true; }
    case char32_t('3'): { const quint8 g[9] = { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('4'): { const quint8 g[9] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }; std::memcpy(out, g, 9); return true; }
    case char32_t('5'): { const quint8 g[9] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('6'): { const quint8 g[9] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('7'): { const quint8 g[9] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }; std::memcpy(out, g, 9); return true; }
    case char32_t('8'): { const quint8 g[9] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }; std::memcpy(out, g, 9); return true; }
    case char32_t('9'): { const quint8 g[9] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }; std::memcpy(out, g, 9); return true; }
    // ── 基础标点 21 命（含空格；'"' 用 0x22 / '\'' 用 0x27 码点做 case 标签——bash/heredoc 引号面）──
    case char32_t(' '): return true; // 空格：步进 6px、无墨（在册零墨字模）。
    case char32_t('!'): { const quint8 g[9] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t(0x22): { const quint8 g[9] = { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t(0x27): { const quint8 g[9] = { 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t('('): { const quint8 g[9] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }; std::memcpy(out, g, 9); return true; }
    case char32_t(')'): { const quint8 g[9] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }; std::memcpy(out, g, 9); return true; }
    case char32_t(','): { const quint8 g[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08 }; std::memcpy(out, g, 9); return true; }
    case char32_t('-'): { const quint8 g[9] = { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t('.'): { const quint8 g[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t('/'): { const quint8 g[9] = { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }; std::memcpy(out, g, 9); return true; }
    case char32_t(':'): { const quint8 g[9] = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t(';'): { const quint8 g[9] = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08 }; std::memcpy(out, g, 9); return true; }
    case char32_t('?'): { const quint8 g[9] = { 0x0E, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04 }; std::memcpy(out, g, 9); return true; }
    case char32_t('+'): { const quint8 g[9] = { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t('='): { const quint8 g[9] = { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t('_'): { const quint8 g[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t('<'): { const quint8 g[9] = { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }; std::memcpy(out, g, 9); return true; }
    case char32_t('>'): { const quint8 g[9] = { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }; std::memcpy(out, g, 9); return true; }
    case char32_t('#'): { const quint8 g[9] = { 0x0A, 0x1F, 0x0A, 0x0A, 0x0A, 0x1F, 0x0A }; std::memcpy(out, g, 9); return true; }
    case char32_t('%'): { const quint8 g[9] = { 0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13 }; std::memcpy(out, g, 9); return true; }
    case char32_t('&'): { const quint8 g[9] = { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D }; std::memcpy(out, g, 9); return true; }
    case char32_t('*'): { const quint8 g[9] = { 0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00 }; std::memcpy(out, g, 9); return true; }
    case char32_t('@'): { const quint8 g[9] = { 0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E }; std::memcpy(out, g, 9); return true; }
    default:
        return false; // 覆盖面外 → 缺字框（调用方填）。
    }
}

} // namespace

bool SignBoardFont::hasGlyph(char32_t ch)
{
    quint8 rows[kGlyphRows];
    return lookupGlyph(ch, rows); // 在册 = 表命中（空格亦真——步进面在册、零墨是合法字模）。
}

void SignBoardFont::glyphRows(char32_t ch, quint8 out[kGlyphRows])
{
    if (!lookupGlyph(ch, out))
        std::memcpy(out, kMissingGlyph, kGlyphRows); // 不在册 → 缺字框占位
}

QImage SignBoardFont::renderBoard(const QString *lines, bool wall)
{
    const int w = wall ? kCanvasWallW : kCanvasStandingW;
    QImage img(w, kCanvasH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent); // 透明底：墨字外露出栅格 sign_board 瓦（淡带 / 木底 / 暗框原样透出）。
    for (int li = 0; li < kLineCount; ++li) {
        const QString &line = lines[li];
        if (line.isEmpty())
            continue; // 空行无墨（未写行 = 淡带原样「空行刻线」）。
        const int textW = int(line.size()) * kAdvance - 1; // 末字符不占尾字距
        const int x0 = (w - textW) > 0 ? (w - textW) / 2 : 0; // 行内水平居中（超长防御钳 0）
        const int top = kFirstLineTop + li * kLinePitch;      // 行 i 字身顶（淡带对齐，见 .h 注）
        for (int ci = 0; ci < int(line.size()); ++ci) {
            quint8 rows[kGlyphRows];
            glyphRows(char32_t(line.at(ci).unicode()), rows);
            const int gx = x0 + ci * kAdvance;
            for (int r = 0; r < kGlyphRows; ++r) {
                if (!rows[r])
                    continue;
                const int py = top + r;
                if (py < 0 || py >= kCanvasH)
                    continue; // 防御越界（下伸行越出画布 → 裁剪不写）。
                for (int c = 0; c < kGlyphCols; ++c) {
                    const int px = gx + c;
                    if (!((rows[r] & (0x10 >> c)) && px >= 0 && px < w))
                        continue; // 越界 / 非墨位 → 跳过（守卫行幸存：摘去下行后循环体仍合法，px 仍被消费）。
                    img.setPixel(px, py, kInkColor); // ← NEG-1 摘面行（文本→像素映射写点；整行摘除零警告）。
                }
            }
        }
    }
    return img;
}
