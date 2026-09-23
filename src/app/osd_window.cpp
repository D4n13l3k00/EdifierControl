#include "osd_window.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace edifier::osd {

namespace {

constexpr int kOsdWidth = 260;
constexpr int kOsdHeight = 58;

HWND g_osdWindow = nullptr;
ULONG_PTR g_gdiplusToken = 0;

float g_osdTimer = 0.0f;
float g_currentAlpha = 0.0f;
float g_targetAlpha = 0.0f;

int g_lastVolume = -1;
int g_lastMaxVolume = 30;
bool g_lastMuted = false;

HDC g_memDc = nullptr;
HBITMAP g_hBitmap = nullptr;
HBITMAP g_hOldBitmap = nullptr;

LRESULT CALLBACK osdWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void addRoundedRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    path.AddArc(x, y, r + r, r + r, 180, 90);
    path.AddArc(x + w - r - r, y, r + r, r + r, 270, 90);
    path.AddArc(x + w - r - r, y + h - r - r, r + r, r + r, 0, 90);
    path.AddArc(x, y + h - r - r, r + r, r + r, 90, 90);
    path.CloseFigure();
}

void renderOsdBitmap(int volume, int maxVolume, bool isMuted) {
    if (!g_osdWindow || !g_memDc) return;

    Gdiplus::Bitmap bmp(kOsdWidth, kOsdHeight, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // 1. Dark rounded surface
    Gdiplus::GraphicsPath bgPath;
    addRoundedRect(bgPath, 1.0f, 1.0f, static_cast<float>(kOsdWidth - 2), static_cast<float>(kOsdHeight - 2), 12.0f);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(242, 20, 21, 24));
    g.FillPath(&bgBrush, &bgPath);

    Gdiplus::Pen borderPen(Gdiplus::Color(65, 255, 255, 255), 1.0f);
    g.DrawPath(&borderPen, &bgPath);

    // 2. Speaker Icon
    Gdiplus::SolidBrush speakerBrush(isMuted ? Gdiplus::Color(220, 235, 75, 60) : Gdiplus::Color(240, 224, 191, 125));
    // Speaker box
    g.FillRectangle(&speakerBrush, 18, 17, 5, 8);
    // Speaker cone
    Gdiplus::Point conePts[] = {
        {23, 17},
        {30, 13},
        {30, 29},
        {23, 25}
    };
    g.FillPolygon(&speakerBrush, conePts, 4);

    if (isMuted) {
        Gdiplus::Pen slashPen(Gdiplus::Color(240, 235, 75, 60), 2.0f);
        g.DrawLine(&slashPen, 15, 12, 33, 30);
    } else {
        Gdiplus::Pen wavePen(Gdiplus::Color(200, 224, 191, 125), 1.6f);
        g.DrawArc(&wavePen, 26, 15, 10, 12, -60, 120);
        g.DrawArc(&wavePen, 24, 12, 16, 18, -60, 120);
    }

    // 3. Title & Volume Text
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font titleFont(&fontFamily, 10.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
    Gdiplus::Font valueFont(&fontFamily, 11.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);

    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(230, 220, 222, 226));
    g.DrawString(L"EDIFIER MR3", -1, &titleFont, Gdiplus::PointF(45.0f, 13.0f), &titleBrush);

    wchar_t valStr[32]{};
    if (isMuted) {
        wcscpy_s(valStr, L"\x0417\x0430\x0433\x043B\x0443\x0448\x0435\x043D\x043E");
    } else {
        swprintf_s(valStr, L"%d / %d", volume, maxVolume);
    }

    Gdiplus::SolidBrush valBrush(isMuted ? Gdiplus::Color(255, 238, 160, 60) : Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::StringFormat rightFormat;
    rightFormat.SetAlignment(Gdiplus::StringAlignmentFar);
    g.DrawString(valStr, -1, &valueFont, Gdiplus::PointF(static_cast<float>(kOsdWidth - 18), 12.0f), &rightFormat, &valBrush);

    // 4. Progress bar
    const float barX = 18.0f;
    const float barY = 38.0f;
    const float barW = static_cast<float>(kOsdWidth - 36);
    const float barH = 6.0f;

    Gdiplus::GraphicsPath trackPath;
    addRoundedRect(trackPath, barX, barY, barW, barH, 3.0f);
    Gdiplus::SolidBrush trackBrush(Gdiplus::Color(200, 38, 40, 46));
    g.FillPath(&trackBrush, &trackPath);

    if (!isMuted && volume > 0 && maxVolume > 0) {
        const float fillW = std::clamp(barW * static_cast<float>(volume) / static_cast<float>(maxVolume), 4.0f, barW);
        Gdiplus::GraphicsPath fillPath;
        addRoundedRect(fillPath, barX, barY, fillW, barH, 3.0f);
        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 224, 191, 125));
        g.FillPath(&fillBrush, &fillPath);
    }

    // Convert to HBITMAP and update layered window
    HBITMAP hBmp = nullptr;
    bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBmp);
    if (hBmp) {
        if (g_hOldBitmap) {
            SelectObject(g_memDc, g_hOldBitmap);
            g_hOldBitmap = nullptr;
        }
        if (g_hBitmap) {
            DeleteObject(g_hBitmap);
            g_hBitmap = nullptr;
        }
        g_hBitmap = hBmp;
        g_hOldBitmap = static_cast<HBITMAP>(SelectObject(g_memDc, g_hBitmap));

        RECT rcWork{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
        POINT ptDst{rcWork.left + (rcWork.right - rcWork.left - kOsdWidth) / 2, rcWork.bottom - kOsdHeight - 48};
        SIZE szDst{kOsdWidth, kOsdHeight};
        POINT ptSrc{0, 0};

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = static_cast<BYTE>(std::clamp(static_cast<int>(255 * g_currentAlpha), 0, 255));
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(g_osdWindow, nullptr, &ptDst, &szDst, g_memDc, &ptSrc, 0, &blend, ULW_ALPHA);
    }
}

} // namespace

void initOsd(HINSTANCE instance) {
    if (g_osdWindow) return;

    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiInput, nullptr);

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, osdWndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"EdifierVolumeOSD", nullptr};
    RegisterClassExW(&wc);

    RECT rcWork{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    const int x = rcWork.left + (rcWork.right - rcWork.left - kOsdWidth) / 2;
    const int y = rcWork.bottom - kOsdHeight - 48;

    g_osdWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Edifier Volume OSD",
        WS_POPUP, x, y, kOsdWidth, kOsdHeight,
        nullptr, nullptr, instance, nullptr
    );

    HDC screenDc = GetDC(nullptr);
    g_memDc = CreateCompatibleDC(screenDc);
    ReleaseDC(nullptr, screenDc);
}

void showVolume(int currentVolume, int maxVolume, bool isMuted) {
    if (!g_osdWindow) return;

    g_lastVolume = currentVolume;
    g_lastMaxVolume = maxVolume;
    g_lastMuted = isMuted;

    g_osdTimer = 1.4f; // hold for 1.4s
    g_targetAlpha = 1.0f;
    g_currentAlpha = 1.0f; // fast instant appearance on change

    renderOsdBitmap(currentVolume, maxVolume, isMuted);
    ShowWindow(g_osdWindow, SW_SHOWNOACTIVATE);
}

void updateOsd(float dt) {
    if (!g_osdWindow) return;

    if (g_osdTimer > 0.0f) {
        g_osdTimer -= dt;
    } else {
        g_targetAlpha = 0.0f;
    }

    if (g_currentAlpha > g_targetAlpha) {
        g_currentAlpha = std::max(0.0f, g_currentAlpha - dt / 0.28f); // smooth 280ms fade out

        // Update window alpha
        RECT rcWork{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
        POINT ptDst{rcWork.left + (rcWork.right - rcWork.left - kOsdWidth) / 2, rcWork.bottom - kOsdHeight - 48};
        SIZE szDst{kOsdWidth, kOsdHeight};
        POINT ptSrc{0, 0};

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = static_cast<BYTE>(std::clamp(static_cast<int>(255 * g_currentAlpha), 0, 255));
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(g_osdWindow, nullptr, &ptDst, &szDst, g_memDc, &ptSrc, 0, &blend, ULW_ALPHA);

        if (g_currentAlpha <= 0.01f) {
            g_currentAlpha = 0.0f;
            ShowWindow(g_osdWindow, SW_HIDE);
        }
    }
}

void shutdownOsd() {
    if (g_osdWindow) {
        ShowWindow(g_osdWindow, SW_HIDE);
        DestroyWindow(g_osdWindow);
        g_osdWindow = nullptr;
    }
    if (g_memDc) {
        if (g_hOldBitmap) SelectObject(g_memDc, g_hOldBitmap);
        if (g_hBitmap) DeleteObject(g_hBitmap);
        DeleteDC(g_memDc);
        g_memDc = nullptr;
    }
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

} // namespace edifier::osd
