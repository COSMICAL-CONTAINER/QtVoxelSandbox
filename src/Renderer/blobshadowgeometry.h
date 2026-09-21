#ifndef BLOWSHADOWGEOMETRY_H
#define BLOWSHADOWGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

#include <QVector>
#include <QVector3D>
#include <type_traits>

// ── t1075 blob 软阴影贴片几何（Renderer 层；玩家与生物的半透明暗椭圆贴地投影）──────────
//
// 选型与落选（设计自由度留痕，派工单钦定头注锚）：
//   · 贴片形态 = **贴地水平八边形 disc**（中心顶点 + 8 缘顶点，扇形三角化），暗色 + 顶点色
//     alpha 径向渐变（中心 kAlpha 满、缘 0）——线性插值给出柔和径向软边。落选①扁圆柱：侧壁
//     无任何视觉必要（贴地投影是平的），白付 4 倍顶点 + 侧壁 z-fight 风险；落选②径向渐变
//     贴图：新增资产文件 + qrc 登记 + 采样成本，而顶点色插值零资产零采样即得同观感
//     （PLAN §9 资产原创面最小化；程序渐变材质 = 派工单允许路径）。
//   · 悬崖边缘 = **中心列语义**：采样权威（EntityShadowField，Game 层）按实体中心列向下找
//     最近承载面，窗口内无承载 → 该实体本拍不出 quad（悬空格不投影）；窗口内有承载但更深
//     → alpha 按腾空高度线性衰减（淡出）+ 超窗缺席。边缘「半格悬空」由八边形贴片对地形的
//     自然重叠表达，不做逐列多采样（落选：footprint 多列采样成本 ×N，观感增益不可感）。
//   · 采样频率 = 消费侧节流：EntityShadowField 15Hz 空转门 QTimer 采样 + 指纹差分（本类
//     setQuads 同值早退零重传）——非每快照沿 / 非每渲染帧。落选每快照沿：实体位移沿 20Hz
//     节流后仍是逐槽风暴面（t500/t935 收口域），贴片 66ms 滞后肉眼不可感；落选每帧：无谓
//     60Hz 世界列扫（派工不变量①「禁 QML 里逐帧扫世界」的 C++ 侧同源纪律）。
//   · 透明度承载 = 顶点色 rgba 的 a 通道（材质 vertexColorsEnabled + alphaMode:Blend——
//     t439/t442 透明段契约「连续 alpha 走 Blend，非 opacity hack」；单 Model 单材质共享，
//     per-quad 淡出全在顶点色，零 per-quad 材质实例 = lessons「N 个 inline Material 重算」
//     反模式的规避）。
//
// 批量面：全部 quad（玩家 + 生物）合**单 Model 单几何单 draw**（world 坐标直写顶点，
//   Model 摆原点；setBounds 逐拍重算 = 视锥剔除正确）。采样权威（Game 层 EntityShadowField）
//   把「对地采样点」算好后随本专用面下发（派工不变量①：贴地 Y 属模拟/权威面，C++ 侧算好，
//   QML 零世界查询零逐帧扫）；本类只做几何装配，零 World/Entities 依赖（Renderer 分层，
//   PLAN §2 依赖只向下）。
//
// 顶点布局：pos(3f) + color(4f rgba) = 7 float（ColorSemantic 按 vec4 读——blockcube.cpp
//   t257 先例：写 3 float 会错位，a 通道必须显式写）。每 quad 9 顶点 + 24 索引（8 扇形 ×
//   三角）；索引缓冲与 addAttribute(IndexSemantic) 成对上传（lessons t35 契约：只声明不
//   setIndexData = 索引不进 GPU + 未用变量警告）。
//
// 分层（PLAN §2）：本类属 Renderer（QQuick3DGeometry 呈现层），不读栅格 / 不识实体；
//   ShadowBlobQuad 值类型随头外泄给 Game 层消费方（QObjectFree 值纪律）。

// 单个阴影贴片的下发值（采样权威产出 → 本类消费；全值成员可独立复制）。
struct ShadowBlobQuad
{
    float x = 0.0f;      // 贴片中心世界 X（= 实体中心 X）
    float y = 0.0f;      // 贴片世界 Y（= 承载面真顶 + 抬升；EntityShadowField 算好）
    float z = 0.0f;      // 贴片中心世界 Z
    float radius = 0.0f; // 贴片半径（格；随实体宽度派生，采样权威算好）
    float alpha = 0.0f;  // 中心 alpha（腾空高度衰减后的不透明度；缘恒 0）
};

class BlobShadowGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BlobShadowGeometry)
    // 渲染面可测计数通道（矩阵腿 + QML visible 门共用；沿 = 内容真变）。
    Q_PROPERTY(int quadCount READ quadCount NOTIFY quadsChanged)

public:
    explicit BlobShadowGeometry(QQuick3DObject *parent = nullptr);

    // 单 quad 顶点/索引拓扑常量（矩阵腿按可数通道断言字节量；改拓扑须同步腿面）。
    static constexpr int kVertsPerQuad = 9;  // 中心 1 + 八边形缘 8
    static constexpr int kIndicesPerQuad = 24; // 8 扇形 × 3
    static constexpr int kFloatsPerVertex = 7; // pos3 + rgba4

    int quadCount() const { return int(m_quads.size()); }

    // 采样权威下发口（Game 层 EntityShadowField 逐拍调；矩阵腿 headless 直调同面）：
    //   同值早退（指纹差分——零变化零重传零沿），内容/条数任一变才重装几何 + 发沿。
    void setQuads(const ShadowBlobQuad *quads, int count);

    // 只读观察面（矩阵腿内容级断言；越界 → nullptr）。
    const ShadowBlobQuad *quads() const { return m_quads.constData(); }
    const ShadowBlobQuad &quadAt(int i) const { return m_quads[i]; }

signals:
    void quadsChanged();

private:
    void rebuild(); // 由 m_quads 重装顶点/索引缓冲 + bounds + GPU 重传（上传序 = lessons t03/t35 文档序）

    QVector<ShadowBlobQuad> m_quads; // 当前已下发集合（观察面 + 指纹差分基线）
};

// 值纪律编译期钉：下发值随头跨层流动（Game → Renderer 合规向下），不携 QObject。
static_assert(std::is_trivially_copyable_v<ShadowBlobQuad>,
              "ShadowBlobQuad must stay a trivial value type (plan §4.2 值纪律)");

#endif // BLOWSHADOWGEOMETRY_H
