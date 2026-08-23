#ifndef MINECARTMANAGER_H
#define MINECARTMANAGER_H

#include <QObject>
#include <QVector3D>
#include <QtQml/qqml.h>

#include <unordered_set> // t658 探测轨占用边沿表（m_detectorOccupied）
#include <vector>

// 矿车实体管理器（t565；Entities 层）。机制等价 MC 1.0 minecart。
//
// 矿车是新实体类型：放在铁轨上（Rail 方块），玩家右键矿车实体 → 骑乘，W/A/D 控制沿轨道前后 /
// 拐弯（轨连接位导向 —— 矿车从当前轨格沿「玩家意图方向最近的连接向」推进到相邻轨格，拐角自动转弯；
// 机制等价 MC 1.0 矿车沿轨行驶 + 弯道自动转向），轨上快速移动（明显快于步行）；离轨（下一格无轨）→ 停。
// 左键挖矿车（t735 ②）：创造单击即毁 + 掉矿车物品（可重放）；生存需连击 kCartHitPoints 下（每击受击
//   摇晃 —— 呈层据 hpAt 绑定驱动摇晃动画），最后一击才毁 + 掉落（用户明确要求的多击耐久语义；机制
//   等价口径下 MC 1.0 矿车本是一击即毁，此为按本工程 spec 的有意偏差）。
//
// 数据形态：每个矿车实体 = {世界坐标 pos（矿车中心，车底贴轨板顶 —— t734 基准=轨格 cell 底+1/16）、水平行进方向 dirX/dirZ（单位向量，
//   轨向四向之一）、朝向 yawDeg（呈现层 Model 据它定向）、速度 speed（blocks/s，W 前进加速 / 松键摩擦衰减）、
//   槽位 alive（slot-reuse 模型）}。呈现层（Main.qml 的 cartHost Repeater）经 count/posAt/yawAt 读数据，
//   自发渲染矿车 Model（原创斗形几何，NoLighting），绝不反向写。
//
// 骑乘（steer）：单机唯一玩家 → MinecartManager 持 m_riderCart（玩家当前骑的矿车索引，-1 = 未骑）。
//   tryMount(origin,dir,maxDist) 跑独立矿车命中射线（findCartHit，slab ray-AABB，同 BoatManager::findBoatHit
//   模式）命中矿车 → m_riderCart = idx + 返 true；dismount(world,...) 清 m_riderCart + 把玩家摆到矿车侧
//   安全位。PlayerController.step 骑乘分支调 tickRiddenCart 推进矿车物理（WASD 输入由 PlayerController 算
//   wish 传入），并据返回的矿车位把玩家 m_pos 同步到车座位（玩家随车位移、骑乘期禁用玩家自身移动）。
//
// 物理（tickRiddenCart，被骑的矿车）：停驻（speed==0）且有 WASD 输入时先按 wish 重选行进向（面向死端壁
//   的输入因无可用连接被拒 → 保持停驻；面向来路的输入选中来路 → 蓄力反推回程，机制等价 MC 尽头轨反向
//   推回）。据 wish 在矿车行进方向上的投影算目标速度（前进 / 后退），速度向目标 lerp（动量）→ 沿「轨连接
//   位」逐格推进（跨格时按行进方向选下一连接向 —— 拐角自动转弯，反向连接不选；仅剩来路（死端）→ 停）。
//   矿车 Y 钉轨面（轨格 cell 底 + 坡面高 + kCartRideH；t734 基准修真：轨板贴 cell 底非 cell 顶）。
//
// 分层（PLAN §2）：本层属 Entities（位于 Game/Physics 之下、World 之上）。向下只读 World
// （blockAt / stateAt，判轨 / 连接位），不依赖 Renderer / Physics / QtQuick3D。tickRiddenCart /
// tryMount / dismount / hitCartFromRay 由 PlayerController（Game/Physics 层）每帧 / 右键 / 左键时调
// （C++ 直调，非 Q_INVOKABLE —— 同 BoatManager / ItemEntityManager 先例）。spawnCart 现带 World* 参数，
// 亦非 Q_INVOKABLE（t708：避 moc 对 World* 前向类型的 metatype 处理）—— 由 PlayerController placeBlock 单入口调。
class World; // 前向声明（tickRiddenCart 只读 World；完整定义在 .cpp include）
class MinecartManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MinecartManager)
    // count：当前矿车槽数（含已释放空槽；slot-reuse 模型下单调不降）。Repeater 作 int model → 生成
    //   0..count-1 delegate。NOTIFY entitiesChanged 驱动 spawn 后 Repeater 追加新 delegate（不重建已有）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：矿车集版本号（随 spawn / 挖毁 / 骑乘物理推进 自增）。供「触碰」绑定作 NOTIFY 触发器
    //   （同 BoatManager.revision 模式）—— posAt/yawAt 是 Q_INVOKABLE 不被 NOTIFY 自动跟踪，需
    //   { revision; posAt(i) } 显式建依赖。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit MinecartManager(QObject *parent = nullptr);

    int count() const { return int(m_carts.size()); }
    int revision() const { return m_revision; }
    // 当前活体矿车数（不含已释放空槽）。
    Q_INVOKABLE int liveCount() const { return m_liveCount; }
    // 第 i 个槽位是否活体。呈现层 delegate 据它 visible（空槽隐藏，slot 复用保 Repeater count 单调不降）。
    Q_INVOKABLE bool aliveAt(int i) const;

    // 在格 (x,y,z) 生成一个矿车实体。位置 = 该格中心、车底贴面：t734 起双模式 —— 目标格是 Rail（isRail
    //   家族）→ 轨上模式（车底贴轨板顶 = cell 底+1/16+坡面高，可行驶）；目标格非 Rail（t734 放宽地面放置）
    //   → 地面静止模式（车底贴 cell 底，kCartGroundH；推不动 —— pushEmptyCart 的 pickTrackStep 无轨不推、
    //   tickRiddenCart 离轨钉停，静止待左键拾取）。
    //   t708 ①：贴轨面 —— 放置格中心（fx=fz=0.5）的坡面高按 mesher / tickRiddenCart 钉轨面同公式叠加
    //   （railProbeDelta 本轴抬升），坡格放置不悬空；拐角 / 十字无坡（mesher 同判）。
    //   t708 ②：初始朝向沿轨延伸 —— 据连接位定轴（X 连接 → 沿 X；否则 Z），单端连接取该延伸向、
    //   对向 / 无连接取 +X / +Z；车头由下方 tick / 玩家进入后按 wish 重定向（S 反推见 tick 负速倒行）。
    //   达 kCap → 跳过 + qWarning（防溢出）。
    //   world 可空（QML 兜底入口缺世界时退地面静止模式 + 默认 +Z 朝向）。t708：本方法带 World* 参数 ——
    //   **非 Q_INVOKABLE**（同 tryMount / dismount 的 C++ 直调约定；Q_INVOKABLE 会让 moc 对 World* 前向
    //   类型做 QMetaType 注册 → 「Meta Types must be fully defined」编译错）。仅 PlayerController placeBlock
    //   调（QML 无调用点 —— Main.qml 只读 count/revision/posAt/yawAt）。
    void spawnCart(int x, int y, int z, World *world = nullptr);

    // 玩家当前骑的矿车索引（-1 = 未骑）。PlayerController.step 据它判骑乘分支。
    Q_INVOKABLE int ridingIndex() const { return m_riderCart; }

    // 第 i 个矿车的世界坐标（呈现层 delegate 绑它摆位）。越界返回 (0,0,0)。
    Q_INVOKABLE QVector3D posAt(int i) const;
    // 第 i 个矿车的朝向（度；车头方向，呈现层矿车 Model eulerRotation.y）。越界返回 0。
    Q_INVOKABLE float yawAt(int i) const;
    // t769 第 i 个矿车的车身俯仰角（度；正 = 车头上扬，同 EntityManager::arrowPitchAt 约定 → 呈现层
    //   delegate eulerRotation.x 直连）。坡上贴合轨面（1:1 坡 ~±45°）、平轨 / 拐角 / 离轨 0；跨段（平↔坡）
    //   沿车头向 ±kCartPitchProbe 采样轨面高差 → 随位置线性过渡（连续无阶跃）。**纯呈现量**：物理判定
    //   （碰撞盒 / 命中射线 / 骑乘座位）不读它。越界 / 空槽返回 0。
    Q_INVOKABLE float pitchAt(int i) const;
    // t735 ② 第 i 个矿车的剩余耐久（可承受击数；满血 = kCartHitPoints，越界 / 空槽返回 0）。呈现层 delegate
    //   绑它（`carts.revision >= 0 ? carts.hpAt(index) : 0` 表达式形式注册 revision 依赖，t498/t556 铁律），
    //   值变小 → onCartHpChanged 触发受击摇晃动画。越界 / 空槽返 0（空槽 delegate 本就 visible=false）。
    Q_INVOKABLE int hpAt(int i) const;

    // 切世界清空（同 BoatManager.clearAll 模式）：释放全部活体槽（保 slot-reuse 单调不变量）+ 清骑乘态。
    Q_INVOKABLE void clearAll() {
        for (size_t i = 0; i < m_carts.size(); ++i)
            if (m_carts[i].alive) releaseSlot(int(i));
        m_riderCart = -1;
        emit entitiesChanged();
    }

    // ── C++ 直调（PlayerController 用；非 Q_INVOKABLE 避 moc 对 World* 前向类型的 metatype 处理）──

    // 跑独立矿车命中射线（slab ray-AABB，同 BoatManager::findBoatHit 模式）：从 origin 沿 dir（单位向量）
    //   maxDist 内命中首个活体矿车 → 返其索引（outDist 写命中距离）；无命中 → -1。
    int findCartHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const;

    // 挖矿车（攻击）：跑 findCartHit 命中矿车 → 按 t735 ②耐久语义分流：
    //   instantBreak=true（caller 创造模式传）→ 直接摧毁且**无掉落**（t767 对齐 t571①「主动破坏掉落仅
    //   生存」——创造瞬破只移除车体，不 emit cartBroken）；否则 hp>1 时扣 1 血（不掉不毁，呈层 hpAt 绑定
    //   变化驱动受击摇晃动画），hp 归零的那一击才 releaseSlot + emit cartBroken（呈层据它 spawnItem 掉
    //   MinecartId 物品）。t735 ①：掉落格带 world 做**非实心邻格散布**（同船 t711 修法）——旧版掉矿车
    //   中心格 = 常与攻击者本人所在格重合，kPickupDist 1.5 半径内 0.5s 免拾窗一过即被 pickupScan 吸回，
    //   用户观感「不掉落」（创造贴脸测更易复现；与船 t661 同根因：掉落点选格，非模式门控）。
    //   若命中的是被骑的矿车（idx == m_riderCart）→ 同步清 m_riderCart。
    //   由 PlayerController.beginMining 调（左键瞄矿车 → 挖矿车，优先于破块路径，同船 hitBoatFromRay 分流模式）。
    //   返 true = 命中并结算（扣血或摧毁；caller 据此发 swingArm + 不再走破块）；false = 未命中（caller 落回破块路径）。
    bool hitCartFromRay(const QVector3D &origin, const QVector3D &dir, float maxDist,
                        World *world = nullptr, bool instantBreak = false);

    // 尝试骑乘：跑 findCartHit 命中矿车 → 设 m_riderCart + 返 true；未命中 / 命中当前骑的 → false。
    //   由 PlayerController placeBlock 矿车段调（右键瞄矿车 → 上车，优先于放矿车）。
    bool tryMount(const QVector3D &origin, const QVector3D &dir, float maxDist);

    // 下车：清 m_riderCart + 把玩家摆到矿车侧安全位（outPlayerFeet 写玩家脚底 m_pos）。
    //   由 PlayerController Shift 下车 / 切世界调。矿车保留在世界（不下车不消失）。返 false = 无骑乘（no-op）。
    bool dismount(World *world, QVector3D &outPlayerFeet);

    // 推进被骑矿车的操控物理（PlayerController.step 骑乘分支调）：wishX/wishZ = 玩家 WASD 据 yaw 算出的
    //   水平单位意图向量。沿「当前行进方向」的投影算目标速度（前进 / 后退），速度 lerp 接近 → 沿轨连接位
    //   逐格推进（advCell 向邻轨格插值移动；跨格时按行进方向选下一连接向 —— 拐角自动转弯；轨尽头停）。
    //   outCartPos 写新矿车中心位（PlayerController 据它把玩家 m_pos 同步到车座位）。
    //   无骑乘（m_riderCart<0）→ no-op。
    void tickRiddenCart(qreal dt, World *world, float wishX, float wishZ, QVector3D &outCartPos);

    // t708 ④/③ 空矿车被推后的滑行（PlayerController.step 每帧调，全模式统一推进）：扫全部未被骑的活体矿车，
    //   有速度（被 pushEmptyCart 推动 / 碰撞获速 / 下坡自然溜）→ 松手摩擦渐停 / 下坡顺坡滑 → 沿轨推进（共享
    //   stepCartAlongRail —— 出轨 / 死端自动停：无轨不前进）→ 钉轨面（坡面贴地）。静止空车不自动起步。
    //   **t735 ④ 语义变更**（覆盖 t708「空车不吃动力轨 boost」注释）：脚下是**通电**动力轨（GoldenRail +
    //   GoldenRailStateOnFlag）→ 摩擦不衰减，反被加速到 boost 档（保持前进直到离开动力段；机制等价动力轨
    //   对无骑手矿车同样供能）。静置（speed≈0）空车在动力轨上**不被弹射起步**（t708 起步闸门保留 —— 弹射
    //   语义只属被骑路径，防玩家放置的车自己跑掉）。
    //   **t736 探测轨占用统一收口在本 tick 末尾**（updateDetectorRailOccupancy）：PlayerController 骑乘 /
    //   非骑乘两分支每帧都调本 tick → 被骑 / 空车 / 停驶车全车种在同一帧重扫占用（旧版只标被骑路径，t658
    //   「占用是骑乘压轨语义」的简化由 t736 推翻 —— 机制等价 MC 1.0 detector rail 对**任何**矿车输出信号）。
    void tickPushedCarts(qreal dt, World *world);

    // t735 ③ 矿车↔矿车碰撞（PlayerController.step 每帧调，骑乘 / 非骑乘分支都调 —— 被骑的车也要能撞开
    //   前方空车）：全部活体矿车两两（O(n²)，kCap=64 上界 → ≤2016 对，每帧可承受）做水平 AABB 相交检测
    //   （简化：不旋转盒，对称半径 kCartCollideSep），重叠对做两件事 ——
    //   (a) 位置去穿插：沿「两车中心连线」把两车各推开半穿透量，但**投影到各自轨轴**（沿轨四向）位移 ——
    //       垂直于轨轴的分量不位移（防把车顶出轨道列 → pinCartY 无轨判死）；**交叉轨道（两车轨轴垂直）对
    //       整体跳过 (a)+(b) 互穿通过各走各轨**（审查修 L3：旧版驶向「停着车的道口」的车被全额反推顶停
    //       在 ~1 格外堵死线路，与本注释「互不挤位」承诺相反）；
    //   (b) 动量传递（**一维沿轨向近似**，注明简化：等质量非弹性碰撞的简化 —— 后车（中心连线方向上速度
    //       更大者）把「接近速度 × kCartMomentumTransfer」沿中心线投影到前车轨轴上加给前车 speed，自身
    //       减同量；不做能量守恒精确解，比例常量可调）。对撞（互相逼近）同公式自然得到「双双减速 / 轻微
    //       反弹」（负速 = 沿 -dir 倒行，stepCartAlongRail 已支持）。
    //   **复审 #3（2026-08-22）轨道守卫**：(a)/(b) 旧版都绕过轨道约束（轨道合法性只在 stepCartAlongRail
    //   格心重选时校验）—— 去穿插位移 / 冲量获速可把车直接送进**无轨列**，下一帧 pinCartY 返 -1 只清速度
    //   不回落 Y → 悬浮永久死车（死端追尾稳定复现）。现两路加守卫：(a) 位移越过当前格边界时先以
    //   scanRailColumnRiding（pinCartY 同语义宽容列扫描，自车当前 Y；复审 #2 起与钉轨定共享低顶净空
    //   放行 / 隔板拒）探测目标列有轨，无轨 → 钳制在当前格边界内
    //   （离轨 / 地面车本列无轨 → 位移恒 0，同 pushEmptyCart「无轨推不动」语义）；(b) 受冲量方向
    //   （A 沿 -n / B 沿 +n）无合法连接（pickTrackStep false：死端 / 离轨）→ 该车不吃冲量。
    //   任一车 speed/pos 被改 → notifyChanged（revision 触碰驱动 QML 位置刷新）。
    void resolveCartCollisions(World *world);

    // t735 ③ 行进矿车轻推玩家（实体互推的车→玩家半边；玩家→车半边 = 既有 pushEmptyCart）：扫全部活体矿车，
    //   |speed| 超阈的车与玩家 AABB（脚底 playerFeet ± playerHalfW，高 playerHeight）水平相交 → 玩家沿车
    //   行进向获轻推冲量（outPushX/Z 累加，caller 写入 m_knockback —— 复用受击击退冲量通道，防穿墙子步 /
    //   指数衰减全免费；**不做伤害**）；车每帧按 kCartBumpDrag 指数掉速（碾过玩家有阻力但不挡停 —— 机制
    //   等价矿车推着实体走）。静置车不推人（推车由 pushEmptyCart 承担）。被骑的车不推人（骑乘分支早退不
    //   调本方法）。caller 每帧重写同值 → 接触期间推力持续、接触结束即自然衰减（非逐帧无界叠加）。
    void resolvePlayerPush(World *world, const QVector3D &playerFeet, float playerHalfW, float playerHeight,
                           qreal dt, float &outPushX, float &outPushZ);

    // t708 ④ 空车被玩家推动：玩家水平 AABB（脚底 ±kPlayerHalfW）与静止空矿车 footprint 重叠 + wish 沿
    //   轨轴有分量 → 把矿车沿轨道推进（按 wish 与该轨格连接向点积最大者定朝向与速度；车无碰撞盒，
    //   推走即让出，不阻断玩家行走）。无世界 / 无输入 / 无重叠 / 已滑行的车 → no-op。返 false = 未推动。
    //   由 PlayerController.step 走路分支调（wish = 玩家世界向移动意图）。
    bool pushEmptyCart(World *world, const QVector3D &playerFeet, float wishX, float wishZ);

    // t658 探测轨占用边沿收尾（t736 起由 updateDetectorRailOccupancy 末尾调）：prev（上一帧占用快照）− 本帧
    //   占用 = 离开沿 → 清该探测轨 state bit4（DetectorRailStateOnFlag）断电（机制等价 MC 1.0 矿车离开即断；
    //   setWaterSilent 静默写 → notePowerWrite → 电力重算断开下游接收器）。prev 空 → 零开销早退。
    void updateDetectorRailEdges(World *world, const std::unordered_set<quint64> &prev);

    // t736 探测轨占用统一重扫（每帧一次，tickPushedCarts 末尾调——骑乘 / 非骑乘帧 PlayerController 都必调
    //   该 tick，占用在帧级收口而非按驱动路径分头标）：快照上一帧占用表 → 清空重建：遍历**全部活体矿车**
    //   （被骑 / 空车 / 停驶车，不再只标被骑路径——机制等价 MC 1.0 detector rail 对任何矿车输出信号），列内
    //   向下扫最近轨格为探测轨 → 幂等置 bit4（DetectorRailStateOnFlag，置过不重写）+ 记占用键；末尾
    //   updateDetectorRailEdges 清离开沿（车推走 / 被挖毁 / clearAll → 断电）。旧版在 tickRiddenCart 内标
    //   被骑车的根因缺陷：空车驶过探测轨不触发（本任务修）；且骑乘帧 tickRiddenCart 先清占用表再只标被骑
    //   车 → 邻轨空车占用每帧被误判离开沿（清位）又在下个 pass 重置 → 电力抖动——统一帧级重扫一并消除。
    void updateDetectorRailOccupancy(World *world);

signals:
    void entitiesChanged();                        // spawn / 挖毁 / 骑乘物理推进触发；驱动 count/revision + QML 绑定刷新
    void cartBroken(int x, int y, int z);          // 矿车被「挖」（攻击，生存末击）→ 呈层据它 spawnItem 掉 MinecartId 物品；t767 起创造瞬破不发（主动破坏掉落仅生存，t571①）

private:
    struct Cart {
        QVector3D pos;       // 矿车中心世界坐标（轨面上；PlayerController 据它把玩家摆车座位）
        float dirX = 0.0f;   // 行进方向 X（单位向量，轨向四向之一；拐角跨格时更新）
        float dirZ = 1.0f;   // 行进方向 Z
        float speed = 0.0f;  // 沿行进方向速度（blocks/s；W 加速 / 松键摩擦衰减 / 轨尽头停）
        float yaw = 0.0f;    // 车头朝向（度；呈现层矿车 Model eulerRotation.y）
        float pitch = 0.0f;  // t769 车身俯仰角（度；正 = 车头上扬。坡上贴合轨面，纯呈现量 —— 见 pitchAt 注释）
        int hp = 0;           // t735 ② 剩余耐久击数（spawnCart 置 kCartHitPoints；创造 instantBreak 不看它）。
                              //   非 default-member-init 常量（kCartHits 定义于类后半部，spawnCart 显式赋值）。
        bool alive = true;   // slot-reuse 槽位占用标志（放末位：聚合初始化尾字段缺省取 default member init）
    };
    std::vector<Cart> m_carts;
    int m_revision = 0;
    int m_riderCart = -1;   // 玩家当前骑的矿车索引（-1 = 未骑）
    std::vector<int> m_freeSlots; // slot-reuse：已释放可复用的槽索引（LIFO）
    int m_liveCount = 0;          // 活体矿车数
    // t658 探测轨当前占用表（本帧被矿车压住的探测轨格；键 = packRailCell 世界坐标打包）。t736 起由
    //   updateDetectorRailOccupancy（tickPushedCarts 开头）每帧快照 prev、重建 cur；用 prev − cur 找
    //   离开沿清位断电。无探测轨场景恒空（零开销）。切世界不显式清（占用表陈旧项的 blockAt 守卫自然
    //   跳过；下帧重建覆盖）。
    std::unordered_set<quint64> m_detectorOccupied;

    int acquireSlot(Cart &&c)
    {
        int slot;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_carts[size_t(slot)] = std::move(c);
        } else {
            m_carts.push_back(std::move(c));
            slot = int(m_carts.size()) - 1;
        }
        ++m_liveCount;
        return slot;
    }
    void releaseSlot(int idx)
    {
        if (idx < 0 || idx >= int(m_carts.size())) return;
        m_carts[size_t(idx)].alive = false;
        m_freeSlots.push_back(idx);
        --m_liveCount;
    }
    void notifyChanged() { ++m_revision; emit entitiesChanged(); }

    // 从矿车所在轨格出发，沿「与 (wantX,wantZ) 点积最大且 dot ≥ 0」的连接向找邻轨格位移 (outDx,outDz)。
    //   返 false = 无可用连接（轨尽头 / 仅剩反向来路连接）。拐角（dot=0）自动选中 → 矿车转弯。反向连接
    //   （dot=-1，来路）不返回：死端轨由此返 false →「轨尽头停」分支接管（防 180° 掉头 + 两端振荡）；
    //   停下后由 tickRiddenCart 停驻重选向分支按 wish 重选（面向来路输入 → 反推回程，机制等价 MC 尽头
    //   轨可反向推回）。
    bool pickTrackStep(World *world, const QVector3D &cartPos, float wantX, float wantZ,
                       int &outDx, int &outDz) const;

    // 复审 #4（2026-08-22）：列内向下扫「最近**可达**轨格」（探测轨占用专用；实现见 .cpp 头注释）：
    //   从 topY 向下最多 2 格找首个 Rail 族格，途中遇**可碰撞实心方块即断**（实心遮挡 → 更下方轨不可达
    //   —— 地面静止车隔着实心地板点亮下方探测轨隧道由此拒；轨 / 火把 / 花草 / 水（无碰撞）不遮挡）。
    //   返轨格 Y（-1 = 列内无可达轨）。只读 World。
    //   复审 #2（2026-08-23）：本严格版仅探测轨占用一处消费（隔板供电防线）；骑乘族四消费端（pinCartY /
    //   pickTrackStep / tickRiddenCart 前置钉定 / railSurfaceYAt / clampShift）改走宽容版
    //   scanRailColumnRiding（低顶净空坡道不判死），见下。
    int scanRailColumn(World *world, int cx, int topY, int cz) const;

    // 复审 #2（2026-08-23）宽容版列扫描（骑乘族专用；严格版语义 + 唯一差异：扫描顶格实心且其正下一格
    //   是轨、且「轨面 + 骑乘高」与 pos.y 一致（容差 kRideScanTol）→ 放行返回该轨）。判「实心紧贴轨上沿」
    //   = 坡顶穿越 / 低顶净空的天花板而非隔板：坡道段 pos.y = 轨Y+rise+0.45，rise>0.55 时 floor(pos.y) =
    //   轨Y+1 —— 紧凑螺旋 / 多层轨道下层坡段该格是上层地板（实心），严格断扫 → 坡上死车 / 俯仰清零 / 采样
    //   失联三症状同根因。一致性校验是承重的（af9ec8e 隔板假支撑防线全靠它）：地面车 pos.y = 地板Y +
    //   kCartGroundH，与地板下平轨骑乘高差恒 0.9375 >> 容差 → 拒；车在轨上时 pos.y 正是 pinCartY 用同一
    //   公式钉出（帧首扫描 fx 未变 → 差恰 0；步内采样 ≤ railRiseAt 全值域）→ 收。topY/fx/fz = 查询列
    //   扫描顶格与格内坐标（= 车 / 采样点 floor 与小数位），refY = 一致性基准高（车当前 pos.y）。只读 World。
    int scanRailColumnRiding(World *world, int cx, int cz, int topY, float fx, float fz, float refY) const;

    // t769 轨格内坡面高（从 pinCartY 抽出的纯查询，Y 钉定 / 俯仰采样共用）：轨格 (bcx,y,bcz) 按连接位定
    //   行进轴（mesher 同源：EW（±X 连接）读 ±X 探针、NS 读 ±Z；0 连接读 RailAxisEWFlag 轴偏好），格内
    //   横向位置 (fx,fz) 上的邻轨抬升叠加（只抬 δ>0 —— 高端平铺、低端画坡；与 PartialBlockGeometry Rail
    //   case 的 riseAtX/riseAtZ 同公式同语义、同读 railProbeDelta → 渲染坡面 / 矿车 Y / 俯仰采样三者同一张面）。
    //   复审 #3（2026-08-23）：拐角格改**四角双线性**（四角高 = mesher armLift 同式：触边两侧臂抬升之和），
    //   旧「四边均值常数」与相邻直臂格线性坡面在格边界不连续（俯仰采样窗跨界 atan2(-0.75,0.5)≈-56° 车头
    //   瞬甩 + Y 钉面跳降 0.75）。V 形凹谷（t710 两端皆 +1）取 2|轴-0.5|；十字无坡。只读 World。
    static float railRiseAt(World *world, int bcx, int y, int bcz, float fx, float fz);

    // t769 轨道面高度采样（俯仰角计算用）：世界坐标 (sx,sz) 所在列自 topY 向下扫最近可达轨格
    //   （scanRailColumnRiding 宽容语义，refY = 车当前 pos.y 作一致性基准 —— 采样点距车心 ≤0.25，其轨面
    //   与车钉定面同源连续，容差窗内）→ 轨格 Y + 格内坡面高（railRiseAt）。列内无可达轨 → 返 false。
    //   caller 传的 topY 应为车所在轨层 +1（采样列与车列至多相邻 → 轨层差 ∈ [-1,+1]，扫描窗恰覆盖）。
    bool railSurfaceYAt(World *world, float sx, float sz, int topY, float refY, float &outY) const;

    // t769 车身俯仰刷新（纯呈现）：以车心为基准、沿车头向 ±kCartPitchProbe 两点采样轨面高（railSurfaceYAt）
    //   → pitch = atan2(前-后, 2·probe)。railY = 车所在列轨层（pinCartY 返回值 / 被骑停驻帧的前置钉定 railY）。
    //   详见 .cpp 实现处头注释（采样窗语义 / 跨段过渡 / 符号约定）。
    void updateCartPitch(Cart &c, World *world, int railY);

    // t708 沿轨推进（共享：被骑 / 空车被推同一物理）：把矿车沿当前行进 dir 推进 speed×dt（支持负速倒行
    //   —— S 反向推力的减速 → 负速退行），正行跨格时重选连接向（拐角自动转弯；轨尽头 / 出轨停）；
    //   倒行跨格同走一张连接表（方向向量取反后查表），**到心重选结果双符号持久化**（t770 ①：正行 dir=
    //   新臂 / 倒行 dir=新臂取反 —— 旧版倒行不写回 dir，下一帧按旧轴横切出轨）；跨格前先验目的格列内
    //   确有轨（防倒退出轨落空悬停）。无轨列 → 清零停。
    //   t734 段终点重写 = 行进向上前方最近的格心（旧「当前格心+行进向」段长 >0.5，在 16ms tick 步长
    //   ~0.06-0.21 下跨格分支永不触发 = 连接重选/拐弯/尽头停全失效 → 直线冲出轨道；新取法每过一格心
    //   必重选，帧率无关）+ 格心起步先验（死端/离轨零位移即停）。
    //   t770 ② 弯道格内强制贴轨约束：每子步把行进向的垂直轴向所在格中心线（floor+0.5）收敛 —— 停驻重选向 /
    //   被推起步等非到心时刻的方向重选不再让车带横向偏移驶出中心线（转向与速度/方向无关，位置几何连续；
    //   正常行驶恒在 .5 上 → no-op）。复审 #23：收敛由「一次钉回」改限速渐进（每 tick ≤kCartCenterSnapPerTick）
    //   —— 段中重选向不再一次性横移 ~0.5 格（被骑时玩家视点同步跳）。
    void stepCartAlongRail(Cart &c, World *world, float dt);

    // t708 钉轨面（共享：被骑 tickRiddenCart / 空车 tickPushedCarts 同一 Y 钉定）：把矿车 Y 钉到所在列向下
    //   扫到的最近轨格 cell 顶 + 坡面高 + kCartRideH。坡面高 = cell 内横向位置上的邻轨抬升叠加（只抬 δ>0 ——
    //   高端平铺、低端画坡；与 PartialBlockGeometry Rail case 的 riseAtX/riseAtZ 同公式同语义、同读
    //   railProbeDelta → 渲染坡面与矿车高度严格一致）。**rise 只叠本轴**（t691：mesher 对直轨只读行进轴的
    //   riseAtX（EW）或 riseAtZ（NS）；轴取法 = 连接位（cpx||cnx → X 轴；0 连接读 RailAxisEWFlag）。拐角 /
    //   十字无坡。返钉到的轨格 Y（-1 = 列内无轨 —— caller 走防御 / 停）。t680 ①：探测轨判定读此返回 Y
    //   （防 floor(pos.y)-1 在坡顶 rise≥0.7 时错层读空气）。
    int pinCartY(Cart &c, World *world);

    static constexpr int kCap = 64;             // 矿车数上限（防溢出；同船 cap 量级）
    // 矿车几何 / 物理常量（机制等价 MC 1.0 minecart；手感可玩）：
    //   矿车斗形外观 ~0.9×0.5×1.0（长轴沿行进方向 Z）；碰撞盒半宽 0.45 / 半长 0.5 / 半高 0.45。
    static constexpr float kCartHalfW  = 0.45f;  // 矿车 footprint 半宽（X；footprint 宽 0.9，匹配车斗）
    static constexpr float kCartHalfL  = 0.50f;  // 矿车 footprint 半长（Z；footprint 长 1.0）
    static constexpr float kCartHalfH  = 0.45f;  // 矿车命中盒半高（0.9 高，含车帮）
    // t734 贴轨修真 / t768 车斗加高重推：矿车中心距**轨格 cell 底**的骑乘高度。轨面真基准 = 轨格 cell 底
    //   + 薄板厚 1/16（PartialBlockGeometry Rail case 的 yr=1/16 常量，板贴 cell 底防 z-fight）。t768 车斗
    //   0.75 高模型（本地 ±0.375；旧 0.3 高扁平车观感 ~0.25 格）→ 中心 = 板顶 1/16 + 底板下沿偏移 0.375 +
    //   0.0125 微隙（防底板与轨板共面 z-fight）= 0.45（t734 旧值 0.225 = 旧底板 −0.15 时代推导）。偏移远离
    //   整数格边界 → floor(pos.y) 定格列无 ULP 取整风险（lessons「resting 复探 FP 边界」条）；坡中段
    //   floor(pos.y) 提前跨 cell 由 scanRailColumn 向下 2 格窗兜住（与旧值同语义）。骑乘脚底 kCartSeatDrop
    //   （playercontroller 同值命名常量 = 底板面 −0.3125）与 Main.qml 底板 piece 三层同值推导，改须同步。
    static constexpr float kCartRideH  = 0.45f;
    // t734 非轨格（地面）放置的静止车：车底贴 cell 底（t768：0.375 底板下沿偏移 + 0.0125 微隙，无轨薄板层）。
    //   放宽放置（可放地上但推不动）后 spawnCart 非轨模式用；离轨静止由推进侧无轨守卫保证。
    static constexpr float kCartGroundH = 0.3875f;
    // 复审 #2（2026-08-23）宽容列扫描一致性容差（格）：scanRailColumnRiding 判「实心紧贴轨上沿」时，
    //   |pos.y − (轨Y + railRiseAt + kCartRideH)| ≤ 本值才放行。取 railRiseAt 全值域 [0,1]（V 谷 / 步内
    //   stale 钉定跨半格坡差的最坏采样）；地面车对地板下轨（隔板在下 1 格、轨在下 2 格）的差 ≥ 1.9375 −
    //   rise ∈ [0.9375, 1.9375] 恒被拒（af9ec8e 隔板假支撑防线，最坏 = 地板下恰为坡顶 rise=1 段）。
    static constexpr float kRideScanTol = 0.5f;
    // 轨上矿车速度（blocks/s）：明显快于步行 4.3（机制等价 MC 1.0 矿车轨上 8 blocks/s）。
    static constexpr float kCartSpeed  = 8.0f;
    // t638 ⑤ 动力轨（GoldenRail）boost 档（blocks/s）：矿车驶上动力轨时的目标速度上限（kCartSpeed 的
    //   1.6×；机制等价 MC 1.0 powered rail 加速——有输入上限提升、无输入弹射 0.35 档向前）。
    static constexpr float kCartBoostSpeed = 12.8f;
    // t667 坡道重力（机制等价 MC 1.0 矿车下坡自加速 / 上坡减速）：
    //   下坡滑行档（blocks/s）：无输入也往这个目标速度溜（略高于平道巡航 8；动力轨 boost 12.8 仍更高）。
    static constexpr float kCartSlopeDownSpeed = 10.0f;
    //   上坡目标速度乘子（<1）：爬上坡时目标速度收窄到该比例（须玩家输入推力才能爬；无输入退化为停）。
    static constexpr float kCartUphillMul = 0.6f;
    // t708 ④ 玩家推动空车的初始速度（blocks/s）：走路撞上静止空车 → 沿轨以该速推走（≈步行速 4.3 同级，
    //   friction 渐停；下坡顺坡溜）。MC 1.0 空车被推速≈行走速量级。
    static constexpr float kCartPushSpeed = 4.0f;
    // t708 ④ 推车判定重叠半径：玩家脚底中心与静止空矿车中心的水平距离 ≤ 0.8 视为「贴住可推」
    //   （≈ 玩家碰撞盒半宽 0.3 + 矿车 footprint 半宽 0.45 + 0.05 容差；同量级玩家站立占格半径 0.5）。
    static constexpr float kCartPushReach = 0.8f;
    // t735 ② 生存矿车耐久击数（可承受的攻击次数；第 kCartHitPoints 击摧毁+掉落）。机制注：MC 1.0 矿车
    //   本是一击即毁，此为用户明确要求的多击耐久语义（可调：改小=更快毁）。创造模式不看它（单击即毁）。
    static constexpr int kCartHitPoints = 3;
    // t735 ③ 车-车碰撞分离距离（格）：两车中心水平距离小于它视为碰撞重叠（AABB 简化：对称半径取
    //   max(kCartHalfW,kCartHalfL)=0.5 ×2，再留 2% 收缩容差防贴轨停驻的两车永久微抖）。
    static constexpr float kCartCollideSep = 0.98f;
    // t735 ③④ 动量传递比（0..1）：碰撞时「接近速度 × 该比」沿中心线传给前车（等质量非弹性碰撞的简化；
    //   1.0=完全非弹性贴走，0=完全弹性穿透不传。0.85 取「后车明显减速、前车吃到大部分速度」的 MC 观感）。
    static constexpr float kCartMomentumTransfer = 0.85f;
    // t735 ③ 行进矿车轻推玩家的冲量强度（blocks/s，写入 m_knockback 通道）。轻推 = 明显小于受击击退
    //   6.0 / 衰减率同 kHitKnockbackDrag → 总位移 ≈ 2.0/4.5 ≈ 0.44 格（推开让位，不弹飞）。
    static constexpr float kCartBumpSpeed = 2.0f;
    // t735 ③ 矿车碾过玩家的掉速率（1/s，指数衰减）：接触期间车速按此衰减（有阻力但不挡停 —— 机制等价
    //   矿车推着实体前进，推开后恢复动力轨 / 重力供能）。
    static constexpr float kCartBumpDrag = 2.0f;
    // 速度 lerp 接近率（1/s；动量感：松键后滑行一段渐停）。
    static constexpr float kCartAccel  = 3.0f;
    // t769 俯仰采样半窗（格）：沿车头向 ±0.25 两点采样轨面高差（≈ 车轮距 —— 斗长 1.0 的半长减帮厚）。
    //   坡中段窗全落坡格 → 1:1 坡恰 45°；跨段折缝（平↔坡）窗横跨两段 → 线性过渡（过渡带 ~0.5 格，与车速 /
    //   帧率无关 —— 轨面高分段线性且连续 → 俯仰随位置连续，无需时间平滑）。
    static constexpr float kCartPitchProbe = 0.25f;
    // 复审 #3（2026-08-23）俯仰钳制（度）：真实轨面坡度上界 = 1:1 坡的 ±45°（V 谷段内局部坡 2:1 但 ±0.25
    //   两点采样窗对称收窄后 ≤~39°；拐角双线性后窗内高差 ≤0.5 → 恰 45°）。旧版无钳：采样窗落在病态几何
    //   （拐角均值常数跨边界 / 探针跨界断层）时冒出 ±56° 以上幻象俯仰 → 车头猛甩。纯呈现护栏（不改判据）。
    static constexpr float kCartPitchMaxDeg = 45.0f;
    // 复审 #23（2026-08-23）贴轨收敛限速（格/tick）：stepCartAlongRail 每子步把行进垂直轴钉向格心线时，
    //   单 tick 收敛量上限（正常行驶恒在 .5 上 → no-op；段中重选向的横向偏移 ≤0.5 → ~5 tick 渐进钉回，
    //   替旧「一次钉回」的 ~0.5 格瞬时横移 —— 被骑时玩家视点同步跳的根因）。取舍见 .cpp 实现处注释。
    static constexpr float kCartCenterSnapPerTick = 0.1f;
    // 空车 / 松键摩擦衰减率（1/s）。
    static constexpr float kCartFriction = 2.0f;
};

#endif // MINECARTMANAGER_H
