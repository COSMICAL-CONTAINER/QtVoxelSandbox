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
    //   instantBreak=true（caller 创造模式传）→ 直接摧毁；否则 hp>1 时扣 1 血（不掉不毁，呈层 hpAt 绑定
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
    //   语义只属被骑路径，防玩家放置的车自己跑掉）。探测轨道标仍只属被骑路径（占用是「骑乘压轨」语义）。
    void tickPushedCarts(qreal dt, World *world);

    // t735 ③ 矿车↔矿车碰撞（PlayerController.step 每帧调，骑乘 / 非骑乘分支都调 —— 被骑的车也要能撞开
    //   前方空车）：全部活体矿车两两（O(n²)，kCap=64 上界 → ≤2016 对，每帧可承受）做水平 AABB 相交检测
    //   （简化：不旋转盒，对称半径 kCartCollideSep），重叠对做两件事 ——
    //   (a) 位置去穿插：沿「两车中心连线」把两车各推开半穿透量，但**投影到各自轨轴**（沿轨四向）位移 ——
    //       垂直于轨轴的分量不位移（防把车顶出轨道列 → pinCartY 无轨判死），交叉轨道两车互不挤位只交换冲量；
    //   (b) 动量传递（**一维沿轨向近似**，注明简化：等质量非弹性碰撞的简化 —— 后车（中心连线方向上速度
    //       更大者）把「接近速度 × kCartMomentumTransfer」沿中心线投影到前车轨轴上加给前车 speed，自身
    //       减同量；不做能量守恒精确解，比例常量可调）。对撞（互相逼近）同公式自然得到「双双减速 / 轻微
    //       反弹」（负速 = 沿 -dir 倒行，stepCartAlongRail 已支持）。
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

    // t658 探测轨占用边沿收尾（tickRiddenCart 末尾调）：prev（上一帧占用快照）− 本帧占用 = 离开沿 →
    //   清该探测轨 state bit4（DetectorRailStateOnFlag）断电（机制等价 MC 1.0 矿车离开即断；setWaterSilent
    //   静默写 → notePowerWrite → 电力重算断开下游接收器）。prev 空 → 零开销早退。
    void updateDetectorRailEdges(World *world, const std::unordered_set<quint64> &prev);

signals:
    void entitiesChanged();                        // spawn / 挖毁 / 骑乘物理推进触发；驱动 count/revision + QML 绑定刷新
    void cartBroken(int x, int y, int z);          // 矿车被「挖」（攻击）→ 呈层据它 spawnItem 掉 MinecartId 物品

private:
    struct Cart {
        QVector3D pos;       // 矿车中心世界坐标（轨面上；PlayerController 据它把玩家摆车座位）
        float dirX = 0.0f;   // 行进方向 X（单位向量，轨向四向之一；拐角跨格时更新）
        float dirZ = 1.0f;   // 行进方向 Z
        float speed = 0.0f;  // 沿行进方向速度（blocks/s；W 加速 / 松键摩擦衰减 / 轨尽头停）
        float yaw = 0.0f;    // 车头朝向（度；呈现层矿车 Model eulerRotation.y）
        int hp = 0;           // t735 ② 剩余耐久击数（spawnCart 置 kCartHitPoints；创造 instantBreak 不看它）。
                              //   非 default-member-init 常量（kCartHits 定义于类后半部，spawnCart 显式赋值）。
        bool alive = true;   // slot-reuse 槽位占用标志（放末位：聚合初始化尾字段缺省取 default member init）
    };
    std::vector<Cart> m_carts;
    int m_revision = 0;
    int m_riderCart = -1;   // 玩家当前骑的矿车索引（-1 = 未骑）
    std::vector<int> m_freeSlots; // slot-reuse：已释放可复用的槽索引（LIFO）
    int m_liveCount = 0;          // 活体矿车数
    // t658 探测轨当前占用表（本帧被骑矿车压住的探测轨格；键 = packRailCell 世界坐标打包）。tickRiddenCart
    //   开头快照为 prev、本帧重建；updateDetectorRailEdges 用 prev − cur 找离开沿清位断电。
    //   无探测轨场景恒空（零开销）。切世界不显式清（占用表陈旧项的 blockAt 守卫自然跳过；下帧重建覆盖）。
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

    // t708 沿轨推进（共享：被骑 / 空车被推同一物理）：把矿车沿当前行进 dir 推进 speed×dt（支持负速倒行
    //   —— S 反向推力的减速 → 负速退行），正行跨格时重选连接向（拐角自动转弯；轨尽头 / 出轨停）；
    //   倒行不翻转 dir（车头保持朝向），跨格前先验目的格列内确有轨（防倒退出轨落空悬停）。无轨列 → 清零停。
    //   t734 段终点重写 = 行进向上前方最近的格心（旧「当前格心+行进向」段长 >0.5，在 16ms tick 步长
    //   ~0.06-0.21 下跨格分支永不触发 = 连接重选/拐弯/尽头停全失效 → 直线冲出轨道；新取法每过一格心
    //   必重选，帧率无关）+ 格心起步先验（死端/离轨零位移即停）。
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
    // t734 贴轨修真：矿车中心距**轨格 cell 底**的骑乘高度。轨面真基准 = 轨格 cell 底 + 薄板厚 1/16
    //   （PartialBlockGeometry Rail case 的 yr=1/16 常量，板贴 cell 底防 z-fight）——旧版 kCartRideH=0.30
    //   配「+1.0 格」（cell 顶）把「格底薄板」误当「格顶」→ 矿车悬浮约一整格（primed TNT / 雪傀儡 restY
    //   基准算错同族 bug：渲染面在格底、物理却从格顶叠）。车底 = 渲染 delegate 底板下沿（Main.qml cartHost
    //   底板 piece：position.y=-0.12、scale.y=0.06 → 底 = 中心 −0.15）→ 中心 = 板顶 1/16 + 0.15 + 0.0125
    //   微隙（防底板与轨板共面 z-fight）= 0.225。偏移远离整数格边界 → floor(pos.y) 定格列无 ULP 取整风险
    //   （lessons「resting 复探 FP 边界」条）。
    static constexpr float kCartRideH  = 0.225f;
    // t734 非轨格（地面）放置的静止车：车底贴 cell 底（0.15 底板偏移 + 0.0125 微隙，无轨薄板层）。
    //   放宽放置（可放地上但推不动）后 spawnCart 非轨模式用；离轨静止由推进侧无轨守卫保证。
    static constexpr float kCartGroundH = 0.1625f;
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
    // 空车 / 松键摩擦衰减率（1/s）。
    static constexpr float kCartFriction = 2.0f;
};

#endif // MINECARTMANAGER_H
