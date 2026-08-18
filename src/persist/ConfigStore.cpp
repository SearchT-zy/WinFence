// 配置存取实现（DESIGN.md §3.7 / §4.9）。
#include "persist/ConfigStore.h"

#include <shlobj.h>   // SHGetKnownFolderPath

#include <fstream>

#include "persist/JsonCodec.h"

namespace winfence {

namespace {

constexpr wchar_t kTimerClass[] = L"WinFenceSaveTimerWnd";
constexpr UINT_PTR kSaveTimerId = 1;
constexpr UINT     kSaveDelayMs = 800;    // §3.2 防抖 800ms

std::wstring ConfigDir()
{
    // Known Folder API：OneDrive/域重定向与中文用户名都安全（§4.10 / §4.3）
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT,
                                    nullptr, &roaming)) || !roaming) {
        return L".";
    }
    std::wstring dir = roaming;
    CoTaskMemFree(roaming);
    dir += L"\\WinFence";
    CreateDirectoryW(dir.c_str(), nullptr);   // 已存在则忽略
    return dir;
}

bool ReadAllBytes(const std::wstring& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streampos len = f.tellg();
    if (len <= 0 || len > 16 * 1024 * 1024) return false;   // 16MB 上限防御
    f.seekg(0, std::ios::beg);
    out.resize((size_t)len);
    f.read(&out[0], len);
    return f.good() || f.eof();
}

// 原子写：tmp → flush → rename（§4.9 第 3 条）
bool AtomicWrite(const std::wstring& path, const std::string& content)
{
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, content.data(), (DWORD)content.size(), &written, nullptr);
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace

bool ConfigStore::Load(Workspace& ws, IconRegistry& icons)
{
    const std::wstring dir = ConfigDir();
    std::string content;
    if (ReadAllBytes(dir + L"\\config.json", content) ||
        ReadAllBytes(dir + L"\\config.json.bak", content)) {
        return JsonCodec::Decode(content, ws, icons);
    }
    return false;
}

void ConfigStore::SetModel(Workspace* ws, IconRegistry* icons)
{
    ws_ = ws;
    icons_ = icons;
}

void ConfigStore::EnsureTimerWindow()
{
    if (timerHwnd_) return;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &ConfigStore::TimerWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kTimerClass;
    RegisterClassExW(&wc);
    // message-only 窗口：不接收广播，只做定时器宿主
    timerHwnd_ = CreateWindowExW(0, kTimerClass, L"", WS_OVERLAPPED,
                                 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                 GetModuleHandleW(nullptr), this);
}

LRESULT CALLBACK ConfigStore::TimerWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<ConfigStore*>(
            reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        auto* self = reinterpret_cast<ConfigStore*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self && msg == WM_TIMER && wp == kSaveTimerId) {
            KillTimer(h, kSaveTimerId);
            self->FlushNow();
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void ConfigStore::ScheduleSave()
{
    dirty_ = true;
    EnsureTimerWindow();
    if (timerHwnd_) SetTimer(timerHwnd_, kSaveTimerId, kSaveDelayMs, nullptr);
}

bool ConfigStore::FlushNow()
{
    if (!ws_ || !icons_) return false;
    if (!dirty_) return true;

    std::string content;
    if (!JsonCodec::Encode(*ws_, *icons_, content)) return false;

    const std::wstring dir = ConfigDir();
    const std::wstring path = dir + L"\\config.json";
    // 旧文件轮换为 .bak（保留一代，§3.7）
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW(path.c_str(), (dir + L"\\config.json.bak").c_str(),
                    MOVEFILE_REPLACE_EXISTING);
    if (!AtomicWrite(path, content)) return false;
    dirty_ = false;
    return true;
}

} // namespace winfence
