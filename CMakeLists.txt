cmake_minimum_required(VERSION 4.0)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)

project(bare_win_ui C CXX)

option(BARE_WIN_UI_TESTING "Build adapter-only native test controls" OFF)

fetch_package("github:holepunchto/bare@1.29.4")
fetch_package("github:holepunchto/libpath")

include(cmake/windows-app-sdk.cmake)

if(BARE_WIN_UI_SELF_CONTAINED)
  find_program(
    BARE_WIN_UI_NODE_EXECUTABLE
    NAMES node node.exe
    REQUIRED
  )
endif()

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

if(BARE_WIN_UI_SELF_CONTAINED)
  target_compile_definitions(
    bare_runtime
    PRIVATE
      BARE_WIN_UI_SELF_CONTAINED
      MICROSOFT_WINDOWSAPPSDK_AUTOINITIALIZE_UNDOCKEDREGFREEWINRT
      MICROSOFT_WINDOWSAPPSDK_UNDOCKEDREGFREEWINRT_AUTO_INITIALIZE
  )

  set(BARE_WIN_UI_AUTO_INITIALIZER_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/windows-app-sdk-auto-initializer"
  )
  set(BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER_SOURCE
    "${BARE_WIN_UI_AUTO_INITIALIZER_DIR}/WindowsAppRuntimeAutoInitializer.cpp"
  )
  set(BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER_SOURCE
    "${BARE_WIN_UI_AUTO_INITIALIZER_DIR}/UndockedRegFreeWinRT-AutoInitializer.cpp"
  )

  add_custom_command(
    OUTPUT
      ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER_SOURCE}
      ${BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER_SOURCE}
    COMMAND
      ${CMAKE_COMMAND} -E make_directory "${BARE_WIN_UI_AUTO_INITIALIZER_DIR}"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER}"
      "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER_SOURCE}"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "${BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER}"
      "${BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER_SOURCE}"
    DEPENDS
      WindowsAppSDK_Foundation
    VERBATIM
  )

  add_custom_target(
    WindowsAppSDKAutoInitializer
    DEPENDS
      ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER_SOURCE}
      ${BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER_SOURCE}
  )

  add_dependencies(bare_runtime WindowsAppSDKAutoInitializer)

  set_source_files_properties(
    ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER_SOURCE}
    ${BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER_SOURCE}
    PROPERTIES
      GENERATED TRUE
      SKIP_PRECOMPILE_HEADERS ON
  )

  target_sources(
    bare_runtime
    PRIVATE
      ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER_SOURCE}
      ${BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER_SOURCE}
  )
endif()

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

if(BARE_WIN_UI_SELF_CONTAINED)
  add_dependencies(bare_runtime WindowsAppRuntimeFiles)
  target_link_libraries(bare_runtime PRIVATE WindowsAppRuntime)
endif()

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

if(BARE_WIN_UI_SELF_CONTAINED)
  set(BARE_WIN_UI_RUNTIME_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/runtime.manifest"
  )

  add_custom_command(
    OUTPUT "${BARE_WIN_UI_RUNTIME_MANIFEST}"
    COMMAND
      "${BARE_WIN_UI_NODE_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/self-contained-runtime-manifest.js"
      generate
      "${CMAKE_CURRENT_SOURCE_DIR}/lib/runtime.manifest"
      "${BARE_WIN_UI_RUNTIME_MANIFEST}"
      "${version}"
      "${WindowsAppSDK_Foundation_SOURCE_DIR}/runtimes-framework/package.appxfragment"
      "${WindowsAppSDK_InteractiveExperiences_SOURCE_DIR}/runtimes-framework/package.appxfragment"
      "${WindowsAppSDK_WinUI_SOURCE_DIR}/runtimes-framework/package.appxfragment"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/self-contained-runtime-manifest.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/lib/runtime.manifest"
      WindowsAppSDK_Foundation
      WindowsAppSDK_InteractiveExperiences
      WindowsAppSDK_WinUI
    VERBATIM
  )

  add_custom_target(
    WindowsAppSDKRuntimeManifest
    DEPENDS "${BARE_WIN_UI_RUNTIME_MANIFEST}"
  )

  add_dependencies(bare_runtime WindowsAppSDKRuntimeManifest)

  set_source_files_properties(
    "${BARE_WIN_UI_RUNTIME_MANIFEST}"
    PROPERTIES GENERATED TRUE
  )
else()
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime.manifest"
    CONTENT "${manifest}"
    NEWLINE_STYLE WIN32
  )
  set(BARE_WIN_UI_RUNTIME_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/runtime.manifest"
  )
endif()

target_sources(
  bare_runtime
  PRIVATE
    "${BARE_WIN_UI_RUNTIME_MANIFEST}"
)

bare_target(target)

install(TARGETS bare_runtime RUNTIME DESTINATION ${target})

install(
  FILES
    $<TARGET_FILE:WebView2_Core>
  DESTINATION ${target}/bare
)

if(BARE_WIN_UI_SELF_CONTAINED)
  install(
    DIRECTORY
      "${WindowsAppSDK_Foundation_SOURCE_DIR}/runtimes-framework/win-x64/native/"
    DESTINATION ${target}/bare
  )

  install(
    DIRECTORY
      "${WindowsAppSDK_InteractiveExperiences_SOURCE_DIR}/runtimes-framework/win-x64/native/"
    DESTINATION ${target}/bare
  )

  install(
    DIRECTORY
      "${WindowsAppSDK_WinUI_SOURCE_DIR}/runtimes-framework/win-x64/native/"
    DESTINATION ${target}/bare
  )

  install(
    DIRECTORY
      "${WindowsAppSDK_Foundation_SOURCE_DIR}/metadata/"
    DESTINATION ${target}/bare
    FILES_MATCHING
      PATTERN "*.winmd"
  )

  install(
    DIRECTORY
      "${WindowsAppSDK_InteractiveExperiences_SOURCE_DIR}/metadata/10.0.18362.0/"
    DESTINATION ${target}/bare/10.0.18362.0
    FILES_MATCHING
      PATTERN "*.winmd"
  )

  install(
    DIRECTORY
      "${WindowsAppSDK_WinUI_SOURCE_DIR}/metadata/"
    DESTINATION ${target}/bare
    FILES_MATCHING
      PATTERN "*.winmd"
  )

  file(TO_CMAKE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/self-contained-runtime.js"
    BARE_WIN_UI_MANIFEST_TOOL
  )
  file(TO_CMAKE_PATH
    "${BARE_WIN_UI_NODE_EXECUTABLE}"
    BARE_WIN_UI_NODE_EXECUTABLE
  )

  configure_file(
    cmake/self-contained-runtime-install.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/self-contained-runtime-install.cmake"
    @ONLY
  )

  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/self-contained-runtime-install.cmake")
else()
  install(
    FILES
      $<TARGET_FILE:WindowsAppSDK_Bootstrap>
    DESTINATION ${target}/bare
  )
endif()
