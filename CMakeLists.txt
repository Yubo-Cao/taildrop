cmake_minimum_required(VERSION 3.21)
project(TailDrop VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "TailDrop is a native Windows app; build it with MSVC (see build.ps1).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no VC++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /wd4100)
add_compile_options("$<$<CONFIG:Release>:/O2;/GL;/Gy>")
add_link_options("$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF>")

add_executable(TailDrop WIN32
  src/app/main.cpp
  src/app/app.cpp
  src/app/flyout.cpp
  src/app/toast.cpp
  src/app/dialogs.cpp
  src/app/ui.cpp
  src/app/json.cpp
  src/app/localapi.cpp
  src/app/util.cpp
  src/app/zip.cpp
  src/app/settings.cpp
  src/app/app.rc
  src/app/app.manifest)
target_link_libraries(TailDrop PRIVATE
  d2d1 dwrite dwmapi imm32 shcore comctl32 shlwapi uxtheme psapi ole32 oleaut32 shell32 user32 gdi32 advapi32)

add_library(TailDropShell SHARED
  src/shell/shell.cpp
  src/shell/shell.rc
  src/shell/shell.def)
target_link_libraries(TailDropShell PRIVATE shlwapi shell32 ole32 user32 advapi32)
