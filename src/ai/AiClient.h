// AI HTTP 客户端（M7b）：WinHTTP 通用 POST JSON（同步，供工作线程调用）。
// DeepSeek（api.deepseek.com /chat/completions）与 Ollama（localhost /api/chat）共用。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#include "platform/WinUtil.h"   // ToUtf8 / FromUtf8（单一实现，避免 ODR 冲突）

namespace winfence {

struct HttpRequest {
    std::wstring host;      // 例：api.deepseek.com / localhost
    int         port = 443;
    bool        tls  = true;             // Ollama 本地为 false
    std::wstring path;                   // /chat/completions 或 /api/chat
    std::string  body;                   // UTF-8 JSON
    std::string  bearer;                 // 可空：Bearer <key>
    int          timeoutMs = 30000;
};

// 成功返回 true 并填充 responseUtf8；失败返回 false + 中文错误。
bool HttpPostJson(const HttpRequest& req, std::string& responseUtf8,
                  std::wstring& errZh);

} // namespace winfence
