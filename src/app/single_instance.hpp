#pragma once
#include <windows.h>

namespace edifier::app {
// Session-local objects coordinate startup even before the first HWND exists.
class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    bool valid() const { return mutex_ && activate_; }
    bool primary() const { return primary_; }
    void requestActivation() const;
    bool takeActivation() const;
private:
    HANDLE activate_{};
    HANDLE mutex_{};
    bool primary_{};
};
}
