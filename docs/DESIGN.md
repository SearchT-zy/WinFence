# WinFence MVP 设计方案（v0.1）

> 对标 Fences 6 基础能力的开源桌面图标栅栏。
> 技术栈：C++ / Win32 API / Direct2D / DirectComposition / DWM。不使用 WPF、Qt。
> 硬约束：不 Hook Explorer、不移动磁盘文件、虚拟归属管理、仅公开官方 API。

---

## 0. 关键架构决策

| 决策点 | 选择 | 理由与备选 |
|---|---|---|
| 栅栏窗口层级 | Z 序锚定在桌面图标层（SHELLDLL_DefView）之上、普通应用窗口之下 | 复刻 Fences 手感：Win+D 可见、不遮挡应用。非 `WS_EX_TOPMOST`（会和正常窗口打架）。`FindWindow`/`SetWindowPos` 均为公开 API，不注入不 Hook。备选：设置项提供"浮动置顶模式" |
| 窗口与渲染模型 | 每个栅栏一个 HWND，`WS_EX_NOREDIRECTIONBITMAP` + DirectComposition + D2D 交换链（预乘 Alpha） | HWND 交换链无法做每像素透明；`UpdateLayeredWindow` 过时且与 D2D 不友好。每栏一个窗口天然支持独立拖动/Z 序，MVP 数量 ≤ 20 无性能问题 |
| 亚克力实现 | 首选 `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW`（Win11 22H2+），回退为纯半透明 + 噪点 | 官方文档化 API。未文档化的 `SetWindowCompositionAttribute(ACCENT_ENABLE_ACRYLICBLURBEHIND)` 只作为编译期开关 `WINFENCE_ENABLE_UNDOCUMENTED_ACCENT`（默认 OFF），因为它有 AV 误报与版本兼容风险 |
| 桌面枚举方式 | `IShellFolder::EnumObjects(CSIDL_DESKTOP)` 而非 `FindFirstFile` | 与 Explorer 实际显示一致；能拿到回收站、"此电脑"等命名空间项的显示名；同时合并 `FOLDERID_Desktop`（用户）与 `FOLDERID_PublicDesktop`（公共）两个物理目录 |
| 图标归属主键 | 桌面绝对路径（规范化、大小写不敏感）+ 64 位自增 UID | 路径做匹配键（文件事件只给路径），UID 做容器内稳定引用。重命名用「删除+新增配对 + FILE_ID 校验」启发式 |
| 文件监控 | `ReadDirectoryChangesW`（OVERLAPPED + IOCP），同时监控用户桌面和公共桌面两个目录 | 全公开 API，无需管理员，无需 USN Journal（USN 需要卷句柄、易触发 AV）。备选：直接引入 MIT 库 efsw（见 §8） |
| 拖拽入栏 | 栅栏窗口实现 OLE `IDropTarget`，只读接收 `CF_HDROP` | 我们不执行任何文件操作；Explorer 拖出方自行负责 |
| 拖拽出栏 | MVP 仅支持"拖到其他栅栏 / 拖到未分组区 / 右键移除"，**不做 OLE 拖出到 Shell** | 安全约束：如果把虚拟图标以 `CF_HDROP` 拖给 Explorer 桌面，Explorer 会发起真实移动/替换对话框，存在覆盖同名文件风险。列为 P2 再评估 |
| JSON 库 | nlohmann/json 单头文件 vendored 进 `third_party/` | 免联网拉依赖（国内 CI/用户友好），`FetchContent` 作为备选开关 |
| 字符串与编码 | 进程内全部 `std::wstring`（UTF-16）+ 只用 W 系列 API；JSON 落盘 UTF-8 | 中文路径唯一安全做法，详见 §4.3 |

---

## 1. 项目整体架构与模块拆分

### 1.1 分层图

```
┌────────────────────────────────────────────────────────────────┐
│  app        入口/生命周期：消息循环、单实例、DPI 初始化、优雅退出     │
├────────────────────────────────────────────────────────────────┤
│  ui         表现层：FenceWindow(HWND)、D2D 渲染、交互、设置弹窗     │
├────────────────────────────────────────────────────────────────┤
│  core       领域层：栅栏模型、桌面快照、文件事件协调、图标缓存        │
├────────────────────────────────────────────────────────────────┤
│  shell      系统互操作：桌面层锚定、Shell 枚举、显示器/DPI 工具      │
│  persist    持久化：JSON 原子读写、schema 校验、版本迁移、备份轮换   │
│  platform   DWM/DComp/OLE 封装、路径安全校验、通用工具              │
│  ai (预留)   迭代二：AI 虚拟分组，仅在用户点击按钮时触发              │
├────────────────────────────────────────────────────────────────┤
│  Win32 (user32 / dwmapi / dcomp) · D2D/DWrite/DXGI · Shell/OLE  │
└────────────────────────────────────────────────────────────────┘
依赖方向严格自上而下；core 不 include ui；ui 不直接碰文件系统。
```

### 1.2 目录结构与模块职责

```
WinFence/
├─ CMakeLists.txt
├─ app.manifest                      # PMv2 / UTF-8 代码页 / longPathAware
├─ resources/
│  └─ winfence.rc                    # 版本信息 + 内嵌 manifest
├─ third_party/nlohmann/json.hpp     # vendored 单头文件
├─ docs/
│  ├─ DESIGN.md                      # 本文档
│  └─ CREDITS.md                     # 参考项目与许可证
├─ src/
│  ├─ app/
│  │  ├─ main.cpp                    # wWinMain：单实例→DPI→OleInit→启动 Workspace
│  │  ├─ App.h/.cpp                  # 生命周期编排、消息循环、退出存盘
│  │  └─ SingleInstance.h/.cpp       # Local\ 命名互斥体
│  ├─ core/
│  │  ├─ Model.h                     # Fence / IconMeta / Workspace（纯数据，见 §2）
│  │  ├─ FenceService.h/.cpp         # 业务规则：加入/移出/折叠/重排/孤儿回收
│  │  ├─ DesktopScanner.h/.cpp       # Shell 枚举 → DesktopSnapshot（用户+公共桌面合并）
│  │  ├─ DesktopWatcher.h/.cpp       # ReadDirectoryChangesW×2 + 防抖 + 配对 → FileEvent 队列
│  │  ├─ Reconciler.h/.cpp           # 快照 diff / 事件 对账：增删改 → 模型更新
│  │  └─ IconCache.h/.cpp            # HICON/HBITMAP → ID2D1Bitmap，LRU，键=(iconIndex, mtime)
│  ├─ ui/
│  │  ├─ FenceWindow.h/.cpp          # 每栏一个 HWND：注册类、消息路由、命中测试
│  │  ├─ FenceRenderer.h/.cpp        # D2D 绘制：圆角容器/标题栏/图标网格/折叠动画
│  │  ├─ Compositor.h/.cpp           # D3D/DXGI/DComp 设备与交换链管理、窗口失联重建
│  │  ├─ DropTarget.h/.cpp           # IDropTarget：CF_HDROP 只读接收 → FenceService
│  │  ├─ Interaction.h/.cpp          # 标题栏拖动(HTCAPTION)、双击折叠、右键菜单、滚动
│  │  └─ SettingsDialog.h/.cpp       # 中文设置弹窗（圆角/透明度/背景/默认样式）
│  ├─ shell/
│  │  ├─ DesktopAnchor.h/.cpp        # 定位 SHELLDLL_DefView/WorkerW、Z 序锚定与重锚
│  │  ├─ ShellEnumerator.h/.cpp      # IShellFolder 枚举、PIDL→路径/显示名
│  │  ├─ MonitorUtil.h/.cpp          # 显示器枚举、DPI 换算、物理/逻辑像素、越界夹取
│  │  └─ IconExtractor.h/.cpp        # IShellItemImageFactory / SHGetImageList 取图标
│  ├─ persist/
│  │  ├─ ConfigStore.h/.cpp          # load/save：原子写、.bak 轮换、schemaVersion 迁移
│  │  └─ JsonCodec.h/.cpp            # Model ↔ JSON 编解码（UTF-8 ⇄ UTF-16）
│  ├─ platform/
│  │  ├─ DwmBackdrop.h/.cpp          # DWMSBT 探测与设置、暗色边框、圆角偏好
│  │  ├─ PathGuard.h/.cpp            # 路径安全校验（§4.9，所有文件操作的唯一入口）
│  │  └─ WinUtil.h                   # COM RAII、错误码转中文、utf8 转换
│  └─ ai/                            # 迭代二占位（见 §6）：AiGroupingService、
│     └─ ...                         # DeepSeekClient / OllamaClient / GroupPlanParser
└─ tests/                            # 可选：JSON 编解码、重命名配对、路径校验的单元测试
```

### 1.3 线程模型

| 线程 | 职责 | 通信方式 |
|---|---|---|
| UI 线程（主） | 所有 HWND、D2D 渲染、**唯一的模型写线程**（单线程模型，免锁） | — |
| Watcher 线程 | IOCP 等待两个桌面目录的变更通知，防抖聚合 | `PostMessage(kMsgFileEvents)` 把一批事件投递回 UI 线程 |
| Worker 池（≤2） | 图标位图提取（慢操作，含磁盘 IO） | 完成后 `PostMessage(kMsgIconReady)` 通知重绘 |

规则：Watcher/Worker **绝不直接改模型**，只发消息；模型无锁。

---

## 2. 核心数据结构

> 所有坐标统一为**物理像素**，持久化时按所在显示器 DPI 折算为 DIP（见 §4.1）。
> `ColorF` 与 `D2D1_COLOR_F` 布局兼容，渲染层直接拷贝转换。

```cpp
// ===== src/core/Model.h =====
using IconUid = uint64_t;          // 进程内稳定自增 ID，持久化保留不复用
using FenceId = uint32_t;

enum class BackdropType : uint8_t { None, Acrylic, Translucent };
enum class IconKind   : uint8_t { File, Folder, Shortcut, VirtualNamespace };
enum class IconSource : uint8_t { UserDesktop, PublicDesktop, Namespace };

struct ColorF { float r, g, b, a; };

// ---------- 图标元数据 ----------
struct IconMeta {
    IconUid        uid = 0;
    std::wstring   sourcePath;           // 桌面绝对路径（规范化后）；命名空间项可为空
    std::wstring   displayName;          // Explorer 风格显示名（.lnk 解析后不带扩展名）
    IconKind       kind = IconKind::File;
    IconSource     source = IconSource::UserDesktop;
    std::vector<uint8_t> pidl;           // 序列化 PIDL，命名空间项的稳定标识
    uint64_t       fileTime = 0;         // 排序 + 重命名配对的时间邻近判断
    uint8_t        shellIconIndex = 0;   // 系统图像列表索引（缓存键的一部分）
    bool           isHidden = false;     // 隐藏/系统项（desktop.ini 等）默认不入栏
};

// ---------- 栅栏样式 ----------
struct FenceStyle {
    BackdropType   backdrop = BackdropType::Acrylic;
    float          opacity  = 0.65f;     // 0.2~1.0
    float          cornerRadiusDip = 8.0f;
    float          titleBarHeightDip = 32.0f;
    ColorF         accent  {0.55f, 0.75f, 0.95f, 1.0f};
    ColorF         border  {1.0f, 1.0f, 1.0f, 0.35f};
    // 字体族固定 "Microsoft YaHei UI"，不做配置项
};

// ---------- 栅栏容器 ----------
struct Fence {
    FenceId        id = 0;
    std::wstring   title;                // 默认 "新建栅栏"
    bool           collapsed = false;
    POINT          posPx     {0, 0};     // 左上角，物理像素（虚拟屏幕坐标系）
    SIZE           sizePx    {280, 320}; // 展开态尺寸
    SIZE           collapsedSizePx {280, 40};
    std::wstring   monitorDevice;        // "\\DISPLAY1"，显示器身份（非索引）
    FenceStyle     style;
    std::vector<IconUid> items;          // 归属图标，顺序即显示顺序
    POINT          scrollOffset {0, 0};
    int32_t        zSeq = 0;             // 栅栏之间的叠放顺序
};

// ---------- 全局工作区（= config.json 顶层）----------
struct Workspace {
    uint32_t             schemaVersion = 1;
    IconUid              nextUid = 1;
    FenceId              nextFenceId = 1;
    std::vector<Fence>   fences;
    FenceStyle           defaultStyle;
    bool                 showOnAllMonitors = true;
    // 不存在"未分组"实体——不在任何 fence.items 里的桌面图标天然是未分组，
    // 由系统桌面展示，无需建模。
};
```

```cpp
// ===== src/core/DesktopScanner.h =====
struct DesktopFileRecord {          // 扫描快照的一条
    std::wstring  normalizedPath;   // 规范化(忽略大小写)绝对路径 = 对账主键
    std::wstring  displayName;
    IconKind      kind;
    IconSource    source;
    uint64_t      fileId = 0;       // GetFileInformationByHandle 的 nFileIndex
    uint64_t      fileTime = 0;
    DWORD         attributes = 0;
};
using DesktopSnapshot = std::vector<DesktopFileRecord>;

// ===== src/core/DesktopWatcher.h =====
enum class FileEventKind { Added, Removed, RenamedFrom, RenamedTo, Modified, Overflow };
struct FileEvent {
    FileEventKind kind;
    std::wstring  path;
    uint64_t      timestampMs = 0;  // 防抖窗内做 delete+add 配对
};
```

### 2.1 config.json 格式（UTF-8 无 BOM，示例）

```json
{
  "schemaVersion": 1,
  "nextUid": 42,
  "nextFenceId": 4,
  "defaultStyle": { "backdrop": "acrylic", "opacity": 0.65,
                   "cornerRadiusDip": 8.0, "titleBarHeightDip": 32.0,
                   "accent": "#8CBFFF", "border": "#FFFFFF59" },
  "fences": [
    {
      "id": 1, "title": "工作文档", "collapsed": false,
      "posDip": { "x": 24.0, "y": 24.0 },
      "sizeDip": { "w": 280.0, "h": 320.0 },
      "collapsedSizeDip": { "w": 280.0, "h": 40.0 },
      "monitor": "\\\\DISPLAY1",
      "style": { "backdrop": "acrylic", "opacity": 0.7, "cornerRadiusDip": 8.0,
                 "titleBarHeightDip": 32.0, "accent": "#8CBFFF", "border": "#FFFFFF59" },
      "items": [
        { "uid": 7, "path": "C:\\Users\\李明\\Desktop\\季度报告.docx",
          "name": "季度报告", "kind": "file", "source": "userDesktop",
          "pidl": "1F50...", "fileTime": 175540000000000000 }
      ],
      "zSeq": 2
    }
  ]
}
```

> 位置持久化为**显示器归一化 DIP**（物理像素 ÷ 该屏 DPI×96，原点取该显示器工作区左上角）。
> 换分辨率/换 DPI/拔显示器时按公式反算并夹取回可见区域，避免"栅栏丢失"。

---

## 3. 核心业务流程

### 3.1 启动恢复

```
wWinMain
 ├─ SingleInstance：CreateMutexW(L"Local\\WinFence.Singleton") 已存在→激活设置窗口→退出
 ├─ 解析 DPI：manifest 已声明 PMv2（运行时再 SetProcessDpiAwarenessContext 兜底）
 ├─ OleInitialize（注意：不是 CoInitialize——拖拽必须 OLE）
 ├─ D2D/DWrite/DComp/D3D 设备创建（Compositor::Init，设备丢失可重建）
 ├─ DesktopAnchor::FindIconLayer()        // 找 SHELLDLL_DefView，记录 HWND
 ├─ ConfigStore::Load()
 │    ├─ 读 config.json 失败/损坏 → 尝试 config.json.bak → 仍失败→全新默认布局
 │    └─ schemaVersion 迁移（>当前版本则拒绝加载并备份原文件，防降级破坏）
 ├─ DesktopScanner::Scan() → DesktopSnapshot
 ├─ Reconciler::ReconcileOnStartup(model, snapshot)
 │    ├─ JSON 里的 item 路径已不存在 → 标记 orphan（保留 7 天后清除，不立即删）
 │    ├─ orphan 期间桌面文件又出现（重命名回来/同步完成）→ 自动恢复归属
 │    └─ 桌面新增文件 → 不属于任何栅栏 = 未分组，无需动作
 ├─ 为每个 Fence 创建 FenceWindow（按 monitorDevice 定位，DPI 折算 + 越界夹取）
 ├─ DesktopAnchor::AnchorAll()            // Z 序锚定到图标层之上（见 §4.4）
 ├─ DesktopWatcher::Start(用户桌面, 公共桌面)
 └─ RegisterWindowMessageW(L"TaskbarCreated") 监听 Explorer 重启
```

### 3.2 图标拖入栅栏（Explorer → WinFence）

```
用户从 Explorer/桌面把图标拖到栅栏窗口上
 ├─ DropTarget::DragEnter / DragOver
 │    ├─ 只认 CF_HDROP；QueryContinue 持续高亮目标栅栏
 │    └─ 不做任何文件写操作
 ├─ DropTarget::Drop
 │    ├─ DragQueryFileW 枚举路径（W 系 API，中文路径无损）
 │    ├─ PathGuard::ValidateDesktopItem(path)   // ★ 安全闸门，见 §4.9
 │    │    ├─ 必须存在且可读（GetFileAttributesW）
 │    │    ├─ 必须位于 用户桌面/公共桌面 目录内（规范化前缀比较，拒绝 ..、符号链接逃逸）
 │    │    ├─ 过滤 desktop.ini / thumbs.db / 隐藏系统项
 │    │    └─ 任一项不通过 → 跳过该项并在 UI 角标提示，绝不猜测
 │    ├─ IconExtractor 提取图标 → Worker 线程 → IconCache
 │    └─ FenceService::AddItems(fenceId, paths)：去重（同路径已在任一栅栏→移动而非复制）
 ├─ 渲染层收到模型变更消息 → 重绘该栅栏
 └─ ConfigStore::ScheduleSave()          // 防抖 800ms 的延迟保存
```

真实文件**一个字节都没动**——归属关系只存在于模型里。同一图标在系统桌面仍可见
（这是"不移动真实图标"约束的直接结果，MVP 接受该行为；隐藏单图标必须跨进程操作
Explorer 的 ListView，被约束 1 禁止）。

### 3.3 移出 / 移动归属

- 栅栏 A 内图标拖到栅栏 B → `FenceService::MoveItem(uid, A, B)`（纯模型操作）。
- 栅栏内图标拖到窗口外（非其他栅栏区域）→ 弹气泡确认"移出到未分组？"→ `RemoveItem`。
- 右键菜单「从此栅栏移除」同上。MVP 不向 Shell 发起 OLE 拖出（§0 安全决策）。

### 3.4 折叠 / 展开

```
双击标题栏（窗口类需 CS_DBLCLKS 才能收到 WM_LBUTTONDBLCLK）
 ├─ collapsed = !collapsed
 ├─ 目标尺寸 = collapsed ? collapsedSizePx : sizePx
 ├─ 动画：DComp 隐式动画（或 150ms 定时器插值），只改 visual 的 Clip/Size，不重排内容
 ├─ 折叠态：内容区不命中（WM_NCHITTEST 只在标题栏返回非 HTTRANSPARENT）
 └─ 保存（防抖）
```

### 3.5 桌面文件变更同步（最复杂的流程）

```
Watcher 线程（IOCP）：
 ├─ GetQueuedCompletionStatus 批量取出 FILE_NOTIFY_INFORMATION
 ├─ 展开宽字符名（中文无损），构造 FileEvent 投入待定队列
 ├─ 300ms 防抖窗口到期 → PostMessage(kMsgFileEvents, batch) 给 UI 线程
 └─ 错误处理：ERROR_NOTIFY_ENUM_DIR（缓冲溢出）→ 发 Overflow 事件；目录句柄失效→重建

UI 线程收到 batch：
 ├─ 第一步：配对重命名
 │    Win32 通知没有 rename 事件——只有独立的 delete + add
 │    启发式：防抖窗内 [Removed(p1) + Added(p2)]，且 p1 的 fileId 记录与
 │    p2 现查的 fileId 相等（GetFileInformationByHandle，仅要求属性不读内容）
 │    → 判定 rename(p1→p2)，其余情况按独立删除/新增处理
 ├─ 第二步：逐事件对账（Reconciler）
 │    ├─ Removed/RenamedFrom：模型中 sourcePath 命中的 item → orphan（记 timestampMs）
 │    ├─ Added/RenamedTo   ：命中 orphan item（按 fileId 或历史配对）→ 恢复并改写路径
 │    │                        否则属于未分组，不动作（同名路径 orphan 恢复归属）
 │    └─ Modified          ：仅刷新 fileTime/图标缓存键
 ├─ 第三步：orphan 垃圾回收——持续 7 天未恢复的 orphan 从 JSON 清除
 ├─ 相应栅栏重绘；OneDrive 桌面重定向场景天然被覆盖（§4.10）
 └─ 防抖保存
```

### 3.6 Z 序保持（桌面层锚定）

```
锚定动作 AnchorAll()：
 ├─ hDefView = DesktopAnchor::FindIconLayer()   // Progman → SHELLDLL_DefView
 ├─ hAbove   = GetWindow(hDefView, GW_HWNDPREV) // 图标层上面那个窗口
 ├─ SetWindowPos(fence, hAbove ? hAbove : HWND_BOTTOM,
 │               0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)
 │               // 插入结果 = 紧贴图标层之上；hAbove 为空则沉底
 └─ 触发重锚的时机：
      ├─ Explorer 重启（TaskbarCreated 广播消息到达）
      ├─ 任意栅栏 WM_WINDOWPOSCHANGED 发现自己被抬到普通窗口带（z 校验）
      ├─ 显示器变更 WM_DISPLAYCHANGE / WM_DPICHANGED
      └─ 兜底：60s 低频定时器校验一次（只在检测到偏移时重锚）

防止栅栏被点击顶起：WM_MOUSEACTIVATE 返回 MA_NOACTIVATE，
所有 SetWindowPos 一律带 SWP_NOACTIVATE，窗口类不含 WS_EX_TOPMOST。

※ 实现风险点①：该配方涉及跨进程 Z 序细节，落地时需用 Spy++ 实测验证。
   Rainmeter 的 "On Desktop" 实现是现成参照（见 §8 / docs/CREDITS.md）。
```

### 3.7 退出与持久化

```
用户退出（托盘菜单/消息）
 ├─ WM_QUERYENDSESSION / WM_ENDSESSION 同样走这条路径（系统关机不丢布局）
 ├─ ConfigStore::FlushNow()
 │    ├─ 序列化 → 写 config.json.tmp（同目录同卷）
 │    ├─ FlushFileBuffers → MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH) 原子替换
 │    └─ 旧文件轮换为 config.json.bak（保留一代）
 ├─ DesktopWatcher::Stop()（CancelIoEx + 线程汇合）
 └─ RevokeDragDrop 等注销，OleUninitialize
```

---

## 4. Windows 系统坑点清单

### 4.1 高分 DPI（P0）

| 坑 | 规避 |
|---|---|
| 未声明 PMv2 时系统对窗口位图拉伸，D2D 画面糊 | manifest 声明 `<dpiAwareness>PerMonitorV2</dpiAwareness>`；`wWinMain` 第一行再调 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` 兜底 |
| `WM_DPICHANGED` 不处理 → 拖栏跨屏后尺寸/字体错乱 | 按建议矩形 `*lParam` 重设窗口尺寸；按 `newDpi/oldDpi` 缩放 sizePx；重建 `IDWriteTextFormat`（字号是创建时烘焙的）；D2D 的 `SetDpi` 同步更新 |
| 物理像素 / 逻辑像素混用 | 项目约定：**运行时一律物理像素**，仅持久化时折算 DIP；换算只允许经过 `MonitorUtil`，公式 `dip = px * 96 / dpi` |
| 125% 下 1 物理像素边框消失/过粗 | 边框宽度按 `dpi/96` 取整，不用固定 1.0f |
| `GetDpiForWindow` 返回 0（窗口未就绪） | 回退 `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` |
| 混合 DPI 拖动过程撕裂 | PMv2 下由系统处理，但必须响应 `WM_DPICHANGED`，否则拖动中直接错位 |

### 4.2 多显示器（P0）

| 坑 | 规避 |
|---|---|
| 副屏在主屏左侧 → 坐标为负，任何"取绝对值/当无符号"的代码全错 | 全程有符号 `LONG`；禁止 `abs()` 用于比较 |
| 用显示器**索引**保存身份 → 重插拔后顺序变化导致栅栏飞到别的屏 | 身份用 `MONITORINFOEXW.szDevice`（`\\DISPLAY1`）字符串；恢复时按设备名找屏，找不到→按工作区比例映射到主屏 |
| 拔掉显示器后栅栏留在虚拟屏幕外"消失" | 每次恢复/显示器变更时 `MonitorFromPoint(MONITOR_DEFAULTTONEAREST)` + 夹取回最近屏工作区 |
| 工作区 vs 全屏区：任务栏占用的区域 | 定位用 `rcWork` 而非 `rcMonitor`，避免栅栏压在任务栏下 |
| 消息不全 | 监听 `WM_DISPLAYCHANGE`、`WM_SETTINGCHANGE`、每个窗口的 `WM_DPICHANGED` |

### 4.3 中文路径与编码（P0）

| 坑 | 规避 |
|---|---|
| ANSI API（`CreateFileA`/`fopen`/`std::string` 路径）在中文系统代码页（GBK）下乱码/丢字 | **只用 W 系列 API + `std::wstring`**；封禁 A 系（可加代码评审规则） |
| 源码中的中文字面量乱码 | 编译加 `/utf-8`（源码与执行字符集均为 UTF-8）；宽字面量统一 `L"中文"` |
| JSON 写出中文乱码 | 落盘 UTF-8 **无 BOM**；`std::filesystem::path` ↔ JSON string 用 `u8string()` 转换（C++20 为 `char8_t`，需显式重解释，封装在 `WinUtil::ToUtf8/FromUtf8`） |
| `SHGetPathFromIDList` 默认缓冲 `MAX_PATH` | 传自备 `WCHAR[MAX_PATH*2]`；全面启用 longPathAware，路径拼接交给 `std::filesystem::path` |
| 大小写与全半角 | 归一化主键用 `CompareStringOrdinal`（快且不受区域影响）做忽略大小写比较；不要 `tolower` |
| 显示名 vs 文件名 | 显示名来自 `GetDisplayNameOf(SIGDN_PARENTRELATIVEEDITING)`（`.lnk` 不带扩展名），不要拿显示名反推文件路径 |

### 4.4 桌面层级 / Z 序（P0）

| 坑 | 规避 |
|---|---|
| 桌面结构：`Progman → SHELLDLL_DefView → SysListView32(图标)`，壁纸在兄弟 `WorkerW`；网上教程常发送未文档化消息 `0x052C` 生成 WorkerW | 不发 `0x052C`（未文档化）。直接 `FindWindowW(L"Progman")` → `FindWindowExW` 向下找 `SHELLDLL_DefView`，纯查询 |
| 点击栅栏把它顶到前台，盖住所有应用 | `WM_MOUSEACTIVATE → MA_NOACTIVATE`；所有 `SetWindowPos` 带 `SWP_NOACTIVATE`；重锚逻辑兜底（§3.6） |
| Explorer 崩溃重启 → DefView HWND 失效、锚定关系丢失 | 注册 `TaskbarCreated` 广播消息，收到即重新 Find + Anchor + 全量重扫桌面 |
| `SetParent` 到 WorkerW 的教程 | **不用**：跨进程 SetParent 破坏 DPI 独立性且不稳定，用 Z 序锚定替代 |
| 任务栏"显示桌面"斜过 | 无需处理，Z 序位置天然正确 |

### 4.5 D2D / DComp / 亚克力（P0）

| 坑 | 规避 |
|---|---|
| `WS_EX_NOREDIRECTIONBITMAP` 窗口没有重定向表面 → 没有 `WM_PAINT`，忘建 DComp 目标则窗口纯透明"假死" | `Compositor` 封装保证：窗口创建成功 ⇔ `DCompositionCreateTargetForHwnd` 成功；DComp 提交失败→整窗重建 |
| 交换链 Alpha 没设预乘 → 边缘黑边/发脏 | `DXGI_SWAP_CHAIN_DESC1.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED`，D2D 目标 `D2D1_ALPHA_MODE_PREMULTIPLIED`，绘制内容全部自预乘 |
| DWM 亚克力 `DWMSBT_TRANSIENTWINDOW` 只在 Win11 22H2 (build 22621)+ 生效；对无标题栏的 DComp 透明窗口表现需实测 | `DwmBackdrop` 启动时探测 build 号，失败链回退：Acrylic → 半透明纯色 + 细噪点纹理。实现风险点②，参照 MicaForEveryone（§8） |
| 设备丢失（锁屏/睡眠恢复/驱动升级） | 监听 `DXGI_ERROR_DEVICE_REMOVED`，销毁 D2D/DXGI/DComp 全链重建；图标缓存保留（HICON 层与设备无关） |
| 圆角区域点击穿透 | 窗口矩形注册，`WM_NCHITTEST` 中对圆角外的四角返回 `HTTRANSPARENT`，让点击落到下面的桌面图标上 |
| D2D 文本中文渲染 | 用 DirectWrite + `"Microsoft YaHei UI"`（回退 `"Microsoft YaHei"`）；长文件名配 `DWRITE_TRIMMING` 省略号 |

### 4.6 OLE 拖放（P1）

| 坑 | 规避 |
|---|---|
| 只 `CoInitialize` 不 `OleInitialize` → `RegisterDragDrop` 返回 E_OUTOFMEMORY | 主线程必须 `OleInitialize / OleUninitialize` |
| 忘记 `RevokeDragDrop` → 窗口销毁后崩溃 | `FenceWindow` 析构严格顺序：Revoke → DestroyWindow |
| 接收 `CF_HDROP` 后直接信任路径 | 一律过 `PathGuard`（§4.9） |
| 拖拽过程中目标高亮闪烁 | `DragOver` 只更新视觉状态不改模型；真正变更在 `Drop` |
| 管理员权限的 Explorer 拖到非管理员 WinFence（或反向）被 UIPI 拦截静默失败 | 检测双方完整性级别，失败时气泡提示"权限不一致"；MVP 不做提权 |

### 4.7 文件监控（P1）

| 坑 | 规避 |
|---|---|
| `FILE_NOTIFY_INFORMATION` 的名字非零结尾 | 按字节长度拷贝再补 `\0`（中文目录名被截断的经典来源） |
| 通知顺序无保证、单事件重复触发 | 300ms 防抖合并；按 path+kind 去重 |
| 缓冲溢出（事件洪泛）返回 `ERROR_NOTIFY_ENUM_DIR` | 收到即全量重扫（复用 Reconciler 的 snapshot diff 路径） |
| **没有 rename 事件**（只有 delete + add） | §3.5 的 fileId 配对启发式；fileId 用 `GetFileInformationByHandle`（ReFS 上 128 位 `FILE_ID_INFO`），打开仅 `FILE_FLAG_BACKUP_SEMANTICS` 不读数据 |
| 监控线程直接改模型 → 竞态 | 单写线程原则：只 `PostMessage` |
| 只监控了用户桌面 | 公共桌面 `C:\Users\Public\Desktop` 单独开第二个监控；两个都失效时定时重建 |
| 监控时递归进 junction/符号链接造成循环 | 桌面只需浅层监控；枚举项遇到 reparse point 不深入 |

### 4.8 图标提取（P1）

| 坑 | 规避 |
|---|---|
| `SHGetFileInfo` 同步取大图标阻塞 UI 数百 ms（网络盘/死链 .lnk） | 提取在 Worker 线程；`IShellItemImageFactory::GetImage(SIIGBF_ICONONLY)`；超时 2s 放弃用默认图标占位 |
| HICON → D2D 位图丢失 Alpha 通道 → 图标带黑底 | `GetIconInfo` + `GetDIBits` 取 32bpp，检查 `bmBitsPixel==32`，按预乘处理；或走 WIC |
| 反复提取同一图标 CPU 高 | `IconCache` 键 = (shellIconIndex, 文件 mtime)，LRU 上限 ~500 项 |
| OneDrive 占位文件被读取触发云下载 | 仅请求图标/属性；检测 `FILE_ATTRIBUTE_RECALL_ON_DATA_OPEN` 时绝不打开数据流 |

### 4.9 文件安全（硬约束落地，P0）

`PathGuard` 是**所有**涉文件路径操作的强制唯一入口：

1. 全程只读：代码库**不存在** `DeleteFileW / MoveFileW / RemoveDirectory` 对桌面项的调用
   （CI grep 禁词强制审查）；本项目对桌面文件唯一的写操作 = 自己的 `config.json`。
2. 拖入校验链：`GetFileAttributesW` 存在性 → `GetFinalPathNameByHandleW` 解析符号链接后
   做前缀比较（防 junction 逃逸）→ 必须以用户桌面或公共桌面的规范化路径为前缀 →
   排除 `desktop.ini`/`thumbs.db`/隐藏系统项。
3. 配置写入原子化：`.tmp → FlushFileBuffers → MoveFileEx(WRITE_THROUGH|REPLACE_EXISTING)`，
   进程任何时刻被杀都不会产生半个 JSON；保留 `.bak` 一代。
4. 加载校验：JSON 解析后逐字段范围检查（尺寸 64~8192px、透明度 0.2~1.0、
   fence 数量 ≤ 64、items ≤ 2000），非法值用默认值替代并记录，整体损坏回退 `.bak`。
5. 拖出到 Shell 不做（§0），从根上杜绝"把文件拖出去触发 Explorer 真实移动/覆盖"。

### 4.10 其他系统坑（P1/P2）

| 坑 | 规避 |
|---|---|
| 桌面被 OneDrive/域策略重定向，`C:\Users\x\Desktop` 不存在 | 桌面路径只从 `SHGetKnownFolderPath(FOLDERID_Desktop)` 运行时获取，禁硬编码 |
| 公共桌面与用户桌面同名文件 | 以完整路径为主键各自建模（Explorer 实际也会同时显示）；显示名冲突不合并 |
| 隐藏文件的"是否显示"跟随 Explorer 文件夹选项 | MVP 策略：跟随系统 `FILE_ATTRIBUTE_HIDDEN` + 默认过滤；不做设置项 |
| 回收站等命名空间"图标" | `IShellFolder` 枚举可识别（无文件系统路径，有 PIDL）。**实现纪要（M4）**：因拖入通道 CF_HDROP 不携带文件路径，PathGuard 无法放行，命名空间项没有进入栅栏的入口 —— 原"MVP 支持展示与入栏"属过度承诺，降级为 **post-MVP**（方案：栅栏右键菜单「添加系统图标…」以 PIDL 入栏 + `SEE_MASK_IDLIST` 打开 + 按 pidl 匹配对账）。枚举层已保留其识别与 PIDL 序列化能力 |
| AV 误报 | 禁用清单（代码库不得出现）：`SetWindowsHookEx`、`WriteProcessMemory`/`ReadProcessMemory`、`CreateRemoteThread`、DLL 注入、全局键盘钩子、未文档化 `SetWindowCompositionAttribute`（默认编译关闭）、驱动。发布建议：CI 产物 + Virustotal 链接 +（可选）代码签名证书 |
| 单实例互斥体被残留 | `Local\` 命名（每用户会话独立）+ abandoned 检测 |
| 关机不触发正常退出消息 | `WM_QUERYENDSESSION` 返回 TRUE 前先同步 Flush；`WM_ENDSESSION` 再 Flush 一次 |
| 托盘图标在 Explorer 重启后消失 | 随 `TaskbarCreated` 重新 `Shell_NotifyIcon` |

---

## 5. CMake 编译方案

- 产物：单个 `WinFence.exe`（`/SUBSYSTEM:WINDOWS`，无控制台窗），x64 为主，ARM64 可选。
- 工具链：VS 2022（v143）+ Windows 10 SDK (10.0.22621+)；CMake ≥ 3.24。
- CRT：`/MT` 静态链接（Release），绿色单文件分发，免 VC Redist。

构建命令：

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# 产物 build\Release\WinFence.exe；便携 zip = exe + LICENSE + README
```

CI（GitHub Actions `windows-latest` 自带 VS 与 SDK，跑上面两条命令 + 上传 artifact）。
实际 CMakeLists.txt / app.manifest / winfence.rc 见仓库根目录与 `resources/`。

> 构建实测补充（已在骨架验证通过）：manifest 由 `winfence.rc` 内嵌时，必须给链接器加
> `/MANIFEST:NO`，否则 MSVC 默认再嵌入一份 manifest，CVTRES 报
> `CVT1100: 资源重复 MANIFEST` → `LNK1123`。CMakeLists 已内置该选项。

---

## 6. AI 智能分组模块（迭代二预留）

架构上只预留 `src/ai/` 模块与 `FenceService::ApplyGroupPlan()` 一个入口，MVP 不编译
任何网络代码（AI 代码放独立 CMake target `winfence-ai`，默认接入）。

1. **仅虚拟分组**：输出仅调用 `FenceService` 的模型操作（创建栅栏 + MoveItem），
   沿用 MVP 单写线程模型，不触碰任何文件 API。
2. **双后端**：`IAiProvider` 接口 + 两个实现——`DeepSeekClient`
   （WinHTTP POST `https://api.deepseek.com/chat/completions`，`model=deepseek-chat`，
   `response_format={"type":"json_object"}`）；`OllamaClient`
   （`http://localhost:11434/api/chat`，`format:"json"`）。密钥用 DPAPI
   `CryptProtectData` 加密存储在 `%APPDATA%\WinFence\ai.key`，绝不明文进 JSON。
3. **隐私**：请求 payload 只含 `[{ "uid", "name", "ext", "kind" }]` 文件名清单；
   不含路径（默认）、不含任何内容、不读文件内部。
4. **严格 JSON**：Prompt 给出固定 schema；解析侧 `GroupPlanParser` 同 schema 防御校验
   （未知字段拒收、uid 必须存在于当前模型、组数 ≤ 20、每组 ≤ 200 项、标题 ≤ 20 字符），
   任何违规整体作废并中文报错，绝不部分应用。
5. **UI 呈现**：结果先以"预览态"展示（虚线边框 + 「应用分组」/「放弃」按钮）；
   应用前自动保存 `preAiSnapshot`；「一键重置 AI 分组」一键还原；之后可手动微调。
6. **禁止自动**：无定时器、无开机触发、无文件事件触发；唯一入口 = 设置弹窗的
   「AI 整理」按钮，请求期间可取消（WinHTTP 超时 30s），失败给中文错误。

输出 schema 约定：`{"groups":[{"title":"办公文档","uids":[7,12]}]}`，
未出现在任何组的图标自动归入未分组。

---

## 7. MVP 边界确认

**做**：多栅栏透明容器（圆角/亚克力/透明度）、拖拽移动与入栏、双击折叠、
JSON 布局持久化与恢复、中文设置弹窗、桌面文件增删改同步。
（M4 追加：内容区滚动 + 滚动条、折叠动画、托盘菜单、应用图标、CI + 禁词审查。）

**不做**（已砍掉）：自动归类规则、多桌面快照、Dock 栏、自动更新、隐藏真实桌面图标、
OLE 拖出到 Shell、全屏自动隐藏、USN 监控、**命名空间项入栏（M4 降级为 post-MVP，见 §4.10）**。

---

## 8. 开源参考项目（详见 docs/CREDITS.md）

直接同类（C# 居多，产品/交互/数据模型参考）：

| 项目 | 许可证 | 参考点 |
|---|---|---|
| Twometer/NoFences | MIT | 品类祖师爷；交互最小集；数据模型可直接参考 |
| Damianttje/Fenceless | MIT | NoFences 增强 fork；JSON 持久化/备份/自动保存策略 |
| kelvinkbk/OpenFences、Xstoudi/Palisades | MIT | 折叠/多屏位置记忆交互 |
| DaveDebugs/Fluid.Fences | GPL-3 | Folder Portal + 快照（迭代方向参考，不可搬码） |

技术模块（同栈 C++/Win32）：

| WinFence 模块 | 参考项目 | 许可证 | 看什么 |
|---|---|---|---|
| shell/DesktopAnchor | rainmeter/rainmeter | GPL-2 | "On Desktop" ZPosition：WorkerW/Progman 检测、TaskbarCreated 重锚 |
| platform/DwmBackdrop | TranslucentTB/TranslucentTB | GPL-3 | accent API 防御性封装；小型 Win32 应用的 CMake+CI 组织 |
| platform/DwmBackdrop | MicaForEveryone/MicaForEveryone | MIT | DWMSBT 各取值效果矩阵，代码可搬 |
| ui/Compositor | microsoft/Windows-classic-samples | MIT | DComp+D2D 透明窗口、OLE DragDrop 官方示例 |
| 工程整体 | microsoft/PowerToys | MIT | FancyZones 的 DPI/多屏/单实例/CI |
| shell/ShellEnumerator | derceg/explorerplusplus | GPL-3 | PIDL 生命周期、EnumObjects、系统图像列表缓存 |
| core/DesktopWatcher | SpartanJ/efsw | MIT | ReadDirectoryChangesW+IOCP 现成库，可引入或对照 |

**许可证纪律**：本项目采用 MIT。MIT 系项目代码可搬（保留版权声明）；GPL 系只读思路、
一行不进仓库。参考清单维护在 `docs/CREDITS.md`。

**反面参考（禁止学习）**：ExplorerPatcher 类（注入/补丁 Explorer）、桌面图标位置保存类
工具（跨进程读写 SysListView32 内存）——正是硬约束 1 禁止的路线，也是 AV 误报重灾区。

---

## 9. 实现风险点（需早期 Demo 验证）

1. **Z 序锚定配方**（§3.6）：跨进程插入位置用 Spy++ 实测，对照 Rainmeter 实现。
2. **DWMSBT 亚克力在 DComp 透明窗口上的实际表现**（§4.5）：决定回退策略触发面，
   对照 MicaForEveryone 的效果矩阵。
