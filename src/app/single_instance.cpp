#include "single_instance.hpp"

namespace edifier::app {
SingleInstance::SingleInstance() {
    activate_ = CreateEventW(nullptr, FALSE, FALSE, L"Local\\EdifierStudioControl.Activate");
    if (!activate_) return;
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\EdifierStudioControl.Instance");
    const DWORD error = GetLastError();
    primary_ = mutex_ && error != ERROR_ALREADY_EXISTS;
}
SingleInstance::~SingleInstance() {
    if (mutex_) CloseHandle(mutex_);
    if (activate_) CloseHandle(activate_);
}
void SingleInstance::requestActivation() const {
    const auto window = FindWindowW(L"EdifierMR3Control", nullptr);
    if (window) {
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        AllowSetForegroundWindow(pid);
    }
    SetEvent(activate_);
}
bool SingleInstance::takeActivation() const {
    return WaitForSingleObject(activate_, 0) == WAIT_OBJECT_0;
}
}
