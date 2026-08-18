// 中文设置弹窗（DESIGN.md MVP 清单第 5 项）。
// MVP 控件：背景效果下拉 + 透明度滑条 + 圆角滑条 → 应用到默认样式与全部现有栅栏。
// 迭代二在此追加「AI 整理」按钮（唯一 AI 触发入口，DESIGN.md §6）。
// 单实例非模态；布局按窗口 DPI 缩放。
#pragma once
#include <windows.h>

#include <functional>

#include "core/Model.h"

namespace winfence {

class ConfigStore;

class SettingsDialog {
public:
    // onApplied：应用后的回调（App 负责：刷新全部栅栏 + 防抖保存）
    static void ShowSingle(HINSTANCE instance, Workspace& ws, ConfigStore& store,
                           std::function<void()> onApplied);

    static void CloseIfOpen();   // 退出时清理
};

} // namespace winfence
