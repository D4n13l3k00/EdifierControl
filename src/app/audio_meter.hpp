#include <cstdint>
#include <string>
#include <vector>

namespace edifier::audio {

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    bool isDefault = false;
};

struct AudioLevels {
    float left = 0.0f;       // 0.0 .. 1.0 (smooth display value, dB-mapped)
    float right = 0.0f;      // 0.0 .. 1.0
    float peakLeft = 0.0f;   // 0.0 .. 1.0 (peak hold mark)
    float peakRight = 0.0f;  // 0.0 .. 1.0
    bool active = false;     // true if audio device queried successfully
};

class AudioMeter {
public:
    AudioMeter();
    ~AudioMeter();

    AudioMeter(const AudioMeter&) = delete;
    AudioMeter& operator=(const AudioMeter&) = delete;

    void update(float dt);
    AudioLevels getLevels() const;

    std::vector<AudioDeviceInfo> getAvailableDevices() const;
    void setDevice(const std::wstring& deviceId);
    std::wstring getSelectedDeviceId() const;
    std::wstring getSelectedDeviceName() const;
    void resetEndpoint();

private:
    struct Impl;
    Impl* m_impl{nullptr};
};

} // namespace edifier::audio

