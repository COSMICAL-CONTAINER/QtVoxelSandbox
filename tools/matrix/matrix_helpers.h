#pragma once
// R20.03 测试分层：矩阵探针套件共享头（原 tools/redstone_matrix_test.cpp 单 TU 拆分）。
// 拆分不变量：段函数体 = 原 main 体的连续行区间**逐字节**搬移（切段 md5 存证见
// build/r2003_proof/；cat(8 段) == whole[296..47995] == 82ac70c7ede1a2ed647fea738734be6b），
// 腿文本 / 断言 / 执行语义零改动；本头只收容 include 枢纽 + 原匿名命名空间共享件 +
// 原 main 局部共享 rig 状态（成员化，名字不变 → 段体免改写）。

// ── include 枢纽（原 L14-92 逐行保序搬移）──
#include <QCoreApplication>
#include <QGuiApplication> // t814 真消费端探针：PlayerController 是 QQuickItem 派生 → 实例化需 Gui 应用对象
#include <QDebug>
#include <QDir>   // t777 探针（羊毛层合成器临时 PNG rig）
#include <QImage> // t777 探针（fur/body 双 PNG 生成 + 合成结果像素断言）
#include <QColor> // t777 探针（像素色对比）
#include <QPainter> // t779 探针（合成源贴图矩形填色）
#include <QJsonDocument> // Review #1 探针（settings.json resourcePack 皮肤路径解析）
#include <QJsonObject>   // Review #1 探针（同上）
#include <QStandardPaths> // Review #1 探针（settings.json 候选位置，同 resolveSettingsPath）
#include <QFile>   // review-e 探针（派生缓存 _r<rev> 存在性 / 旧版清理断言）
#include <QRegularExpression> // t813 探针（stamp / git 哈希格式正则）
#include <QUrl>    // Review 2026-08-24 #5 探针（packFileUrl 查询串剥离断言：QUrl::toLocalFile）
#include <QMetaObject> // t898 review27 #1 探针（sleepLying Q_PROPERTY 契约面：indexOfProperty/property 经 metaobject 读回）
#include <cmath>
#include <cstdio>  // t1010 探针：消息钩子默认处理器兜底直写 stderr
#include <cstring> // t965 探针 std::memcpy（vertexData 直读顶点 u 分量）
#include <algorithm> // t795 探针 std::max（环带切比雪夫距离判定）
#include <vector>   // t824 探针 std::vector<int>（池允许集）
#include <map>       // t1012 探针 std::map（carveCell 地板点位登记，阴影模型）
#include <queue>        // t1014 探针 std::queue（矿脉连通域 BFS）
#include <unordered_set> // t1014 探针 std::unordered_set（矿块格坐标集）
#include <QQmlEngine>   // t874/t875 真链探针：QQmlEngine + qmlRegisterType —— 真 QML 面板 × 真 C++ Hotbar 同台
#include <QQmlContext>  // t874/t875 真链探针：rootContext()->setContextProperty + qmlContext（wrapper 作用域链）
#include <QQmlComponent> // t874/t875 真链探针：setData+base URL 直载源树 AnvilUI.qml / EnchantingTableUI.qml
#include <QQuickItem>   // t874/t875 真链探针：面板 root / 宿主容器 Item
#include <QQuickWindow> // t891 探针：pc 挂窗置 captured（placeBlock 入口门；grab 载体，无 show）
#include <QMouseEvent>  // t949 探针：合成右键 press 直调 eventFilter（真实输入翻译链第一站）
#include <QThread>      // t950 探针：msleep 越过掉落物新生免拾窗（isPickupReady 墙钟，无注入缝）
#include <QRandomGenerator> // t1006 探针：进场散布复刻随机列（同 QML Math.random 语义位）
#include <QDirIterator>    // t1023 探针：src/ 递归扫线程原语（meshing 线程模式事实钉）
#include <functional> // t1006 探针：视觉树 delegate 计数递归 lambda

#include "blockregistry.h"
#include "toolregistry.h" // t762 黑曜石挖掘规则探针（miningTime / canHarvest / miningSpeedMul 纯表查询）
#include "hotbar.h"       // t763 附魔数值生效链探针（EPF 路由 / 耐久消耗概率 / 攻击伤 tooltip 源）
#include "recipe.h"       // t802 全配方审计探针（match 两阶段匹配 + recipeAt 全表自匹配回归）
#include "smelting.h"     // t802 云杉链熔炉补缺探针（SpruceLog→木炭 + 云杉原木/木板燃料）
#include "playerstate.h"  // t755 死亡态硬锁探针（致死落库 0 / heal 死亡免疫 / respawn 复位链）
#include "playerprogress.h" // review #22 农夫计数回放链式补前置探针（loadVariant → unlockWithAncestry）
#include "world.h"
#include "chunkgeometry.h" // t860 cutout 折叠探针：terrain 段 ChunkGeometry 直调（顶点数行为级断言）
#include "worldstore.h"   // t822 存档 round-trip 探针（真 WorldStore SQLite：savePlayerData/loadPlayerData
                          //   玩家态 JSON 落盘读回；World 层直编，t622 序列化链首次自动化覆盖）
#include "partialblockgeometry.h" // t737 拐角象限断言（mesher 同源调用）
#include "minecartmanager.h"      // t737 环线矿车绕圈断言（骑乘 / 空车两路）
#include "entitymanager.h"        // 审查 #1 末影眼巡航高度回归探针（spawnEnderEye + enderEyeCruiseYAt）
#include "resourcepackmanager.h"  // t785 生物蛋探针（生成式染色表 spawnEggTint 条目存在性直调）
#include "keybindmanager.h"       // t1022 键位重映射探针（默认表完整性 / 冲突拒收 / settings.json round-trip）
#include "itementitymanager.h"    // t804 掉落物火焚探针（item 入 Fire 格 0.8s 焚毁 + itemBurned 烟信号）
#include "boatmanager.h"          // t805 船上岸回归探针（水/陆速比 + 同层湿沙挡停 + 冰面豁免保留）
#include "buildinfo.h"            // t813 构建版本戳探针（stamp / gitHash 格式断言；Core 叶子直编）
#include "frameprofiler.h"        // t933 跨世界泄漏探针（reflood 计数快照差分；Core 叶子直编）
#include "playercontroller.h"     // t814 真消费端探针（Game 层 PlayerController 直编：firePowerTnt/fireDispenserAtQml）
#include "raycast.h"              // t938 铁轨可选中探针【t983 改版：选体薄板 sub-AABB 精确命中 / HitPartial 相机同几何】
#include "worldclock.h"           // t889 暂停语义探针（WorldClock.running 停表行为级 + 源码钉）
#include "xporbmanager.h"         // t889 暂停语义探针（墙钟顺延三管理器调用面钉）；t858 feeder 探针共用
#include "xporbinstancing.h"      // t858 经验球 instancing 试点探针（feeder 实例表内容级断言）
#include "blockdropinstancing.h"  // t1027 掉落物 instancing 治理首批探针（族分桶 / 实例表内容级断言）
#include "glowshellinstancing.h"  // t1039 掉落物 instancing 批 3 探针（光晕壳族收纳 / 颜色实例表 / 空转门 / 容量降级）
#include "tooldropinstancing.h"   // t1041 掉落物 instancing 批 4 探针（工具 3D 族 tier 色 / 弓弦 stringPass / 空转门）
#include "billboarddropinstancing.h" // t1041 批 4 探针（billboard 图标族双池 / 朝相机旋转 / 空转门）
#include "dispenserstore.h"       // t814 发射器/投掷器 per-block 库存（分派 + 扣减断言源）
#include "cheststore.h"           // t1013 箱子矿车内容键存储（转正 / 回生 / 掉落链断言源）
#include "loottable.h"            // t1035 豹猫驯服分化探针（fishingPool 直调：生鱼=驯服道具来源钉）
#include "mobmodel.h"             // review24 低危收尾（#35）：Renderer 白名单长度 ↔ Entities MobType 上界互钉
                                   //   （Renderer 在 Entities 之下，mobmodel.cpp 不得 include entitymanager.h——
                                   //   PLAN §2 低层永不 include 高层；互钉只能落在本测试 TU，它合法 include 全栈）
#include "itemshapegeometry.h"    // t880 异形物品 3D 模型族探针（ItemShapeGeometry 几何契约直调：顶点数/bounds）；t965 形态按钮组态变直调共用
#include "blockcube.h"            // t965 形态按钮组探针（BlockCube 状态态变顶面瓦片行为级直调——同 ItemShapeGeometry 先例）
#include "unitcube.h"             // t966 拖拽方向 rig 探针（真 ResourceBrowser.qml 实例化所需类型注册——羊眼/腿/傀儡头 overlay 用）
#include "bedmodelgeometry.h"     // t966 同上（床预览分支）
#include "enchantbookbox.h"       // t966 同上（附魔台书预览）
#include "mobbow.h"               // t966 同上（骷髅持弓预览）
#include <QVector3D>              // t966 rig：近面世界坐标（scenePosition / 旋转后角点）
#include <QQuaternion>            // t966 rig：sceneRotation（Quick3D 真实合成四元数，行为级读回）
#include <QSet>                   // t967 探针：三分区不交性判定
#include <QSqlDatabase>           // t974 竞态窗口阳性腿：第二连接 BEGIN EXCLUSIVE 瞬持库写锁
#include <QSqlQuery>              // t974 同上（锁持有 / 释放 SQL）

// ── 编译期互钉 + 文件级 extern 直连（原 L94-111 逐行搬移）──
// review24 低危收尾（#35）：MobModel 合法 mobType 白名单表长（kValidMobTypeCount，mobmodel.h public 常量
//   ↔ mobmodel.cpp kValidMobModelType 表编译期互钉）必须覆盖整个 EntityManager::MobType 枚举（t1012③ 起
//   上界 = MobCaveSpider=20，实值经核：MobTest=0 .. MobCaveSpider=20 共 21 值）。枚举中部插值 /
//   尾部新增忘补表行时本断言编译期拦截（t782「整表错位静默钳猪」根因的复刻防线）。
static_assert(MobModel::kValidMobTypeCount == EntityManager::MobCaveSpider + 1,
              "MobModel 白名单长度必须覆盖整个 EntityManager::MobType（0..MobCaveSpider）——"
              "新增 mobType 须同步 kValidMobModelType 表 + mobmodel.h kValidMobTypeCount");

// t777 探针：羊毛层合成器（resourcepackmanager.cpp 文件级函数，头文件外声明 → extern 直连；spawnEggTint
//   进了 .h 因 EggTint 是头内类型，本函数签名纯 QString 无需入头）。review #6：第 3 参 revision 进文件名
//   `_r<rev>`（换包逐版缓存 + 清旧；探针传任意探针版号）。
extern QString generateSheepWoolFaceFile(const QString &furPath, const QString &bodyPath, int revision);
// t829② 夜行者下巴补全合成器（resourcepackmanager.cpp 文件级自由函数；探针密闭 rig 直调，同上先例）。
extern QString generateNightwalkerChinFile(const QString &texPath, int revision);
// Review 2026-08-23 #1 探针：slim 皮肤布局探测器（同上 extern 直连；签名纯 QImage 无需入头）。
extern bool probeSlimSkinLayout(const QImage &tex);
// Review 2026-08-24 #5 探针：pack 原文件直返 URL 构造器（?r=<revision> cache-bust；同上 extern 直连）。
extern QString packFileUrl(const QString &localPath, int revision);

// ── 原匿名命名空间共享件（原 L113-221；去 namespace 壳，自由函数加 inline，体逐行搬移）──
using BR = BlockRegistry;

// ── rig 布局常量：所有 rig 摆在世界高空（y0 平台层；地形 / 树冠最高 ~33，40 以上必空）──
constexpr int kRigY = 41;

inline void tickN(World &w, int n) { for (int i = 0; i < n; ++i) w.tickRedstone(); }

// 源描述：id + 激活 state + 关断方式（state 位清零 vs 整块移除）。
struct SourceDef {
    const char *name;
    quint8 id;
    quint8 onState;                 // 激活态 state（拉杆/按钮/板 bit0；探测轨 bit4；火把/红石块 0）
    bool removeToOff;               // true = 关断 = 清 Air（火把 / 红石块无开关位）
    bool dustTrail;                 // true = 粉传导场景（lever(on) - 粉×3 - 接收器）
};

// 接收器核对：通电后期望（信号 or state 位）+ 降沿后期望（state 位清零；信号型无降沿语义）。
struct RecvDef {
    const char *name;
    quint8 id;
    quint8 onFlag;                  // 通电位（信号型填 0 → 走信号计数判定）
    bool signalBased;               // TNT / 发射器 / 投掷器 = 上升沿信号
};

// t886 获物弹速抛物解镜像（PlayerController useFishingRod 获物分支；P18 双钉——改值须两处同步）：
//   弹出点 = 浮标格向上**列扫**首个非水格 +0.225（review26 #7 口径——kFishCatchPopOffset；直上空气 =
//   floor(bobY)+1+0.225，静水/流动水同式）；目标 = 玩家脚位 +0.9（m_height 1.8 之半）；
//   T = clamp(0.45+0.055D, 0.5, 1.4)；vy = Δy/T + 14T（½g，g=28 掉落物重力镜像）；返 |v|。
inline float fishCatchSpeedMirror(const QVector3D &bobPos, const QVector3D &playerFeet)
{
    const float spY = std::floor(bobPos.y()) + 1.225f; // 列扫：直上空气格顶 +0.225（rig 水池皆无冰盖）
    const float dx = playerFeet.x() - bobPos.x();
    const float dz = playerFeet.z() - bobPos.z();
    const float dy = (playerFeet.y() + 0.9f) - spY;
    const float D = std::sqrt(dx * dx + dz * dz);
    const float T = std::max(0.5f, std::min(1.4f, 0.45f + 0.055f * D));
    const float vy = dy / T + 14.0f * T;
    return std::sqrt((dx / T) * (dx / T) + (dz / T) * (dz / T) + vy * vy);
}

// ── 套件级源码钉帮手（review0907 B-P1-1）────────────────────────────────────────────
// 痛点：套件 ~896 处 contains 源码钉多为裸 contains（不滤注释）——把被钉语句整行注释掉，
//   或注释文本里恰好出现 needle 时钉不红 = 假绿。本帮手统一「读文件 → 字符串感知剥注释 →
//   needle 只在非注释文本 contains（含 count>=minCount 形态）」，t989 stripQmlComments989 /
//   t1008a 逐行滤 / t1023a 滤块注释三处先例的套件级抽版。
// 注释语义按后缀分派：.txt/.py/.cmake → '#' 到行尾（CMake/Python）；其余（C++/QML）→
//   '//' 行注释 + '/* */' 块注释。字符串感知（"..." 与 '...'，反斜杠逃逸），注释体丢弃、
//   注释起止各补一空格防 token 粘连、换行保留。
// 返回：失配 pin 的 id 列表（"id(x出现次数<minCount)" 形态；空 = 全绿），调用方自行落 diag。
// needle 纪律：必须锚真实语句（剥注释后仍在）。**禁止**把 needle 拼进尾注释（如「// t1017」
//   ——剥注释后必失配）；显示名 / UI 文案钉只允许作为 copy 钉单独列账（t1022B 先例），不得与
//   接线钉混计。struct 有 ctor（minCount 缺省 1），聚合 braced 初始化直接可用。
struct SrcPin
{
    SrcPin(const char *i, const char *ndl, int mc = 1) : id(i), needle(ndl), minCount(mc) {}
    const char *id;
    const char *needle;
    int minCount;
};
inline QStringList pinSet(const QString &path, std::initializer_list<SrcPin> pins)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringList() << QStringLiteral("<file-unreadable:%1>").arg(path);
    const QString src = QString::fromUtf8(f.readAll());
    const bool hashLine = path.endsWith(QLatin1String(".txt")) || path.endsWith(QLatin1String(".py"))
        || path.endsWith(QLatin1String(".cmake"));
    QString code;
    code.reserve(src.size());
    enum St { Code, Str, Chr, Line, Block };
    St st = Code;
    int i = 0;
    const int n = src.size();
    while (i < n) {
        const QChar c = src.at(i);
        const QChar nx = (i + 1 < n) ? src.at(i + 1) : QChar(u'\0');
        if (st == Code) {
            if (c == u'"') { st = Str; code.append(c); }
            else if (c == u'\'') { st = Chr; code.append(c); }
            else if (hashLine && c == u'#') { st = Line; code.append(u' '); }
            else if (c == u'/' && nx == u'/') { st = Line; code.append(u' '); ++i; }
            else if (c == u'/' && nx == u'*') { st = Block; code.append(u' '); ++i; }
            else code.append(c);
        } else if (st == Str || st == Chr) {
            code.append(c);
            if (c == u'\\') { if (i + 1 < n) { code.append(src.at(i + 1)); ++i; } }
            else if ((st == Str && c == u'"') || (st == Chr && c == u'\'')) st = Code;
        } else if (st == Line) {
            if (c == u'\n') { st = Code; code.append(c); }
        } else { // Block
            if (c == u'*' && nx == u'/') { st = Code; code.append(u' '); ++i; }
            else if (c == u'\n') code.append(c);
        }
        ++i;
    }
    QStringList miss;
    const auto missTag = [](const SrcPin &p, int cnt) {
        return QStringLiteral("%1(x%2<%3)").arg(QLatin1String(p.id)).arg(cnt).arg(p.minCount);
    };
    for (const SrcPin &p : pins) {
        const int cnt = code.count(QString::fromUtf8(p.needle));
        if (cnt < p.minCount) miss << missTag(p, cnt);
    }
    return miss;
}

// ── 套件运行器：原 main() 局部共享 rig 状态成员化（R20.03）──
// 段函数按原执行序在 runAll() 中调度；w / slotIdx / 信号计数器 / totalFail 跨段共享
// （review26-1 腿间泄漏教训：段间状态与执行序不可变，本类只做等价提升不改时序）。
class MatrixRun
{
public:
    MatrixRun();   // 原 main L229-294：World rig 三 setter + 信号计数器 QObject::connect（序不变）
    void runAll(); // 原 main 尾部：段依序执行 + total FAIL 汇总（原 L47997）

    World w;         // 单一 rig 世界（原 main 局部 w；全段共享，构造序同原）
    int slotIdx = 0; // rig 网格游标（跨段单调；耗尽 qFatal 同原）
    int tntFired = 0, dispFired = 0;
    int lastTntX = -1, lastTntY = -1, lastTntZ = -1;
    int dropItemCount = 0, lastDropId = 0, lastDropX = -1, lastDropY = -1, lastDropZ = -1;
    int snowFellCount = 0;
    int totalFail = 0;

    QPair<int, int> nextSlot(); // 原 main lambda（L249-261）同体成员化
    void placeRigBlock(World &world, int x, int y, int z, BR::Id id, quint8 st); // 原 lambda（L268-274）

    // ── R20.03 目标 B：--filter 腿门控 ──
    //   legFilter 非空时：腿名（PASS 行名）含子串的组才执行，未命中组逐名输出
    //   "SKIP | <name>" 并计 skipCount；为空时直接执行（输出与无参逐位一致）。
    //   记账粒度：静态打印语句（循环腿多次执行按语句计 1）；t740 矩阵循环例外，
    //   逐腿运行期名（section01 专用单名 runLeg 形态）。
    QString legFilter;
    int skipCount = 0;
    void runLegMulti(const QStringList &names, const std::function<void()> &body);
    void runLeg(const QString &name, const std::function<void()> &body)
    {
        runLegMulti(QStringList { name }, body);
    }

private:
    void section01_redstone_core(); // 原 L296-6156 逐字节段
    void section02_early_probes(); // 原 L6157-12287 逐字节段
    void section03_mid_probes(); // 原 L12288-18560 逐字节段
    void section04_instancing(); // 原 L18561-24789 逐字节段
    void section05_render_ui(); // 原 L24790-30840 逐字节段
    void section06_worldgen_drag(); // 原 L30841-37539 逐字节段
    void section07_chests_mobs(); // 原 L37540-43624 逐字节段
    void section08_recent(); // 原 L43625-47995 逐字节段
    void section09_foundations(); // R20.05 基础类型探针（新段置尾：原八段零改动，runAll 末执行——
                                  //   只读 rig 世界 w + 段内 setBlock 均即写即清，不污染任何腿族）
    void section10_command_event_snapshot(); // R20.06 Command/Event/Snapshot 探针（置尾先例沿用：
                                             //   纯类型/队列行为面——rig 世界零接触，接 section09）
    void section11_gamesession(); // R20.07 GameSession 探针（置尾先例沿用：自建 fresh 小世界
                                  //   ×3（48×48×96 s82 双生 A/B + 共享 C），rig 世界 w 零接触，
                                  //   接 section10）
    void section12_worldfacade(); // R20.08 WorldFacade 探针（置尾先例沿用：自建 fresh 小世界
                                  //   ×4（wQ 查询 / wA+wB 写入双生 / wF mesher 门+会话共享），
                                  //   rig 世界 w 零接触，接 section11）
    void section13_editbuffer(); // R20.09 EditBuffer 探针（置尾先例沿用：r2009a 纯类型腿零
                                 //   世界；r2009b-d 自建 fresh 小世界 ×4（双生 wB1/wB2 +
                                 //   wC1 + wD1），rig 世界 w 零接触，接 section12）
    void section14_chunklifecycle(); // R20.10 Chunk lifecycle 探针（置尾先例沿用：r2010a
                                     //   纯图腿零世界；r2010b-d 自建 fresh 小世界 ×4
                                     //   （wB 稳态 / wC 卸载重载 / wS1+wS2 存档往返），
                                     //   rig 世界 w 零接触，接 section13）
    void section15_generationjob(); // R20.11 GenerationJob 同步版 ChunkScheduler 探针（置尾
                                    //   先例沿用：r2011a-c 纯请求模型腿零世界（裸 scheduler
                                    //   + 测试 worker）；r2011d 裸 3×3 ChunkManager +
                                    //   fresh 小世界 ×1，rig 世界 w 零接触，接 section14）
    void section16_backgroundgen(); // R20.12 后台 GenerationJob 探针（置尾先例沿用：r2012a-c
                                    //   零世界（裸 scheduler + 真线程后台 worker）；r2012d
                                    //   裸 3×3 ChunkManager + fresh 小世界 ×1，rig 世界 w
                                    //   零接触，接 section15）
    void section17_meshbuilder(); // R20.13 MeshBuilder 探针（置尾先例沿用：r2013a-c 自建
                                  //   fresh 小世界 48×48×96 s82（快照采集/逐位等价/计数穿透）；
                                  //   r2013d 零世界（纯源码钉 + 裸快照），rig 世界 w 零接触，
                                  //   接 section16）
    void section18_entitystore(); // R20.14 EntityStore 探针（置尾先例沿用：r2014a-b store/
                                  //   Adapter 直驱孪生等价 + 快照权威（裸 World 仅焚毁子面）；
                                  //   r2014c Adapter 消费面回归（notify 沿计数 + feeder 面）；
                                  //   r2014d 纯源码钉 + 编译期钉，rig 世界 w 零接触，
                                  //   接 section17）
    void section19_savecoordinator(); // R20.15 SaveCoordinator 探针（置尾先例沿用：r2015a-c
                                      //   自建 fresh 小世界 + 临时 SQLite 库（QDir::temp()
                                      //   pid 键名，测试自清理）；r2015d 纯源码钉 + 编译期钉，
                                      //   rig 世界 w 零接触，接 section18）
    void section20_generationpolicy(); // §29.4-P1 GenerationPolicy 策略层骨架探针（置尾先例
                                       //   沿用：r2016a-d 全段零世界（纯函数决策组件，缝 =
                                       //   QMap 合成生命周期表），rig 世界 w 零接触，接
                                       //   section19）
    void section21_chunkstreamdriver(); // §29.4-P2 ChunkStreamDriver 玩家移动驱动探针（置尾
                                        //   先例沿用：r2018a-d 全段零世界（纯编排值组件，缝 =
                                        //   QMap 合成生命周期表），rig 世界 w 零接触，接
                                        //   section20）
    void section22_chunkevictor(); // §29.4-P3 ChunkEvictor 卸载+Edits-on-evict 探针（置尾先例
                                   //   沿用：r2019a/c/d 零世界（纯编排值组件 + 自校验 QMap
                                   //   缝）；r2019b 真世界 + 真 SaveCoordinator + fresh 临时
                                   //   库（r2015 先例），rig 世界 w 零接触，接 section21）
    void section23_meshworker(); // D6 worker meshing 探针（置尾先例沿用：r2020a-c 零世界
                                 //   （手写快照 rig + 真线程 MeshWorker）；r2020a 另自建
                                 //   fresh 小世界采集真实快照，rig 世界 w 零接触，接
                                 //   section22）
    void section24_qmldynamization(); // §29.4-P4 QML 动态化探针（置尾先例沿用：r2021a/b/c
                                      //   自建 fresh 小世界（48×48×96 s82 ×3 + 32×48×96
                                      //   s82 非方阵 ×1；r2021c 另挂真 QQmlEngine 真链
                                      //   harness——t874 家族同门），rig 世界 w 零接触；
                                      //   r2021d 纯源码钉，接 section23）
    void section25_sparseworld(); // §29.5-W1 稀疏世界核探针（置尾先例沿用：r2022 承重墙/语义族
                                  //   /恒等承重三腿自建 fresh 小世界族——固定世界四 setter
                                  //   incantation + sparse 构造缝（同 seed 同 dims 双世界），
                                  //   rig 世界 w 零接触；r2022 钉面腿纯源码钉，接 section24）
};
