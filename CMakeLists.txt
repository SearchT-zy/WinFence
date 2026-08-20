cmake_minimum_required(VERSION 3.24)
project(WinFence VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 静态链接 CRT（Release /MT）：绿色单文件分发，免 VC Redist
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(WINFENCE_ENABLE_UNDOCUMENTED_ACCENT
       "Use undocumented SetWindowCompositionAttribute as acrylic fallback (AV risk)" OFF)
option(WINFENCE_USE_FETCHCONTENT
       "Fetch nlohmann_json via FetchContent instead of the vendored header" OFF)

# ---------------------------------------------------------------- 依赖
if(WINFENCE_USE_FETCHCONTENT)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
  set(WINFENCE_JSON_TARGET nlohmann_json::nlohmann_json)
else()
  # vendored 单头文件：third_party/nlohmann/json.hpp
  add_library(winfence_json INTERFACE)
  target_include_directories(winfence_json INTERFACE third_party)
  set(WINFENCE_JSON_TARGET winfence_json)
endif()

# ---------------------------------------------------------------- 目标
add_executable(winfence
  WIN32
  resources/winfence.rc
  src/app/main.cpp
  src/app/App.cpp
  src/app/SingleInstance.cpp
  src/shell/DesktopAnchor.cpp
  src/shell/MonitorUtil.cpp
  src/platform/DwmBackdrop.cpp
  src/platform/WinUtil.cpp
  src/platform/PathGuard.cpp
  src/platform/DesktopIcons.cpp
  src/persist/ConfigStore.cpp
  src/persist/JsonCodec.cpp
  src/core/FenceService.cpp
  src/core/DesktopScanner.cpp
  src/core/DesktopWatcher.cpp
  src/core/Reconciler.cpp
  src/shell/ShellEnumerator.cpp
  src/shell/IconExtractor.cpp
  src/ui/Compositor.cpp
  src/ui/IconCache.cpp
  src/ui/DropTarget.cpp
  src/ui/SettingsDialog.cpp
  src/ui/OrganizeHint.cpp
  src/ui/AiPreviewDialog.cpp
  src/ui/DockWindow.cpp
  src/ai/AiClient.cpp
  src/ai/AiGrouping.cpp
  src/ui/FenceRenderer.cpp
  src/ui/FenceWindow.cpp
)

target_include_directories(winfence PRIVATE src)

target_compile_definitions(winfence PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
  WINVER=0x0A00 _WIN32_WINNT=0x0A00
  $<$<BOOL:${WINFENCE_ENABLE_UNDOCUMENTED_ACCENT}>:WINFENCE_ENABLE_UNDOCUMENTED_ACCENT>)

target_compile_options(winfence PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive- /Zc:__cplusplus /MP>)

# manifest 由 winfence.rc 内嵌（1 RT_MANIFEST），禁用链接器默认生成的第二份，
# 否则 CVTRES 报 CVT1100"资源重复 MANIFEST"
if(MSVC)
  target_link_options(winfence PRIVATE "/MANIFEST:NO")
endif()

target_link_libraries(winfence PRIVATE
  ${WINFENCE_JSON_TARGET}
  d2d1 dxgi dxguid d3d11 dcomp dwmapi          # 渲染与合成
  dwrite windowscodecs                          # 文本与位图转换
  shell32 shcore ole32 oleaut32 uuid            # Shell / OLE / DPI
  user32 gdi32 advapi32 shlwapi comctl32 winhttp crypt32)

set_target_properties(winfence PROPERTIES OUTPUT_NAME "WinFence")

install(TARGETS winfence RUNTIME DESTINATION .)
