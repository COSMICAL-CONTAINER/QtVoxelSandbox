#include "chunkgridlines.h"

#include <QByteArray>
#include <QVector3D>
#include <QVector>

// 画区块边界网格（Lines 模式：每 2 顶点一段独立线段，与 WireCube 同 primitive）。
//   - 纵线：每个 (x 边界, z 边界) 交点处，从 (xb, 0, zb) 到 (xb, height, zb)。
//   - 顶 / 底面水平连线：沿每条 x 边界（固定 xb，z 走 0..depth）与每条 z 边界（固定 zb，x 走 0..width），
//     各在 y=0 与 y=height 画一条。
// 顶点为世界坐标（Model 摆 0,0,0）。写入顺序（lessons-learned）：clear → setVertexData → setStride
// → setBounds → setPrimitiveType(Lines) → addAttribute(PositionSemantic) → update()。漏 update() 后端不上传 GPU。
ChunkGridLines::ChunkGridLines(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 默认全 0 → 空几何（QML visible 门控，未就绪时不显）
}

void ChunkGridLines::setWorldWidth(int v)
{
    if (v == m_worldWidth) return;
    m_worldWidth = v;
    emit worldWidthChanged();
    rebuild();
}

void ChunkGridLines::setWorldDepth(int v)
{
    if (v == m_worldDepth) return;
    m_worldDepth = v;
    emit worldDepthChanged();
    rebuild();
}

void ChunkGridLines::setWorldHeight(int v)
{
    if (v == m_worldHeight) return;
    m_worldHeight = v;
    emit worldHeightChanged();
    rebuild();
}

void ChunkGridLines::setChunkSize(int v)
{
    if (v == m_chunkSize) return;
    m_chunkSize = v;
    emit chunkSizeChanged();
    rebuild();
}

void ChunkGridLines::rebuild()
{
    // 尺寸未就绪 / 非法 → 空几何（QML visible 门控，不显；防 0 尺寸生成空线段）。
    if (m_worldWidth <= 0 || m_worldDepth <= 0 || m_worldHeight <= 0 || m_chunkSize <= 0) {
        clear();
        update();
        return;
    }

    QVector<float> verts;
    const float y0 = 0.0f;
    const float y1 = float(m_worldHeight);
    // 追加一段独立线段（2 顶点 × 3 float）。Lines 模式下每 2 顶点一段，无索引、无共享。
    auto addLine = [&verts](float ax, float ay, float az, float bx, float by, float bz) {
        verts.append(ax); verts.append(ay); verts.append(az);
        verts.append(bx); verts.append(by); verts.append(bz);
    };

    // 纵线：遍历每个 (x 边界, z 边界) 交点。世界尺寸恒为 chunkSize 整数倍（t276 worldChunksPerSide*16），
    //   故步长 chunkSize 恰好在 worldWidth/worldDepth 收口（xb/zb 最后取到 = worldWidth/worldDepth）。
    for (int xb = 0; xb <= m_worldWidth; xb += m_chunkSize) {
        for (int zb = 0; zb <= m_worldDepth; zb += m_chunkSize) {
            addLine(float(xb), y0, float(zb), float(xb), y1, float(zb));
        }
    }
    // 沿 x 边界的水平连线（固定 xb，z 走 0..depth），顶 + 底各一条。
    for (int xb = 0; xb <= m_worldWidth; xb += m_chunkSize) {
        addLine(float(xb), y0, 0.0f, float(xb), y0, float(m_worldDepth));
        addLine(float(xb), y1, 0.0f, float(xb), y1, float(m_worldDepth));
    }
    // 沿 z 边界的水平连线（固定 zb，x 走 0..width），顶 + 底各一条。
    for (int zb = 0; zb <= m_worldDepth; zb += m_chunkSize) {
        addLine(0.0f, y0, float(zb), float(m_worldWidth), y0, float(zb));
        addLine(0.0f, y1, float(zb), float(m_worldWidth), y1, float(zb));
    }

    clear();
    // QByteArray(const char*, int) 深拷贝；勿用 fromRawData（QVector 离开作用域会悬空）。
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.constData()),
                             verts.size() * int(sizeof(float))));
    setStride(3 * int(sizeof(float)));
    setBounds(QVector3D(0.0f, 0.0f, 0.0f),
              QVector3D(float(m_worldWidth), float(m_worldHeight), float(m_worldDepth)));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    update();
}
