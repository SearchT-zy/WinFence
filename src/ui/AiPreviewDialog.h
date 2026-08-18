// AI 分组预览窗口（M7b，DESIGN.md §6.5）：先预览，确认才落库。
// 只读文本列出各组 + [应用分组] / [放弃]；应用前由 FenceService 自动快照。
#pragma once
#include <windows.h>

#include <functional>
#include <utility>
#include <vector>

#include "core/Model.h"

namespace winfence {

class ConfigStore;

class AiPreviewDialog {
public:
    using Plan = std::vector<std::pair<std::wstring, std::vector<IconUid>>>;

    // owner：父窗口；onApplied：应用成功后的回调（App 负责刷新栅栏/Dock）
    static void Show(HINSTANCE instance, HWND owner, Workspace& ws,
                     const IconRegistry& icons, ConfigStore& store,
                     const Plan& plan, std::function<void()> onApplied);
};

} // namespace winfence
