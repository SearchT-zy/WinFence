// 中文设置弹窗：样式/开关/AI 智能分组（M7b 唯一 AI 触发入口，DESIGN.md §6.6）。
// 单实例非模态；布局按窗口 DPI 缩放。
#pragma once
#include <windows.h>

#include <functional>

#include "core/Model.h"

namespace winfence {

class ConfigStore;

class SettingsDialog {
public:
    // onApplied：任何模型/设置变化后的回调（App 负责：刷新栅栏+Dock、同步可见性）
    static void ShowSingle(HINSTANCE instance, Workspace& ws, const IconRegistry& icons,
                           ConfigStore& store, std::function<void()> onApplied);

    static void CloseIfOpen();   // 退出时清理
};

} // namespace winfence
