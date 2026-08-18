// 对账器实现（DESIGN.md §3.5）。
#include "core/Reconciler.h"

#include <algorithm>

#include "platform/WinUtil.h"   // EqualNoCase

namespace winfence {

namespace {

constexpr uint64_t kOrphanKeepMs = 7ull * 24 * 60 * 60 * 1000;   // 7 天（§3.5）

IconUid FindUidByPath(const IconRegistry& icons, const std::wstring& path)
{
    for (const auto& [uid, m] : icons)
        if (EqualNoCase(m.sourcePath, path)) return uid;
    return 0;
}

std::wstring BaseName(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

std::wstring DisplayNameOf(const std::wstring& path, IconKind kind)
{
    std::wstring name = BaseName(path);
    if (kind == IconKind::Shortcut) {   // .lnk/.url 不带扩展名（§4.3）
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
    }
    return name;
}

IconKind ClassifyKind(const std::wstring& path)
{
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    if (lower.size() >= 4 &&
        (lower.compare(lower.size() - 4, 4, L".lnk") == 0 ||
         lower.compare(lower.size() - 4, 4, L".url") == 0))
        return IconKind::Shortcut;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return IconKind::Folder;
    return IconKind::File;
}

uint64_t NowMs()
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000;
}

} // namespace

void Reconciler::ReconcileOnStartup(Workspace& ws, IconRegistry& icons,
                                    const DesktopSnapshot& snap)
{
    const uint64_t now = NowMs();
    for (const auto& rec : snap) {
        const IconUid uid = FindUidByPath(icons, rec.normalizedPath);
        if (uid != 0) {
            auto& m = icons[uid];
            m.fileTime = rec.fileTime;    // 刷新 mtime（图标缓存键）
            m.orphan   = false;
            m.orphanSinceMs = 0;
            if (m.displayName.empty()) m.displayName = rec.displayName;
        } else {
            IconMeta m;
            m.uid = ws.nextUid++;
            m.sourcePath  = rec.normalizedPath;
            m.displayName = rec.displayName;
            m.kind   = rec.kind;
            m.source = rec.source;
            m.fileTime = rec.fileTime;
            icons[m.uid] = std::move(m);
        }
    }
    for (auto& [uid, m] : icons) {
        if (m.sourcePath.empty()) continue;   // 命名空间项不判 orphan
        bool present = false;
        for (const auto& rec : snap)
            if (EqualNoCase(m.sourcePath, rec.normalizedPath)) { present = true; break; }
        if (!present && !m.orphan) {
            m.orphan = true;          // 桌面已消失 → 保留归属待恢复（§3.5）
            m.orphanSinceMs = now;
        }
    }
}

bool Reconciler::ApplyEvents(Workspace& ws, IconRegistry& icons,
                             const DesktopWatcher::EventBatch& batch,
                             DesktopSnapshot& overflowRescanOut)
{
    bool changed = false;
    const uint64_t now = NowMs();

    // 溢出 → 调用方全量重扫（§4.7）
    const bool overflow = std::any_of(batch.begin(), batch.end(), [](const FileEvent& e) {
        return e.kind == FileEventKind::Overflow;
    });
    if (overflow) {
        overflowRescanOut = DesktopScanner().Scan();
        ReconcileOnStartup(ws, icons, overflowRescanOut);
        return true;
    }

    // 第一步：配对重命名（同批 RenamedFrom + RenamedTo，§3.5）
    std::vector<bool> used(batch.size(), false);
    for (size_t i = 0; i < batch.size(); ++i) {
        if (batch[i].kind != FileEventKind::RenamedFrom || used[i]) continue;
        for (size_t j = i + 1; j < batch.size(); ++j) {
            if (batch[j].kind != FileEventKind::RenamedTo || used[j]) continue;
            const std::wstring& from = batch[i].path;
            const std::wstring& to   = batch[j].path;
            const IconUid uid = FindUidByPath(icons, from);
            if (uid != 0) {
                auto& m = icons[uid];
                m.sourcePath  = to;                        // 归属随重命名迁移
                m.kind        = ClassifyKind(to);
                m.displayName = DisplayNameOf(to, m.kind);
                m.orphan      = false;
                m.orphanSinceMs = 0;
                changed = true;
            }
            used[i] = used[j] = true;
            break;
        }
    }

    // 第一步半（兜底）：同批 Removed(旧) + Added(新)，旧路径已知且新路径未知
    // → 视为重命名（§3.5 启发式；防事件链被拆分到不同通知块）
    for (size_t i = 0; i < batch.size(); ++i) {
        if (batch[i].kind != FileEventKind::Removed || used[i]) continue;
        const IconUid uidOld = FindUidByPath(icons, batch[i].path);
        if (uidOld == 0) continue;
        for (size_t j = 0; j < batch.size(); ++j) {
            if (batch[j].kind != FileEventKind::Added || used[j]) continue;
            if (FindUidByPath(icons, batch[j].path) != 0) continue;
            auto& m = icons[uidOld];
            m.sourcePath  = batch[j].path;
            m.kind        = ClassifyKind(batch[j].path);
            m.displayName = DisplayNameOf(batch[j].path, m.kind);
            m.orphan      = false;
            m.orphanSinceMs = 0;
            used[i] = used[j] = true;
            changed = true;
            break;
        }
    }

    // 第二步：逐事件处理
    for (size_t i = 0; i < batch.size(); ++i) {
        if (used[i]) continue;
        const auto& e = batch[i];
        const IconUid uid = FindUidByPath(icons, e.path);
        switch (e.kind) {
        case FileEventKind::Added:
            if (uid != 0) {
                auto& m = icons[uid];
                if (m.orphan) { m.orphan = false; m.orphanSinceMs = 0; changed = true; }
                m.fileTime = 0;   // 触发图标缓存按新文件重取
            } else {
                // 新文件：注册为未分组（用户拖入时才归属）
                if (!BaseName(e.path).empty()) {
                    IconMeta m;
                    m.uid = ws.nextUid++;
                    m.sourcePath  = e.path;
                    m.kind        = ClassifyKind(e.path);
                    m.displayName = DisplayNameOf(e.path, m.kind);
                    icons[m.uid] = std::move(m);
                    changed = true;
                }
            }
            break;

        case FileEventKind::Removed:
            if (uid != 0 && !icons[uid].orphan) {
                icons[uid].orphan = true;
                icons[uid].orphanSinceMs = now;
                changed = true;
            }
            break;

        case FileEventKind::Modified:
            if (uid != 0) {
                icons[uid].fileTime = 0;   // mtime 变化 → 图标缓存键失效重取
                changed = true;
            }
            break;

        default:
            break;
        }
    }

    GcOrphans(ws, icons, now);
    return changed;
}

void Reconciler::GcOrphans(Workspace& ws, IconRegistry& icons, uint64_t nowMs)
{
    std::vector<IconUid> dead;
    for (const auto& [uid, m] : icons)
        if (m.orphan && m.orphanSinceMs && nowMs - m.orphanSinceMs > kOrphanKeepMs)
            dead.push_back(uid);
    if (dead.empty()) return;
    for (IconUid uid : dead) {
        for (auto& f : ws.fences)
            f.items.erase(std::remove(f.items.begin(), f.items.end(), uid), f.items.end());
        icons.erase(uid);
    }
}

} // namespace winfence
