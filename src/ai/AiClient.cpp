// AI HTTP 客户端实现（WinHTTP，M7b）。
#include "ai/AiClient.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

namespace winfence {

bool HttpPostJson(const HttpRequest& req, std::string& responseUtf8,
                  std::wstring& errZh)
{
    responseUtf8.clear();
    HINTERNET session = WinHttpOpen(L"WinFence/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        errZh = L"网络初始化失败";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, req.timeoutMs, req.timeoutMs);

    HINTERNET connect = WinHttpConnect(session, req.host.c_str(),
                                       (INTERNET_PORT)req.port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        errZh = L"无法连接到 " + req.host;
        return false;
    }

    const DWORD flags = req.tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hreq = WinHttpOpenRequest(connect, L"POST", req.path.c_str(),
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hreq) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        errZh = L"构造请求失败";
        return false;
    }

    std::wstring headers = L"Content-Type: application/json";
    if (!req.bearer.empty())
        headers += L"\r\nAuthorization: Bearer " + FromUtf8(req.bearer);

    const BOOL sent = WinHttpSendRequest(
        hreq, headers.c_str(), -1,
        (LPVOID)req.body.data(), (DWORD)req.body.size(),
        (DWORD)req.body.size(), 0);
    const BOOL got = sent && WinHttpReceiveResponse(hreq, nullptr);

    bool ok = false;
    if (!got) {
        errZh = L"请求失败（网络不通或超时）";
    } else {
        DWORD status = 0, size = sizeof(status), dummy = 0;
        WinHttpQueryHeaders(hreq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, &dummy);
        std::string body;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hreq, &avail) || avail == 0) break;
            std::string chunk(avail, '\0');
            DWORD readBytes = 0;
            if (!WinHttpReadData(hreq, chunk.data(), avail, &readBytes)) break;
            body.append(chunk.data(), readBytes);
        }
        if (status >= 200 && status < 300) {
            responseUtf8 = std::move(body);
            ok = true;
        } else {
            errZh = L"服务返回错误 HTTP " + std::to_wstring(status) +
                    L"：" + FromUtf8(body.substr(0, 200));
        }
    }

    WinHttpCloseHandle(hreq);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

} // namespace winfence
