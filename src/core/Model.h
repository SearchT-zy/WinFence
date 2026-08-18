// WinFence 领域模型（纯数据，无行为）。定义见 docs/DESIGN.md §2。
// 约定：坐标一律物理像素；持久化时经 MonitorUtil 折算为显示器归一化 DIP（§4.1）。
// core 层不依赖 ui：ColorF 与 D2D1_COLOR_F 布局兼容，渲染层自行拷贝转换。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace winfence {

using IconUid = uint64_t;   // 进程内稳定自增 ID，持久化保留不复用
using FenceId = uint32_t;

enum class BackdropType : uint8_t { None, Acrylic, Translucent };
enum class IconKind   : uint8_t { File, Folder, Shortcut, VirtualNamespace };
enum class IconSource : uint8_t { UserDesktop, PublicDesktop, Namespace };

// 与 D2D1_COLOR_F 内存布局兼容
struct ColorF { float r, g, b, a; };

// ---------- 图标元数据（DESIGN.md §2）----------
struct IconMeta {
    IconUid        uid = 0;
    std::wstring   sourcePath;           // 桌面绝对路径（规范化后）；命名空间项可为空
    std::wstring   displayName;          // Explorer 风格显示名（.lnk 解析后不带扩展名）
    IconKind       kind = IconKind::File;
    IconSource     source = IconSource::UserDesktop;
    std::vector<uint8_t> pidl;           // 序列化 PIDL，命名空间项的稳定标识
    uint64_t       fileTime = 0;         // 排序 + 重命名配对的时间邻近判断
    uint8_t        shellIconIndex = 0;   // 系统图像列表索引（IconCache 键的一部分）
    bool           isHidden = false;     // 隐藏/系统项（desktop.ini 等）默认不入栏

    // ---- 运行期状态（不持久化）----
    bool           orphan = false;       // 桌面上已消失，等待恢复（§3.5，7 天回收）
    uint64_t       orphanSinceMs = 0;
};

// ---------- 栅栏样式 ----------
struct FenceStyle {
    BackdropType   backdrop = BackdropType::Acrylic;
    float          opacity  = 0.65f;     // 合法范围 0.2~1.0（JsonCodec 校验）
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
    POINT          posPx     {0, 0};     // 左上角，物理像素（虚拟屏幕坐标系，可为负）
    SIZE           sizePx    {280, 320}; // 展开态尺寸（合法 64~8192）
    SIZE           collapsedSizePx {280, 40};
    std::wstring   monitorDevice;        // MONITORINFOEXW.szDevice，如 "\\DISPLAY1"（非索引）
    FenceStyle     style;
    std::vector<IconUid> items;          // 归属图标，顺序即显示顺序（≤2000）
    POINT          scrollOffset {0, 0};
    int32_t        zSeq = 0;             // 栅栏之间的叠放顺序
};

// ---------- Dock 栏（M6：屏幕底部黑色半透明栏，macOS 式）----------
struct Dock {
    bool                 visible = false;   // 默认关闭，设置里开启
    std::vector<IconUid> items;             // Dock 内图标，顺序即显示顺序（≤100）
};

// ---------- 全局工作区（= config.json 顶层）----------
struct Workspace {
    uint32_t             schemaVersion = 1;
    IconUid              nextUid = 1;
    FenceId              nextFenceId = 1;
    std::vector<Fence>   fences;         // ≤64（JsonCodec 校验）
    FenceStyle           defaultStyle;   // 新建栅栏的初始样式
    bool                 showOnAllMonitors = true;
    Dock                 dock;

    // 设计说明：不存在"未分组"实体——不在任何 fence.items 里的桌面图标
    // 天然是未分组，由系统桌面展示，无需建模（DESIGN.md §2）。
};

// 运行期图标注册表：uid → IconMeta（含未入栏项，供扫描对账与 AI 分组引用）。
// 仅 UI 线程读写（单写线程模型，§1.3）。
using IconRegistry = std::unordered_map<IconUid, IconMeta>;

} // namespace winfence
