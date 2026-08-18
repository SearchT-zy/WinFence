// 文件监控实现（DESIGN.md §3.5 / §4.7）。
#include "core/DesktopWatcher.h"

#include <cstring>

namespace winfence {

namespace {

constexpr DWORD kBufferSize = 64 * 1024;   // 64KB，溢出时发 Overflow 全量重扫
constexpr DWORD kQuietMs    = 200;         // 静默防抖窗（§3.5 300ms 量级的轻量版）

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

bool DesktopWatcher::Start(const std::wstring& userDesktop,
                           const std::wstring& publicDesktop, HWND target)
{
    if (thread_) return true;   // 已在运行
    if (userDesktop.empty() || publicDesktop.empty() || !target) return false;

    target_  = target;
    dirs_[0] = userDesktop;
    dirs_[1] = publicDesktop;

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;
    thread_ = CreateThread(nullptr, 0, &DesktopWatcher::ThreadProc, this, 0, nullptr);
    return thread_ != nullptr;
}

void DesktopWatcher::Stop()
{
    if (!thread_) return;
    if (stopEvent_) SetEvent(stopEvent_);
    WaitForSingleObject(thread_, 5000);
    CloseHandle(thread_);
    thread_ = nullptr;
    if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
    if (iocp_) { CloseHandle(iocp_); iocp_ = nullptr; }   // 线程退出后安全关闭
}

DWORD WINAPI DesktopWatcher::ThreadProc(LPVOID self)
{
    return static_cast<DesktopWatcher*>(self)->Run();
}

DWORD DesktopWatcher::Run()
{
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!iocp_) return 1;

    // 每目录：句柄 + OVERLAPPED + 缓冲（OVERLAPPED 在挂起期间必须存活，§4.7）
    struct Watched {
        HANDLE           file = nullptr;
        OVERLAPPED       ov{};
        std::vector<BYTE> buffer;
    } watched[2]{};

    for (int i = 0; i < 2; ++i) {
        watched[i].file = CreateFileW(dirs_[i].c_str(), FILE_LIST_DIRECTORY,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                      nullptr);
        if (watched[i].file == INVALID_HANDLE_VALUE) {
            watched[i].file = nullptr;
            continue;
        }
        if (!CreateIoCompletionPort(watched[i].file, iocp_, (ULONG_PTR)i, 0)) {
            CloseHandle(watched[i].file);
            watched[i].file = nullptr;
            continue;
        }
        watched[i].buffer.resize(kBufferSize);
    }

    auto arm = [&](int i) {
        if (!watched[i].file) return;
        memset(&watched[i].ov, 0, sizeof(OVERLAPPED));
        DWORD dummy = 0;
        // 完成例程传 nullptr → 完成投递到关联的 IOCP
        ReadDirectoryChangesW(watched[i].file, watched[i].buffer.data(), kBufferSize,
                              FALSE,   // 浅层监控（防 junction 循环，§4.7）
                              FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES,
                              &dummy, &watched[i].ov, nullptr);
    };
    arm(0);
    arm(1);

    EventBatch pending;
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED ov = nullptr;
        // 超时即"静默窗到期"：有积压就冲刷（防抖），无积压继续等
        BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, kQuietMs);

        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) break;

        if (ok && key < 2 && ov == &watched[key].ov) {
            const int i = (int)key;
            if (bytes == 0) {
                // 缓冲溢出（事件洪泛）→ 全量重扫信号（§4.7）
                pending.push_back({FileEventKind::Overflow, L"", NowMs()});
            } else {
                auto* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    watched[i].buffer.data());
                for (;;) {
                    // 名字非零结尾：按字节长度构造（§4.7 中文截断坑）
                    std::wstring name(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
                    FileEventKind kind;
                    switch (fni->Action) {
                    case FILE_ACTION_ADDED:            kind = FileEventKind::Added; break;
                    case FILE_ACTION_REMOVED:          kind = FileEventKind::Removed; break;
                    case FILE_ACTION_MODIFIED:         kind = FileEventKind::Modified; break;
                    case FILE_ACTION_RENAMED_OLD_NAME: kind = FileEventKind::RenamedFrom; break;
                    case FILE_ACTION_RENAMED_NEW_NAME: kind = FileEventKind::RenamedTo; break;
                    default:                           kind = FileEventKind::Modified; break;
                    }
                    std::wstring base = dirs_[i];
                    if (!base.empty() && base.back() == L'\\') base.pop_back();
                    pending.push_back({kind, base + L"\\" + name, NowMs()});   // 单反斜杠拼接
                    if (!fni->NextEntryOffset) break;
                    fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                        reinterpret_cast<BYTE*>(fni) + fni->NextEntryOffset);
                }
            }
            arm(i);   // 立即重挂，缩小丢事件窗口
        } else if (!ok && !pending.empty()) {
            // 静默窗到期且无新事件 → 冲刷（所有权转移给 UI 线程）
            auto* batch = new EventBatch(std::move(pending));
            pending.clear();
            if (!PostMessageW(target_, kMsgFileEvents, (WPARAM)batch, 0))
                delete batch;
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (watched[i].file) {
            CancelIoEx(watched[i].file, &watched[i].ov);
            CloseHandle(watched[i].file);
        }
    }
    return 0;
}

} // namespace winfence
