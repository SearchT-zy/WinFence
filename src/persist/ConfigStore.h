// 配置存取：原子写、.bak 轮换、防抖保存（DESIGN.md §3.1 / §3.7 / §4.9）。
// 路径：%APPDATA%\WinFence\config.json（Known Folder API，中文用户名安全）。
// 原子写：config.json.tmp → FlushFileBuffers → MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)。
#pragma once
#include <windows.h>

#include "core/Model.h"

namespace winfence {

class ConfigStore {
public:
    // Load 失败链：config.json → config.json.bak → 返回 false（调用方用默认布局）
    bool Load(Workspace& ws, IconRegistry& icons);

    void SetModel(Workspace* ws, IconRegistry* icons);  // 供 FlushNow 取当前模型
    void ScheduleSave();                                // 防抖 800ms（隐藏定时器窗口）
    bool FlushNow();                                    // 立即原子写 + .bak 轮换

private:
    void EnsureTimerWindow();
    static LRESULT CALLBACK TimerWndProc(HWND, UINT, WPARAM, LPARAM);

    HWND timerHwnd_ = nullptr;
    Workspace* ws_ = nullptr;
    IconRegistry* icons_ = nullptr;
    bool dirty_ = false;
};

} // namespace winfence
