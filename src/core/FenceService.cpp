// 栅栏业务规则实现（DESIGN.md §3.2/§3.3）。模型唯一写入口（UI 线程）。
#include "core/FenceService.h"

#include <algorithm>

#include "platform/WinUtil.h"

namespace winfence {

namespace {

Fence* FindFence(Workspace& ws, FenceId id)
{
    for (auto& f : ws.fences)
        if (f.id == id) return &f;
    return nullptr;
}

// 按（忽略大小写的）路径在注册表里找图标
IconUid FindByPath(const IconRegistry& icons, const std::wstring& path)
{
    for (const auto& [uid, m] : icons)
        if (EqualNoCase(m.sourcePath, path)) return uid;
    return 0;
}

IconKind ClassifyKind(const std::wstring& pathLower)
{
    if (pathLower.size() >= 4 &&
        pathLower.compare(pathLower.size() - 4, 4, L".lnk") == 0)
        return IconKind::Shortcut;
    if (pathLower.size() >= 4 &&
        pathLower.compare(pathLower.size() - 4, 4, L".url") == 0)
        return IconKind::Shortcut;
    return IconKind::File;
}

std::wstring DisplayNameFromPath(const std::wstring& path, IconKind kind)
{
    size_t pos = path.find_last_of(L"\\/");
    std::wstring name = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
    // .lnk/.url 解析后的显示名不带扩展名（Explorer 风格，§4.3）
    if (kind == IconKind::Shortcut) {
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
    }
    return name;
}

} // namespace

bool FenceService::AddItems(Workspace& ws, IconRegistry& icons, FenceId fence,
                            const std::vector<std::wstring>& paths)
{
    Fence* target = FindFence(ws, fence);
    if (!target) return false;

    bool added = false;
    for (const auto& path : paths) {
        IconUid uid = FindByPath(icons, path);
        if (uid == 0) {
            IconMeta m;
            m.uid = ws.nextUid++;
            m.sourcePath = path;
            m.kind = ClassifyKind(path);
            m.displayName = DisplayNameFromPath(path, m.kind);
            uid = m.uid;
            icons[uid] = std::move(m);
        } else {
            icons[uid].orphan = false;    // 曾消失又出现 → 恢复（§3.5）
        }

        // 去重：同一路径已在任一栅栏 → 移动而非复制（§3.2）
        for (auto& f : ws.fences)
            f.items.erase(std::remove(f.items.begin(), f.items.end(), uid), f.items.end());

        if (target->items.size() >= 2000) break;
        target->items.push_back(uid);
        added = true;
    }
    return added;
}

bool FenceService::MoveItem(Workspace& ws, FenceId from, FenceId to, IconUid uid)
{
    Fence* src = FindFence(ws, from);
    Fence* dst = FindFence(ws, to);
    if (!src || !dst || from == to) return false;
    auto it = std::find(src->items.begin(), src->items.end(), uid);
    if (it == src->items.end()) return false;
    if (std::find(dst->items.begin(), dst->items.end(), uid) != dst->items.end())
        return false;
    src->items.erase(it);
    dst->items.push_back(uid);
    return true;
}

bool FenceService::RemoveItem(Workspace& ws, IconUid uid)
{
    for (auto& f : ws.fences) {
        auto it = std::find(f.items.begin(), f.items.end(), uid);
        if (it != f.items.end()) { f.items.erase(it); return true; }
    }
    return false;
}

void FenceService::ToggleCollapse(Workspace& ws, FenceId fence)
{
    if (Fence* f = FindFence(ws, fence)) f->collapsed = !f->collapsed;
}

FenceId FenceService::CreateFence(Workspace& ws, const std::wstring& title)
{
    Fence f;
    f.id = ws.nextFenceId++;
    f.title = title.empty() ? L"新建栅栏" : title;
    f.style = ws.defaultStyle;
    f.zSeq = (int32_t)ws.fences.size();
    ws.fences.push_back(f);
    return f.id;
}

bool FenceService::DeleteFence(Workspace& ws, FenceId fence)
{
    // 内部图标回到未分组（仅从 items 移除，注册表保留），绝不触碰文件（§4.9）
    for (size_t i = 0; i < ws.fences.size(); ++i) {
        if (ws.fences[i].id == fence) {
            ws.fences.erase(ws.fences.begin() + (long)i);
            return true;
        }
    }
    return false;
}

std::vector<IconUid> FenceService::RegisterIcons(Workspace& ws, IconRegistry& icons,
                                                 const std::vector<std::wstring>& paths)
{
    std::vector<IconUid> uids;
    for (const auto& path : paths) {
        IconUid uid = FindByPath(icons, path);
        if (uid == 0) {
            IconMeta m;
            m.uid = ws.nextUid++;
            m.sourcePath = path;
            m.kind = ClassifyKind(path);
            m.displayName = DisplayNameFromPath(path, m.kind);
            uid = m.uid;
            icons[uid] = std::move(m);
        } else {
            icons[uid].orphan = false;
        }
        uids.push_back(uid);
    }
    return uids;
}

void FenceService::DetachFromAll(Workspace& ws, IconUid uid)
{
    for (auto& f : ws.fences)
        f.items.erase(std::remove(f.items.begin(), f.items.end(), uid), f.items.end());
    auto& d = ws.dock.items;
    d.erase(std::remove(d.begin(), d.end(), uid), d.end());
}

bool FenceService::ApplyGroupPlan(Workspace& ws, const GroupPlan& plan)
{
    // 1) 快照当前归属（一键重置的依据，§6.5）
    ws.aiBackup.present = true;
    ws.aiBackup.fences.clear();
    ws.aiBackup.dock = ws.dock.items;
    for (const auto& f : ws.fences)
        ws.aiBackup.fences.emplace_back(f.id, f.items);

    // 2) 逐组：同名栅栏复用，否则新建（瀑布位）
    for (size_t gi = 0; gi < plan.size(); ++gi) {
        const auto& [title, uids] = plan[gi];
        Fence* target = nullptr;
        for (auto& f : ws.fences)
            if (f.title == title) { target = &f; break; }
        if (!target) {
            Fence f;
            f.id = ws.nextFenceId++;
            f.title = title;
            f.style = ws.defaultStyle;
            f.posPx = {50 + (LONG)(gi % 4) * 340, 50 + (LONG)(gi / 4) * 360};
            f.sizePx = {280, 320};
            f.collapsedSizePx = {280, 40};
            ws.fences.push_back(f);
            target = &ws.fences.back();
        }
        for (IconUid uid : uids) {
            DetachFromAll(ws, uid);
            if (target->items.size() < 2000) target->items.push_back(uid);
        }
    }
    return true;
}

bool FenceService::ResetAiGrouping(Workspace& ws)
{
    if (!ws.aiBackup.present) return false;
    for (auto& f : ws.fences) f.items.clear();
    ws.dock.items.clear();
    for (const auto& [id, items] : ws.aiBackup.fences)
        for (auto& f : ws.fences)
            if (f.id == id) { f.items = items; break; }
    ws.dock.items = ws.aiBackup.dock;
    ws.aiBackup.present = false;
    ws.aiBackup.fences.clear();
    ws.aiBackup.dock.clear();
    return true;
}

} // namespace winfence
