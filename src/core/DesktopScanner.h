// Shell 枚举：IShellFolder::EnumObjects 合并用户桌面 + 公共桌面（DESIGN.md §2 / §4.3）。
// 桌面路径只从 SHGetKnownFolderPath(FOLDERID_Desktop / FOLDERID_PublicDesktop)
// 运行时获取，禁止硬编码（§4.10 OneDrive 重定向）。
#pragma once
#include "core/Model.h"
#include <vector>

namespace winfence {

// 扫描快照的一条：描述"桌面上实际存在什么"，与 IconMeta（归属模型）解耦。
struct DesktopFileRecord {
    std::wstring  normalizedPath;   // 规范化(忽略大小写)绝对路径 = 对账主键
    std::wstring  displayName;      // SIGDN_PARENTRELATIVEEDITING（.lnk 不带扩展名）
    IconKind      kind = IconKind::File;
    IconSource    source = IconSource::UserDesktop;
    uint64_t      fileId = 0;       // GetFileInformationByHandle nFileIndex，重命名配对用
    uint64_t      fileTime = 0;
    DWORD         attributes = 0;   // OneDrive 占位检测（§4.8）
};

using DesktopSnapshot = std::vector<DesktopFileRecord>;

class DesktopScanner {
public:
    // 全量扫描。枚举走 Shell 命名空间与 Explorer 视图一致；
    // 默认过滤 FILE_ATTRIBUTE_HIDDEN 与 desktop.ini / thumbs.db（§4.10）。
    DesktopSnapshot Scan();
};

} // namespace winfence
