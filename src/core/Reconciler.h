// 对账器（DESIGN.md §3.5）：文件事件 → 模型更新。
// 配对重命名：同批 RenamedFrom(p1)+RenamedTo(p2)（Win32 会成对投递）；
// 兜底：Removed+Added 时间邻近启发式（fileId 校验 M4 加强）。
// orphan 保留 7 天后回收（§3.5）。
#pragma once
#include "core/DesktopScanner.h"
#include "core/DesktopWatcher.h"
#include "core/Model.h"

namespace winfence {

class Reconciler {
public:
    // 启动对账：路径已消失 → orphan；新文件 → 注册为未分组（§3.1）
    static void ReconcileOnStartup(Workspace& ws, IconRegistry& icons,
                                   const DesktopSnapshot& snap);

    // 事件批量对账：先配对重命名，再逐事件处理（§3.5）。返回是否有模型变化。
    static bool ApplyEvents(Workspace& ws, IconRegistry& icons,
                            const DesktopWatcher::EventBatch& batch,
                            DesktopSnapshot& overflowRescanOut);

    // orphan 垃圾回收：持续 7 天未恢复的从注册表清除（§3.5）
    static void GcOrphans(Workspace& ws, IconRegistry& icons, uint64_t nowMs);
};

} // namespace winfence
