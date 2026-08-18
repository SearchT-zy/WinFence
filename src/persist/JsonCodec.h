// Model ↔ JSON 编解码（nlohmann/json，vendored）。schema 见 DESIGN.md §2.1。
// 编码铁律：
//   - 落盘 UTF-8 无 BOM；std::wstring ↔ json string 经 WinUtil::ToUtf8/FromUtf8
//     （C++20 u8string 是 char8_t，需显式重解释，§4.3）
//   - 位置持久化为显示器归一化 DIP（posDip/sizeDip），monitor 用设备名字符串
// 解码铁律（§4.9 第 4 条）：
//   - 逐字段范围检查：尺寸 64~8192px、透明度 0.2~1.0、fence ≤64、items ≤2000
//   - 非法值用默认替代并记录；整体损坏由 ConfigStore 回退 .bak
#pragma once
#include "core/Model.h"
#include <string>

namespace winfence {

class JsonCodec {
public:
    static bool Encode(const Workspace& ws, const IconRegistry& icons, std::string& utf8Out);
    static bool Decode(const std::string& utf8In, Workspace& ws, IconRegistry& icons);
};

} // namespace winfence
