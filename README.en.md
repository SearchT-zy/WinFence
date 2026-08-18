# WinFence

**[English](README.en.md) | [简体中文](README.md)**

---

Open-source desktop icon fences (à la Stardock Fences) plus a macOS-style Dock for Windows 10/11 — group your desktop icons into draggable, resizable, collapsible tech-style (HUD) glass containers, or pin them to a translucent dock with hover magnification and single-click launch. **Your real files are never moved.**

![release](https://img.shields.io/badge/release-v0.1.0-blue) ![license](https://img.shields.io/badge/license-MIT-green) ![c++](https://img.shields.io/badge/C%2B%2B-Win32%20%2F%20D2D%20%2F%20DComp-orange)

**[⬇ Download v0.1.0](https://github.com/SearchT-zy/WinFence/releases/latest)** (portable zip, no dependencies)

## Features

### Fences
- ✅ Tech/HUD visual language: dark blue-black gradient, neon dual-stroke border with outer glow, corner brackets
- ✅ Acrylic glass (Win11 `DWMSBT` system backdrop, graceful fallback), above desktop icons, below app windows
- ✅ Move / **resize from all 8 edges** / double-click roll-up (150 ms animation) / content scrolling
- ✅ "＋" button on the title bar to create a fence instantly; inline rename; hover highlight
- ✅ Global hotkey **Ctrl+Alt+N** (auto-fallback to Ctrl+Alt+Shift+N if taken)
- ✅ Drop icons in from the desktop or Explorer (**virtual grouping**); drag between fences; live reorder
- ✅ Double-click to open, right-click to remove; crisp 256 px icons (`SHIL_JUMBO`) with full CJK filename support

### Dock (macOS-style)
- ✅ Translucent black rounded bar (same tech style), bottom-center, always on top
- ✅ Hover magnification + name bubble; single-click launch
- ✅ Two-way drag with fences, in-dock reorder, right-click remove/hide

### AI grouping
- ✅ Dual backends: **DeepSeek cloud** (API key stored DPAPI-encrypted) / **local Ollama** (data never leaves the machine)
- ✅ **Manual trigger only** (the "AI 整理" button) — nothing runs automatically
- ✅ Privacy: uploads only filenames/extensions/kinds — **no paths, no file contents**
- ✅ Strict JSON validation: hallucinated or duplicated uids invalidate the whole plan — never partially applied
- ✅ **Preview before apply**; automatic backup before applying; one-click reset

### System integration
- ✅ JSON layout persistence (atomic writes + `.bak` rotation, monitor-normalized coordinates, multi-DPI / multi-monitor safe)
- ✅ Live desktop sync: create / delete / rename automatically reconciled (renames migrate membership, orphans kept 7 days)
- ✅ **Fully D2D-drawn settings panel** (pill buttons / glowing sliders / toggles, live apply) + tray menu
- ✅ Hide desktop icons (clean desktop), autostart

### Safety by design

WinFence **does not inject into or hook Explorer** and uses only public Windows APIs
(enforced by a banned-API audit in CI). Icon membership lives solely in WinFence's own
JSON config — the codebase contains no calls that delete or move your desktop files.
See the [design document](docs/DESIGN.md) (Chinese) for details.

## Build & Run

Requires Visual Studio 2022, Windows 10 SDK (10.0.22621+), and CMake ≥ 3.24.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# Output: build\Release\WinFence.exe (static CRT, portable single file)
```

Or grab a prebuilt binary from [Releases](https://github.com/SearchT-zy/WinFence/releases/latest).

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) — Full design: architecture / data structures / core flows / Windows pitfall checklist (Chinese)
- [docs/CREDITS.md](docs/CREDITS.md) — Reference projects & license discipline

## License

MIT — see [LICENSE](LICENSE). Third-party dependency (nlohmann/json, MIT) and
reference projects are listed in [docs/CREDITS.md](docs/CREDITS.md).
