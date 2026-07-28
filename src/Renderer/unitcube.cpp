#include "unitcube.h"

#include <QByteArray>
#include <QVector3D>

// 72 顶点 = 12 三角形 ×2（正绕序 CCW 朝外 + 反绕序 CW 朝内）→ **双面渲染**：每面正反都画，
// 默认 Backface 剔除下无论从外侧还是内侧看都有正面三角形可见 → 方块化人形模型缝隙处不再透视穿透
// （B3 修复：用户反馈第三人称仰角穿透模型）。
// 跨度 ±0.5 = 1×1×1 居中原点（与 #Cube 同基准；Model.scale 复用原取值）。
// 写入顺序（lessons-learned）：clear → setVertexData → setStride → setBounds
// → setPrimitiveType(Triangles) → addAttribute(PositionSemantic) → update()。漏 update() 后端不上传 GPU。
UnitCube::UnitCube(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr float h = 0.5f;
    const float v[72 * 3] = {
        // +X 面（外法线 +X）
         h, -h, -h,   h,  h, -h,   h,  h,  h,
         h, -h, -h,   h,  h,  h,   h, -h,  h,
        // -X 面（外法线 -X）
        -h, -h,  h,  -h,  h,  h,  -h,  h, -h,
        -h, -h,  h,  -h,  h, -h,  -h, -h, -h,
        // +Y 面（顶，外法线 +Y）
        -h,  h, -h,  -h,  h,  h,   h,  h,  h,
        -h,  h, -h,   h,  h,  h,   h,  h, -h,
        // -Y 面（底，外法线 -Y）
        -h, -h,  h,   h, -h,  h,   h, -h, -h,
        -h, -h,  h,   h, -h, -h,  -h, -h, -h,
        // +Z 面（外法线 +Z）
        -h, -h,  h,   h, -h,  h,   h,  h,  h,
        -h, -h,  h,   h,  h,  h,  -h,  h,  h,
        // -Z 面（外法线 -Z）
         h, -h, -h,  -h, -h, -h,  -h,  h, -h,
         h, -h, -h,  -h,  h, -h,   h,  h, -h,
        // ===== 反绕序（CW 朝内）—— 双面渲染：每面再画一次反向三角形 =====
        // +X 面 反向
         h, -h, -h,   h,  h,  h,   h,  h, -h,
         h, -h, -h,   h, -h,  h,   h,  h,  h,
        // -X 面 反向
        -h, -h,  h,  -h,  h, -h,  -h,  h,  h,
        -h, -h,  h,  -h, -h, -h,  -h,  h, -h,
        // +Y 面 反向
        -h,  h, -h,   h,  h,  h,  -h,  h,  h,
        -h,  h, -h,   h,  h, -h,   h,  h,  h,
        // -Y 面 反向
        -h, -h,  h,   h, -h, -h,   h, -h,  h,
        -h, -h,  h,  -h, -h, -h,   h, -h, -h,
        // +Z 面 反向
        -h, -h,  h,   h,  h,  h,   h, -h,  h,
        -h, -h,  h,  -h,  h,  h,   h,  h,  h,
        // -Z 面 反向
         h, -h, -h,  -h,  h, -h,  -h, -h, -h,
         h, -h, -h,   h,  h, -h,  -h,  h, -h,
    };

    clear();
    // QByteArray(const char*, int) 深拷贝；勿用 fromRawData（栈数组会悬空）。
    setVertexData(QByteArray(reinterpret_cast<const char *>(v), int(sizeof(v))));
    setStride(3 * int(sizeof(float)));
    setBounds(QVector3D(-h, -h, -h), QVector3D(h, h, h));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    update();
}
