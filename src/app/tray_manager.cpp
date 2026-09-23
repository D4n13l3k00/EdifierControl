#include "tray_manager.hpp"
#include "autorun.hpp"
#include "osd_window.hpp"
#include "protocol/protocol.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace edifier::app {

TrayManager::~TrayManager() {
    destroy();
}

bool TrayManager::init(HWND window, UINT callbackMessage, UINT iconId) {
    m_window = window;
    m_callbackMessage = callbackMessage;
    m_iconId = iconId;

    memset(&m_tray, 0, sizeof(m_tray));
    m_tray.cbSize = sizeof(m_tray);
    m_tray.hWnd = m_window;
    m_tray.uID = m_iconId;
    m_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_tray.uCallbackMessage = m_callbackMessage;
    m_tray.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!m_tray.hIcon) {
        m_tray.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102));
    }
    wcscpy_s(m_tray.szTip, L"Edifier Control");

    const BOOL addOk = Shell_NotifyIconW(NIM_ADD, &m_tray);
    if (addOk) {
        NOTIFYICONDATAW vData{};
        vData.cbSize = sizeof(vData);
        vData.hWnd = m_window;
        vData.uID = m_iconId;
        vData.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &vData);
    }
    return addOk != FALSE;
}

void TrayManager::destroy() {
    if (m_tray.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &m_tray);
        m_tray.hWnd = nullptr;
    }
}

void TrayManager::onTaskbarCreated() {
    if (m_window) {
        init(m_window, m_callbackMessage, m_iconId);
    }
}

void TrayManager::openConfigInNotepad() {
    wchar_t appData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    std::filesystem::path dir = std::filesystem::path(appData) / L"EdifierControl";
    std::filesystem::path file = dir / L"config.json";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::exists(file, ec)) {
        std::ofstream f(file);
        if (f.is_open()) {
            f << "{\n";
            f << "  \"lastAddress\": 0,\n";
            f << "  \"deviceName\": \"EDIFIER MR3\",\n";
            f << "  \"modelName\": \"MR3\",\n";
            f << "  \"serviceUuid\": \"\"\n";
            f << "}\n";
        }
    }
    const std::wstring param = L"\"" + file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"notepad.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
}

void TrayManager::updateTooltip(const BleSnapshot& snap) {
    std::wstring tip;
    if (snap.state == edifier::LinkState::Ready) {
        std::wstring model = snap.modelName.empty() ? L"MR3" : snap.modelName;
        const wchar_t* presetName = L"Monitor";
        if (snap.eqKnown) {
            if (snap.eqCurrent == 1) presetName = L"Music";
            else if (snap.eqCurrent == 2) presetName = L"Custom";
        }
        if (snap.volumeKnown) {
            tip = L"EDIFIER " + model + L" — " + std::to_wstring(snap.volumeCurrent) + L"/" +
                  std::to_wstring(snap.volumeMax) + L" · " + presetName;
        } else {
            tip = L"EDIFIER " + model + L" · " + presetName;
        }
    } else if (snap.state == edifier::LinkState::Connecting || snap.state == edifier::LinkState::Scanning) {
        tip = L"EDIFIER MR3 — Подключение…";
    } else {
        tip = L"EDIFIER MR3 — Не подключено";
    }

    if (tip != m_lastTip) {
        m_lastTip = tip;
        wcsncpy_s(m_tray.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &m_tray);
    }
}

void TrayManager::handleWheel(short delta, BleClient* ble) {
    if (!ble || delta == 0) return;
    ble->changeVolume(delta > 0 ? 1 : -1);
    auto snap = ble->snapshot();
    edifier::osd::showVolume(snap.volumeCurrent, snap.volumeMax, snap.volumeCurrent == 0);
}

void TrayManager::showMenu(BleClient* ble, bool isWindowVisible,
                           const std::function<void()>& onToggleWindow,
                           const std::function<void()>& onQuit) {
    if (m_menuOpen) return;

    static auto lastMenuTime = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMenuTime).count();
    if (elapsed < 250) return;
    lastMenuTime = now;

    m_menuOpen = true;

    POINT point{};
    if (!GetCursorPos(&point) || (point.x == 0 && point.y == 0)) {
        RECT rc{};
        NOTIFYICONIDENTIFIER nid{sizeof(nid), m_window, m_iconId, {}};
        if (SUCCEEDED(Shell_NotifyIconGetRect(&nid, &rc))) {
            point.x = (rc.left + rc.right) / 2;
            point.y = rc.top;
        }
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        m_menuOpen = false;
        return;
    }

    // 1. Открыть / Скрыть меню окна
    AppendMenuW(menu, MF_STRING, 1001, isWindowVisible ? L"Скрыть окно" : L"Открыть окно");
    SetMenuDefaultItem(menu, 1001, FALSE);

    auto snap = ble ? ble->snapshot() : edifier::BleSnapshot{};
    const bool isReady = (snap.state == edifier::LinkState::Ready);
    const bool isMuted = (snap.volumeKnown && snap.volumeCurrent == 0);
    AppendMenuW(menu, isReady ? MF_STRING : (MF_STRING | MF_GRAYED), 1003, isMuted ? L"Включить звук" : L"Заглушить звук");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 2. Быстрый свитч пресетов (Monitor / Music / Custom)
    const UINT presetFlags = isReady ? MF_STRING : (MF_STRING | MF_GRAYED);
    AppendMenuW(menu, presetFlags, 2001, L"Monitor");
    AppendMenuW(menu, presetFlags, 2002, L"Music");
    AppendMenuW(menu, presetFlags, 2003, L"Custom");

    UINT activeCmd = 2001;
    if (snap.eqKnown) {
        if (snap.eqCurrent == 1) activeCmd = 2002;
        else if (snap.eqCurrent == 2) activeCmd = 2003;
    }
    CheckMenuRadioItem(menu, 2001, 2003, activeCmd, MF_BYCOMMAND);

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 3. Автозапуск с Windows
    const bool autoStart = edifier::autorun::isEnabled();
    AppendMenuW(menu, MF_STRING | (autoStart ? MF_CHECKED : MF_UNCHECKED), 3001, L"Автозапуск с Windows");

    // 4. Открыть конфиг в Блокноте
    AppendMenuW(menu, MF_STRING, 3002, L"Открыть конфиг в Блокноте");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 5. Закрыть приложение
    AppendMenuW(menu, MF_STRING, 1002, L"Закрыть приложение");

    SetForegroundWindow(m_window);

    const auto choice = TrackPopupMenuEx(menu,
        TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        point.x, point.y, m_window, nullptr);

    PostMessageW(m_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    m_menuOpen = false;

    if (choice == 1001) {
        if (onToggleWindow) onToggleWindow();
    } else if (choice == 1003 && ble) {
        static int trayUnmuteVol = 15;
        if (snap.volumeCurrent > 0) {
            trayUnmuteVol = snap.volumeCurrent;
            ble->sendMutation(edifier::Command::SetVolume, {0});
            ble->sendRead(edifier::Command::GetVolume);
            edifier::osd::showVolume(0, snap.volumeMax, true);
        } else {
            const int nextVol = (trayUnmuteVol > 0 ? trayUnmuteVol : 15);
            ble->sendMutation(edifier::Command::SetVolume, {static_cast<std::uint8_t>(nextVol)});
            ble->sendRead(edifier::Command::GetVolume);
            edifier::osd::showVolume(nextVol, snap.volumeMax, false);
        }
    } else if (choice >= 2001 && choice <= 2003) {
        if (ble) {
            ble->setEqPreset(choice - 2001);
        }
    } else if (choice == 3001) {
        edifier::autorun::setEnabled(!autoStart);
    } else if (choice == 3002) {
        openConfigInNotepad();
    } else if (choice == 1002) {
        if (onQuit) onQuit();
    }
}

} // namespace edifier::app
