#include "app_window.hpp"
#include "d3d_renderer.hpp"
#include "tray_manager.hpp"
#include "audio_meter.hpp"
#include "osd_window.hpp"
#include "single_instance.hpp"
#include "ble/ble_client.hpp"
#include "protocol/protocol.hpp"
#include "ui/ui.hpp"

#include <windows.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>

namespace {

edifier::app::AppWindow* g_appWindow = nullptr;

void showPopupProxy(bool show) {
    if (g_appWindow) {
        g_appWindow->show(show, "ui");
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    auto setDpiAwareness = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (setDpiAwareness) {
        setDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    const std::wstring_view cmd = GetCommandLineW();

    if (cmd.find(L"--self-test") != std::wstring_view::npos) {
        std::string report;
        const bool ok = edifier::selfTest(report);
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) {
                DWORD written = 0;
                WriteFile(hOut, report.data(), static_cast<DWORD>(report.size()), &written, nullptr);
            }
        }
        return ok ? 0 : 2;
    }

    const bool dumpShots = cmd.find(L"--dump-shots") != std::wstring_view::npos;
    std::optional<edifier::app::SingleInstance> singleInstance;
    if (!dumpShots) {
        singleInstance.emplace();
        if (!singleInstance->valid()) return 1;
        if (!singleInstance->primary()) {
            singleInstance->requestActivation();
            return 0;
        }
    }

    bool running = true;
    edifier::BleClient ble;
    edifier::audio::AudioMeter audioMeter;

    edifier::app::TrayManager tray;
    edifier::app::AppWindow appWindow;
    g_appWindow = &appWindow;

    edifier::app::WindowCallbacks callbacks;
    callbacks.onTrayMenu = [&]() {
        tray.showMenu(&ble, appWindow.isRequestedVisible(),
            [&]() { appWindow.toggle("tray_menu"); },
            [&]() { running = false; });
    };
    callbacks.onMouseWheel = [&](short delta) {
        tray.handleWheel(delta, &ble);
    };
    callbacks.onPowerResume = [&]() {
        audioMeter.resetEndpoint();
        ble.disconnect();
        ble.connect();
    };
    callbacks.onPowerSuspend = [&]() {
        ble.disconnect();
    };
    callbacks.onQuit = [&]() {
        running = false;
    };

    if (!appWindow.init(instance, callbacks)) {
        return 1;
    }

    edifier::app::D3dRenderer renderer;
    if (!renderer.init(appWindow.getWindow())) {
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().LogFilename = nullptr;
    edifier::ui::styleUi();
    edifier::ui::loadFonts();
    ImGui_ImplWin32_Init(appWindow.getWindow());
    ImGui_ImplDX11_Init(renderer.getDevice(), renderer.getContext());

    tray.init(appWindow.getWindow(), edifier::app::AppWindow::kTrayMessage, edifier::app::AppWindow::kTrayId);

    if (dumpShots) {
        auto renderAndSave = [&](int page, const wchar_t* filename, int h = edifier::app::AppWindow::kHeight) {
            if (h != edifier::app::AppWindow::kHeight) {
                SetWindowPos(appWindow.getWindow(), nullptr, 0, 0, edifier::app::AppWindow::kWidth, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                renderer.resize(edifier::app::AppWindow::kWidth, h);
            }
            for (int f = 0; f < 3; ++f) {
                ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
                ImGui::GetStyle().Alpha = 1.0f;
                edifier::ui::renderApp(ble, showPopupProxy, true, page, static_cast<float>(h));
                ImGui::Render();
                renderer.beginFrame();
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                renderer.endFrame(false);
            }
            renderer.captureScreenshot(filename);
            if (h != edifier::app::AppWindow::kHeight) {
                SetWindowPos(appWindow.getWindow(), nullptr, 0, 0, edifier::app::AppWindow::kWidth, edifier::app::AppWindow::kHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                renderer.resize(edifier::app::AppWindow::kWidth, edifier::app::AppWindow::kHeight);
            }
        };

        renderAndSave(0, L"design/runtime-sound.png");
        renderAndSave(0, L"design/runtime-sound.png");
        renderAndSave(7, L"design/runtime-meter-popup.png");
        renderAndSave(5, L"design/runtime-dropdown.png", edifier::ui::kExpandedHeight);
        renderAndSave(1, L"design/runtime-eq.png");
        renderAndSave(-9, L"design/runtime-eq-9band.png");
        renderAndSave(6, L"design/runtime-acoustic.png");
        renderAndSave(2, L"design/runtime-device.png");
        renderAndSave(-2, L"design/runtime-disconnected.png");
        renderAndSave(-3, L"design/runtime-devices.png");

        ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
        renderer.shutdown();
        tray.destroy();
        appWindow.shutdown();
        return 0;
    }

    const bool minimized = (cmd.find(L"--minimized") != std::wstring_view::npos) ||
                           (cmd.find(L"--silent") != std::wstring_view::npos);
    const bool designPreview = cmd.find(L"--design-preview") != std::wstring_view::npos;
    const bool preview9Band = cmd.find(L"--preview-9band") != std::wstring_view::npos;
    const int forcedPreviewPage = preview9Band ? -9 : -1;
    if (!minimized) {
        appWindow.show(true);
    }

    edifier::osd::initOsd(instance);
    HPOWERNOTIFY hPowerNotify = RegisterSuspendResumeNotification(appWindow.getWindow(), DEVICE_NOTIFY_WINDOW_HANDLE);

    auto lastFrame = std::chrono::steady_clock::now();
    MSG message{};
    int currentHeight = edifier::app::AppWindow::kHeight;
    float currentHeightFloat = static_cast<float>(edifier::app::AppWindow::kHeight);

    while (running) {
        tray.updateTooltip(ble.snapshot());
        if (singleInstance && singleInstance->takeActivation()) {
            appWindow.show(true);
        }

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;

        edifier::osd::updateOsd(dt);

        const int targetHeight = edifier::ui::getTargetHeight();
        const bool resized = appWindow.updateFrame(dt, targetHeight, currentHeight, currentHeightFloat);
        if (resized) {
            renderer.resize(edifier::app::AppWindow::kWidth, currentHeight);
        }

        if (appWindow.isRequestedVisible() || appWindow.getVisibility() > 0.01f) {
            audioMeter.update(dt);
            const auto audioLevels = audioMeter.getLevels();

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ImGui::GetStyle().Alpha = std::clamp(appWindow.getVisibility(), 0.f, 1.f);

            edifier::ui::renderApp(ble, showPopupProxy, designPreview || preview9Band,
                                   forcedPreviewPage, currentHeightFloat, &audioLevels, &audioMeter);

            ImGui::Render();
            renderer.beginFrame();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            renderer.endFrame(true);
        }
    }

    if (hPowerNotify) {
        UnregisterSuspendResumeNotification(hPowerNotify);
    }

    edifier::osd::shutdownOsd();
    ble.disconnect();

    tray.destroy();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    renderer.shutdown();
    appWindow.shutdown();

    return 0;
}
