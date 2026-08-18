// 一次性教育提示（M7a）：首次成功拖入图标后，解释"虚拟收纳"并给出
// 干净桌面的一键入口（隐藏桌面图标）。回答或关闭后不再打扰。
#pragma once
#include <windows.h>

#include "core/Model.h"

namespace winfence {

class ConfigStore;

void MaybeShowVirtualGroupingHint(HWND owner, Workspace& ws, ConfigStore& store);

} // namespace winfence
