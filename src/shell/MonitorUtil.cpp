// 显示器 / DPI 工具实现（DESIGN.md §4.1 / §4.2）。
#include "shell/MonitorUtil.h"
#include <shellscalingapi.h>   // GetDpiForMonitor（shcore.lib）
#include <algorithm>
#include <vector>

namespace winfence {

namespace {

MonitorInfoEx FromHmonitor(HMONITOR monitor)
{
    MonitorInfoEx e;
    e.dpiX = 96;
    if (!monitor) return e;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return e;
    e.device    = mi.szDevice;
    e.workArea  = mi.rcWork;
    e.fullArea  = mi.rcMonitor;
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dx, &dy))) { dx = 96; }
    e.dpiX = dx;
    return e;
}

struct EnumCtx { std::vector<MonitorInfoEx>* out; };

BOOL CALLBACK EnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    MonitorInfoEx e = FromHmonitor(monitor);
    ctx->out->push_back(e);
    return TRUE;
}

} // namespace

std::vector<MonitorInfoEx> MonitorUtil::Enumerate()
{
    std::vector<MonitorInfoEx> out;
    EnumCtx ctx{&out};
    EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return out;
}

MonitorInfoEx MonitorUtil::FromPoint(POINT px)
{
    return FromHmonitor(MonitorFromPoint(px, MONITOR_DEFAULTTONEAREST));
}

MonitorInfoEx MonitorUtil::Primary()
{
    return FromHmonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
}

RECT MonitorUtil::ClampToWorkArea(RECT r, const MonitorInfoEx& m)
{
    if (r.left < m.workArea.left)   r.left  = m.workArea.left;
    if (r.top  < m.workArea.top)    r.top   = m.workArea.top;
    if (r.right  > m.workArea.right)  r.right  = m.workArea.right;
    if (r.bottom > m.workArea.bottom) r.bottom = m.workArea.bottom;
    // 保底可见尺寸（§4.10 加载校验同源：宽 ≥64 / 高 ≥40）
    if (r.right - r.left < 64)
        r.right = std::min<LONG>(r.left + 64, m.workArea.right);
    if (r.bottom - r.top < 40)
        r.bottom = std::min<LONG>(r.top + 40, m.workArea.bottom);
    return r;
}

} // namespace winfence
