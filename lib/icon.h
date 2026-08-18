#ifndef BARE_WIN_UI_ICON_H
#define BARE_WIN_UI_ICON_H

#include <windows.h>

#include <string>
#include <vector>

static inline std::wstring
bare_win_ui_packaged_icon_path() {
  std::vector<wchar_t> executable(MAX_PATH);

  for (;;) {
    DWORD length = GetModuleFileNameW(
      nullptr,
      executable.data(),
      static_cast<DWORD>(executable.size())
    );
    if (length == 0) return L"";
    if (length < executable.size()) {
      std::wstring path(executable.data(), length);
      auto executable_separator = path.find_last_of(L"\\/");
      if (executable_separator == std::wstring::npos) return L"";
      auto app_separator = path.find_last_of(L"\\/", executable_separator - 1);
      if (app_separator == std::wstring::npos) return L"";

      auto icon = path.substr(0, app_separator) + L"\\Assets\\Logo.ico";
      auto attributes = GetFileAttributesW(icon.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return L"";
      }
      return icon;
    }
    executable.resize(executable.size() * 2);
  }
}

static inline HICON
bare_win_ui_load_packaged_icon(int width, int height) {
  auto path = bare_win_ui_packaged_icon_path();
  if (path.empty()) return nullptr;

  return reinterpret_cast<HICON>(LoadImageW(
    nullptr,
    path.c_str(),
    IMAGE_ICON,
    width,
    height,
    LR_LOADFROMFILE
  ));
}

#endif
