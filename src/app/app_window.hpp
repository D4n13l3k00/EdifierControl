#pragma once

#include "ui/ui.hpp"
#include <windows.h>
#include <functional>
#include <chrono>

namespace edifier::app {

struct WindowCallbacks {
    std::function<void()> onTrayMenu;
    std::function<void(short delta)> onMouseWheel;
    std::function<void()> onPowerResume;
    std::function<void()> onPowerSuspend;
    std::function<void()> onQuit;
};

class AppWindow {
public:
    static constexpr int kWidth = edifier::ui::kWidth;
    static constexpr int kHeight = edifier::ui::kHeight;
    static constexpr UINT kTrayMessage = WM_APP + 1;
    static constexpr UINT kTrayId = 1;

    AppWindow();
    ~AppWindow();

    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    bool init(HINSTANCE instance, const WindowCallbacks& callbacks);
    void shutdown();

    HWND getWindow() const { return m_window; }
    HWND getOverlay() const { return m_overlay; }

    void show(bool show, const char* reason = "ui");
    void toggle(const char* reason = "toggle");

    bool isRequestedVisible() const { return m_requestedVisible; }
    float getVisibility() const { return m_visibility; }

    // Updates animations, window position, alpha, and returns whether D3D swapchain needs resize
    bool updateFrame(float dt, int targetHeight, int& currentHeight, float& currentHeightFloat);

private:
    RECT calculatePopupRect(float visibility, int height) const;
    void positionOverlay();

    static LRESULT WINAPI staticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT WINAPI staticOverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT handleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleOverlayMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_instance{nullptr};
    HWND m_window{nullptr};
    HWND m_overlay{nullptr};
    WindowCallbacks m_callbacks;

    bool m_requestedVisible{false};
    float m_visibility{0.0f};
    std::chrono::steady_clock::time_point m_openTime{};
    std::chrono::steady_clock::time_point m_lastToggle{};

    static AppWindow* s_instance;
};

} // namespace edifier::app
