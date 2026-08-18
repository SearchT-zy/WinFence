// 单实例互斥体 Local\WinFence.Singleton（DESIGN.md §3.1 / §4.10）。
// Local\ 前缀 = 每用户会话独立；需区分 ERROR_ALREADY_EXISTS 与 WAIT_ABANDONED。
#pragma once
#include <string>

namespace winfence {

class SingleInstance {
public:
    explicit SingleInstance(std::wstring name);  // L"Local\\WinFence.Singleton"
    ~SingleInstance();                            // 释放互斥体
    bool IsPrimary() const { return primary_; }   // false = 已有实例，应退出
    void ActivateExisting();                      // 激活已有实例的设置窗口
private:
    void* handle_ = nullptr;   // HANDLE（实现时使用真类型）
    bool  primary_ = false;
};

} // namespace winfence
