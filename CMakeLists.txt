cmake_minimum_required(VERSION 4.0)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)

project(bare_win_ui C CXX)

option(BARE_WIN_UI_TESTING "Build adapter-only native test controls" OFF)

fetch_package("github:holepunchto/bare@1.29.4")
fetch_package("github:holepunchto/libpath")

include(cmake/windows-app-sdk.cmake)

add_bare_module(bare_win_ui)

target_sources(
  ${bare_win_ui}
  PRIVATE
    binding.cc
)

if(BARE_WIN_UI_TESTING)
  target_compile_definitions(${bare_win_ui} PRIVATE BARE_WIN_UI_TESTING)
endif()

target_compile_options(
  ${bare_win_ui}
  PRIVATE
    /GR-
)

target_precompile_headers(
  ${bare_win_ui}
  PRIVATE
    lib/windows-app-sdk.h
)

target_link_libraries(
  ${bare_win_ui}
  PUBLIC
    WindowsAppSDK
    shell32
)

add_executable(bare_runtime)

target_sources(
  bare_runtime
  PRIVATE
    lib/runtime.cc
)

target_precompile_headers(bare_runtime REUSE_FROM ${bare_win_ui})

set_target_properties(
  bare_runtime
  PROPERTIES
  CXX_STANDARD 20
  OUTPUT_NAME bare
  ENABLE_EXPORTS ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_link_libraries(
  bare_runtime
  PUBLIC
    $<LINK_LIBRARY:WHOLE_ARCHIVE,bare_static>
  PRIVATE
    rlimit_static
    path_static
  PRIVATE
    WindowsAppSDK
)

target_link_options(
  bare_runtime
  PRIVATE
    /subsystem:windows
    /entry:mainCRTStartup
)

file(READ "package.json" pkg)

string(JSON version GET "${pkg}" version)

file(READ "lib/runtime.manifest" manifest)

string(CONFIGURE "${manifest}" manifest)

file(GENERATE OUTPUT "runtime.manifest" CONTENT "${manifest}" NEWLINE_STYLE WIN32)

target_sources(
  bare_runtime
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/runtime.manifest"
)

bare_target(target)

install(TARGETS bare_runtime RUNTIME DESTINATION ${target})

install(
  FILES
    $<TARGET_FILE:WebView2_Core>
    $<TARGET_FILE:WindowsAppSDK_Bootstrap>
  DESTINATION ${target}/bare
)
