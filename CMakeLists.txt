cmake_minimum_required(VERSION 3.20)
project(PES21FX LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # sin depender del runtime de VC instalado

include(FetchContent)
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        v1.3.3)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.5)
FetchContent_Declare(directxtk
  GIT_REPOSITORY https://github.com/microsoft/DirectXTK.git
  GIT_TAG        oct2024)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
  FetchContent_Populate(minhook)
endif()
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()
FetchContent_GetProperties(directxtk)
if(NOT directxtk_POPULATED)
  FetchContent_Populate(directxtk)
endif()

add_library(minhook STATIC
  ${minhook_SOURCE_DIR}/src/buffer.c
  ${minhook_SOURCE_DIR}/src/hook.c
  ${minhook_SOURCE_DIR}/src/trampoline.c
  ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

add_library(PES21FX SHARED
  src/main.cpp
  src/d3d11_proxy.cpp
  src/d3d11_exports.asm
  src/d3d11.def
  src/config.cpp
  src/hooks.cpp
  src/overlay.cpp
  src/presets.cpp
  src/catalog.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${directxtk_SOURCE_DIR}/Src/DDSTextureLoader.cpp)
target_include_directories(PES21FX PRIVATE src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
  ${directxtk_SOURCE_DIR}/Inc ${directxtk_SOURCE_DIR}/Src)
target_compile_definitions(PES21FX PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(PES21FX PRIVATE minhook dxgi dxguid)
set_target_properties(PES21FX PROPERTIES OUTPUT_NAME "d3d11" PREFIX "")
