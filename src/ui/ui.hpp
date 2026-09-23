#pragma once

namespace edifier { class BleClient; }
namespace edifier::audio { struct AudioLevels; class AudioMeter; }

namespace edifier::ui {
inline constexpr int kWidth = 440;
inline constexpr int kHeight = 368;
inline constexpr int kExpandedHeight = 424;
void styleUi();
void loadFonts();
void renderApp(BleClient& ble, void (*setPopupVisible)(bool), bool designPreview = false, int forcedPage = -1, float currentWindowHeight = static_cast<float>(kHeight), const audio::AudioLevels* audioLevels = nullptr, audio::AudioMeter* audioMeter = nullptr);
int getTargetHeight();
void closeProfileDropdown();

}



