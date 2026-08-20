include(ExternalProject)

function(fetch_nuget_package name version result)
  set(one_value_keywords
    SHA256
  )
  set(multi_value_keywords
    BUILD_COMMAND
  )

  cmake_parse_arguments(
    PARSE_ARGV 3 ARGV "" "${one_value_keywords}" "${multi_value_keywords}"
  )

  if(NOT ARGV_SHA256)
    message(FATAL_ERROR "NuGet package '${name} ${version}' must have a pinned SHA-256")
  endif()

  string(LENGTH "${ARGV_SHA256}" sha256_length)
  if(NOT sha256_length EQUAL 64 OR NOT ARGV_SHA256 MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR "NuGet package '${name} ${version}' has an invalid SHA-256")
  endif()

  set(prefix "${CMAKE_CURRENT_BINARY_DIR}/_nuget/${name}/${version}")

  set(target "${name}_${version}")

  set(${result} ${target} PARENT_SCOPE)

  set(${result}_SOURCE_DIR "${prefix}/src/${target}" PARENT_SCOPE)
  set(${result}_BINARY_DIR "${prefix}/src/${target}-build" PARENT_SCOPE)

  if(TARGET ${target})
    return()
  endif()

  if(NOT ARGV_BUILD_COMMAND)
    set(ARGV_BUILD_COMMAND "")
  endif()

  set(package_url "https://www.nuget.org/api/v2/package/${name}/${version}")
  set(package_hash_arguments)
  if(ARGV_SHA256)
    list(APPEND package_hash_arguments URL_HASH "SHA256=${ARGV_SHA256}")
  endif()
  if(DEFINED ENV{BARE_WIN_UI_NUGET_CACHE})
    file(TO_CMAKE_PATH "$ENV{BARE_WIN_UI_NUGET_CACHE}" package_cache)
    set(cached_package "${package_cache}/${name}.${version}.nupkg")
    if(EXISTS "${cached_package}")
      if(NOT ARGV_SHA256)
        message(STATUS "Ignoring cached NuGet package without a pinned SHA-256: ${cached_package}")
      else()
        file(SHA256 "${cached_package}" cached_package_sha256)
        if("${cached_package_sha256}" STREQUAL "${ARGV_SHA256}")
          message(STATUS "Using verified cached NuGet package: ${cached_package}")
          set(package_url "${cached_package}")
        else()
          message(WARNING "Ignoring cached NuGet package with invalid SHA-256: ${cached_package}")
        endif()
      endif()
    endif()
  endif()

  ExternalProject_Add(
    ${target}
    PREFIX "${prefix}"
    URL "${package_url}"
    ${package_hash_arguments}
    CONFIGURE_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_COMMAND ${ARGV_BUILD_COMMAND}
    EXCLUDE_FROM_ALL
    LOG_DOWNLOAD ON
    LOG_UPDATE ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
    LOG_MERGED_STDOUTERR ON
    LOG_OUTPUT_ON_FAILURE ON
  )
endfunction()

if(MSVC AND CMAKE_GENERATOR_PLATFORM)
  set(arch ${CMAKE_GENERATOR_PLATFORM})
elseif(CMAKE_SYSTEM_PROCESSOR)
  set(arch ${CMAKE_SYSTEM_PROCESSOR})
else()
  set(arch ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()

string(TOLOWER "${arch}" arch)

if(arch MATCHES "arm64|aarch64")
  set(arch "arm64")
elseif(arch MATCHES "x64|x86_64|amd64")
  set(arch "x64")
else()
  message(FATAL_ERROR "Unsupported architecture '${arch}'")
endif()

fetch_nuget_package(
  Microsoft.Windows.CppWinRT
  2.0.250303.1
  CppWinRT
  SHA256 955e3051b35db1c00177ed14ab6b7b995c3b32a53fdb6c184ea53b1dba66e439
  BUILD_COMMAND "<SOURCE_DIR>/bin/cppwinrt.exe" -in local -output "<BINARY_DIR>/include"
)

add_executable(CppWinRT IMPORTED GLOBAL)

add_dependencies(CppWinRT ${CppWinRT})

set_target_properties(
  CppWinRT
  PROPERTIES
  IMPORTED_LOCATION "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
)

# Make sure our C++/WinRT headers take precendence over the system provided
# Windows SDK headers.
include_directories(BEFORE SYSTEM "${CppWinRT_BINARY_DIR}/include")

fetch_nuget_package(
  Microsoft.Web.WebView2
  1.0.3595.46
  WebView2
  SHA256 f448c20859199ecd846a7d693710d143a8cd019ad1dcdc4cd6332d842d871054
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -in "<SOURCE_DIR>/lib"
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WebView2} CppWinRT)

add_library(WebView2 INTERFACE)

add_dependencies(WebView2 ${WebView2})

target_include_directories(
  WebView2
  INTERFACE
    "${WebView2_SOURCE_DIR}/include"
    "${WebView2_BINARY_DIR}/include"
)

add_library(WebView2_Core SHARED IMPORTED GLOBAL)

add_dependencies(WebView2_Core WebView2)

set_target_properties(
  WebView2_Core
  PROPERTIES
  IMPORTED_LOCATION "${WebView2_SOURCE_DIR}/runtimes/win-${arch}/native_uap/Microsoft.Web.WebView2.Core.dll"
)

fetch_nuget_package(
  Microsoft.WindowsAppSDK.Base
  1.8.250831001
  WindowsAppSDK_Base
  SHA256 76c20ad89f166ef204cd698648d1b160ded1d71607aeebcfb8303e1a539a4937
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WindowsAppSDK_Base} CppWinRT)

add_library(WindowsAppSDK_Base INTERFACE)

add_dependencies(WindowsAppSDK_Base ${WindowsAppSDK_Base})

target_include_directories(
  WindowsAppSDK_Base
  INTERFACE
    "${WindowsAppSDK_Base_SOURCE_DIR}/include"
    "${WindowsAppSDK_Base_BINARY_DIR}/include"
)

fetch_nuget_package(
  Microsoft.WindowsAppSDK.InteractiveExperiences
  1.8.251104001
  WindowsAppSDK_InteractiveExperiences
  SHA256 228887d702976fc660b549d7e34a9ae5f58fa5856e02d1c6095254b5eb566423
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -in "<SOURCE_DIR>/metadata/10.0.18362.0"
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WindowsAppSDK_InteractiveExperiences} CppWinRT)

add_library(WindowsAppSDK_InteractiveExperiences INTERFACE)

add_dependencies(WindowsAppSDK_InteractiveExperiences ${WindowsAppSDK_InteractiveExperiences})

target_include_directories(
  WindowsAppSDK_InteractiveExperiences
  INTERFACE
    "${WindowsAppSDK_InteractiveExperiences_SOURCE_DIR}/include"
    "${WindowsAppSDK_InteractiveExperiences_BINARY_DIR}/include"
)

fetch_nuget_package(
  Microsoft.WindowsAppSDK.Runtime
  1.8.251106002
  WindowsAppSDK_Runtime
  SHA256 6615d3073104c93840492c94480d2ddadb303f401f775c9bcab2f0d9134df8f3
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WindowsAppSDK_Runtime} CppWinRT)

add_library(WindowsAppSDK_Runtime INTERFACE)

add_dependencies(WindowsAppSDK_Runtime ${WindowsAppSDK_Runtime})

target_include_directories(
  WindowsAppSDK_Runtime
  INTERFACE
    "${WindowsAppSDK_Runtime_SOURCE_DIR}/include"
    "${WindowsAppSDK_Runtime_BINARY_DIR}/include"
)

fetch_nuget_package(
  Microsoft.WindowsAppSDK.Foundation
  1.8.251104000
  WindowsAppSDK_Foundation
  SHA256 45321e61a49c818f5d066f3d95b60144cc20ab60e17169e5cc6dd317d0348157
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -ref "${WindowsAppSDK_InteractiveExperiences_SOURCE_DIR}/metadata/10.0.18362.0"
    -in "<SOURCE_DIR>/metadata"
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WindowsAppSDK_Foundation} CppWinRT WindowsAppSDK_InteractiveExperiences)

add_library(WindowsAppSDK_Foundation INTERFACE)

add_dependencies(WindowsAppSDK_Foundation ${WindowsAppSDK_Foundation})

target_include_directories(
  WindowsAppSDK_Foundation
  INTERFACE
    "${WindowsAppSDK_Foundation_SOURCE_DIR}/include"
    "${WindowsAppSDK_Foundation_BINARY_DIR}/include"
)

if(arch STREQUAL "x64")
  set(BARE_WIN_UI_SELF_CONTAINED TRUE)

  set(
    BARE_WIN_UI_WINDOWS_APP_RUNTIME_AUTO_INITIALIZER
    "${WindowsAppSDK_Foundation_SOURCE_DIR}/include/WindowsAppRuntimeAutoInitializer.cpp"
  )
  set(
    BARE_WIN_UI_UNDOCKED_REG_FREE_WINRT_AUTO_INITIALIZER
    "${WindowsAppSDK_Foundation_SOURCE_DIR}/include/UndockedRegFreeWinRT-AutoInitializer.cpp"
  )

  set(
    BARE_WIN_UI_WINDOWS_APP_RUNTIME_DLL
    "${WindowsAppSDK_Foundation_SOURCE_DIR}/runtimes-framework/win-x64/native/Microsoft.WindowsAppRuntime.dll"
  )
  set(
    BARE_WIN_UI_WINDOWS_APP_RUNTIME_LIB
    "${WindowsAppSDK_Foundation_SOURCE_DIR}/lib/native/x64/Microsoft.WindowsAppRuntime.lib"
  )
  set(
    BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/windows-app-sdk-runtime"
  )
  set(
    BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DLL
    "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DIR}/Microsoft.WindowsAppRuntime.dll"
  )
  set(
    BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_LIB
    "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DIR}/Microsoft.WindowsAppRuntime.lib"
  )

  add_custom_command(
    OUTPUT
      ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DLL}
      ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_LIB}
    COMMAND
      ${CMAKE_COMMAND} -E make_directory "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DIR}"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_DLL}"
      "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DLL}"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_LIB}"
      "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_LIB}"
    DEPENDS ${WindowsAppSDK_Foundation}
    VERBATIM
  )

  add_custom_target(
    WindowsAppRuntimeFiles
    DEPENDS
      ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DLL}
      ${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_LIB}
  )

  add_library(WindowsAppRuntime SHARED IMPORTED GLOBAL)

  add_dependencies(WindowsAppRuntime WindowsAppRuntimeFiles)

  set_target_properties(
    WindowsAppRuntime
    PROPERTIES
    IMPORTED_LOCATION "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_DLL}"
    IMPORTED_IMPLIB "${BARE_WIN_UI_WINDOWS_APP_RUNTIME_BUILD_LIB}"
  )
else()
  set(BARE_WIN_UI_SELF_CONTAINED FALSE)

  add_library(WindowsAppSDK_Bootstrap SHARED IMPORTED GLOBAL)

  add_dependencies(WindowsAppSDK_Bootstrap WindowsAppSDK_Foundation)

  set_target_properties(
    WindowsAppSDK_Bootstrap
    PROPERTIES
    IMPORTED_LOCATION "${WindowsAppSDK_Foundation_SOURCE_DIR}/runtimes/win-${arch}/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
    IMPORTED_IMPLIB "${WindowsAppSDK_Foundation_SOURCE_DIR}/lib/native/${arch}/Microsoft.WindowsAppRuntime.Bootstrap.lib"
  )
endif()

fetch_nuget_package(
  Microsoft.WindowsAppSDK.Widgets
  1.8.250904007
  WindowsAppSDK_Widgets
  SHA256 b15c6d06c599fb2a8d9a0582546d79fc68925ec3906689c514770ccf7e0f3457
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -in "<SOURCE_DIR>/metadata"
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WindowsAppSDK_Widgets} CppWinRT)

add_library(WindowsAppSDK_Widgets INTERFACE)

add_dependencies(WindowsAppSDK_Widgets ${WindowsAppSDK_Widgets})

target_include_directories(
  WindowsAppSDK_Widgets
  INTERFACE
    "${WindowsAppSDK_Widgets_SOURCE_DIR}/include"
    "${WindowsAppSDK_Widgets_BINARY_DIR}/include"
)

fetch_nuget_package(
  Microsoft.WindowsAppSDK.WinUI
  1.8.251105000
  WindowsAppSDK_WinUI
  SHA256 ac8a8680b957598b1ceb696ca91168585735036488654c34e8f072574b351347
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -ref "${WebView2_SOURCE_DIR}/lib"
    -ref "${WindowsAppSDK_InteractiveExperiences_SOURCE_DIR}/metadata/10.0.18362.0"
    -ref "${WindowsAppSDK_Foundation_SOURCE_DIR}/metadata"
    -in "<SOURCE_DIR>/metadata"
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WindowsAppSDK_WinUI} CppWinRT WebView2 WindowsAppSDK_InteractiveExperiences WindowsAppSDK_Foundation)

add_library(WindowsAppSDK_WinUI INTERFACE)

add_dependencies(WindowsAppSDK_WinUI ${WindowsAppSDK_WinUI})

target_include_directories(
  WindowsAppSDK_WinUI
  INTERFACE
    "${WindowsAppSDK_WinUI_SOURCE_DIR}/include"
    "${WindowsAppSDK_WinUI_BINARY_DIR}/include"
)

fetch_nuget_package(
  Microsoft.WindowsAppSDK
  1.8.251106002
  WindowsAppSDK
  SHA256 715b600661b12f77b23d4a0f9c303fdaed5e7eee05cc97c0289c331416835f64
  BUILD_COMMAND
    "${CppWinRT_SOURCE_DIR}/bin/cppwinrt.exe"
    -ref sdk
    -output "<BINARY_DIR>/include"
)

add_dependencies(${WindowsAppSDK} CppWinRT)

add_library(WindowsAppSDK INTERFACE)

add_dependencies(WindowsAppSDK ${WindowsAppSDK})

target_include_directories(
  WindowsAppSDK
  INTERFACE
    "${WindowsAppSDK_SOURCE_DIR}/include"
    "${WindowsAppSDK_BINARY_DIR}/include"
)

target_link_libraries(
  WindowsAppSDK
  INTERFACE
    WebView2
    WindowsApp
    WindowsAppSDK_Base
    WindowsAppSDK_InteractiveExperiences
    WindowsAppSDK_Runtime
    WindowsAppSDK_Foundation
    WindowsAppSDK_Widgets
    WindowsAppSDK_WinUI
)
