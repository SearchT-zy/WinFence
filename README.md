# WinFence

开源的 Windows 10/11 桌面图标栅栏（Fences）——把桌面图标分组收纳进可拖拽、可折叠的
亚克力毛玻璃圆角容器。对标 Stardock Fences 的**基础能力**（MVP），不移动你的任何真实文件。

![status](https://img.shields.io/badge/status-design%20%2F%20skeleton-orange) ![license](https://img.shields.io/badge/license-MIT-blue)

## 特性（MVP 范围）

- ✅ 多个透明栅栏容器：圆角 / Win11 亚克力 / 透明度可调（Direct2D 渲染）
- ✅ 拖拽移动栅栏；从桌面/资源管理器拖图标**虚收录纳**（不移动真实文件）
- ✅ 双击标题栏折叠/展开
- ✅ 布局 JSON 持久化，启动恢复
- ✅ 中文设置弹窗
- ✅ 桌面文件增删改实时同步（新增/删除/重命名自动对账）
- 🚧 迭代二：AI 智能分组（DeepSeek API / 本地 Ollama，仅手动触发）

### 安全设计（为什么不"动"你的文件）

WinFence **不注入、不 Hook Explorer**，只使用 Windows 公开 API；归属关系保存在
WinFence 自己的 JSON 配置里，你的桌面文件原封不动。详见[设计文档](docs/DESIGN.md)。

## 构建

要求：Visual Studio 2022 + Windows 10 SDK (10.0.22621+) + CMake ≥ 3.24。

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# 产物：build\Release\WinFence.exe（静态 CRT，绿色单文件）
```

## 文档

- [docs/DESIGN.md](docs/DESIGN.md) — 完整设计：架构 / 数据结构 / 核心流程 / Windows 坑点清单
- [docs/CREDITS.md](docs/CREDITS.md) — 参考项目与许可证纪律

## 当前状态

**MVP 核心已实现并实测通过**（M1~M3）：

- ✅ 亚克力圆角栅栏窗口（DComp+D2D，Win+D 可见、应用窗口之下、桌面图标之上）
- ✅ 拖动/缩放/双击折叠；标题栏右键：新建/删除栅栏/退出
- ✅ 图标渲染入栏（真实桌面文件图标 + 中文文件名）+ 双击打开 + 右键移除
- ✅ 从 Explorer/桌面拖入收纳（OLE，经 PathGuard 安全闸门，不动真实文件）
- ✅ JSON 布局持久化（原子写 + .bak 轮换，显示器归一化 DIP 坐标）
- ✅ 桌面文件实时同步（新增/删除/重命名自动对账，重命名归属迁移，orphan 7 天保留）
- ✅ 托盘菜单 + 中文设置弹窗（背景效果/透明度/圆角）
- ✅ M4：内容区滚轮滚动 + 滚动条、折叠/展开动画、应用图标、WM_CLOSE 健壮性、
  GitHub Actions CI（含 AV 红线禁词审查）

待办：真实拖拽手势的人工体验打磨、命名空间项（回收站）入栏（post-MVP，见 DESIGN.md §4.10）。
实现纪要见 `docs/DESIGN.md`；构建产物 `build\Release\WinFence.exe`。

## License

MIT — 见 [LICENSE](LICENSE)。第三方依赖（nlohmann/json，MIT）与参考项目见
[docs/CREDITS.md](docs/CREDITS.md)。
