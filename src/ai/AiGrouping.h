// AI 智能分组编排器（M7b，DESIGN.md §6 六条边界逐条落地）。
//  1. 仅虚拟分组：产物只含 uid→组名，落库走 FenceService（模型操作）
//  2. 双后端：DeepSeek 云端 / Ollama 本地（ws.ai.provider 切换）
//  3. 隐私：请求体只有 {uid, name, ext, kind}，不含路径、不读内容
//  4. 严格 JSON：任何校验违规整体作废（绝不部分应用）
//  5. 应用前自动快照（Workspace::aiBackup），支持一键重置
//  6. 禁止自动：唯一入口是设置弹窗的「AI 整理」按钮（人工点击）
// 本函数含网络 IO，必须在【工作线程】调用。
#pragma once
#include "core/Model.h"

#include <string>
#include <utility>
#include <vector>

namespace winfence {

struct AiJobResult {
    bool ok = false;
    std::wstring errZh;                                    // 失败原因（中文）
    std::vector<std::pair<std::wstring, std::vector<IconUid>>> groups;   // 组名→uids
};

AiJobResult RunAiGrouping(const Workspace& ws, const IconRegistry& icons);

// DeepSeek API Key：DPAPI 加密存储 %APPDATA%\WinFence\ai.key（绝不入 config.json）
std::wstring LoadApiKey();
void         SaveApiKey(const std::wstring& key);

} // namespace winfence
