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
#include "entitymanager.h"        // 审查 #1 末影眼巡航高度回归探针（spawnAbyssEye + abyssEyeCruiseYAt）
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
    void section26_sparse_population(); // §29.5-W1b sparse population parity 探针（置尾先例沿用：
                                  //   r2023a fixed 零变化墙 / r2023b parity 承重墙 / r2023c
                                  //   加载顺序无关 / r2023d 结构钉——80×80×96 fresh 小世界族 +
                                  //   纯源码钉，rig 世界 w 零接触，接 section25）
    void section27_streaming_wiring(); // §29.5-W2 位置源 + 驱动接线 探针（置尾先例沿用：
                                  //   r2024a fixed 零活动承重墙 / r2024b 通电走查承重 /
                                  //   r2024c 真线程收割+#7 契约+背压 / r2024d 结构钉——
                                  //   80×80×96 fresh sparse 小世界族 + 48×48×96 fixed 小世界
                                  //   + 真 PlayerController 走查 + 真线程 worker（防 flake
                                  //   deadline 有界），rig 世界 w 零接触，接 section26）
    void section28_eviction_persistence(); // §29.5-W3 驱逐 + Edits-on-evict 落盘 探针（置尾
                                  //   先例沿用：r2025a fixed 零活动墙（无附加表构造）/
                                  //   r2025b 驱逐回灌承重（真临时库 fresh+用后即删：persist
                                  //   先于转移序柱 + ⑥⑦擦槽 + revision 沿 + blob 物化逐位
                                  //   恒等 + population 跳过）/ r2025c 失败中止（真锁注入）
                                  //   + 实体先移除语义 / r2025d 结构钉——80×80×96 fresh
                                  //   sparse 小世界族，rig 世界 w 零接触，接 section27）
    void section29_wiring_meshworker(); // §29.5-W4 bake→worker 网格化 探针（置尾先例沿用：
                                  //   r2026a fixed 零变化墙（无 worker 构造 + 全同步内联）/
                                  //   r2026b 异步等价承重墙（真执行器线程 + tick 尾收割拍 →
                                  //   收割应用 ≡ 同步直调逐位恒等 + 账面对账）/ r2026c 回退
                                  //   与卫生（满载/已停拒绝 → 内联回退计数可见 + 最新快照胜
                                  //   + 注销卫生）/ r2026d 结构钉（F3 worker 列 + QML 零触碰
                                  //   反探 + 收割拍单点收口 + 单一权威兼容反探）——
                                  //   80×80×96 fresh sparse 小世界族 + 48×48×96 fixed 小世界
                                  //   + 真线程收割（防 flake deadline 有界），rig 世界 w
                                  //   零接触，接 section28）
    void section30_streaming_persistence(); // §29.5-W5 后端 流式世界持久化 + D2/D3 标志 +
                                  //   overlay 合并 探针（置尾先例沿用：r2027a fixed 零活动
                                  //   墙[保存/读档全语义逐位不动 + 库面零流式表] / r2027b
                                  //   流式存读往返承重墙[保存含冲洗 + 销毁重建逐位恒等 +
                                  //   二次幂等，真临时库 fresh+用后即删] / r2027c D3 转换
                                  //   往返[blob 物化 + 代次对代次仲裁 + 逆翻照旧] /
                                  //   r2027d 结构钉[worldstore 禁触反探 + additive 正面钉 +
                                  //   冲洗失败不谎报 + QML 零触碰]——80×80×96 sparse 族 +
                                  //   48×48×96 fixed 族 + 真线程收割（防 flake deadline
                                  //   有界），rig 世界 w 零接触，接 section29）
    void section31_streaming_ui(); // §29.5-W5b 流式 UI 探针（置尾先例沿用：r2028a 默认关零
                                  //   变化墙[真链分流恒 false + 会话零构造 + 三写/往返回归 +
                                  //   库面零流式表] / r2028b 开关+转换通电面[真 QQmlEngine ×
                                  //   真桥单例：标志行 + sparse 进入通电 + 真玩家走查生产泵收敛 +
                                  //   D3 转换 blob 逐位 + 冲洗生产面 + 行回灌仲裁] / r2028c
                                  //   QML 面钉[变更面集中 + Q_INVOKABLE 逐一正面钉 + 词元禁触 +
                                  //   值组件零暴露]——真临时库 fresh+用后即删 + 真线程收敛，
                                  //   rig 世界 w 零接触，接 section30）
    void section32_residual_sweep(); // t1055 agent-review 残余清偿合集一 探针（置尾先例沿用：
                                  //   r2029a scheduler detachWorker 守卫[销账+摘指针+泵安全
                                  //   no-op+重挂一致] / r2029b lastDirtyChunks 值快照语义 /
                                  //   r2029c submitAsync fail-fast 默认[204 可见穿透+零影响
                                  //   回归柱] / r2029d weather 0 值回落 qInfo 诊断[行为零变
                                  //   化]——裸 scheduler + 零线程替身 worker + fresh 小世界
                                  //   ×2，rig 世界 w 零接触，接 section31）
    void section33_savebridge_wiring(); // t1057 SaveCoordinator 生产接线 探针（置尾先例沿用：
                                  //   r2031a Clean 路径零变化墙[返回语义/计数观测/参数透传/
                                  //   往返回归/旧档 Fresh] / r2031b marker→complete 往返+
                                  //   中断恢复收敛[真保存走桥 + FaultHook 经桥注入 + 戳权威] /
                                  //   r2031c #5② 开库失败可区分[真锁占 + open 级失败两面目] /
                                  //   r2031d 结构钉[worldstore 零触碰反探 + additive 正面钉 +
                                  //   QML 两处例外面钉 + 生产零挂载]——真 QQmlEngine real-chain
                                  //   （真桥单例）+ fresh 临时库，rig 世界 w 零接触，接 section32）
    void section34_observation_lines(); // t1059 P5 观测前置 探针（置尾先例沿用：r2033a fixed
                                  //   全零行+行格式钉 / r2033b 流式行计数与权威面逐项恒等
                                  //   [四窗：初载/取消走查/网格收割/驱逐落盘] / r2033c dt 负/
                                  //   NaN 分域计数——sparse fresh 小世界族 + 真临时库 +
                                  //   ChunkGeometry 桥提交，rig 世界 w 零接触，接 section33）
    void section35_fixed_async_bake(); // §29.7 t1060 fixed 世界 bake 异步化 探针（置尾先例沿用：
                                  //   r2034a fixed 异步≡同步逐位等价承重墙[地形/流体/异形/边界
                                  //   四形态] / r2034b 风暴摊平[dayMul 跨门提交数=段数 + 单拍
                                  //   应用上界 + 排干收敛 + 终态逐位] / r2034c 回退与卫生[env
                                  //   全同步 + 退化满载内联回退 + 析构卫生] / r2034d 结构钉
                                  //   [env 缝/惰性单例源序/单实例线程身份/不双起线程/收割宿主
                                  //   单点收口+生产链行为柱/单一权威反探/F3 推送面]——
                                  //   fixed 小世界族（96×96×96 s82 + 48×48×96 s82），rig 世界
                                  //   w 零接触，接 section34）
    void section36_streaming_entry(); // t1061 无限世界生产入口 livelock 探针（置尾先例沿用：
                                  //   r2035a fixed 零变化墙[真链分流恒 false + 会话零构造 +
                                  //   驻留沿零发射 + 通电门/泵墙/进入分流三钉] / r2035b 入口
                                  //   收敛承重墙[真链生产尺寸进入：物化突发恰一条驻留沿 +
                                  //   沿时刻驻留集合流[减员全在 gen 半径外 + 驻留数单调不降]
                                  //   + 收敛到请求集尺寸零半径内驱逐 + 收敛后沿停 + 整池重建
                                  //   消费面钉] / r2035c 预生成中心×出生分歧[合规驱逐 + 错位域
                                  //   收敛 + 走回重物化逐位恒等 + 收敛后驻留稳] / r2035d 走离
                                  //   走回+结构钉[首沿门前惰性 + 擦槽驱逐 + Edits-on-evict +
                                  //   单一权威源钉族 + 物化批口五落点]——fixed 宿主 48×48×96
                                  //   s82 + 核心域 160×160×96/80×80×96 + 真临时库，rig 世界
                                  //   w 零接触，接 section35）
    void section37_walkback_identity(); // t1062 走回重生成内容漂移收口 探针（置尾先例沿用：
                                  //   r2036a fixed 世界零变化墙[同 seed 双 generate 逐位恒等 +
                                  //   generate() 体全 pass 字面钉 + 物化链反探禁出] / r2036b
                                  //   物化时序无关承重墙[全驱逐→逆序重物化 ≡ 初见逐位 + 同
                                  //   chunk ×3 轮恒等 + 零掩蔽口径留痕] / r2036c 预生成 vs
                                  //   按需三方恒等[A 初见 == A 走回重物化 == B 空域按需 +
                                  //   未动邻块零扰动] / r2036d 结构钉[统一物化路径单一权威 +
                                  //   钳制/快照面不回退 + fixed 反探]——80×80×96 fresh sparse
                                  //   小世界族，rig 世界 w 零接触，接 section36）
    void section38_slot_pool_patch(); // t1063 Main.qml 差分池 patch 探针（置尾先例沿用：
                                  //   r2037a fixed 世界零变化墙[真桥进入握手恒 false + 驻留
                                  //   零沿 + 池零 patch 零重建 + 段指针逐位不动] / r2037b
                                  //   增量等价承重墙[每沿后池 ≡ 整池重建参照逐项恒等 + 幸存
                                  //   组指针复用 + 重加组 canonical 归位新对象] / r2037c 增量
                                  //   性计量腿[每沿 churn = 恰变化键组规模 + 批口单沿双键 +
                                  //   走查模拟账本闭合] / r2037d 结构钉[消费面恰一处 + 增量
                                  //   入口 + 组对齐源钉 + 消费端零触碰 + 玩法路径]——
                                  //   r2037a-c 真 QQmlEngine × 真 World 池镜像 wrapper
                                  //   （r2021c 真链 harness 同门），r2037d 纯源码钉，rig
                                  //   世界 w 零接触，接 section37）
    void section39_exit_save_backoff(); // t1064 退出存档失败重试退避 探针（置尾先例沿用：r2038a 无锁常态墙[首试即真零重试 + 零退避双证 + +3 口径 + 完成门/toast 语义面] / r2038b 退避承重[真锁 BEGIN EXCLUSIVE：桥直调窗 [100,300]ms + 锁窗内释放→重试收敛恰一次] / r2038c 上限与不谎报[锁全程持有：恰一次重试即止 + 计数零动 + 台账历史原样 + 释放后收敛] / r2038d 结构钉[共用实现单点 + 两调用点 + 旧 0ms 重放禁入 + 完成门/归还序零触碰复钉 + 退避常量 ≤300ms 源钉]——真 QQmlEngine × 真桥单例 × 生产 wrapper 原文抽取（brace 配平），rig 世界 w 零接触，接 section38）
    void section40_slot_pool_fixed_switch(); // t1065 流式→固定世界切换池残留收口 探针（置尾先例沿用：r2039a fixed-only 零变化墙[真链进入恒 false×2 + 驻留零沿 + 池零 patch 零重派生 + 段指针逐位不动 + 时代标记恒 false] / r2039b 切换承重墙[真链流式走查产负坐标驻留键 → detach → 进 fixed → 时代门重派生恰一次 → 池与固定网格逐位一致 + 负坐标组清零 + churn 账本闭合] / r2039c 往返腿[fixed→流式→fixed→流式全链池一致性 + fixed 两轮键集逐位一致 + 流式再进入重派生不回退 + 差分路径仍活] / r2039d 结构钉[时代标记单点落账 + fixed 收口恰一处 + 函数域抽体钉 + 两消费端体零触碰反探 + 生命周期/C++ 模式读面禁入 QML 反探 + reinitializeAsFixed 体零沿语义钉]——r2039a-c 真 QQmlEngine × 真 World 池镜像 wrapper（section38 同门镜像家族），r2039d 纯源码钉，rig 世界 w 零接触，接 section39）
    void section41_save_busy_zero(); // t1066 保存链 BUSY_TIMEOUT=0 探针（置尾先例沿用：r2040a 无锁零变化墙[首试即真恰 1 调用 + 墙钟 ≤300 + +3 口径 + 台账 Clean + 探锁零退避面 + 完成门/toast 语义面] / r2040b 首试即时性承重[真锁全程持有 + 真链零注入：首试 <1s 诚实败 + 整链[探锁→150ms 档→恰一次重试] <2s 有界 + 计数零动 + 台账历史原样 + 释放后收敛] / r2040c 锁中途释放收敛[t1082：事件驱动锁窗[持锁者见退避打卡才放手] + 真链零注入：首试瞬时败→探锁→退避打卡恰一次[逻辑轮次面]→档→重试收敛真 + 恰一次重试 + 代次单调 2/2 + 墙钟上界] / r2040d 结构钉[逐连接放置钉[每 addDatabase 后 2 行内必跟归零设置 + 每文件设置点计数 4/2/1/1 = 新增连接漏配即红] + 剥注释 minCount 复钉 + r2038 零触碰复钉 + 探锁连接保持 0 钉]——真 QQmlEngine × 真桥单例 × 生产 wrapper 原文抽取（brace 配平），零故障注入直面真锁，rig 世界 w 零接触，接 section40）
    void section42_ip_root_rename(); // t1067 §9 区隔改名批收口探针（置尾先例沿用：r2041a 旧行为等价墙[点燃 6/20 格 + 生产钩子熄门 + 直挖连通域熄灭 + 12 框架环开环/完整性复检 + 真临时库存读往返 id/state 逐位保真] / r2041b 旧词根清零钉[src/ 全树四词根命中行必携 §9 记载标记 + 每文件计数 == 盘点口径 3/15/2/1/4 总 25] / r2041c 用户可见面钉[displayName 新名断言 + blockName 内部分类字面断言 + nameForBlock(EndEyeId)=="暗渊之眼" + 旧中文名命中行必携 §9 + 每文件计数 == 盘点口径总 7] / r2041d 结构钉[数值 id 逐位 111/131/138/0x01/32/0x23A + 编译期 static_assert 互钉 + 方法族声明面源钉 + t1067 映射头注锚串裸文本在场钉]——各腿自建 fresh 世界/临时库，rig 世界 w 零接触，接 section41）
    void section43_ender_root_rename(); // t1068 §9 Ender 族改名收口探针（置尾先例沿用：r2042a 旧行为等价墙[眼 kind 7 巡航 +8 / 珠 kind 8 非整格自格落点 / 改名钩子传送 + Survival 自伤 (5, AbyssPearlTp) + Creative 零伤 / shapeless 配方答不变眼 id + 内部字面 abyss_eye / 真临时库存读往返双物品逐位保真] / r2042b 旧词根清零钉[src/ 全树八词根命中行必携 §9 记载标记 + 每文件计数 == 盘点口径 11/6/3/4 总 24] / r2042c 用户可见面钉[nameForBlock 双物品新名 + 死因文案 + 读取面图标路径 + 自绘入口 + 调色板 id 面] / r2042d 结构钉[数值 id 逐位 0x243/0x23A/7/8/16 + 编译期 static_assert 互钉 + 改名族声明面源钉 + QML 面 + t1068 映射头注锚串裸文本在场钉]——各腿自建 fresh 世界/临时库，rig 世界 w 零接触，接 section42）
    void section44_residual_pair(); // t1069 残余清偿合集探针（置尾先例沿用：r2043a 既有行为零变化墙[相邻族腿名计数源钉 ×3 + 池路径契约钉 + 生产 patch 体原文抽取执行初建/单键驱逐/重加全链池行为逐位] / r2043b 口径收窄承重[收窄门逐字源钉 + 固定世界 comparator 敏感面行为柱：旧列高基准盲区的块缘带体素腐败必被全逐位比对捕获 + 退役留痕锚] / r2043c 精确摘除承重（生产 patch 体原文执行：幸存组队列条目保留恰 4 + 销组条目摘净零残留 + 归属 ⊆ 驻留账本 + 无销组沿队列逐位不动 + 二次驱逐只摘自家三条 + 保留 4 段 = 计量的渐进重建节省） / r2043d 结构钉[摘除谓词族源钉 + 全清写点精确计数恰两处 + 两消费端/稳态兜底契约句零触碰反探 + 段入口 comparator 锚 + PASS 如实化锚 + 退役留痕锚]——各腿自建 fresh 世界，rig 世界 w 零接触，接 section43）
    void section45_savepath_opt(); // t1070 流式/存档路径优化合集三件探针（置尾先例沿用：r2044a 零变化墙[相邻族基线计数源钉 ×3 + 组件保存面逐位双保存（同 coordinator dims 复用路径逐字节）+ fixed 会话惰性面] / r2044b 件一承重[走回全 Load 波：worker 双计数对账 executed 推进而 generated 恒冻 + 逐位恒等回灌 + 九行稳定] / r2044c 件三承重[真链三保存：唯一 dims 首建重建恰 +1、二三次保存复用恒 0 计数断言 + 冻结点三面（含变更/窗内漂移不泄/活体漂移真实）+ 台账 Clean 1/1→2/2→3/3] / r2044d 结构钉[件一权威源钉族（NEG-1 韧性选针：token/序/信封针在门变异下存活）+ 件二量化注锚 + 件三桥顶针（栈上实例恰 1 = 只读恢复面）+ 禁触面反探（worldstore 与 chunk_edits 数据面零触碰）]——r2044b 真会话真线程波 + r2044c 真 QQmlEngine 真链 × 真桥单例，rig 世界 w 零接触，接 section44）
    void section46_skeleton_integrity(); // t1071 测试骨架完整性合集探针（置尾先例沿用：r2045a 件一承重[section01 局部账本零残留反探 + 成员账本唯一写点钉 + 记账通路行为柱（净零往返）] / r2045b 件二承重[聚合 4σ 双窗在场 + 旧绝对断言退役双面 + 权重分布面保留清单 + 粉在场确定性源钉族 + 腿名/PASS 同步] / r2045c 结构钉[md5 钉面修订留痕锚 + 全量 md5 存证 + 成员账本跨段共享注锚] / r2045d 结构钉[flake 名单在场钉（四项名单整串逐字 + 复跑清协议 + t789 退出注）]——纯源码钉/零世界腿，rig 世界 w 零接触，接 section45）
    void section47_streaming_outer_content(); // t1073 流式外环内容三合一诊断修复探针（置尾先例沿用：
                                  //   r2046a fixed 零变化墙[双 generate 逐位恒等 + 归一/域门单一权威钉 +
                                  //   旧钳制面反探禁出] / r2046b 外环内容承重墙[生产尺寸核心 160×160 真链
                                  //   走查 6 chunk 出核：外环树/矿/carve 三签名齐备 + 走回重物化逐位恒等] /
                                  //   r2046c 刷怪域承重[外环黑夜自然刷怪 + 外环日光燃烧 + AI 钳制不拽核] /
                                  //   r2046d 结构钉[扩展域置位单点 + lattice 带族 + 域门调用面 + 反探族]——
                                  //   fixed 48×48×96 s82 + sparse 核心 160×160×96 ×2 + 真临时库，rig 世界
                                  //   w 零接触，接 section46）
    void section48_streaming_outer_light(); // t1074 无限世界外环挖方块全黑探针段（置尾先例沿用：
                                  //   r2047a fixed 零变化墙[fixed 挖掘/火把行为柱 + 域门/盒钳制 Fixed 分支
                                  //   字面钉] / r2047b 外环全黑复现+修复承重[生产尺寸真链出核 6 chunk：
                                  //   播种柱 + 挖掘 2 深=挖出格天光满 + 跨 chunk 缘火把方块光渗入] /
                                  //   r2047c 负坐标外环腿[负侧播种柱 + 挖掘亮 + 负侧 chunk 缘火把跨 chunk] /
                                  //   r2047d 结构钉[负坐标安全标脏 floorDiv 循环 + sparse 统一物化门边界读
                                  //   分支 + sparse 无界盒 + 旧码三字面反探禁出]——fixed 48×48×96 s82 +
                                  //   sparse 核心 160×160×96 ×2 + 真临时库，rig 世界 w 零接触，接 section47）
    void section49_entity_blob_shadow(); // t1075 blob 软阴影（玩家+生物贴地投影）探针段（置尾
                                  //   先例沿用：r2048a 平地贴地承重墙[玩家+双 mob 贴地 Y/alpha/
                                  //   半径 + 指纹差分零变化零 bump + 几何可数通道] / r2048b 悬崖
                                  //   边缘+腾空衰减[悬浮块顶满档→同高悬空淡出 0.25→中空 0.625→
                                  //   超窗缺席单调链] / r2048c 未物化缺席+fixed/sparse 贴地逐位
                                  //   一致[radius-0 同 seed 孪生 + 按需物化缝] / r2048d 结构钉
                                  //   [采样权威门/几何契约/QML 接线与材质契约/注册族 + Main.qml
                                  //   零对地采样反探 + EntityStore 零触碰反探]——fixed 48×48×96
                                  //   s82 + sparse radius-0 孪生，rig 世界 w 零接触，接 section48）
    void section50_streaming_negative_walk(); // t1076 大核流式世界负向走查零收敛复现定界段（置尾
                                  //   先例沿用：r2049 负向走查收敛断言[生产尺寸真链逐 chunk 负向
                                  //   走查 (4,5)→(-3,5) 每站有界泵拍 → 终站静置 ±2 方窗 25 键
                                  //   deadline 150000ms 收敛 + 六态直方/F3 流式行差分/驻留键样
                                  //   本/loadChunkAt 兜底探针四域失败签名 diag]——fixed 48×48×96
                                  //   s82 + sparse 核心 160×160×96 s82 + 真临时库，rig 世界
                                  //   w 零接触，接 section49）
    void section51_bonemeal_flora_boneblock(); // t1077 骨粉催生草丛/花 + 骨块段（置尾先例沿用：
                                  //   r2050a 催生承重墙 + r2050b 骨块承重墙 + r2050c 分布/边界墙 +
                                  //   r2050d 结构钉腿，World 直调 + 纯表腿 + 源码钉，rig 世界零接触，接 section50）
    void section52_dye_mixing(); // t1078 染料获取链补全段（置尾先例沿用：r2051a 染料获取承重墙
                                  //   [源自含复核 + 墨囊转换双格 + 全表不动点闭包 15/16 色] +
                                  //   r2051b 混色行为柱[9 条 2→2 产率/序无关 + 混色→羊毛两跳链] +
                                  //   r2051c 边界与覆盖墙[非配方组合全 nullptr 无效应不消耗] +
                                  //   r2051d 结构钉[注册族 + 名去重恰一次 + 染料右键分流单点反探 +
                                  //   QML 零触碰反探 + t1078 锚点]，纯表腿 + 源码钉，rig 世界零接触，接 section51）
    void section53_hopper(); // t1080 漏斗机制四语义探针段（置尾先例沿用：r2052a 收集承重墙 +
                                  //   r2052b 输出/抽取承重墙 + r2052c 红石锁停/边界墙 + r2052d 结构钉/
                                  //   持久化往返，fresh 小世界族 + 纯表腿 + 源码钉，rig 世界零接触，
                                  //   接 section52）
    void section54_spawner(); // t1081 刷怪笼条件刷怪 + 破坏语义探针段（置尾先例沿用：r2053a 条件
    void section55_jukebox(); // t1083 唱片机 + 音乐盘探针段（置尾先例沿用：r2054a 放入/吐出承重墙
                              //   [真链放入 state 逐位/生存消耗/started 沿；吐出盘还原+清位+stopped 沿；
                              //   创造不消耗]，r2054b 音乐盘+战利品承重墙 [三盘 id/名/调色板/不可堆叠
                              //   双面/映射 round-trip/配方命中负例/两池挂接逐位/确定性 roll]，r2054c
                              //   播放语义+边界墙 [到期前不吐/到期自动吐+双吐守卫/载入续播/生存破坏吐盘
                              //   +创造破坏不吐/非盘拒收无效应不消耗]，r2054d 结构钉 [def 行逐字段/
                              //   kMcBlockId 84/state 编解码权威/音色族/图集 191/配方·tick 接线源钉/QML
                              //   路由钉+状态机零 QML 反探]。真链 rig = t945 同门，fresh 48×48×96 s82，
                              //   rig 世界零接触，接 section54）
                                  //   刷怪承重墙[节流/距离窗/昼夜光照门/火把压停/双 rig 采样确定性] +
                                  //   r2053b 刷出合法性+破坏承重墙[盘缘采样承重/盘外拒采/支撑门保留/
                                  //   破笼即停/def 破坏面] + r2053c 与地牢族解耦墙[fixed 手放笼 + sparse
                                  //   负坐标外环笼 + 零地牢路径反探] + r2053d 结构钉[常量族/state 编码/
                                  //   签名接线/采样表哈希/QML XP 线/单一权威与零触碰反探]——fixed 48×48×96
                                  //   s82 小世界族 + sparse 构造缝 ×1，rig 世界零接触，接 section53）
};
