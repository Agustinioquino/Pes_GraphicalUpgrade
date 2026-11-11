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
  src/lang.cpp
  src/presets.cpp
  src/catalog.cpp
  src/grass.cpp
  src/statecache.cpp
  src/menutest.cpp
  src/facegen.cpp
  src/inspect.cpp src/ssss.cpp src/headshadow.cpp src/pitchwear.cpp src/lod.cpp src/lightmeter.cpp src/gtao.cpp src/ssgi.cpp src/exedump.cpp src/asyncshot.cpp src/exepatch.cpp src/camhook.cpp src/soleditor.cpp src/musculo.cpp src/musculo_ui.cpp src/personajes.cpp src/fisico.cpp src/codewatch.cpp src/memwatch.cpp src/skintone.cpp src/glass.cpp src/ring.cpp src/cuerpo.cpp src/manos.cpp src/sunwatch.cpp src/solreloj.cpp src/grassdist.cpp src/brazos.cpp src/inflate.cpp src/keyblock.cpp src/expr.cpp src/exprbones.cpp src/faceinject.cpp src/facetess.cpp src/arrugasjug.cpp src/bocajug.cpp src/tiempo.cpp src/freecam.cpp src/caras_hd.cpp src/caramalla.cpp src/mouthmask.cpp src/gpumeter.cpp src/gani.cpp src/panel.cpp src/cmdlistguard.cpp src/relwatch.cpp src/cobertura.cpp src/rastro.cpp src/rastro_v4.cpp src/publico.cpp src/solluz.cpp src/motorlink.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${directxtk_SOURCE_DIR}/Src/DDSTextureLoader.cpp
  ${directxtk_SOURCE_DIR}/Src/ScreenGrab.cpp
  ${directxtk_SOURCE_DIR}/Src/WICTextureLoader.cpp)
target_include_directories(PES21FX PRIVATE src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
  ${directxtk_SOURCE_DIR}/Inc ${directxtk_SOURCE_DIR}/Src)
target_compile_definitions(PES21FX PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(PES21FX PRIVATE minhook dxgi dxguid ole32 uuid windowscodecs ws2_32)
set_target_properties(PES21FX PROPERTIES OUTPUT_NAME "d3d11" PREFIX "")
# (07/11) mapa del enlazador (d3d11.map, junto a la DLL): el medidor de VRAM lo lee para nombrar el archivo del mod que creo cada recurso
if(MSVC)
  target_link_options(PES21FX PRIVATE "/MAP")
endif()
