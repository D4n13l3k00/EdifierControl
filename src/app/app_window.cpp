#include "app_window.hpp"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace edifier::app {

AppWindow* AppWindow::s_instance = nullptr;

static constexpr bool isTrayToggleEvent(UINT eventCode) {
    return eventCode == NIN_SELECT || eventCode == NIN_KEYSELECT;
}

AppWindow::AppWindow() {
    s_instance = this;
}

AppWindow::~AppWindow() {
    shutdown();
    if (s_instance == this) s_instance = nullptr;
}

bool AppWindow::init(HINSTANCE instance, const WindowCallbacks& callbacks) {
    m_instance = instance;
    m_callbacks = callbacks;

    const UINT msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    ChangeWindowMessageFilter(kTrayMessage, 1 /* MSGFLT_ADD */);
    ChangeWindowMessageFilter(msgTaskbarCreated, 1);
    ChangeWindowMessageFilter(WM_CONTEXTMENU, 1);
    ChangeWindowMessageFilter(WM_COMMAND, 1);

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, staticWindowProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"EdifierMR3Control", nullptr};
    RegisterClassExW(&wc);

    m_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, wc.lpszClassName, L"Edifier Control",
        WS_POPUP, 0, 0, kWidth, kHeight, nullptr, nullptr, instance, nullptr);
    if (!m_window) return false;

    typedef BOOL(WINAPI* PFN_CWMFE)(HWND, UINT, DWORD, PCHANGEFILTERSTRUCT);
    auto pChangeFilter = reinterpret_cast<PFN_CWMFE>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx"));
    if (pChangeFilter) {
        pChangeFilter(m_window, kTrayMessage, 1 /* MSGFLT_ALLOW */, nullptr);
        pChangeFilter(m_window, msgTaskbarCreated, 1, nullptr);
        pChangeFilter(m_window, WM_CONTEXTMENU, 1, nullptr);
        pChangeFilter(m_window, WM_COMMAND, 1, nullptr);
    }

    SetLayeredWindowAttributes(m_window, 0, 0, LWA_ALPHA);
    const DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_window, 33, &corner, sizeof(corner));

    WNDCLASSEXW owc{sizeof(owc), CS_CLASSDC, staticOverlayProc, 0, 0, instance, nullptr, LoadCursorW(nullptr, IDC_ARROW),
        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)), nullptr, L"EdifierDismissOverlay", nullptr};
    RegisterClassExW(&owc);

    m_overlay = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST, owc.lpszClassName, L"",
        WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, instance, nullptr);
    if (m_overlay) {
        SetLayeredWindowAttributes(m_overlay, 0, 1, LWA_ALPHA);
    }

    return true;
}

void AppWindow::shutdown() {
    if (m_overlay) {
        DestroyWindow(m_overlay);
        m_overlay = nullptr;
    }
    if (m_window) {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_instance) {
        UnregisterClassW(L"EdifierDismissOverlay", m_instance);
        UnregisterClassW(L"EdifierMR3Control", m_instance);
        m_instance = nullptr;
    }
}

RECT AppWindow::calculatePopupRect(float visibility, int height) const {
    RECT trayRect{};
    NOTIFYICONIDENTIFIER icon{sizeof(icon), m_window, kTrayId, {}};
    const bool hasTrayRect = SUCCEEDED(Shell_NotifyIconGetRect(&icon, &trayRect));
    POINT anchor{hasTrayRect ? (trayRect.left + trayRect.right) / 2 : 0, hasTrayRect ? trayRect.top : 0};
    if (!hasTrayRect) GetCursorPos(&anchor);

    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);

    const int x = std::clamp(anchor.x - kWidth + 42, info.rcWork.left + 10, info.rcWork.right - kWidth - 10);
    const int restY = info.rcWork.bottom - height - 8;
    const int hiddenY = info.rcWork.bottom + 18;
    const int y = hiddenY + static_cast<int>((restY - hiddenY) * visibility);
    return {x, y, x + kWidth, y + height};
}

void AppWindow::positionOverlay() {
    if (!m_overlay) return;
    HMONITOR monitor = MonitorFromWindow(m_window, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);

    const int vx = info.rcWork.left;
    const int vy = info.rcWork.top;
    const int vw = info.rcWork.right - info.rcWork.left;
    const int vh = info.rcWork.bottom - info.rcWork.top;
    SetWindowPos(m_overlay, HWND_TOPMOST, vx, vy, vw, vh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (m_window) {
        SetWindowPos(m_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void AppWindow::show(bool show, const char* reason) {
    if (!show && reason && strcmp(reason, "ui") == 0) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_openTime).count();
        if (elapsed < 350) return;
    }

    m_requestedVisible = show;
    if (show) {
        m_openTime = std::chrono::steady_clock::now();
        if (ImGui::GetCurrentContext()) {
            ImGui::GetIO().ClearInputMouse();
        }
        positionOverlay();
        const int h = edifier::ui::getTargetHeight();
        const auto rect = calculatePopupRect(m_visibility, h);
        SetWindowPos(m_window, HWND_TOPMOST, rect.left, rect.top, kWidth, h, SWP_SHOWWINDOW);
        SetForegroundWindow(m_window);
        SetActiveWindow(m_window);
    } else {
        if (m_overlay) ShowWindow(m_overlay, SW_HIDE);
        edifier::ui::closeProfileDropdown();
    }
}

void AppWindow::toggle(const char* reason) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastToggle).count();
    if (elapsed < 250) return;
    m_lastToggle = now;
    show(!m_requestedVisible, reason);
}

bool AppWindow::updateFrame(float dt, int targetHeight, int& currentHeight, float& currentHeightFloat) {
    const float target = m_requestedVisible ? 1.f : 0.f;
    const float rate = 1.f - std::exp(-dt * 9.f);
    m_visibility += (target - m_visibility) * rate;

    if (m_requestedVisible || m_visibility > 0.01f) {
        const float tHeight = static_cast<float>(targetHeight);
        const float hRate = 1.f - std::exp(-dt * 18.f);
        currentHeightFloat += (tHeight - currentHeightFloat) * hRate;
        if (std::abs(tHeight - currentHeightFloat) < 0.25f) {
            currentHeightFloat = tHeight;
        }

        const int nextHeight = std::clamp(static_cast<int>(std::round(currentHeightFloat)), kHeight, edifier::ui::kExpandedHeight);
        bool resized = false;
        if (nextHeight != currentHeight) {
            currentHeight = nextHeight;
            resized = true;
        }

        const float smooth = m_visibility * m_visibility * (3.f - 2.f * m_visibility);
        const auto rect = calculatePopupRect(smooth, currentHeight);
        SetWindowPos(m_window, HWND_TOPMOST, rect.left, rect.top, kWidth, currentHeight, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetLayeredWindowAttributes(m_window, 0, static_cast<BYTE>(255 * std::clamp(smooth, 0.f, 1.f)), LWA_ALPHA);
        return resized;
    }

    ShowWindow(m_window, SW_HIDE);
    if (m_overlay) ShowWindow(m_overlay, SW_HIDE);
    currentHeightFloat = static_cast<float>(kHeight);
    currentHeight = kHeight;
    Sleep(12);
    return false;
}

LRESULT WINAPI AppWindow::staticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (s_instance) {
        return s_instance->handleWindowMessage(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT WINAPI AppWindow::staticOverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (s_instance) {
        return s_instance->handleOverlayMessage(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT AppWindow::handleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    static const UINT msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (message == msgTaskbarCreated) {
        if (m_callbacks.onTrayMenu) {
            // Taskbar restarted
        }
        return 0;
    }

    if (message == kTrayMessage) {
        const UINT eventCode = LOWORD(lParam);
        if (isTrayToggleEvent(eventCode)) {
            toggle("tray_icon_select");
        } else if (eventCode == WM_CONTEXTMENU || eventCode == WM_RBUTTONUP) {
            if (m_callbacks.onTrayMenu) m_callbacks.onTrayMenu();
        } else if (eventCode == WM_MOUSEWHEEL) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (m_callbacks.onMouseWheel) m_callbacks.onMouseWheel(delta);
        }
        return 0;
    }

    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return true;
    }

    switch (message) {
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            if (m_callbacks.onPowerResume) m_callbacks.onPowerResume();
        } else if (wParam == PBT_APMSUSPEND) {
            if (m_callbacks.onPowerSuspend) m_callbacks.onPowerSuspend();
        }
        return TRUE;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            show(false, "escape_key");
            return 0;
        }
        break;

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (m_callbacks.onMouseWheel) m_callbacks.onMouseWheel(delta);
        break;
    }

    case WM_ACTIVATE: {
        const WORD state = LOWORD(wParam);
        if (state == WA_INACTIVE) {
            if (m_requestedVisible && m_visibility > 0.8f) {
                POINT pt;
                GetCursorPos(&pt);
                RECT trayRect{};
                NOTIFYICONIDENTIFIER icon{sizeof(icon), m_window, kTrayId, {}};
                if (SUCCEEDED(Shell_NotifyIconGetRect(&icon, &trayRect)) && PtInRect(&trayRect, pt)) {
                    return 0;
                }
                HWND activeWnd = reinterpret_cast<HWND>(lParam);
                if (activeWnd && activeWnd != m_overlay && activeWnd != m_window) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(activeWnd, &pid);
                    if (pid != GetCurrentProcessId()) {
                        show(false, "deactivate");
                    }
                }
            }
        }
        return 0;
    }

    case WM_CONTEXTMENU:
        return 0;

    case WM_NCHITTEST: {
        const auto hit = DefWindowProcW(window, message, wParam, lParam);
        if (hit == HTCLIENT) {
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window, &p);
            if (p.y < 48 && p.x < 250) return HTCAPTION;
        }
        return hit;
    }

    case WM_DESTROY:
        if (m_callbacks.onQuit) m_callbacks.onQuit();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT AppWindow::handleOverlayMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
        show(false, "overlay_click");
        return 0;

    case WM_RBUTTONDOWN: {
        POINT pt;
        GetCursorPos(&pt);
        RECT trayRect{};
        NOTIFYICONIDENTIFIER icon{sizeof(icon), m_window, kTrayId, {}};
        const bool hasTrayRect = SUCCEEDED(Shell_NotifyIconGetRect(&icon, &trayRect));
        show(false, "overlay_rclick");
        if (hasTrayRect && PtInRect(&trayRect, pt)) {
            if (m_callbacks.onTrayMenu) m_callbacks.onTrayMenu();
        }
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(window, &ps);
        EndPaint(window, &ps);
        return 0;
    }
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace edifier::app
