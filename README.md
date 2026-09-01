# QtVoxelSandbox

一个用 **Qt 6 / C++ / Qt Quick 3D (QRhi)** 从零实现的体素沙盒游戏，玩法致敬《Minecraft》经典时代
（挖掘、建造、合成、附魔、红石、矿车、生物……全部代码与美术资源均为原创）。

A voxel sandbox game built from scratch with Qt 6 / C++ / Qt Quick 3D —
inspired by classic-era Minecraft. All code and assets are original.

> 本项目为独立爱好项目，与 Mojang Studios 或 Microsoft 无任何关联。
> "Minecraft" 是 Mojang Studios 的商标，此处仅作玩法描述性引用。
> This project is not affiliated with Mojang or Microsoft.
> Minecraft is a trademark of Mojang Studios, used here for description only.

## 特性

- **体素世界**：Perlin 噪声地形、区块按需生成、昼夜循环与光照重烘焙；每个世界独立 SQLite 存档
- **三模式**：生存 / 创造 / 观察者
- **挖掘与建造**：上百种方块与物品，贴图全部由 `tools/build_*.py` 程序化生成（零外部美术）
- **物品系统**：合成、铁砧（修复 / 合并 / 改名 / 冲突检测）、附魔台与 20 条附魔（含弓 / 钓竿专属词缀）
- **红石**：红石粉、火把（含熔断）、红石块、动力轨链（8 轨链式供能、连接位拓扑）
- **矿车与铁轨**：坡道物理、多车挤压、载人驾驶、脱轨重挂
- **生物**：十余种生物 AI——驯服（狼 / 豹猫）、装备拾取穿戴、幼年体、骑士组合、白天避光等
- **音效**：由 CC0 音源（`tools/cc0_audio/`）经 `tools/build_sounds.py` 程序化合成
- **工程化**：397 项无头行为探针全绿（`tools/redstone_matrix_test.cpp`）、构建版本戳、零警告构建

## 构建

依赖：**Qt 6.11.1+**（Core / Gui / Quick / Qml / Quick3D / Sql）、CMake ≥ 3.16、C++20 编译器。
重新生成美术 / 音频资源时另需 Python 3（仓库已含生成产物，日常构建不需要）。

```bash
cmake -B build -DCMAKE_PREFIX_PATH=<你的 Qt 安装路径>
cmake --build build
./build/voxelsandbox   # Windows 下为 voxelsandbox.exe
```

## 项目结构

```
src/        游戏源码（Core / Game / World / Entities / Renderer / ui）
tools/      资源生成脚本（贴图 build_*.py、音频 build_sounds.py）与测试矩阵
textures/   程序化生成的贴图产物
sounds/     合成音频产物
docs/       开发日志与审查记录（中文）
```

## 许可

代码与仓库内全部生成资源均以 **[MIT License](LICENSE)** 发布。
CC0 音源出处记录见 [tools/cc0_audio/SOURCES.md](tools/cc0_audio/SOURCES.md)。

## 致谢

玩法设计致敬 Minecraft（Mojang Studios）。感谢 Qt Project 提供的框架与渲染基础设施。
