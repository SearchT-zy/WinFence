// 显示器 / DPI 工具：唯一合法的像素折算入口（DESIGN.md §4.1 / §4.2）。
// 公式：dip = px * 96 / dpi（往返换算只允许经过本类）。
// 身份用 MONITORINFOEXW.szDevice 字符串（非索引）；定位用 rcWork（避开任务栏）；
// 越界夹取 MonitorFromPoint(MONITOR_DEFAULTTONEAREST)（防"栅栏消失"）。
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace winfence {

struct MonitorInfoEx {
    std::wstring device;      // "\\DISPLAY1"
    RECT workArea;            // rcWork
    RECT fullArea;            // rcMonitor
    UINT dpiX = 96;
};

class MonitorUtil {
public:
    static std::vector<MonitorInfoEx> Enumerate();
    static MonitorInfoEx FromPoint(POINT px);                 // MONITOR_DEFAULTTONEAREST
    static MonitorInfoEx Primary();                          // MONITOR_DEFAULTTOPRIMARY
    static float  PxToDip(int px, UINT dpi) { return px * 96.0f / dpi; }
    static int    DipToPx(float dip, UINT dpi) { return (int)(dip * dpi / 96.0f + 0.5f); }
    static RECT   ClampToWorkArea(RECT r, const MonitorInfoEx& m);  // 夹回可见工作区
};

} // namespace winfence
