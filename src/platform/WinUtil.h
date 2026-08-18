// 通用工具：COM RAII、错误码转中文、UTF-8 转换（DESIGN.md §4.3）。
// ToUtf8/FromUtf8 是 JSON 落盘的唯一转换通道（C++20 char8_t 显式重解释）。
#pragma once
#include <string>

namespace winfence {

std::string  ToUtf8(const std::wstring& ws);   // UTF-16 → UTF-8（无 BOM）
std::wstring FromUtf8(const std::string& s);

// HRESULT/Win32 错误 → 中文可读信息（设置弹窗与气泡提示用）
std::wstring HresultToMessage(long hr);

// 归一化主键比较：CompareStringOrdinal 忽略大小写（快且不受区域影响，§4.3）
int  CompareNoCase(const std::wstring& a, const std::wstring& b);
bool EqualNoCase(const std::wstring& a, const std::wstring& b);

} // namespace winfence
