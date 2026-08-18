// Model ↔ JSON 编解码实现。schema 见 DESIGN.md §2.1。
// 编码：UTF-8 无 BOM；解码：逐字段范围校验（§4.9 第 4 条）。
// 注意：位置 DIP 折算需要显示器信息 —— 这是 persist 侧唯一直接依赖 shell 的点
//（MonitorUtil），为保持"显示器归一化 DIP"的 schema 承诺（§2.1）所接受的横向依赖。
#include "persist/JsonCodec.h"

#include <nlohmann/json.hpp>

#include "platform/WinUtil.h"
#include "shell/MonitorUtil.h"

namespace winfence {

using nlohmann::json;

namespace {

constexpr uint32_t kMaxFences = 64;
constexpr uint32_t kMaxItems  = 2000;
constexpr float    kMinSizePx = 64.0f;
constexpr float    kMaxSizePx = 8192.0f;

// ---------- 枚举 ↔ 字符串 ----------
const char* KindStr(IconKind k)
{
    switch (k) {
    case IconKind::Folder:          return "folder";
    case IconKind::Shortcut:        return "shortcut";
    case IconKind::VirtualNamespace:return "namespace";
    default:                        return "file";
    }
}
IconKind KindFrom(const std::string& s)
{
    if (s == "folder")           return IconKind::Folder;
    if (s == "shortcut")         return IconKind::Shortcut;
    if (s == "namespace")        return IconKind::VirtualNamespace;
    return IconKind::File;
}
const char* SourceStr(IconSource s)
{
    switch (s) {
    case IconSource::PublicDesktop: return "publicDesktop";
    case IconSource::Namespace:     return "namespace";
    default:                        return "userDesktop";
    }
}
IconSource SourceFrom(const std::string& s)
{
    if (s == "publicDesktop") return IconSource::PublicDesktop;
    if (s == "namespace")     return IconSource::Namespace;
    return IconSource::UserDesktop;
}
const char* BackdropStr(BackdropType b)
{
    switch (b) {
    case BackdropType::None:        return "none";
    case BackdropType::Translucent: return "translucent";
    default:                        return "acrylic";
    }
}
BackdropType BackdropFrom(const std::string& s)
{
    if (s == "none")        return BackdropType::None;
    if (s == "translucent") return BackdropType::Translucent;
    return BackdropType::Acrylic;
}

// ---------- 颜色 ↔ #RRGGBBAA ----------
std::string ColorToHex(const ColorF& c)
{
    auto ch = [](float f) { return ((uint32_t)(f * 255.0f + 0.5f)) & 0xFF; };
    char buf[16];
    sprintf_s(buf, "#%02X%02X%02X%02X", ch(c.r), ch(c.g), ch(c.b), ch(c.a));
    return buf;
}
bool HexToColor(const std::string& s, ColorF& out)
{
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    auto nib = [&](size_t i) -> int {
        char c = s[i];
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    auto byteAt = [&](size_t i) -> int {
        int hi = nib(i), lo = nib(i + 1);
        return (hi < 0 || lo < 0) ? -1 : hi * 16 + lo;
    };
    int r = byteAt(1), g = byteAt(3), b = byteAt(5);
    int a = (s.size() == 9) ? byteAt(7) : 255;
    if (r < 0 || g < 0 || b < 0 || a < 0) return false;
    out = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    return true;
}

// ---------- PIDL ↔ hex ----------
std::string BytesToHex(const std::vector<uint8_t>& v)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string s;
    s.reserve(v.size() * 2);
    for (uint8_t b : v) { s += kHex[b >> 4]; s += kHex[b & 0xF]; }
    return s;
}
std::vector<uint8_t> HexToBytes(const std::string& s)
{
    std::vector<uint8_t> v;
    if (s.size() % 2) return v;
    v.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(s[i]), lo = nib(s[i + 1]);
        if (hi < 0 || lo < 0) return {};
        v.push_back((uint8_t)(hi * 16 + lo));
    }
    return v;
}

// ---------- 样式（含范围钳制）----------
json StyleToJson(const FenceStyle& st)
{
    return json{
        {"backdrop", BackdropStr(st.backdrop)},
        {"opacity", st.opacity},
        {"cornerRadiusDip", st.cornerRadiusDip},
        {"titleBarHeightDip", st.titleBarHeightDip},
        {"accent", ColorToHex(st.accent)},
        {"border", ColorToHex(st.border)}};
}
FenceStyle StyleFrom(const json& j, const FenceStyle& def)
{
    FenceStyle st = def;
    if (j.contains("backdrop") && j["backdrop"].is_string())
        st.backdrop = BackdropFrom(j["backdrop"].get<std::string>());
    if (j.contains("opacity") && j["opacity"].is_number()) {
        double v = j["opacity"].get<double>();
        if (v >= 0.2 && v <= 1.0) st.opacity = (float)v;   // §4.9 范围校验
    }
    if (j.contains("cornerRadiusDip") && j["cornerRadiusDip"].is_number()) {
        double v = j["cornerRadiusDip"].get<double>();
        if (v >= 0.0 && v <= 32.0) st.cornerRadiusDip = (float)v;
    }
    if (j.contains("titleBarHeightDip") && j["titleBarHeightDip"].is_number()) {
        double v = j["titleBarHeightDip"].get<double>();
        if (v >= 20.0 && v <= 64.0) st.titleBarHeightDip = (float)v;
    }
    ColorF c{};
    if (j.contains("accent") && j["accent"].is_string() &&
        HexToColor(j["accent"].get<std::string>(), c)) {
        // 旧默认主题色自动升级为科技风霓虹青（#8CBFFF → #3EC9F5）
        if (j["accent"].get<std::string>() == "#8CBFFF")
            c = {0.243f, 0.788f, 0.961f, 1.0f};
        st.accent = c;
    }
    if (j.contains("border") && j["border"].is_string() &&
        HexToColor(j["border"].get<std::string>(), c)) st.border = c;
    return st;
}

// 找显示器（按设备名，找不到回主屏，§4.2）
MonitorInfoEx FindMonitor(const std::wstring& device)
{
    for (const auto& m : MonitorUtil::Enumerate())
        if (m.device == device) return m;
    return MonitorUtil::Primary();
}

float ClampSize(double v)
{
    if (v < kMinSizePx) return kMinSizePx;
    if (v > kMaxSizePx) return kMaxSizePx;
    return (float)v;
}

// 折叠态只需容纳标题栏：独立下限 24px（ClampSize 的 64px 下限是给展开态的；
// 曾因此把 40DIP 折叠高在解码时钳成 64，M4 实测踩坑）
float ClampCollapsedHeight(double v)
{
    if (v < 24.0) return 24.0;
    if (v > kMaxSizePx) return kMaxSizePx;
    return (float)v;
}

} // namespace

bool JsonCodec::Encode(const Workspace& ws, const IconRegistry& icons, std::string& utf8Out)
{
    try {
        json j;
        j["schemaVersion"]     = ws.schemaVersion;
        j["nextUid"]           = ws.nextUid;
        j["nextFenceId"]       = ws.nextFenceId;
        j["showOnAllMonitors"] = ws.showOnAllMonitors;
        j["hintHideIconsDismissed"] = ws.hintHideIconsDismissed;
        j["ai"] = {{"provider", ToUtf8(ws.ai.provider)}, {"model", ToUtf8(ws.ai.model)}};
        if (ws.aiBackup.present) {
            json bf = json::array();
            for (const auto& [id, items] : ws.aiBackup.fences) {
                json fr{{"id", id}, {"items", items}};
                bf.push_back(std::move(fr));
            }
            j["aiBackup"] = {{"present", true}, {"fences", std::move(bf)},
                             {"dock", ws.aiBackup.dock}};
        }
        j["defaultStyle"]      = StyleToJson(ws.defaultStyle);

        json jIcons = json::array();
        for (const auto& [uid, m] : icons) {
            json im{
                {"uid", uid},
                {"path", ToUtf8(m.sourcePath)},
                {"name", ToUtf8(m.displayName)},
                {"kind", KindStr(m.kind)},
                {"source", SourceStr(m.source)},
                {"fileTime", m.fileTime}};
            if (!m.pidl.empty()) im["pidl"] = BytesToHex(m.pidl);
            jIcons.push_back(std::move(im));
        }
        j["icons"] = std::move(jIcons);

        // Dock（M6）
        {
            json dk;
            dk["visible"] = ws.dock.visible;
            json dItems = json::array();
            for (IconUid uid : ws.dock.items) dItems.push_back(uid);
            dk["items"] = std::move(dItems);
            j["dock"] = std::move(dk);
        }

        json jFences = json::array();
        for (const auto& f : ws.fences) {
            const auto mon = FindMonitor(f.monitorDevice);
            // §2.1：显示器归一化 DIP（原点=该屏工作区左上角）
            json jf{
                {"id", f.id},
                {"title", ToUtf8(f.title)},
                {"collapsed", f.collapsed},
                {"monitor", ToUtf8(f.monitorDevice)},
                {"zSeq", f.zSeq},
                {"posDip", {
                    {"x", MonitorUtil::PxToDip(f.posPx.x - mon.workArea.left, mon.dpiX)},
                    {"y", MonitorUtil::PxToDip(f.posPx.y - mon.workArea.top, mon.dpiX)}}},
                {"sizeDip", {
                    {"w", MonitorUtil::PxToDip(f.sizePx.cx, mon.dpiX)},
                    {"h", MonitorUtil::PxToDip(f.sizePx.cy, mon.dpiX)}}},
                {"collapsedSizeDip", {
                    {"w", MonitorUtil::PxToDip(f.collapsedSizePx.cx, mon.dpiX)},
                    {"h", MonitorUtil::PxToDip(f.collapsedSizePx.cy, mon.dpiX)}}},
                {"scrollPx", {
                    {"x", MonitorUtil::PxToDip(f.scrollOffset.x, mon.dpiX)},
                    {"y", MonitorUtil::PxToDip(f.scrollOffset.y, mon.dpiX)}}},
                {"style", StyleToJson(f.style)}};
            json items = json::array();
            for (IconUid uid : f.items) items.push_back(uid);
            jf["items"] = std::move(items);
            jFences.push_back(std::move(jf));
        }
        j["fences"] = std::move(jFences);

        utf8Out = j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        return true;
    } catch (...) {
        return false;
    }
}

bool JsonCodec::Decode(const std::string& utf8In, Workspace& ws, IconRegistry& icons)
{
    try {
        json j = json::parse(utf8In, nullptr, false);   // 不抛异常，手动检查
        if (j.is_discarded() || !j.is_object()) return false;
        if (!j.contains("schemaVersion") || !j["schemaVersion"].is_number_unsigned())
            return false;

        const uint32_t schema = j["schemaVersion"].get<uint32_t>();
        if (schema > ws.schemaVersion) return false;    // 高版本拒载（§3.1 防降级破坏）
        ws.schemaVersion = schema;

        if (j.contains("nextUid") && j["nextUid"].is_number_unsigned())
            ws.nextUid = j["nextUid"].get<uint64_t>();
        if (j.contains("nextFenceId") && j["nextFenceId"].is_number_unsigned())
            ws.nextFenceId = j["nextFenceId"].get<uint32_t>();
        if (j.contains("showOnAllMonitors") && j["showOnAllMonitors"].is_boolean())
            ws.showOnAllMonitors = j["showOnAllMonitors"].get<bool>();
        if (j.contains("hintHideIconsDismissed") && j["hintHideIconsDismissed"].is_boolean())
            ws.hintHideIconsDismissed = j["hintHideIconsDismissed"].get<bool>();
        if (j.contains("ai") && j["ai"].is_object()) {
            ws.ai.provider = FromUtf8(j["ai"].value("provider", std::string("deepseek")));
            ws.ai.model    = FromUtf8(j["ai"].value("model", std::string()));
        }
        if (j.contains("aiBackup") && j["aiBackup"].is_object()) {
            const auto& jb = j["aiBackup"];
            ws.aiBackup.present =
                jb.contains("present") && jb["present"].is_boolean() &&
                jb["present"].get<bool>();
            if (jb.contains("fences") && jb["fences"].is_array()) {
                for (const auto& jf : jb["fences"]) {
                    if (!jf.is_object() || !jf.contains("id")) continue;
                    std::vector<IconUid> items;
                    if (jf.contains("items") && jf["items"].is_array())
                        for (const auto& ju : jf["items"])
                            if (ju.is_number_unsigned())
                                items.push_back(ju.get<IconUid>());
                    ws.aiBackup.fences.emplace_back(jf["id"].get<FenceId>(),
                                                    std::move(items));
                }
            }
            if (jb.contains("dock") && jb["dock"].is_array())
                for (const auto& ju : jb["dock"])
                    if (ju.is_number_unsigned())
                        ws.aiBackup.dock.push_back(ju.get<IconUid>());
        }
        if (j.contains("defaultStyle"))
            ws.defaultStyle = StyleFrom(j["defaultStyle"], ws.defaultStyle);

        icons.clear();
        if (j.contains("icons") && j["icons"].is_array()) {
            for (const auto& ji : j["icons"]) {
                if (!ji.is_object()) continue;
                try {
                    IconMeta m;
                    m.uid = ji.at("uid").get<uint64_t>();
                    if (m.uid == 0 || icons.count(m.uid)) continue;   // 0/重复 uid 丢弃
                    m.sourcePath  = FromUtf8(ji.value("path", ""));
                    m.displayName = FromUtf8(ji.value("name", ""));
                    m.kind   = KindFrom(ji.value("kind", "file"));
                    m.source = SourceFrom(ji.value("source", "userDesktop"));
                    m.fileTime = ji.value("fileTime", (uint64_t)0);
                    if (ji.contains("pidl") && ji["pidl"].is_string())
                        m.pidl = HexToBytes(ji["pidl"].get<std::string>());
                    icons[m.uid] = std::move(m);
                    if (m.uid >= ws.nextUid) ws.nextUid = m.uid + 1;
                } catch (...) { /* 单条坏记录跳过，不整体失败 */ }
            }
        }

        ws.dock.visible = false;
        ws.dock.items.clear();
        if (j.contains("dock") && j["dock"].is_object()) {
            const auto& jd = j["dock"];
            if (jd.contains("visible") && jd["visible"].is_boolean())
                ws.dock.visible = jd["visible"].get<bool>();
            if (jd.contains("items") && jd["items"].is_array()) {
                for (const auto& ju : jd["items"]) {
                    if (ws.dock.items.size() >= 100) break;
                    if (ju.is_number_unsigned())
                        ws.dock.items.push_back(ju.get<IconUid>());
                }
            }
        }

        ws.fences.clear();
        if (j.contains("fences") && j["fences"].is_array()) {
            for (const auto& jf : j["fences"]) {
                if (!jf.is_object() || ws.fences.size() >= kMaxFences) continue;
                try {
                    Fence f;
                    f.id = jf.at("id").get<uint32_t>();
                    if (f.id == 0) continue;
                    f.title = FromUtf8(jf.value("title", std::string("栅栏")));
                    if (jf.contains("collapsed") && jf["collapsed"].is_boolean())
                        f.collapsed = jf["collapsed"].get<bool>();
                    f.monitorDevice = FromUtf8(jf.value("monitor", std::string()));
                    if (jf.contains("zSeq") && jf["zSeq"].is_number_integer())
                        f.zSeq = jf["zSeq"].get<int32_t>();
                    if (jf.contains("style")) f.style = StyleFrom(jf["style"], ws.defaultStyle);

                    const auto mon = FindMonitor(f.monitorDevice);
                    if (jf.contains("posDip") && jf["posDip"].is_object()) {
                        f.posPx = {
                            mon.workArea.left +
                                MonitorUtil::DipToPx((float)jf["posDip"].value("x", 24.0), mon.dpiX),
                            mon.workArea.top +
                                MonitorUtil::DipToPx((float)jf["posDip"].value("y", 24.0), mon.dpiX)};
                    }
                    if (jf.contains("sizeDip") && jf["sizeDip"].is_object()) {
                        f.sizePx = {
                            (LONG)ClampSize(MonitorUtil::DipToPx(
                                (float)jf["sizeDip"].value("w", 280.0), mon.dpiX)),
                            (LONG)ClampSize(MonitorUtil::DipToPx(
                                (float)jf["sizeDip"].value("h", 320.0), mon.dpiX))};
                    }
                    if (jf.contains("collapsedSizeDip") && jf["collapsedSizeDip"].is_object()) {
                        f.collapsedSizePx = {
                            (LONG)ClampSize(MonitorUtil::DipToPx(
                                (float)jf["collapsedSizeDip"].value("w", 280.0), mon.dpiX)),
                            (LONG)ClampCollapsedHeight(MonitorUtil::DipToPx(
                                (float)jf["collapsedSizeDip"].value("h", 40.0), mon.dpiX))};
                    } else {
                        f.collapsedSizePx = {f.sizePx.cx, (LONG)ClampCollapsedHeight(
                            MonitorUtil::DipToPx(40.0f, mon.dpiX))};
                    }
                    if (jf.contains("scrollPx") && jf["scrollPx"].is_object()) {
                        f.scrollOffset = {
                            (LONG)std::max(0, MonitorUtil::DipToPx(
                                (float)jf["scrollPx"].value("x", 0.0), mon.dpiX)),
                            (LONG)std::max(0, MonitorUtil::DipToPx(
                                (float)jf["scrollPx"].value("y", 0.0), mon.dpiX))};
                    }

                    if (jf.contains("items") && jf["items"].is_array()) {
                        for (const auto& ju : jf["items"]) {
                            if (f.items.size() >= kMaxItems) break;
                            if (ju.is_number_unsigned())
                                f.items.push_back(ju.get<IconUid>());
                        }
                    }
                    if (f.id >= ws.nextFenceId) ws.nextFenceId = f.id + 1;
                    ws.fences.push_back(std::move(f));
                } catch (...) { /* 单条坏栅栏跳过 */ }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace winfence
