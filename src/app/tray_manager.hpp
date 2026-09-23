#pragma once

#include "ble/ble_client.hpp"
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <string>

namespace edifier::app {

class TrayManager {
public:
    TrayManager() = default;
    ~TrayManager();

    TrayManager(const TrayManager&) = delete;
    TrayManager& operator=(const TrayManager&) = delete;

    bool init(HWND window, UINT callbackMessage, UINT iconId = 1);
    void destroy();

    void updateTooltip(const BleSnapshot& snap);
    void showMenu(BleClient* ble, bool isWindowVisible,
                  const std::function<void()>& onToggleWindow,
                  const std::function<void()>& onQuit);

    void handleWheel(short delta, BleClient* ble);
    void onTaskbarCreated();

    bool isMenuOpen() const { return m_menuOpen; }

    static void openConfigInNotepad();

private:
    HWND m_window{nullptr};
    UINT m_callbackMessage{0};
    UINT m_iconId{1};
    NOTIFYICONDATAW m_tray{};
    bool m_menuOpen{false};
    std::wstring m_lastTip;
};

} // namespace edifier::app
