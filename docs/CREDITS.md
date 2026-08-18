# CREDITS — 参考项目与许可证

WinFence 在设计与实现过程中参考了以下开源项目。许可证纪律见文末。

## 直接同类（产品/交互/数据模型参考）

| 项目 | 许可证 | 参考内容 |
|---|---|---|
| [Twometer/NoFences](https://github.com/Twometer/NoFences) | MIT | 品类祖师爷；透明可拖容器 + 标题栏 + 图标拖入的交互最小集；数据模型 |
| [Damianttje/Fenceless](https://github.com/Damianttje/Fenceless) | MIT | NoFences 增强 fork；JSON 持久化、启动恢复备份、防抖自动保存、托盘菜单、日志 |
| [kelvinkbk/OpenFences](https://github.com/kelvinkbk/OpenFences) | 开源 | 折叠（roll-up）、多显示器位置记忆交互 |
| [Xstoudi/Palisades](https://github.com/xstoudi/palisades) / [Walkoud/Palisades](https://github.com/Walkoud/Palisades) | MIT | 容器重命名/重着色/透明度交互 |
| [DaveDebugs/Fluid.Fences](https://github.com/DaveDebugs/Fluid.Fences) | GPL-3.0 | Folder Portal（栅栏镜像真实文件夹）与多显示器快照——迭代方向参考 |

## 技术模块参考（同栈 C++/Win32）

| WinFence 模块 | 参考项目 | 许可证 | 参考内容 |
|---|---|---|---|
| shell/DesktopAnchor | [rainmeter/rainmeter](https://github.com/rainmeter/rainmeter) | GPL-2.0 | "On Desktop" ZPosition：Progman/WorkerW/SHELLDLL_DefView 检测、TaskbarCreated 重锚、SWP_NOACTIVATE 纪律 |
| ui/Compositor | [microsoft/Windows-classic-samples](https://github.com/microsoft/Windows-classic-samples) | MIT | DirectComposition + D2D 透明窗口官方示例、OLE DragDrop 示例 |
| platform/DwmBackdrop | [TranslucentTB/TranslucentTB](https://github.com/TranslucentTB/TranslucentTB) | GPL-3.0 | 未文档化 accent API 的防御性封装（探测/降级/版本分支）；小型 Win32 应用的 CMake + CI 组织 |
| platform/DwmBackdrop | [MicaForEveryone/MicaForEveryone](https://github.com/MicaForEveryone/MicaForEveryone) | MIT | DWMWA_SYSTEMBACKDROP_TYPE 各取值在真实 Win32 窗口上的效果矩阵 |
| 工程整体 | [microsoft/PowerToys](https://github.com/microsoft/PowerToys) | MIT | FancyZones 模块的 PMv2 DPI 处理、多显示器坐标系、单实例、托盘、GitHub Actions 发布 |
| shell/ShellEnumerator | [derceg/explorerplusplus](https://github.com/derceg/explorerplusplus) | GPL-3.0 | PIDL 生命周期管理、IShellFolder::EnumObjects、显示名/解析名双轨、系统图像列表缓存 |
| core/DesktopWatcher | [SpartanJ/efsw](https://github.com/SpartanJ/efsw) | MIT | ReadDirectoryChangesW + IOCP 文件监控；可作为依赖引入或对照实现 |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | MIT | vendored 于 `third_party/nlohmann/json.hpp`（v3.11.3） |

## 第三方依赖

| 依赖 | 许可证 | 引入方式 |
|---|---|---|
| nlohmann/json v3.11.3 | MIT | 单头文件 vendored（`third_party/`），保留原始版权声明 |

## 许可证纪律（contributors 必读）

本项目采用 **MIT** 许可证。因此：

- **MIT 系**（NoFences、Fenceless、PowerToys、MicaForEveryone、efsw、nlohmann/json）：
  代码**可以**直接搬运/改写，但必须保留其原版权声明，并在本文件登记。
- **GPL 系**（Rainmeter、TranslucentTB、explorerplusplus、lively、Fluid.Fences）：
  **只允许阅读理解思路后自行重写**，任何一行代码不得进入本仓库（包括"照着打一遍"）。
  提交前请自查 diff 与所读 GPL 源码的相似度。
- 新增参考项目时，请同步更新本文件。

## 反面参考（禁止学习的路线）

- **ExplorerPatcher 类**（注入/补丁 Explorer）——违反本项目硬约束 1。
- **桌面图标位置保存/恢复类工具**（DesktopOK 等，跨进程读写 SysListView32 内存）
  ——违反硬约束 1，且是杀毒软件误报重灾区。
