# WinFence

**[English](README.en.md) | [简体中文](README.md)**

---

Open-source desktop icon fences (à la Stardock Fences) plus a macOS-style Dock for Windows 10/11 — group your desktop icons into draggable, collapsible acrylic-glass rounded containers, or pin them to a translucent black dock at the bottom of the screen with hover magnification and single-click launch. **Your real files are never moved.**

![status](https://img.shields.io/badge/status-v0.1.0%20M6-blue) ![license](https://img.shields.io/badge/license-MIT-green) ![c++](https://img.shields.io/badge/C%2B%2B-Win32%20%2F%20D2D%20%2F%20DComp-orange)

## Features

### Fences
- ✅ Multiple acrylic rounded translucent containers (Win11 `DWMSBT` system backdrop, graceful fallback on older systems), layered above desktop icons but below app windows
- ✅ Drag to move / edge resize / double-click to roll up (150 ms animation) / content scrolling
- ✅ Drop icons in from the desktop or Explorer (**virtual grouping** — files stay where they are)
- ✅ Double-click to open, right-click to remove; drag between fences, live reorder, drop-target highlight
- ✅ Inline fence rename; icon hover highlight
- ✅ Crisp 256 px icons (`SHIL_JUMBO`) with full CJK filename support

### Dock (macOS-style)
- ✅ Translucent black rounded bar, bottom-center, always on top
- ✅ Hover magnification + name bubble; single-click launch
- ✅ Two-way drag with fences, in-dock reorder, hide on demand

### System integration
- ✅ JSON layout persistence (atomic writes + `.bak` rotation, monitor-normalized coordinates, multi-DPI / multi-monitor safe)
- ✅ Live desktop sync: create / delete / rename automatically reconciled (renames migrate membership, orphans kept 7 days)
- ✅ Tray menu + settings dialog in Chinese (backdrop, opacity, corner radius, Dock toggle, hide desktop icons, autostart)
- 🚧 Planned: AI grouping (DeepSeek API / local Ollama, manual trigger only, filenames only)

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

Or grab a prebuilt binary from the GitHub Actions artifacts.

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) — Full design: architecture / data structures / core flows / Windows pitfall checklist (Chinese)
- [docs/CREDITS.md](docs/CREDITS.md) — Reference projects & license discipline

## License

MIT — see [LICENSE](LICENSE). Third-party dependency (nlohmann/json, MIT) and
reference projects are listed in [docs/CREDITS.md](docs/CREDITS.md).
