#pragma once
#include <windows.h>

namespace edifier::osd {

void initOsd(HINSTANCE instance);
void showVolume(int currentVolume, int maxVolume, bool isMuted);
void updateOsd(float dt);
void shutdownOsd();

} // namespace edifier::osd
