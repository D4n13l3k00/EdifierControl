#pragma once

#include <windows.h>
#include <string>

namespace edifier::autorun {

inline constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const wchar_t* kAppName = L"EdifierControl";

inline bool isEnabled() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    const auto res = RegQueryValueExW(key, kAppName, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return res == ERROR_SUCCESS;
}

inline bool setEnabled(bool enable) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &key) != ERROR_SUCCESS) return false;
    bool ok = false;
    if (enable) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(path) + L"\" --minimized";
        const auto res = RegSetValueExW(key, kAppName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(cmd.c_str()),
            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        ok = (res == ERROR_SUCCESS);
    } else {
        const auto res = RegDeleteValueW(key, kAppName);
        ok = (res == ERROR_SUCCESS || res == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}

} // namespace edifier::autorun
