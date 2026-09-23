#include "ui.hpp"
#include "ble/ble_client.hpp"
#include "app/autorun.hpp"
#include "app/audio_meter.hpp"
#include "app/osd_window.hpp"
#include <windows.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace edifier::ui {
namespace {
ImFont *bodyFont{}, *labelFont{}, *titleFont{}, *cardTitleFont{}, *heroFont{}, *numberFont{}, *smallFont{}, *microFont{};
constexpr ImU32 background = IM_COL32(16, 17, 19, 255);       // #101113
constexpr ImU32 surface    = IM_COL32(27, 28, 31, 255);       // #1B1C1F
constexpr ImU32 raised     = IM_COL32(39, 40, 44, 255);       // #27282C
constexpr ImU32 container  = IM_COL32(42, 45, 49, 255);       // #2A2D31
constexpr ImU32 ink        = IM_COL32(245, 242, 235, 255);    // #F5F2EB
constexpr ImU32 muted      = IM_COL32(166, 166, 173, 255);    // #A6A6AD
constexpr ImU32 lightMuted = IM_COL32(199, 199, 204, 255);    // #C7C7CC
constexpr ImU32 gold       = IM_COL32(224, 191, 125, 255);    // #E0BF7D
constexpr ImU32 green      = IM_COL32(168, 204, 174, 255);    // #A8CCAE
constexpr ImU32 trackBg    = IM_COL32(59, 59, 64, 255);       // #3B3B40
constexpr ImU32 darkText   = IM_COL32(15, 17, 19, 255);       // #0F1113

inline float ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

inline ImU32 lerpColor(ImU32 c1, ImU32 c2, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const int r1 = (c1) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = (c1 >> 16) & 0xFF, a1 = (c1 >> 24) & 0xFF;
    const int r2 = (c2) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = (c2 >> 16) & 0xFF, a2 = (c2 >> 24) & 0xFF;
    const int r = static_cast<int>(r1 + (r2 - r1) * t);
    const int g = static_cast<int>(g1 + (g2 - g1) * t);
    const int b = static_cast<int>(b1 + (b2 - b1) * t);
    const int a = static_cast<int>(a1 + (a2 - a1) * t);
    return IM_COL32(r, g, b, a);
}

struct AnimState {
    float hover{0.0f};
    float press{0.0f};
};

static std::unordered_map<ImGuiID, AnimState> g_anims;

inline AnimState& updateAnim(ImGuiID id, bool hovered, bool held, float hSpeed = 14.0f, float pSpeed = 24.0f) {
    auto& st = g_anims[id];
    const float dt = ImGui::GetIO().DeltaTime;
    const float targetH = hovered ? 1.0f : 0.0f;
    const float curHSpeed = hovered ? hSpeed : (hSpeed * 0.7f);
    st.hover += (targetH - st.hover) * std::min(1.0f, dt * curHSpeed);
    if (std::abs(st.hover - targetH) < 0.002f) st.hover = targetH;

    const float targetP = held ? 1.0f : 0.0f;
    const float curPSpeed = held ? pSpeed : (pSpeed * 0.5f);
    st.press += (targetP - st.press) * std::min(1.0f, dt * curPSpeed);
    if (std::abs(st.press - targetP) < 0.002f) st.press = targetP;

    return st;
}

ImU32 color(ImU32 c) {
    const auto alpha = static_cast<unsigned>((c >> 24) * ImGui::GetStyle().Alpha);
    return (c & 0x00ffffff) | (alpha << 24);
}

void text(float x, float y, const char* s, ImU32 c = ink, ImFont* font = nullptr) {
    auto* f = font ? font : bodyFont;
    const auto p = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddText(f, f->FontSize, {p.x + x, p.y + y}, color(c), s);
}

std::string truncateText(ImFont* font, const std::string& str, float maxW) {
    if (!font || str.empty() || font->CalcTextSizeA(font->FontSize, 10000.f, 0.f, str.c_str()).x <= maxW) {
        return str;
    }
    std::string s = str;
    while (!s.empty()) {
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
            s.pop_back();
        }
        if (!s.empty()) s.pop_back();
        std::string cand = s + "...";
        if (font->CalcTextSizeA(font->FontSize, 10000.f, 0.f, cand.c_str()).x <= maxW) {
            return cand;
        }
    }
    return "...";
}

int getAudioDeviceIconType(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.find("head") != std::string::npos || lower.find("науш") != std::string::npos ||
        lower.find("neo") != std::string::npos || lower.find("ear") != std::string::npos ||
        lower.find("bud") != std::string::npos || lower.find("airpod") != std::string::npos ||
        lower.find("wh-") != std::string::npos) {
        return 1; // Headphones
    }
    if (lower.find("monitor") != std::string::npos || lower.find("монитор") != std::string::npos ||
        lower.find("tv") != std::string::npos || lower.find("hdmi") != std::string::npos ||
        lower.find("display") != std::string::npos || lower.find("nvidia") != std::string::npos ||
        lower.find("amd high") != std::string::npos) {
        return 2; // Monitor
    }
    return 0; // Speaker / default
}

void centered(float x, float y, float w, const char* s, ImU32 c = ink, ImFont* font = nullptr) {
    auto* f = font ? font : bodyFont;
    text(x + (w - f->CalcTextSizeA(f->FontSize, 1000.f, 0.f, s).x) * 0.5f, y, s, c, f);
}

void rect(float x, float y, float w, float h, ImU32 c, float radius = 8.f) {
    const auto p = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled({p.x + x, p.y + y}, {p.x + x + w, p.y + y + h}, color(c), radius);
}

bool button(const char* id, const char* label, float x, float y, float w, float h = 32.f, bool active = false, ImFont* font = nullptr, float radius = 8.f) {
    ImGui::SetCursorPos({x, y});
    const bool pressed = ImGui::InvisibleButton(id, {w, h});
    const ImGuiID btnId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const auto& anim = updateAnim(btnId, hovered, held);
    const float hProgress = ease(anim.hover);
    const float pProgress = ease(anim.press);

    ImU32 bgCol;
    ImU32 textCol;
    if (active) {
        constexpr ImU32 goldHovered = IM_COL32(236, 206, 146, 255);
        constexpr ImU32 goldPressed = IM_COL32(208, 175, 109, 255);
        bgCol = lerpColor(gold, goldHovered, hProgress);
        if (pProgress > 0.001f) {
            bgCol = lerpColor(bgCol, goldPressed, pProgress);
        }
        textCol = darkText;
    } else {
        constexpr ImU32 raisedHovered = IM_COL32(56, 58, 64, 255);
        constexpr ImU32 raisedPressed = IM_COL32(32, 33, 36, 255);
        bgCol = lerpColor(raised, raisedHovered, hProgress);
        if (pProgress > 0.001f) {
            bgCol = lerpColor(bgCol, raisedPressed, pProgress);
        }
        textCol = lerpColor(ink, IM_COL32(255, 255, 255, 255), hProgress);
    }

    const float pressDepth = pProgress * 1.0f;
    rect(x + pressDepth, y + pressDepth, w - pressDepth * 2.0f, h - pressDepth * 2.0f, bgCol, radius);

    auto* f = font ? font : bodyFont;
    centered(x, y + (h - f->FontSize) * 0.5f + pressDepth * 0.5f, w, label, textCol, f);
    return pressed;
}

void drawCheckmark(float cx, float cy, ImU32 col);

bool greenCheckButton(const char* id, const char* label, float x, float y, float w, float h = 28.f, ImFont* font = nullptr) {
    ImGui::SetCursorPos({x, y});
    const bool pressed = ImGui::InvisibleButton(id, {w, h});
    const ImGuiID btnId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const auto& anim = updateAnim(btnId, hovered, held);
    const float hProgress = ease(anim.hover);
    const float pProgress = ease(anim.press);

    constexpr ImU32 greenHover = IM_COL32(185, 218, 190, 255);
    constexpr ImU32 greenPress = IM_COL32(148, 186, 154, 255);
    ImU32 bg = lerpColor(green, greenHover, hProgress);
    if (pProgress > 0.001f) {
        bg = lerpColor(bg, greenPress, pProgress);
    }

    const float pressDepth = pProgress * 1.0f;
    rect(x + pressDepth, y + pressDepth, w - pressDepth * 2.0f, h - pressDepth * 2.0f, bg, h * 0.5f);

    auto* f = font ? font : bodyFont;
    const float textW = f->CalcTextSizeA(f->FontSize, 1000.f, 0.f, label).x;
    const float contentW = 10.f + 6.f + textW;
    const float startX = x + (w - contentW) * 0.5f;
    drawCheckmark(startX + 4.f, y + h * 0.5f + pressDepth * 0.5f, darkText);
    text(startX + 14.f, y + (h - f->FontSize) * 0.5f + pressDepth * 0.5f, label, darkText, f);
    return pressed;
}

bool rightTextLink(const char* id, float rightX, float y, const char* s, ImU32 c = gold, ImFont* font = nullptr) {
    auto* f = font ? font : bodyFont;
    const ImVec2 size = f->CalcTextSizeA(f->FontSize, 1000.f, 0.f, s);
    const float x = rightX - size.x;
    ImGui::SetCursorPos({x, y});
    const bool pressed = ImGui::InvisibleButton(id, size);
    const ImGuiID btnId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    const auto& anim = updateAnim(btnId, hovered, held, 14.0f, 20.0f);
    const float hProgress = ease(anim.hover);
    const float pProgress = ease(anim.press);

    const int r = std::min(255, static_cast<int>((c & 0xFF) * 1.18f));
    const int g = std::min(255, static_cast<int>(((c >> 8) & 0xFF) * 1.18f));
    const int b = std::min(255, static_cast<int>(((c >> 16) & 0xFF) * 1.18f));
    const ImU32 hoverCol = IM_COL32(r, g, b, (c >> 24) & 0xFF);
    const ImU32 linkColor = lerpColor(c, hoverCol, hProgress);

    const float pressDepth = pProgress * 0.5f;
    text(x, y + pressDepth, s, linkColor, f);
    return pressed;
}

void drawMagnifier(float cx, float cy, ImU32 col) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();
    const float x = p.x + cx, y = p.y + cy;
    dl->AddCircle({x - 2.5f, y - 2.5f}, 5.5f, color(col), 16, 1.8f);
    dl->AddLine({x + 1.8f, y + 1.8f}, {x + 6.8f, y + 6.8f}, color(col), 2.0f);
}

void drawSpeaker(float cx, float cy, ImU32 col) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();
    const float x = p.x + cx, y = p.y + cy;
    dl->AddRect({x - 7.f, y - 9.f}, {x + 7.f, y + 9.f}, color(col), 2.5f, 0, 1.6f);
    dl->AddCircle({x, y - 4.5f}, 2.0f, color(col), 12, 1.4f);
    dl->AddCircle({x, y + 3.0f}, 4.2f, color(col), 16, 1.5f);
    dl->AddCircleFilled({x, y + 3.0f}, 1.6f, color(col));
}

void drawHeadphones(float cx, float cy, ImU32 col) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();
    const float x = p.x + cx, y = p.y + cy;
    dl->PathArcTo({x, y + 2.f}, 7.5f, 3.14159265f, 6.2831853f, 16);
    dl->PathStroke(color(col), 0, 1.8f);
    dl->AddRectFilled({x - 8.5f, y + 1.f}, {x - 5.5f, y + 8.f}, color(col), 1.5f);
    dl->AddRectFilled({x + 5.5f, y + 1.f}, {x + 8.5f, y + 8.f}, color(col), 1.5f);
}

void drawMonitor(float cx, float cy, ImU32 col) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();
    const float x = p.x + cx, y = p.y + cy;
    dl->AddRect({x - 8.5f, y - 7.5f}, {x + 8.5f, y + 4.f}, color(col), 2.0f, 0, 1.6f);
    dl->AddLine({x, y + 4.f}, {x, y + 7.f}, color(col), 1.6f);
    dl->AddLine({x - 4.f, y + 7.f}, {x + 4.f, y + 7.f}, color(col), 1.6f);
}

void drawCheckmark(float cx, float cy, ImU32 col) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();
    const float x = p.x + cx, y = p.y + cy;
    dl->AddLine({x - 4.5f, y + 0.2f}, {x - 1.2f, y + 3.5f}, color(col), 2.0f);
    dl->AddLine({x - 1.2f, y + 3.5f}, {x + 4.8f, y - 3.5f}, color(col), 2.0f);
}

void drawChevronDown(float cx, float cy, ImU32 col) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();
    const float x = p.x + cx, y = p.y + cy;
    dl->AddLine({x - 3.5f, y - 1.8f}, {x, y + 2.0f}, color(col), 1.6f);
    dl->AddLine({x, y + 2.0f}, {x + 3.5f, y - 1.8f}, color(col), 1.6f);
}

void drawAnimatedChevron(float cx, float cy, float progress, ImU32 col) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();
    const float x = p.x + cx, y = p.y + cy;
    const float apexY = (1.f - progress) * (y + 2.0f) + progress * (y - 2.0f);
    const float baseY = (1.f - progress) * (y - 1.8f) + progress * (y + 1.8f);
    dl->AddLine({x - 3.5f, baseY}, {x, apexY}, color(col), 1.6f);
    dl->AddLine({x, apexY}, {x + 3.5f, baseY}, color(col), 1.6f);
}

std::string utf8(std::wstring_view s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;
}

static bool g_openMeterPopup = false;

void drawAudioMeter(float x, float y, const audio::AudioLevels* levels, bool preview, audio::AudioMeter* audioMeter = nullptr) {
    auto* dl = ImGui::GetWindowDrawList();
    const auto p = ImGui::GetWindowPos();

    float lVal = 0.0f, rVal = 0.0f;
    float lPeak = 0.0f, rPeak = 0.0f;

    if (levels && levels->active) {
        lVal = levels->left;
        rVal = levels->right;
        lPeak = levels->peakLeft;
        rPeak = levels->peakRight;
    } else if (preview) {
        lVal = 0.76f;
        rVal = 0.62f;
        lPeak = 0.90f;
        rPeak = 0.76f;
    }

    const float labelW = 11.f;
    const float barX = x + labelW;
    const float barW = 110.f;
    const float channelH = 7.5f;
    const float channelGap = 1.5f;

    // Arturia studio graduations (-60 dB to 0 dB non-linear):
    // -48 dB (12%), -36 dB (28%), -24 dB (48%), -18 dB (62%), -12 dB (76%), -6 dB (90%)
    constexpr float tickFracs[] = {0.12f, 0.28f, 0.48f, 0.62f, 0.76f, 0.90f};

    // Numeric dB labels above: "-inf", "-36", "-24", "-12", "-6", "0"
    auto* f = microFont ? microFont : smallFont;
    if (f) {
        centered(barX - 10.f, y, 20.f, "-inf", lightMuted, f);
        centered(barX + barW * 0.28f - 10.f, y, 20.f, "-36", lightMuted, f);
        centered(barX + barW * 0.48f - 10.f, y, 20.f, "-24", lightMuted, f);
        centered(barX + barW * 0.76f - 10.f, y, 20.f, "-12", lightMuted, f);
        centered(barX + barW * 0.90f - 8.f,  y, 16.f, "-6",  lightMuted, f);
        centered(barX + barW - 6.f,          y, 12.f, "0",   lightMuted, f);
    }

    const float trackTopY = y + 12.0f;
    const float lTrackY = trackTopY;
    const float rTrackY = lTrackY + channelH + channelGap;

    auto getBarColor = [](float val) -> ImU32 {
        if (val >= 0.98f) return IM_COL32(235, 75, 60, 255);
        if (val >= 0.90f) return IM_COL32(238, 160, 60, 255);
        return gold;
    };

    // 1. L Channel Background & Fill
    text(x, lTrackY + (channelH - (f ? f->FontSize : 9.f)) * 0.5f, "L", lightMuted, f);
    rect(barX, lTrackY, barW, channelH, trackBg, 2.0f);
    if (lVal > 0.005f) {
        const float wL = std::clamp(barW * lVal, 3.f, barW);
        rect(barX, lTrackY, wL, channelH, getBarColor(lVal), 2.0f);
    }
    if (lPeak > 0.02f) {
        const float px = barX + std::clamp(barW * lPeak, 2.f, barW - 1.5f);
        rect(px - 0.75f, lTrackY - 0.5f, 1.5f, channelH + 1.0f, IM_COL32(255, 255, 255, 225), 0.5f);
    }

    // 2. R Channel Background & Fill
    text(x, rTrackY + (channelH - (f ? f->FontSize : 9.f)) * 0.5f, "R", lightMuted, f);
    rect(barX, rTrackY, barW, channelH, trackBg, 2.0f);
    if (rVal > 0.005f) {
        const float wR = std::clamp(barW * rVal, 3.f, barW);
        rect(barX, rTrackY, wR, channelH, getBarColor(rVal), 2.0f);
    }
    if (rPeak > 0.02f) {
        const float px = barX + std::clamp(barW * rPeak, 2.f, barW - 1.5f);
        rect(px - 0.75f, rTrackY - 0.5f, 1.5f, channelH + 1.0f, IM_COL32(255, 255, 255, 225), 0.5f);
    }

    // 3. Tick marks OVER indicators (strictly inside channels, at all 6 graduations)
    const ImU32 tickCol = color(IM_COL32(220, 222, 230, 230));

    for (int i = 0; i < 6; ++i) {
        const float tx = p.x + barX + barW * tickFracs[i];
        // Inside L channel only
        dl->AddLine({tx, p.y + lTrackY}, {tx, p.y + lTrackY + channelH}, tickCol, 1.2f);
        // Inside R channel only
        dl->AddLine({tx, p.y + rTrackY}, {tx, p.y + rTrackY + channelH}, tickCol, 1.2f);
    }

    // 4. Interactive Device Selector
    ImGui::SetCursorPos({x - 2.f, y});
    ImGui::InvisibleButton("meterAreaBtn", {labelW + barW + 4.f, 30.0f});
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::IsItemClicked(ImGuiMouseButton_Left) || g_openMeterPopup) {
        ImGui::OpenPopup("meter_source_popup");
        g_openMeterPopup = false;
    }
    if (ImGui::IsItemHovered()) {
        std::string curDev = "По умолчанию (Windows)";
        if (audioMeter) {
            auto selName = audioMeter->getSelectedDeviceName();
            if (!selName.empty()) curDev = utf8(selName);
        }
        ImGui::SetTooltip("Источник dB: %s\n(Клик для выбора устройства)", curDev.c_str());
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 3.f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(22, 23, 26, 252));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(52, 54, 62, 255));

    ImGui::SetNextWindowSizeConstraints(ImVec2(270.f, 0.f), ImVec2(380.f, 290.f));

    if (ImGui::BeginPopup("meter_source_popup")) {
        std::vector<audio::AudioDeviceInfo> devices;
        std::wstring selectedId;
        if (audioMeter) {
            devices = audioMeter->getAvailableDevices();
            selectedId = audioMeter->getSelectedDeviceId();
        } else if (preview) {
            devices = {
                {L"{mock-1}", L"Динамики (Realtek Audio)", true},
                {L"{mock-2}", L"Edifier MR3 (Bluetooth)", false},
                {L"{mock-3}", L"LG 27GP850 (NVIDIA High Definition Audio)", false}
            };
            selectedId = L"";
        }

        float maxTextW = labelFont->CalcTextSizeA(labelFont->FontSize, 1000.f, 0.f, "По умолчанию (Windows)").x;
        for (const auto& dev : devices) {
            float tw = labelFont->CalcTextSizeA(labelFont->FontSize, 1000.f, 0.f, utf8(dev.name).c_str()).x;
            if (tw > maxTextW) maxTextW = tw;
        }
        const float menuW = std::clamp(maxTextW + 72.f, 260.f, 350.f);

        auto* meterDl = ImGui::GetWindowDrawList();
        ImFont* hFont = microFont ? microFont : smallFont;
        ImVec2 curPos = ImGui::GetCursorScreenPos();
        meterDl->AddText(hFont, hFont->FontSize, {curPos.x + 8.f, curPos.y + 2.f}, color(muted), "ИСТОЧНИК СИГНАЛА dB");
        ImGui::Dummy(ImVec2(menuW, hFont->FontSize + 6.f));

        curPos = ImGui::GetCursorScreenPos();
        meterDl->AddLine({curPos.x + 4.f, curPos.y}, {curPos.x + menuW - 4.f, curPos.y}, color(IM_COL32(42, 45, 50, 255)), 1.0f);
        ImGui::Dummy(ImVec2(menuW, 5.f));

        auto drawSourceItem = [&](const char* itemId, int iconType, const std::string& title, const std::string& subtitle, bool isSel) -> bool {
            const float itemH = subtitle.empty() ? 32.f : 40.f;
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = ImVec2(p0.x + menuW, p0.y + itemH);

            ImGui::PushID(itemId);
            const bool pressed = ImGui::InvisibleButton("btn", ImVec2(menuW, itemH));
            const ImGuiID btnId = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();
            ImGui::PopID();

            const auto& anim = updateAnim(btnId, hovered, held);
            const float hProg = ease(anim.hover);
            const float pProg = ease(anim.press);
            const float itmDepth = pProg * 0.8f;

            const float rx0 = p0.x + itmDepth;
            const float ry0 = p0.y + itmDepth;
            const float rx1 = p1.x - itmDepth;
            const float ry1 = p1.y - itmDepth;
            const float rw = rx1 - rx0;
            const float rh = ry1 - ry0;

            ImU32 textCol;
            ImU32 subCol;
            ImU32 iconCol;

            if (isSel) {
                constexpr ImU32 goldH = IM_COL32(236, 206, 146, 255);
                const ImU32 bgCol = lerpColor(gold, goldH, hProg);
                meterDl->AddRectFilled({rx0, ry0}, {rx1, ry1}, color(bgCol), 8.f);
                textCol = darkText;
                subCol = IM_COL32(65, 52, 32, 255);
                iconCol = darkText;
            } else {
                const ImU32 itmBg = lerpColor(0, raised, hProg);
                if (itmBg != 0) {
                    meterDl->AddRectFilled({rx0, ry0}, {rx1, ry1}, color(itmBg), 8.f);
                }
                textCol = lerpColor(ink, IM_COL32(255, 255, 255, 255), hProg);
                subCol = muted;
                iconCol = lerpColor(muted, ink, hProg);
            }

            const float iconCx = rx0 + 18.f;
            const float iconCy = ry0 + rh * 0.5f;
            if (iconType == 1) {
                meterDl->PathArcTo({iconCx, iconCy + 1.f}, 6.5f, 3.14159265f, 6.2831853f, 16);
                meterDl->PathStroke(color(iconCol), 0, 1.6f);
                meterDl->AddRectFilled({iconCx - 7.5f, iconCy}, {iconCx - 5.f, iconCy + 6.f}, color(iconCol), 1.2f);
                meterDl->AddRectFilled({iconCx + 5.f, iconCy}, {iconCx + 7.5f, iconCy + 6.f}, color(iconCol), 1.2f);
            } else if (iconType == 2) {
                meterDl->AddRect({iconCx - 7.5f, iconCy - 6.f}, {iconCx + 7.5f, iconCy + 3.f}, color(iconCol), 2.0f, 0, 1.4f);
                meterDl->AddLine({iconCx, iconCy + 3.f}, {iconCx, iconCy + 6.f}, color(iconCol), 1.4f);
                meterDl->AddLine({iconCx - 3.5f, iconCy + 6.f}, {iconCx + 3.5f, iconCy + 6.f}, color(iconCol), 1.4f);
            } else {
                meterDl->AddRect({iconCx - 6.f, iconCy - 7.5f}, {iconCx + 6.f, iconCy + 7.5f}, color(iconCol), 2.0f, 0, 1.4f);
                meterDl->AddCircle({iconCx, iconCy - 3.8f}, 1.6f, color(iconCol), 12, 1.2f);
                meterDl->AddCircle({iconCx, iconCy + 2.5f}, 3.5f, color(iconCol), 16, 1.3f);
                meterDl->AddCircleFilled({iconCx, iconCy + 2.5f}, 1.3f, color(iconCol));
            }

            const float maxLabelW = rw - 36.f - (isSel ? 24.f : 8.f);
            const std::string truncTitle = truncateText(labelFont, title, maxLabelW);

            if (subtitle.empty()) {
                const float ty = ry0 + (rh - labelFont->FontSize) * 0.5f;
                meterDl->AddText(labelFont, labelFont->FontSize, {rx0 + 36.f, ty}, color(textCol), truncTitle.c_str());
            } else {
                ImFont* sFont = microFont ? microFont : smallFont;
                const std::string truncSub = truncateText(sFont, subtitle, maxLabelW);
                meterDl->AddText(labelFont, labelFont->FontSize, {rx0 + 36.f, ry0 + 5.f}, color(textCol), truncTitle.c_str());
                meterDl->AddText(sFont, sFont->FontSize, {rx0 + 36.f, ry0 + 5.f + labelFont->FontSize + 1.f}, color(subCol), truncSub.c_str());
            }

            if (isSel) {
                const float chkX = rx1 - 14.f;
                const float chkY = ry0 + rh * 0.5f;
                meterDl->AddLine({chkX - 4.5f, chkY + 0.2f}, {chkX - 1.2f, chkY + 3.5f}, color(textCol), 2.0f);
                meterDl->AddLine({chkX - 1.2f, chkY + 3.5f}, {chkX + 4.8f, chkY - 3.5f}, color(textCol), 2.0f);
            }

            return pressed;
        };

        const bool isDef = selectedId.empty();
        if (drawSourceItem("dev_default", 0, "По умолчанию (Windows)", "Системное устройство вывода", isDef)) {
            if (audioMeter) audioMeter->setDevice(L"");
            ImGui::CloseCurrentPopup();
        }

        if (!devices.empty()) {
            curPos = ImGui::GetCursorScreenPos();
            meterDl->AddLine({curPos.x + 8.f, curPos.y + 2.f}, {curPos.x + menuW - 8.f, curPos.y + 2.f}, color(IM_COL32(40, 42, 48, 255)), 1.0f);
            ImGui::Dummy(ImVec2(menuW, 5.f));
        }

        for (std::size_t i = 0; i < devices.size(); ++i) {
            const auto& dev = devices[i];
            const bool isSelected = (!selectedId.empty() && dev.id == selectedId);
            const std::string name = utf8(dev.name);
            const std::string sub = dev.isDefault ? "По умолчанию в системе" : "";
            const int icon = getAudioDeviceIconType(name);
            const std::string idStr = "dev_" + std::to_string(i);

            if (drawSourceItem(idStr.c_str(), icon, name, sub, isSelected)) {
                if (audioMeter) audioMeter->setDevice(dev.id);
                ImGui::CloseCurrentPopup();
            }
        }

        if (devices.empty()) {
            curPos = ImGui::GetCursorScreenPos();
            meterDl->AddText(smallFont ? smallFont : bodyFont, 11.f, {curPos.x + 12.f, curPos.y + 4.f}, color(muted), "Другие устройства не найдены");
            ImGui::Dummy(ImVec2(menuW, 24.f));
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}
}

static bool g_profileOpen = false;
static int g_currentPage = 0;

int getTargetHeight() {
    return (g_currentPage == 0 && g_profileOpen) ? kExpandedHeight : kHeight;
}

void closeProfileDropdown() {
    g_profileOpen = false;
}

void styleUi() {
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    auto& s=ImGui::GetStyle();s.WindowPadding={0,0};s.ChildBorderSize=0;s.WindowBorderSize=0;
    s.WindowRounding=16;s.ChildRounding=0;s.FrameRounding=8;s.PopupRounding=12;
    s.FramePadding={12,8};s.ItemSpacing={8,8};
    s.ScrollbarSize=6.f;s.ScrollbarRounding=4.f;
    auto cv=[](ImU32 c){return ImGui::ColorConvertU32ToFloat4(c);};
    s.Colors[ImGuiCol_WindowBg]=cv(background);s.Colors[ImGuiCol_ChildBg]={0,0,0,0};
    s.Colors[ImGuiCol_PopupBg]=cv(surface);s.Colors[ImGuiCol_Text]=cv(ink);s.Colors[ImGuiCol_TextDisabled]=cv(muted);
    s.Colors[ImGuiCol_Button]=cv(raised);s.Colors[ImGuiCol_ButtonHovered]=cv(IM_COL32(54,55,60,255));
    s.Colors[ImGuiCol_ButtonActive]=cv(gold);s.Colors[ImGuiCol_Header]=cv(raised);s.Colors[ImGuiCol_HeaderHovered]=cv(raised);
    s.Colors[ImGuiCol_CheckMark]=cv(gold);
    s.Colors[ImGuiCol_ScrollbarBg]=cv(IM_COL32(0,0,0,0));
    s.Colors[ImGuiCol_ScrollbarGrab]=cv(IM_COL32(65,67,75,255));
    s.Colors[ImGuiCol_ScrollbarGrabHovered]=cv(IM_COL32(95,98,110,255));
    s.Colors[ImGuiCol_ScrollbarGrabActive]=cv(gold);
}
void renderApp(BleClient& ble, void (*showPopup)(bool), bool designPreview, int forcedPage, float currentWindowHeight, const audio::AudioLevels* audioLevels, audio::AudioMeter* audioMeter) {
    static int page = 0, oldPage = 0, volume = 0, preset = 0, eqSubTab = 0;
    static float transition = 1.f, pill = 0.f, pillStart = 0.f;
    static bool volumeDirty = false, powerDialog = false;
    static CustomEqState eq;
    static EqCalibrationState calib;
    static std::uint64_t volumeRev{}, eqRev{}, presetRev{}, calibRev{};
    static int lastPageQueried = -1;
    static bool wasReady = false;

    float dropdownProgress = 0.0f;
    if (kExpandedHeight > kHeight) {
        dropdownProgress = std::clamp((currentWindowHeight - static_cast<float>(kHeight)) / static_cast<float>(kExpandedHeight - kHeight), 0.0f, 1.0f);
    }

    if (forcedPage >= 0 && forcedPage <= 2) {
        page = forcedPage;
        oldPage = forcedPage;
        pillStart = page * 136.f;
        pill = page * 136.f;
        transition = 1.0f;
        g_profileOpen = false;
        if (forcedPage == 1) eqSubTab = 0;
    } else if (forcedPage == 5) {
        page = 0;
        oldPage = 0;
        pillStart = 0.f;
        pill = 0.f;
        transition = 1.0f;
        g_profileOpen = true;
        dropdownProgress = 1.0f;
        currentWindowHeight = static_cast<float>(kExpandedHeight);
    } else if (forcedPage == 6) {
        page = 1;
        oldPage = 1;
        pillStart = 136.f;
        pill = 136.f;
        transition = 1.0f;
        g_profileOpen = false;
        eqSubTab = 1;
    } else if (forcedPage == -9) {
        page = 1;
        oldPage = 1;
        pillStart = 136.f;
        pill = 136.f;
        transition = 1.0f;
        g_profileOpen = false;
        eqSubTab = 0;
    } else if (forcedPage == 7) {
        page = 0;
        oldPage = 0;
        pillStart = 0.f;
        pill = 0.f;
        transition = 1.0f;
        g_profileOpen = false;
        g_openMeterPopup = true;
    }
    g_currentPage = page;

    auto snap = ble.snapshot();

    // Offline visual preview
    if (designPreview) {
        if (forcedPage == -2) {
            // Preview disconnected empty state
            snap.state = LinkState::Idle;
            snap.discovered.clear();
        } else if (forcedPage == -3) {
            // Preview device selection list
            snap.state = LinkState::Scanning;
            snap.discovered = {
                {L"EDIFIER MR3", L"Гостиная • 2 м", L"", 0x112233445566ULL, -48},
                {L"NeoBass HiFi", L"Кабинет • 5 м", L"", 0x778899AABBCCULL, -68},
                {L"Studio Monitor", L"Студия • 8 м", L"", 0xDDEEFF001122ULL, -80}
            };
            snap.selectedAddress = 0x112233445566ULL;
        } else if (forcedPage == -9) {
            // Preview 9-band custom EQ (MR5 / MR4.5 / MR4 MKII)
            snap.state = LinkState::Ready;
            snap.modelName = L"MR5";
            snap.deviceName = L"EDIFIER MR5";
            snap.nineBandFamily = true;
            snap.volumeKnown = snap.eqKnown = snap.ledKnown = true;
            snap.volumeCurrent = 20;
            snap.volumeMax = 30;
            snap.eqCurrent = 0;
            snap.ledCurrent = true;
            snap.volumeRevision = snap.eqRevision = snap.ledRevision = snap.customEqRevision = snap.calibrationRevision = 1;
            snap.customEq.valid = true;
            snap.customEq.eqIndex = 12;
            snap.customEq.byte0 = 0;
            snap.customEq.bands = {
                {0, 0,  32,   6, 0},
                {1, 0,  64,  10, 0},
                {2, 0, 125,   8, 0},
                {3, 0, 250,   4, 0},
                {4, 0, 500,   6, 0},
                {5, 0, 1000, 12, 0},
                {6, 0, 2000,  8, 0},
                {7, 0, 4000, 10, 0},
                {8, 0, 8000,  6, 0}
            };
            snap.calibration.valid = true;
            snap.calibration.index = 0;
            snap.calibration.byte0 = 0;
            snap.calibration.lowCutoffFreq = 80;
            snap.calibration.lowCutoffSlope = 2; // -18 dB/oct
            snap.calibration.acousticSpace = 1;  // -1 dB
            snap.calibration.desktopControl = 1; // On
            snap.firmwareVersion = "v1.0.3";
            snap.firmwareRevision = 1;
        } else {
            snap.state = LinkState::Ready;
            snap.volumeKnown = snap.eqKnown = snap.ledKnown = true;
            snap.volumeCurrent = 18;
            snap.eqCurrent = 0;
            snap.ledCurrent = true;
            snap.volumeRevision = snap.eqRevision = snap.ledRevision = snap.customEqRevision = snap.calibrationRevision = 1;
            snap.customEq.valid = true;
            snap.customEq.eqIndex = 7;
            snap.customEq.byte0 = 0;
            snap.customEq.bands = {
                {0, 0, 125,  10, 0},
                {1, 0, 250,   6, 0},
                {2, 0, 500,   4, 0},
                {3, 0, 1000,  8, 0},
                {4, 0, 4000, 12, 0},
                {5, 0, 8000, 10, 0}
            };
            snap.calibration.valid = true;
            snap.calibration.index = 0;
            snap.calibration.byte0 = 0;
            snap.calibration.lowCutoffFreq = 80;
            snap.calibration.lowCutoffSlope = 2; // -18 dB/oct
            snap.calibration.acousticSpace = 1;  // -1 dB
            snap.calibration.desktopControl = 1; // On
            snap.firmwareVersion = "v1.0.3";
            snap.firmwareRevision = 1;
        }
    }

    const bool ready = snap.state == LinkState::Ready;
    if (ready) {
        if (!wasReady || (page == 1 && lastPageQueried != 1)) {
            if (page == 1) {
                ble.sendRead(Command::GetEq);
                ble.sendRead(Command::GetCustomEq);
                lastPageQueried = 1;
            }
        }
        if (page != 1) lastPageQueried = page;
        wasReady = true;
    } else {
        wasReady = false;
        lastPageQueried = -1;
    }

    if (!ImGui::IsAnyItemActive()) {
        if (snap.volumeKnown && volumeRev != snap.volumeRevision) {
            volume = snap.volumeCurrent; volumeRev = snap.volumeRevision; volumeDirty = false;
        }
        if (snap.customEq.valid && (eqRev != snap.customEqRevision || designPreview)) {
            eq = snap.customEq; eqRev = snap.customEqRevision;
        }
        if (snap.calibration.valid && (calibRev != snap.calibrationRevision || designPreview)) {
            calib = snap.calibration; calibRev = snap.calibrationRevision;
        }
    }
    if (!snap.customEq.valid && !designPreview) eq = {};
    if (!snap.calibration.valid && !designPreview) calib = {};
    if (designPreview) {
        eq = snap.customEq;
        calib = snap.calibration;
    }
    if (snap.eqKnown && presetRev != snap.eqRevision) { preset = snap.eqCurrent; presetRev = snap.eqRevision; }
    if (!ready) volumeDirty = false;


    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    transition = animations ? std::min(1.f, transition + ImGui::GetIO().DeltaTime / 0.32f) : 1.f;
    const float progress = ease(transition);
    pill = pillStart + (page * 136.f - pillStart) * progress;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({float(kWidth), currentWindowHeight});
    ImGui::Begin("Edifier", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (g_profileOpen) {
            g_profileOpen = false;
        } else {
            showPopup(false);
        }
    }

    // Header (Identity left, Action right)
    const std::string title = "EDIFIER  /  " + utf8(snap.modelName.empty() ? L"MR3" : snap.modelName);
    text(20, 16, title.c_str(), ink, titleFont);
    const auto wp = ImGui::GetWindowPos();

    // Stereo Audio Level Meter (between title and action button)
    drawAudioMeter(192.f, 15.f, audioLevels, designPreview, audioMeter);

    const bool inDiscovery = !ready && (snap.state == LinkState::Scanning || snap.state == LinkState::Connecting || !snap.discovered.empty());

    if (ready) {
        ImGui::GetWindowDrawList()->AddCircleFilled({wp.x + 24, wp.y + 45}, 3.5f, color(green));
        text(34, 39, designPreview ? "Предпросмотр" : "Подключено", green, smallFont);

        // --- 4-bar BLE RSSI Indicator ---
        const float rssiX = 118.f;
        const float rssiBaseY = 47.f;
        short rssiVal = designPreview ? -52 : snap.rssi;
        int activeBars = 0;
        const char* rssiQuality = "Нет данных";
        ImU32 rssiColor = color(green);
        if (rssiVal >= -55) { activeBars = 4; rssiQuality = "Отличный сигнал"; rssiColor = color(green); }
        else if (rssiVal >= -70) { activeBars = 3; rssiQuality = "Хороший сигнал"; rssiColor = color(green); }
        else if (rssiVal >= -82) { activeBars = 2; rssiQuality = "Средний сигнал"; rssiColor = color(gold); }
        else if (rssiVal > -127) { activeBars = 1; rssiQuality = "Слабый сигнал"; rssiColor = color(IM_COL32(235, 75, 60, 255)); }

        const float barHeights[4] = {3.5f, 5.5f, 7.5f, 9.5f};
        for (int b = 0; b < 4; ++b) {
            const float bx = rssiX + b * 4.0f;
            const float bh = barHeights[b];
            const ImU32 bCol = (b < activeBars) ? rssiColor : color(IM_COL32(65, 67, 75, 180));
            rect(bx, rssiBaseY - bh, 2.5f, bh, bCol, 1.0f);
        }
        ImGui::SetCursorPos({rssiX - 2.f, rssiBaseY - 12.f});
        ImGui::InvisibleButton("rssiArea", {18.f, 14.f});
        if (ImGui::IsItemHovered()) {
            char rssiTip[64];
            std::snprintf(rssiTip, sizeof(rssiTip), "Сигнал BLE: %d dBm (%s)", rssiVal, rssiQuality);
            ImGui::SetTooltip("%s", rssiTip);
        }

        if (button("hide", "Скрыть", 328, 18, 92, 32)) showPopup(false);
    } else if (inDiscovery) {
        ImGui::GetWindowDrawList()->AddCircle({wp.x + 24, wp.y + 45}, 3.5f, color(muted), 0, 1.2f);
        text(34, 39, "Поиск…", muted, smallFont);
        if (button("cancelDiscovery", "Отмена", 328, 18, 92, 32)) {
            ble.disconnect();
        }
    } else {
        ImGui::GetWindowDrawList()->AddCircle({wp.x + 24, wp.y + 45}, 3.5f, color(muted), 0, 1.2f);
        text(34, 39, "Не подключено", muted, smallFont);
        if (button("hide", "Скрыть", 328, 18, 92, 32)) showPopup(false);
    }

    // DISCONNECTED / DISCOVERY SCREENS
    if (!ready) {
        if (!inDiscovery) {
            // Disconnected state
            centered(20, 126, 400, "Ваш звук. Под контролем.", ink, heroFont);
            centered(20, 158, 400, "Включите колонку и начните поиск.", ink, bodyFont);
            if (button("findSpeaker", "Найти колонку", 130, 204, 180, 36, false, bodyFont)) {
                ble.scan();
            }
        } else {
            // Device Discovery List
            const float badgeCx = 38.f, badgeCy = 74.f;
            ImGui::GetWindowDrawList()->AddCircleFilled({wp.x + badgeCx, wp.y + badgeCy}, 18.f, color(raised));
            drawMagnifier(badgeCx, badgeCy, green);

            char countText[64];
            const auto count = snap.discovered.size();
            if (count == 1) {
                std::snprintf(countText, sizeof(countText), "Найдено 1 устройство");
            } else if (count >= 2 && count <= 4) {
                std::snprintf(countText, sizeof(countText), "Найдено %zu устройства", count);
            } else if (count > 4) {
                std::snprintf(countText, sizeof(countText), "Найдено %zu устройств", count);
            } else {
                std::snprintf(countText, sizeof(countText), "Поиск устройств…");
            }
            text(68, 62, countText, ink, cardTitleFont);
            text(68, 82, "Выберите колонку и подключитесь", muted, bodyFont);

            const float startY = 112.f;
            for (std::size_t i = 0; i < snap.discovered.size() && i < 3; ++i) {
                const auto& dev = snap.discovered[i];
                const float cardY = startY + i * 72.f;

                rect(20, cardY, 400, 64, raised, 16.f);
                rect(32, cardY + 12, 40, 40, container, 10.f);

                std::string devName = utf8(dev.deviceName);
                if (devName.empty()) devName = "EDIFIER MR3";

                if (devName.find("Head") != std::string::npos || devName.find("Neo") != std::string::npos) {
                    drawHeadphones(52, cardY + 32, ink);
                } else if (devName.find("Monitor") != std::string::npos && devName.find("EDIFIER") == std::string::npos) {
                    drawMonitor(52, cardY + 32, ink);
                } else {
                    drawSpeaker(52, cardY + 32, ink);
                }

                text(84, cardY + 14, devName.c_str(), ink, cardTitleFont);
                std::string devDetail = utf8(dev.modelName);
                if (devDetail.empty()) devDetail = "MR3 · " + std::to_string(dev.rssi) + " dBm";
                text(84, cardY + 36, devDetail.c_str(), muted, bodyFont);

                ImGui::PushID(static_cast<int>(i));
                const bool isTarget = (i == 0 || snap.selectedAddress == dev.address);
                if (isTarget) {
                    const char* connLabel = (snap.state == LinkState::Connecting && snap.selectedAddress == dev.address)
                        ? "Подключение…" : "Подключить";
                    if (greenCheckButton("btnConnPill", connLabel, 292, cardY + 18, 116, 28, bodyFont)) {
                        ble.connect(dev.address);
                    }
                } else {
                    if (rightTextLink("btnConnLink", 396, cardY + 22, "+  Подключить", ink, bodyFont)) {
                        ble.connect(dev.address);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
        return;
    }

    // MAIN NAVIGATION TABS (Frames 1-3)
    const char* tabs[] = {"Звук", "Эквалайзер", "Устройство"};
    rect(20 + pill, 64, 128, 32, gold, 8.f);

    for (int i = 0; i < 3; i++) {
        ImGui::SetCursorPos({20 + i * 136.f, 64});
        ImGui::PushID(i);
        const bool tabPressed = ImGui::InvisibleButton("tab", {128, 32});
        const ImGuiID tabId = ImGui::GetItemID();
        const bool tabHovered = ImGui::IsItemHovered();
        const bool tabHeld = ImGui::IsItemActive();
        const auto& tabAnim = updateAnim(tabId, tabHovered, tabHeld);
        if (i != page && tabAnim.hover > 0.001f) {
            rect(20 + i * 136.f, 64, 128, 32, lerpColor(0, IM_COL32(40, 42, 46, 160), ease(tabAnim.hover)), 8.f);
        }

        if (tabPressed && i != page) {
            oldPage = page;
            page = i;
            pillStart = pill;
            transition = 0;
            g_profileOpen = false;
            if (page == 1 && ready) {
                ble.sendRead(Command::GetEq);
                ble.sendRead(Command::GetCustomEq);
                lastPageQueried = 1;
            }
        }
        const ImU32 unselectedTextCol = lerpColor(ink, IM_COL32(255, 255, 255, 255), ease(tabAnim.hover));
        centered(20 + i * 136.f, 74, 128, tabs[i], i == page ? darkText : unselectedTextCol, bodyFont);
        ImGui::PopID();
    }

    auto panel = [&](int which, bool interactive, float opacity, float dx) {
        ImGui::SetCursorPos({20 + dx, 108});
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * opacity);
        ImGui::PushID(which);
        const float panelHeight = (which == 0) ? (258.f + 56.f * dropdownProgress) : 258.f;
        ImGui::BeginChild("panel", {400, panelHeight}, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | (interactive ? 0 : ImGuiWindowFlags_NoInputs));
        ImGui::BeginDisabled(!interactive);

        if (which == 0) {
            // TAB 1: ЗВУК
            rect(0, 0, 400, 100, surface, 16.f);

            static int unmutedVolume = 15;
            const bool isMuted = (volume == 0);
            if (volume > 0) unmutedVolume = volume;

            // 1. Mute / Speaker Icon Button and Label at y=24
            const float spkX = 16.f;
            const float spkY = 24.f;
            auto* spkDl = ImGui::GetWindowDrawList();
            const auto spkP = ImGui::GetWindowPos();
            const ImU32 spkCol = isMuted ? color(IM_COL32(235, 75, 60, 255)) : color(muted);

            // Speaker box
            spkDl->AddRectFilled({spkP.x + spkX, spkP.y + spkY + 3.f}, {spkP.x + spkX + 4.f, spkP.y + spkY + 9.f}, spkCol, 1.0f);
            // Speaker cone
            const ImVec2 cone[4] = {
                {spkP.x + spkX + 4.f, spkP.y + spkY + 3.f},
                {spkP.x + spkX + 10.f, spkP.y + spkY - 0.5f},
                {spkP.x + spkX + 10.f, spkP.y + spkY + 12.5f},
                {spkP.x + spkX + 4.f, spkP.y + spkY + 9.f}
            };
            spkDl->AddConvexPolyFilled(cone, 4, spkCol);
            if (isMuted) {
                spkDl->AddLine({spkP.x + spkX - 1.f, spkP.y + spkY - 1.f}, {spkP.x + spkX + 13.f, spkP.y + spkY + 13.f}, color(IM_COL32(235, 75, 60, 255)), 1.8f);
            } else {
                spkDl->PathArcTo({spkP.x + spkX + 8.f, spkP.y + spkY + 6.f}, 5.f, -0.9f, 0.9f, 8);
                spkDl->PathStroke(spkCol, 0, 1.4f);
            }

            ImGui::SetCursorPos({14.f, 20.f});
            if (ImGui::InvisibleButton("muteSpkBtn", {110.f, 22.f})) {
                if (volume > 0) {
                    unmutedVolume = volume;
                    volume = 0;
                    if (interactive) {
                        ble.sendMutation(Command::SetVolume, {0});
                        ble.sendRead(Command::GetVolume);
                    }
                } else {
                    volume = (unmutedVolume > 0 ? unmutedVolume : 15);
                    if (interactive) {
                        ble.sendMutation(Command::SetVolume, {static_cast<std::uint8_t>(volume)});
                        ble.sendRead(Command::GetVolume);
                    }
                }
                volumeDirty = false;
                edifier::osd::showVolume(volume, snap.volumeMax, volume == 0);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(isMuted ? "Включить звук (Unmute)" : "Заглушить (Mute)");
            }

            text(36, 24, isMuted ? "Звук заглушен" : "Громкость", isMuted ? IM_COL32(235, 75, 60, 255) : muted, labelFont);

            char amount[32];
            if (isMuted) {
                std::snprintf(amount, sizeof(amount), "MUTE");
            } else if (snap.volumeKnown) {
                std::snprintf(amount, sizeof(amount), "%d / %d", volume, snap.volumeMax);
            } else {
                std::snprintf(amount, sizeof(amount), "— / %d", snap.volumeMax);
            }
            const auto amtSize = numberFont->CalcTextSizeA(numberFont->FontSize, 1000.f, 0.f, amount);
            const float amtX = 384.f - amtSize.x;
            text(amtX, 16, amount, isMuted ? IM_COL32(235, 75, 60, 255) : ink, numberFont);

            // Clickable amount text
            ImGui::SetCursorPos({amtX - 4.f, 14.f});
            if (ImGui::InvisibleButton("muteNumBtn", {amtSize.x + 8.f, amtSize.y + 4.f})) {
                if (volume > 0) {
                    unmutedVolume = volume;
                    volume = 0;
                    if (interactive) {
                        ble.sendMutation(Command::SetVolume, {0});
                        ble.sendRead(Command::GetVolume);
                    }
                } else {
                    volume = (unmutedVolume > 0 ? unmutedVolume : 15);
                    if (interactive) {
                        ble.sendMutation(Command::SetVolume, {static_cast<std::uint8_t>(volume)});
                        ble.sendRead(Command::GetVolume);
                    }
                }
                volumeDirty = false;
                edifier::osd::showVolume(volume, snap.volumeMax, volume == 0);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(isMuted ? "Кликните, чтобы включить звук" : "Кликните, чтобы заглушить");
            }

            ImGui::BeginDisabled(!snap.volumeKnown);
            ImGui::SetCursorPos({16, 56});
            ImGui::InvisibleButton("volume", {368, 28});
            const bool volHovered = ImGui::IsItemHovered();
            const bool volActive = ImGui::IsItemActive();
            if (volActive) {
                const float local = ImGui::GetIO().MousePos.x - ImGui::GetWindowPos().x - 16;
                int next = std::clamp(int(std::round(local / 368.f * snap.volumeMax)), 0, snap.volumeMax);
                if (next != volume) {
                    volumeDirty = true;
                    volume = next;
                    if (volume > 0) unmutedVolume = volume;
                    edifier::osd::showVolume(volume, snap.volumeMax, volume == 0);
                }
            }
            if (ImGui::IsItemDeactivated() && volumeDirty && interactive) {
                ble.sendMutation(Command::SetVolume, {static_cast<std::uint8_t>(volume)});
                ble.sendRead(Command::GetVolume);
                volumeDirty = false;
            }
            if (volHovered || volActive) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            rect(16, 66, 368, 8, trackBg, 4.f);
            const float fill = 368.f * volume / std::max(1, snap.volumeMax);
            if (snap.volumeKnown && fill > 0) rect(16, 66, fill, 8, isMuted ? IM_COL32(235, 75, 60, 255) : gold, 4.f);

            // Draggable grab thumb / knob
            if (snap.volumeKnown) {
                const float thumbX = std::clamp(16.f + fill, 16.f, 384.f);
                const float thumbY = 70.0f;
                const auto& thumbAnim = updateAnim(ImGui::GetID("volThumb"), volHovered, volActive, 14.f, 20.f);
                const float hExp = ease(thumbAnim.hover);
                const float aExp = ease(thumbAnim.press);
                const float thumbR = 7.0f + hExp * 1.2f + aExp * 0.8f;

                auto* dl = ImGui::GetWindowDrawList();
                const auto p = ImGui::GetWindowPos();
                const ImVec2 center = {p.x + thumbX, p.y + thumbY};

                // 1. Soft drop shadow
                dl->AddCircleFilled({center.x, center.y + 1.2f}, thumbR + 2.5f, color(IM_COL32(0, 0, 0, 80)), 24);
                // 2. High-contrast dark rim
                dl->AddCircleFilled(center, thumbR + 1.5f, color(IM_COL32(20, 21, 24, 255)), 24);
                // 3. Thumb body
                const ImU32 bodyBase = isMuted ? IM_COL32(235, 75, 60, 255) : gold;
                const ImU32 bodyHover = isMuted ? IM_COL32(245, 95, 80, 255) : IM_COL32(238, 212, 158, 255);
                const ImU32 thumbCol = lerpColor(bodyBase, bodyHover, hExp);
                dl->AddCircleFilled(center, thumbR, color(thumbCol), 24);
                // 4. Subtle inner tactile grab dot (dimple)
                dl->AddCircleFilled(center, thumbR * 0.35f, color(IM_COL32(25, 23, 19, 190)), 16);
            }

            if (interactive && snap.volumeKnown && (ImGui::IsItemHovered() || ImGui::IsWindowHovered())) {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f) {
                    ble.changeVolume(wheel > 0.0f ? 1 : -1);
                    volume = ble.snapshot().volumeCurrent;
                    if (volume > 0) unmutedVolume = volume;
                    volumeDirty = false;
                    edifier::osd::showVolume(volume, snap.volumeMax, volume == 0);
                }
            }
            ImGui::EndDisabled();

            const char* presets[] = {"Monitor", "Music", "Custom"};
            const char* currentPreset = (snap.eqKnown && preset >= 0 && preset < 3) ? presets[preset] : "Monitor";

            const ImGuiID rowId = ImGui::GetID("profileRowBtn");
            const auto pRow = ImGui::GetWindowPos();
            const bool rowHovered = interactive && ImGui::IsMouseHoveringRect({pRow.x, pRow.y + 112}, {pRow.x + 400, pRow.y + 160});
            const bool rowHeld = rowHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const auto& rowAnim = updateAnim(rowId, rowHovered, rowHeld, 12.0f, 20.0f);
            const ImU32 rowBg = lerpColor(surface, IM_COL32(35, 36, 40, 255), ease(rowAnim.hover));
            const float rowPress = ease(rowAnim.press) * 0.8f;

            rect(rowPress, 112 + rowPress, 400 - rowPress * 2.f, 48 - rowPress * 2.f, rowBg, 10.f);
            text(14, 128 + rowPress * 0.5f, "Профиль звучания", ink, bodyFont);

            const auto curSize = bodyFont->CalcTextSizeA(bodyFont->FontSize, 1000.f, 0.f, currentPreset);
            const float rightLabelX = 368.f - curSize.x;
            text(rightLabelX, 128 + rowPress * 0.5f, currentPreset, gold, bodyFont);
            drawAnimatedChevron(382, 136 + rowPress * 0.5f, dropdownProgress, gold);

            ImGui::SetCursorPos({0, 112});
            if (ImGui::InvisibleButton("profileRowBtn", {400, 48})) {
                g_profileOpen = !g_profileOpen;
            }

            if (dropdownProgress > 0.001f) {
                const float cardH = 126.f * dropdownProgress;
                auto* dl = ImGui::GetWindowDrawList();
                const auto p = ImGui::GetWindowPos();
                dl->PushClipRect({p.x, p.y + 168}, {p.x + 400, p.y + 168 + cardH}, true);
                rect(0, 168, 400, 126, surface, 16.f);

                const float itemAlpha = std::clamp(dropdownProgress * 1.6f - 0.2f, 0.f, 1.f);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * itemAlpha);
                for (int i = 0; i < 3; ++i) {
                    const float itemY = 172.f + i * 40.f;
                    ImGui::SetCursorPos({4, itemY});
                    ImGui::PushID(i);
                    const bool isSel = (preset == i);
                    const bool itemHovered = interactive && ImGui::IsMouseHoveringRect(
                        {p.x + 4, p.y + itemY}, {p.x + 396, p.y + itemY + 38});
                    const bool itemHeld = itemHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
                    const ImGuiID itmId = ImGui::GetID("presetItem");
                    const auto& itmAnim = updateAnim(itmId, itemHovered, itemHeld);
                    const float hProg = ease(itmAnim.hover);
                    const float pProg = ease(itmAnim.press);
                    const float itmDepth = pProg * 0.8f;

                    if (isSel) {
                        constexpr ImU32 goldH = IM_COL32(236, 206, 146, 255);
                        rect(4 + itmDepth, itemY + itmDepth, 392 - itmDepth * 2.f, 38 - itmDepth * 2.f, lerpColor(gold, goldH, hProg), 8.f);
                        text(18, itemY + 11 + itmDepth * 0.5f, presets[i], darkText, labelFont);
                        drawCheckmark(376, itemY + 19 + itmDepth * 0.5f, darkText);
                    } else {
                        const ImU32 itmBg = lerpColor(0, raised, hProg);
                        if (itmBg != 0) {
                            rect(4 + itmDepth, itemY + itmDepth, 392 - itmDepth * 2.f, 38 - itmDepth * 2.f, itmBg, 8.f);
                        }
                        text(18, itemY + 11 + itmDepth * 0.5f, presets[i], lerpColor(ink, IM_COL32(255, 255, 255, 255), hProg), labelFont);
                    }
                    if (interactive && dropdownProgress > 0.6f && ImGui::InvisibleButton("presetItem", {392, 38})) {
                        preset = i;
                        ble.setEqPreset(i);
                        g_profileOpen = false;
                    }
                    ImGui::PopID();
                }
                ImGui::PopStyleVar();
                dl->PopClipRect();
            }
        } else if (which == 1) {
            // TAB 2: ЭКВАЛАЙЗЕР И АКУСТИЧЕСКАЯ КОМПЕНСАЦИЯ
            const std::size_t bandCount = (eq.valid && !eq.bands.empty()) ? eq.bands.size() : (snap.nineBandFamily ? 9 : 6);
            char eqTabTitle[32];
            std::snprintf(eqTabTitle, sizeof(eqTabTitle), "%zu-полосный EQ", bandCount);
            if (button("subTabEq", eqTabTitle, 0, 0, 196, 28, eqSubTab == 0, labelFont, 6.f)) {
                eqSubTab = 0;
                if (ready) ble.sendRead(Command::GetCustomEq);
            }

            if (button("subTabAcoustic", "Срез НЧ и акустика", 204, 0, 196, 28, eqSubTab == 1, labelFont, 6.f)) {
                eqSubTab = 1;
                if (ready) ble.sendRead(Command::GetEq);
            }

            if (eqSubTab == 0) {
                // ПОДВКЛАДКА 1: КАСТОМНЫЙ EQ
                rect(0, 34, 400, 142, surface, 12.f);

                if (!eq.valid || eq.bands.empty()) {
                    centered(0, 80, 400, "Настройки ещё не получены", muted);
                    if (button("readEq", "Загрузить эквалайзер", 92, 116, 216)) ble.sendRead(Command::GetCustomEq);
                } else {
                    const std::size_t count = eq.bands.size();
                    const float padX = 16.f;
                    const float availWidth = 400.f - padX * 2.f;
                    const float bandWidth = (count <= 6) ? 46.f : ((count <= 8) ? 38.f : 34.f);
                    const float totalBandsWidth = bandWidth * count;
                    const float spacing = count > 1 ? (availWidth - totalBandsWidth) / static_cast<float>(count - 1) : 0.f;
                    const float thumbRadius = (count > 6) ? 4.5f : 5.5f;

                    const float topLabelY = 50.f;
                    const float trackY = 69.f;
                    const float trackH = 72.f;
                    const float bottomLabelY = 149.f;

                    for (std::size_t i = 0; i < count; i++) {
                        auto& band = eq.bands[i];
                        const float bx = padX + i * (bandWidth + spacing);
                        const float dB = band.gain * 0.5f - 3.0f;
                        char label[24];
                        std::snprintf(label, sizeof(label), "%+.1f", dB);
                        centered(bx, topLabelY, bandWidth, label, lightMuted, smallFont);

                        ImGui::PushID(static_cast<int>(i));
                        ImGui::SetCursorPos({bx, 46});
                        ImGui::InvisibleButton("gain", {bandWidth, 118.f});
                        if (ImGui::IsItemActive()) {
                            const float localY = ImGui::GetIO().MousePos.y - ImGui::GetWindowPos().y - trackY;
                            band.gain = static_cast<std::uint8_t>(std::clamp(int(std::round((1.f - localY / trackH) * 12.f)), 0, 12));
                        }
                        if (ImGui::IsItemDeactivated() && interactive) {
                            ble.sendMutation(Command::SetCustomEqBand, makeCustomEqBand(eq, i));
                        }

                        const float trackX = bx + (bandWidth - 4.f) * 0.5f;
                        rect(trackX, trackY, 4, trackH, trackBg, 2.f);

                        const auto p = ImGui::GetWindowPos();
                        const float thumbY = trackY + trackH * (1.f - band.gain / 12.f);
                        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + bx + bandWidth * 0.5f, p.y + thumbY}, thumbRadius, color(gold));

                        if (band.frequency >= 1000) {
                            const float k = band.frequency / 1000.f;
                            if (std::abs(k - std::round(k)) < 0.05f) {
                                std::snprintf(label, sizeof(label), "%uk", static_cast<unsigned>(std::round(k)));
                            } else {
                                std::snprintf(label, sizeof(label), "%gk", k);
                            }
                        } else {
                            std::snprintf(label, sizeof(label), "%u", band.frequency);
                        }
                        centered(bx, bottomLabelY, bandWidth, label, lightMuted, smallFont);
                        ImGui::PopID();
                    }
                }

                // БАЗОВЫЙ ПРОФИЛЬ
                rect(0, 184, 400, 42, surface, 10.f);
                text(14, 198, "Базовый профиль", ink, bodyFont);

                const bool isMonitor = (eq.byte0 == 0);
                const bool isMusic = (eq.byte0 == 1);

                if (button("baseMonitorBtn", "Монитор", 204, 191, 92, 28, isMonitor, labelFont, 6.f)) {
                    eq.byte0 = 0;
                    ble.setCustomEqBase(0);
                }

                if (button("baseMusicBtn", "Музыка", 302, 191, 92, 28, isMusic, labelFont, 6.f)) {
                    eq.byte0 = 1;
                    ble.setCustomEqBase(1);
                }

                text(0, 234, "Настройте звук под себя", muted, bodyFont);
                if (rightTextLink("refreshEqLink", 260, 234, "Обновить с колонок", muted, smallFont)) {
                    ble.sendRead(Command::GetCustomEq);
                }
                ImGui::BeginDisabled(!eq.valid);
                if (rightTextLink("resetEqLink", 400, 234, "Сбросить EQ", gold, bodyFont)) {
                    for (std::size_t i = 0; i < eq.bands.size(); i++) {
                        eq.bands[i].gain = 6;
                        ble.sendMutation(Command::SetCustomEqBand, makeCustomEqBand(eq, i));
                    }
                    ble.sendRead(Command::GetCustomEq);
                }
                ImGui::EndDisabled();

            } else {
                // ПОДВКЛАДКА 2: АКУСТИКА И СРЕЗ НЧ (Sound Calibration)
                if (!calib.valid) {
                    rect(0, 34, 400, 204, surface, 12.f);
                    centered(0, 85, 400, "Настройки акустики загружаются…", ink, bodyFont);
                    centered(0, 110, 400, "Срез НЧ, крутизна и акустическое пространство", muted, smallFont);
                    if (button("readCalibBtn", "Загрузить с колонок", 100, 146, 200, 32)) {
                        ble.sendRead(Command::GetEq);
                    }
                } else {
                    // Карточка 1: Срез низких частот (Low Cutoff)
                    rect(0, 34, 400, 92, surface, 12.f);
                    text(14, 44, "Срез низких частот", ink, bodyFont);
                    char freqStr[32];
                    std::snprintf(freqStr, sizeof(freqStr), "%d Гц", calib.lowCutoffFreq ? calib.lowCutoffFreq : 20);
                    const auto fsz = labelFont->CalcTextSizeA(labelFont->FontSize, 1000.f, 0.f, freqStr);
                    text(386.f - fsz.x, 44, freqStr, gold, labelFont);

                    // Ползунок частоты с кнопками - и +
                    if (button("freqMinus", "-", 14, 62, 26, 24, false, labelFont, 5.f)) {
                        calib.lowCutoffFreq = static_cast<std::uint8_t>(std::max(20, (calib.lowCutoffFreq ? calib.lowCutoffFreq : 20) - 5));
                        calib.valid = true;
                        ble.setEqCalibration(calib);
                    }
                    if (button("freqPlus", "+", 360, 62, 26, 24, false, labelFont, 5.f)) {
                        calib.lowCutoffFreq = static_cast<std::uint8_t>(std::min(100, (calib.lowCutoffFreq ? calib.lowCutoffFreq : 20) + 5));
                        calib.valid = true;
                        ble.setEqCalibration(calib);
                    }

                    const float trackStartX = 48.f, trackW = 304.f;
                    rect(trackStartX, 71, trackW, 6, trackBg, 3.f);
                    const float currentFreqNorm = std::clamp(((calib.lowCutoffFreq ? calib.lowCutoffFreq : 20) - 20) / 80.f, 0.f, 1.f);
                    if (currentFreqNorm > 0.01f) {
                        rect(trackStartX, 71, trackW * currentFreqNorm, 6, gold, 3.f);
                    }
                    const auto wp2 = ImGui::GetWindowPos();
                    const float freqThumbX = trackStartX + trackW * currentFreqNorm;

                    ImGui::SetCursorPos({trackStartX, 62});
                    ImGui::InvisibleButton("freqTrackBtn", {trackW, 24.f});
                    const bool freqHovered = ImGui::IsItemHovered();
                    const bool freqActive = ImGui::IsItemActive();
                    if (freqHovered || freqActive) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    }
                    if (freqActive) {
                        const float localX = ImGui::GetIO().MousePos.x - ImGui::GetWindowPos().x - trackStartX;
                        const float norm = std::clamp(localX / trackW, 0.f, 1.f);
                        const int steps = static_cast<int>(std::round(norm * 16.f));
                        calib.lowCutoffFreq = static_cast<std::uint8_t>(20 + steps * 5);
                        calib.valid = true;
                    }
                    if (ImGui::IsItemDeactivated() && interactive) {
                        ble.setEqCalibration(calib);
                    }

                    // Tactile knob for frequency slider
                    const auto& fAnim = updateAnim(ImGui::GetID("freqKnob"), freqHovered, freqActive, 14.f, 20.f);
                    const float fThumbR = 6.0f + ease(fAnim.hover) * 1.0f + ease(fAnim.press) * 0.6f;
                    auto* fDl = ImGui::GetWindowDrawList();
                    const ImVec2 fCenter = {wp2.x + freqThumbX, wp2.y + 74.f};
                    fDl->AddCircleFilled({fCenter.x, fCenter.y + 1.f}, fThumbR + 2.0f, color(IM_COL32(0, 0, 0, 70)), 20);
                    fDl->AddCircleFilled(fCenter, fThumbR + 1.2f, color(IM_COL32(20, 21, 24, 255)), 20);
                    fDl->AddCircleFilled(fCenter, fThumbR, color(gold), 20);
                    fDl->AddCircleFilled(fCenter, fThumbR * 0.35f, color(IM_COL32(25, 23, 19, 180)), 14);

                    // Крутизна (Slope)
                    text(14, 98, "Крутизна:", muted, smallFont);
                    const char* slopes[] = {"-6 дБ", "-12 дБ", "-18 дБ", "-24 дБ"};
                    for (int s = 0; s < 4; ++s) {
                        const float sx = 124.f + s * 66.f;
                        const bool isSel = (calib.lowCutoffSlope == s);
                        char sId[32];
                        std::snprintf(sId, sizeof(sId), "slopeBtn%d", s);
                        if (button(sId, slopes[s], sx, 94, 60, 24, isSel, smallFont, 5.f)) {
                            calib.lowCutoffSlope = static_cast<std::uint8_t>(s);
                            calib.valid = true;
                            ble.setEqCalibration(calib);
                        }
                    }

                    // Карточка 2: Акустическое пространство (Acoustic Space)
                    rect(0, 132, 400, 58, surface, 10.f);
                    text(14, 140, "Акустическое пространство", ink, bodyFont);
                    const char* spaceLabels[] = {"0 дБ", "-1 дБ", "-2 дБ", "-3 дБ", "-4 дБ"};
                    for (int sp = 0; sp < 5; ++sp) {
                        const float spX = 14.f + sp * 76.f;
                        const bool isSel = (calib.acousticSpace == sp);
                        char spId[32];
                        std::snprintf(spId, sizeof(spId), "spaceBtn%d", sp);
                        if (button(spId, spaceLabels[sp], spX, 158, 70, 24, isSel, smallFont, 5.f)) {
                            calib.acousticSpace = static_cast<std::uint8_t>(sp);
                            calib.valid = true;
                            ble.setEqCalibration(calib);
                        }
                    }

                    // Карточка 3: Настольный режим (Desktop Control)
                    rect(0, 196, 400, 36, surface, 10.f);
                    text(14, 206, "Компенсация отражений стола", ink, bodyFont);
                    const char* dcText = calib.desktopControl ? "Включена" : "Выключена";
                    if (rightTextLink("dcToggle", 386, 206, dcText, gold, bodyFont)) {
                        calib.desktopControl = calib.desktopControl ? 0 : 1;
                        calib.valid = true;
                        ble.setEqCalibration(calib);
                    }

                    // Сброс акустики и Обновить
                    text(0, 240, "0 дБ — центр, -4 дБ — угол", muted, smallFont);
                    if (rightTextLink("refreshCalibLink", 275, 240, "Обновить с колонок", muted, smallFont)) {
                        ble.sendRead(Command::GetEq);
                    }
                    if (rightTextLink("resetCalibLink", 400, 240, "Сбросить акустику", gold, bodyFont)) {
                        calib.lowCutoffFreq = 20;
                        calib.lowCutoffSlope = 0;
                        calib.acousticSpace = 0;
                        calib.desktopControl = 0;
                        ble.resetEqCalibration();
                    }
                }
            }
        } else {
            // TAB 3: УСТРОЙСТВО

            rect(0, 0, 400, 48, surface, 10.f);
            text(14, 16, "Питание", ink, bodyFont);
            if (rightTextLink("powerBtn", 386, 16, "Выключить…", gold, bodyFont)) {
                powerDialog = true;
            }

            rect(0, 60, 400, 48, surface, 10.f);
            text(14, 76, "Версия прошивки", ink, bodyFont);
            const std::string verText = snap.firmwareVersion.empty() ? (ready ? "v1.0.3" : "—") : snap.firmwareVersion;
            const auto verSize = bodyFont->CalcTextSizeA(bodyFont->FontSize, 1000.f, 0.f, verText.c_str());
            text(386.f - verSize.x, 76, verText.c_str(), muted, bodyFont);

            rect(0, 120, 400, 48, surface, 10.f);
            text(14, 136, "Автозапуск", ink, bodyFont);
            const bool autorunActive = edifier::autorun::isEnabled();
            if (rightTextLink("autorunToggle", 386, 136, autorunActive ? "Включён" : "Выключен", gold, bodyFont)) {
                edifier::autorun::setEnabled(!autorunActive);
            }

            rect(0, 180, 400, 48, surface, 10.f);
            text(14, 196, "Качество связи (RSSI)", ink, bodyFont);
            std::string rssiStr = "—";
            if (ready) {
                short rVal = designPreview ? -52 : snap.rssi;
                if (rVal > -127) {
                    char rBuf[64];
                    std::snprintf(rBuf, sizeof(rBuf), "%d dBm", rVal);
                    rssiStr = rBuf;
                }
            }
            const auto rssiSize = bodyFont->CalcTextSizeA(bodyFont->FontSize, 1000.f, 0.f, rssiStr.c_str());
            text(386.f - rssiSize.x, 196, rssiStr.c_str(), gold, bodyFont);
        }

        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::PopStyleVar();
    };

    const float t = ease(transition);
    const float direction = page > oldPage ? 1.f : -1.f;
    if (transition < 1 && oldPage != page) panel(oldPage, false, 1 - t, -14 * direction * t);
    panel(page, true, oldPage == page ? 1.f : t, 14 * direction * (1 - t));

    if (powerDialog) {
        ImGui::OpenPopup("Выключить колонку?");
        powerDialog = false;
    }
    ImGui::SetNextWindowPos({220, 160}, ImGuiCond_Appearing, {.5f, .5f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 16});
    if (ImGui::BeginPopupModal("Выключить колонку?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Колонка перестанет воспроизводить звук.");
        if (ImGui::Button("Отмена", {140, 32})) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Выключить", {140, 32})) {
            ble.sendMutation(Command::Shutdown, {});
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

void loadFonts() {
    wchar_t dir[MAX_PATH]{};
    GetWindowsDirectoryW(dir, MAX_PATH);
    const auto regularPath = std::filesystem::path(dir) / L"Fonts" / L"Inter-Regular.ttf";
    const auto mediumPath  = std::filesystem::path(dir) / L"Fonts" / L"Inter-Medium.ttf";
    const auto semiPath    = std::filesystem::path(dir) / L"Fonts" / L"Inter-SemiBold.ttf";
    const auto fallback    = std::filesystem::path(dir) / L"Fonts" / L"segoeui.ttf";

    const auto regFont = std::filesystem::exists(regularPath) ? regularPath.string() : fallback.string();
    const auto medFont = std::filesystem::exists(mediumPath) ? mediumPath.string() :
                         (std::filesystem::exists(semiPath) ? semiPath.string() : regFont);

    auto& io = ImGui::GetIO();
    static const ImWchar ranges[] = {
        0x20, 0xFF,
        0x400, 0x52F,
        0x2000, 0x206F,
        0x2190, 0x21FF,
        0x2700, 0x27BF,
        0
    };
    bodyFont      = io.Fonts->AddFontFromFileTTF(regFont.c_str(), 12.f, nullptr, ranges);
    labelFont     = io.Fonts->AddFontFromFileTTF(regFont.c_str(), 13.f, nullptr, ranges);
    cardTitleFont = io.Fonts->AddFontFromFileTTF(medFont.c_str(), 15.f, nullptr, ranges);
    titleFont     = io.Fonts->AddFontFromFileTTF(medFont.c_str(), 17.f, nullptr, ranges);
    heroFont      = io.Fonts->AddFontFromFileTTF(regFont.c_str(), 20.f, nullptr, ranges);
    numberFont    = io.Fonts->AddFontFromFileTTF(regFont.c_str(), 28.f, nullptr, ranges);
    smallFont     = io.Fonts->AddFontFromFileTTF(regFont.c_str(), 11.f, nullptr, ranges);
    microFont     = io.Fonts->AddFontFromFileTTF(regFont.c_str(), 9.f, nullptr, ranges);

    if (!bodyFont)      bodyFont      = io.Fonts->AddFontDefault();
    if (!labelFont)     labelFont     = bodyFont;
    if (!cardTitleFont) cardTitleFont = bodyFont;
    if (!titleFont)     titleFont     = bodyFont;
    if (!heroFont)      heroFont      = titleFont;
    if (!numberFont)    numberFont    = bodyFont;
    if (!smallFont)     smallFont     = bodyFont;
    if (!microFont)     microFont     = smallFont;
}
}


