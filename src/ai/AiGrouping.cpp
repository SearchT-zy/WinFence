// AI 智能分组编排器实现（M7b）。
#include "ai/AiGrouping.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <set>

#include "ai/AiClient.h"

namespace winfence {

using nlohmann::json;

namespace {

std::wstring AiKeyPath()
{
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT,
                                    nullptr, &roaming)) || !roaming)
        return L"ai.key";
    std::wstring p = roaming;
    CoTaskMemFree(roaming);
    p += L"\\WinFence";
    CreateDirectoryW(p.c_str(), nullptr);
    return p + L"\\ai.key";
}

std::string ToHex(const std::string& raw)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string s;
    s.reserve(raw.size() * 2);
    for (unsigned char c : raw) { s += kHex[c >> 4]; s += kHex[c & 0xF]; }
    return s;
}

std::string FromHex(const std::string& hex)
{
    std::string out;
    if (hex.size() % 2) return out;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = nib(hex[i]), lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back((char)(hi * 16 + lo));
    }
    return out;
}

const char kEntropy[] = "WinFence.ai.v1";

bool Protect(const std::string& plain, std::string& out)
{
    DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()};
    DATA_BLOB entropy{sizeof(kEntropy) - 1, (BYTE*)kEntropy};
    DATA_BLOB outBlob{};
    if (!CryptProtectData(&in, nullptr, &entropy, nullptr, nullptr, 0, &outBlob))
        return false;
    out.assign((char*)outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return true;
}

bool Unprotect(const std::string& cipher, std::string& out)
{
    DATA_BLOB in{(DWORD)cipher.size(), (BYTE*)cipher.data()};
    DATA_BLOB entropy{sizeof(kEntropy) - 1, (BYTE*)kEntropy};
    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, 0, &outBlob))
        return false;
    out.assign((char*)outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return true;
}

} // namespace

std::wstring LoadApiKey()
{
    HANDLE f = CreateFileW(AiKeyPath().c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    char buf[8192];
    DWORD read = 0;
    ReadFile(f, buf, sizeof(buf), &read, nullptr);
    CloseHandle(f);
    std::string cipher;
    if (!Unprotect(FromHex(std::string(buf, read)), cipher)) return {};
    return FromUtf8(cipher);
}

void SaveApiKey(const std::wstring& key)
{
    std::string cipher;
    if (!Protect(ToUtf8(key), cipher)) return;
    HANDLE f = CreateFileW(AiKeyPath().c_str(), GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    const std::string hex = ToHex(cipher);
    DWORD written = 0;
    WriteFile(f, hex.data(), (DWORD)hex.size(), &written, nullptr);
    CloseHandle(f);
}

AiJobResult RunAiGrouping(const Workspace& ws, const IconRegistry& icons)
{
    AiJobResult result;

    // ---- 输入：所有可见桌面项（仅 uid/名称/扩展名/类型；不含路径，§6.3）----
    json items = json::array();
    std::set<IconUid> known;
    for (const auto& [uid, m] : icons) {
        if (m.orphan || m.sourcePath.empty()) continue;
        if (items.size() >= 60) break;   // 防超长 prompt
        known.insert(uid);
        json it{{"uid", uid}, {"name", ToUtf8(m.displayName)}};
        const size_t dot = m.sourcePath.find_last_of(L'.');
        it["ext"] = (dot == std::wstring::npos)
            ? "" : ToUtf8(m.sourcePath.substr(dot + 1));
        it["kind"] = m.kind == IconKind::Folder   ? "folder"
                   : m.kind == IconKind::Shortcut ? "shortcut"
                                                  : "file";
        items.push_back(std::move(it));
    }
    if (items.size() < 4) {
        result.errZh = L"桌面图标太少（<4），无需 AI 分组";
        return result;
    }

    // ---- 请求体（双后端，§6.2）----
    const bool ollama = (ws.ai.provider == L"ollama");
    const std::wstring modelW = ws.ai.model.empty()
        ? (ollama ? L"qwen2.5:7b" : L"deepseek-chat")
        : ws.ai.model;
    const std::string model = ToUtf8(modelW);
    const std::string sysPrompt =
        "You are a desktop icon organizer. Group the given desktop items by their "
        "semantic purpose inferred from file names. Output ONLY a JSON object, no "
        "other text, in this exact schema: "
        "{\"groups\":[{\"title\":\"组名\",\"uids\":[1,2]}]}. "
        "Rules: 2-6 groups; each group 2-12 items; titles are concise Chinese "
        "(<=6 chars); omit items that fit no clear category.";
    const std::string userMsg = items.dump();

    json body;
    body["model"] = model;
    body["messages"] = json::array({
        json{{"role", "system"}, {"content", sysPrompt}},
        json{{"role", "user"}, {"content", userMsg}},
    });
    if (ollama) {
        body["format"] = "json";
        body["stream"] = false;
    } else {
        body["temperature"] = 0;
        body["response_format"] = {{"type", "json_object"}};
    }

    HttpRequest req;
    if (ollama) {
        req.host = L"localhost";
        req.port = 11434;
        req.tls  = false;
        req.path = L"/api/chat";
    } else {
        req.host = L"api.deepseek.com";
        req.port = 443;
        req.tls  = true;
        req.path = L"/chat/completions";
        req.bearer = ToUtf8(LoadApiKey());
        if (req.bearer.empty()) {
            result.errZh = L"未设置 DeepSeek API Key（在设置中填写）";
            return result;
        }
    }
    req.body = body.dump();
    req.timeoutMs = 60000;

    std::string respUtf8;
    if (!HttpPostJson(req, respUtf8, result.errZh)) return result;

    // ---- 解析响应：提取 content（§6.4 严格 JSON）----
    json resp = json::parse(respUtf8, nullptr, false);
    if (resp.is_discarded()) {
        result.errZh = L"服务响应不是合法 JSON";
        return result;
    }
    std::string content;
    if (resp.contains("choices")) {                    // OpenAI/DeepSeek 形态
        try { content = resp.at("choices").at(0).at("message").at("content").get<std::string>(); }
        catch (...) { result.errZh = L"响应缺少 content 字段"; return result; }
    } else if (resp.contains("message")) {             // Ollama 形态
        try { content = resp.at("message").at("content").get<std::string>(); }
        catch (...) { result.errZh = L"响应缺少 content 字段"; return result; }
    } else {
        result.errZh = L"无法识别的响应格式";
        return result;
    }

    json plan = json::parse(content, nullptr, false);
    if (plan.is_discarded() || !plan.is_object() || !plan.contains("groups") ||
        !plan["groups"].is_array() || plan["groups"].empty()) {
        result.errZh = L"模型输出不符合约定格式（期望 {\"groups\":[...]}）";
        return result;
    }
    if (plan["groups"].size() > 8) {
        result.errZh = L"分组数超过上限（8）";
        return result;
    }

    // ---- 校验：uid 必须存在且不跨组重复；标题长度受限（违规整体作废，§6.4）----
    std::set<IconUid> used;
    for (const auto& g : plan["groups"]) {
        if (!g.is_object() || !g.contains("title") || !g.contains("uids") ||
            !g["title"].is_string() || !g["uids"].is_array() || g["uids"].empty()) {
            result.errZh = L"分组结构不合法（缺 title/uids）";
            return result;
        }
        const std::wstring title = FromUtf8(g["title"].get<std::string>());
        if (title.empty() || title.size() > 12) {
            result.errZh = L"分组标题为空或过长（≤12 字符）";
            return result;
        }
        std::vector<IconUid> uids;
        for (const auto& ju : g["uids"]) {
            if (!ju.is_number_unsigned()) {
                result.errZh = L"uids 含非法值";
                return result;
            }
            const IconUid uid = ju.get<IconUid>();
            if (known.count(uid) == 0) {
                result.errZh = L"uids 含未知图标（模型幻觉），已整体作废";
                return result;
            }
            if (used.count(uid)) {
                result.errZh = L"同一图标出现在多个分组，已整体作废";
                return result;
            }
            used.insert(uid);
            uids.push_back(uid);
        }
        if (uids.size() > 30) {
            result.errZh = L"单组图标数超上限（30）";
            return result;
        }
        result.groups.emplace_back(title, std::move(uids));
    }
    if (result.groups.empty()) {
        result.errZh = L"未产生任何有效分组";
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace winfence
