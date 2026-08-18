// AI 分组模块（迭代二预留，DESIGN.md §6）。MVP 不编译任何网络代码。
//
// 六条边界（实现时逐条对照）：
//  1. 仅虚拟分组：输出只走 FenceService::ApplyGroupPlan（模型操作），不碰文件 API。
//  2. 双后端：DeepSeekClient（WinHTTP，response_format=json_object）
//     与 OllamaClient（localhost:11434，format:"json"），实现本接口。
//  3. 隐私：payload 只含 [{uid, name, ext, kind}]，不含路径（默认）、不读文件内容。
//  4. 严格 JSON：GroupPlanParser 按固定 schema 防御校验，违规整体作废绝不部分应用。
//  5. UI：先"预览态"（应用/放弃），应用前保存 preAiSnapshot，支持一键重置。
//  6. 禁止自动：无定时器/开机/文件事件触发；唯一入口 = 设置弹窗「AI 整理」按钮，
//     可取消，超时 30s，失败中文报错。
// 密钥存储：DPAPI CryptProtectData → %APPDATA%\WinFence\ai.key，绝不进 config.json。
#pragma once
#include "core/Model.h"
#include <string>
#include <vector>

namespace winfence {

struct GroupPlan {                    // 严格 JSON 输出 schema：组 ≤20、每组 ≤200、标题 ≤20 字符
    struct Group { std::wstring title; std::vector<IconUid> uids; };
    std::vector<Group> groups;        // 未出现的 uid 自动归入未分组
};

struct AiInputItem { uint64_t uid; std::wstring name; std::wstring ext; IconKind kind; };

class IAiProvider {
public:
    virtual ~IAiProvider() = default;
    virtual bool GenerateGroups(const std::vector<AiInputItem>& items,
                                GroupPlan& out, std::wstring& errZh) = 0;
};

} // namespace winfence
