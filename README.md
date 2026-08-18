# WinFence

**[简体中文](README.md) | [English](README.en.md)**

---

开源的 Windows 10/11 桌面图标栅栏（Fences）+ macOS 式 Dock 栏——把桌面图标分组收纳进可拖拽、可折叠的亚克力毛玻璃圆角容器，或在屏幕底部黑色半透明 Dock 中悬停放大、单击启动。对标 Stardock Fences 的基础能力，**不移动你的任何真实文件**。

![status](https://img.shields.io/badge/status-v0.1.0%20M6-blue) ![license](https://img.shields.io/badge/license-MIT-green) ![c++](https://img.shields.io/badge/C%2B%2B-Win32%20%2F%20D2D%20%2F%20DComp-orange)

## 功能特性

### 栅栏容器（Fences）
- ✅ 多个亚克力圆角半透明容器（Win11 `DWMSBT` 毛玻璃，旧系统自动回退），位于桌面图标之上、应用窗口之下
- ✅ 拖动 / 边缘缩放 / 双击折叠（150ms 平滑动画）/ 内容区滚动
- ✅ 从桌面或资源管理器**拖入收纳**（虚拟归属，真实文件原地不动）
- ✅ 图标双击打开、右键移除；栅栏间互拖、栏内拖动排序、拖拽高亮
- ✅ 栅栏重命名（标题栏内联编辑）；悬停高亮
- ✅ 256px 高清图标（`SHIL_JUMBO`），中文文件名完整支持

### Dock 栏（macOS 式）
- ✅ 屏幕底部黑色半透明圆角条，常驻顶层
- ✅ 悬停图标放大 + 名称气泡；单击启动
- ✅ 与栅栏双向拖拽、栏内排序；可随需隐藏

### 系统集成
- ✅ 布局 JSON 持久化（原子写 + `.bak` 轮换，显示器归一化坐标，多 DPI/多显示器安全）
- ✅ 桌面文件实时同步：新增/删除/重命名自动对账（重命名归属迁移、orphan 7 天保留）
- ✅ 托盘菜单 + 中文设置弹窗（背景效果/透明度/圆角/开关 Dock/隐藏桌面图标/开机自启）
- 🚧 迭代计划：AI 智能分组（DeepSeek API / 本地 Ollama，仅手动触发，只读文件名）

### 安全设计（为什么不"动"你的文件）

WinFence **不注入、不 Hook Explorer**，只使用 Windows 公开 API（CI 设有高危 API 禁词审查）；
图标归属关系只保存在 WinFence 自己的 JSON 配置里，代码中不存在删除/移动桌面文件的调用。
详见[设计文档](docs/DESIGN.md)。

## 构建与运行

要求：Visual Studio 2022 + Windows 10 SDK (10.0.22621+) + CMake ≥ 3.24。

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# 产物：build\Release\WinFence.exe（静态 CRT，绿色单文件）
```

或直接从 GitHub Actions 的构建产物下载（Artifacts）。

## 文档

- [docs/DESIGN.md](docs/DESIGN.md) — 完整设计：架构 / 数据结构 / 核心流程 / Windows 坑点清单（中文）
- [docs/CREDITS.md](docs/CREDITS.md) — 参考项目与许可证纪律

## License

MIT — 见 [LICENSE](LICENSE)。第三方依赖（nlohmann/json，MIT）与参考项目见
[docs/CREDITS.md](docs/CREDITS.md)。
