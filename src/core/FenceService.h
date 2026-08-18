// 栅栏业务规则：模型唯一写入口，仅 UI 线程调用（DESIGN.md §1.3 线程模型）。
// 流程细节见 §3.2（拖入）/ §3.3（移出移动）/ §3.4（折叠）。
// 迭代二：AI 分组的落库也走本类（ApplyGroupPlan，§6），保证不绕过模型约束。
#pragma once
#include "core/Model.h"
#include <utility>
#include <vector>

namespace winfence {

class FenceService {
public:
    // 拖入：调用方必须先过 PathGuard::ValidateDesktopItem（§4.9）。
    // 同路径已在其他栏 → 移动而非复制。
    static bool AddItems(Workspace& ws, IconRegistry& icons,
                         FenceId fence, const std::vector<std::wstring>& paths);

    // 仅注册路径到图标注册表（不入栏），返回 uid 列表；Dock 拖入等场景使用
    static std::vector<IconUid> RegisterIcons(Workspace& ws, IconRegistry& icons,
                                              const std::vector<std::wstring>& paths);

    // 通用归属转移第一步：把 uid 从所有栅栏与 Dock 中移除（目标自行追加）
    static void DetachFromAll(Workspace& ws, IconUid uid);

    static bool MoveItem(Workspace& ws, FenceId from, FenceId to, IconUid uid);
    static bool RemoveItem(Workspace& ws, IconUid uid);        // 移出到未分组
    static void ToggleCollapse(Workspace& ws, FenceId fence);  // §3.4

    static FenceId CreateFence(Workspace& ws, const std::wstring& title);
    static bool    DeleteFence(Workspace& ws, FenceId fence);  // 内部图标回未分组，不删文件

    // 迭代二占位：应用 AI 分组计划（严格校验后调用，见 ai/AiProvider.h）
    // bool ApplyGroupPlan(Workspace& ws, const GroupPlan& plan);

    // ---- M7b：AI 分组应用 / 一键重置（§6.1/§6.5 仅模型操作）----
    using GroupPlan = std::vector<std::pair<std::wstring, std::vector<IconUid>>>;
    static bool ApplyGroupPlan(Workspace& ws, const GroupPlan& plan);
    static bool ResetAiGrouping(Workspace& ws);
};

} // namespace winfence
