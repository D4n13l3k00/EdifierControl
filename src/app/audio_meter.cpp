#include "audio_meter.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace edifier::audio {

namespace {

std::filesystem::path getAppConfigPath() {
    wchar_t appData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return std::filesystem::path(appData) / L"EdifierControl" / L"config.json";
}

std::wstring loadConfigAudioDeviceId() {
    try {
        const auto path = getAppConfigPath();
        if (!std::filesystem::exists(path)) return {};
        std::ifstream file(path);
        if (!file.is_open()) return {};
        std::string line;
        while (std::getline(file, line)) {
            auto pos = line.find("\"audioDeviceId\"");
            if (pos != std::string::npos) {
                auto firstQuote = line.find('"', pos + 15);
                if (firstQuote != std::string::npos) {
                    auto secondQuote = line.find('"', firstQuote + 1);
                    if (secondQuote != std::string::npos) {
                        std::string sub = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                        int len = MultiByteToWideChar(CP_UTF8, 0, sub.c_str(), -1, nullptr, 0);
                        if (len > 1) {
                            std::wstring w(len - 1, 0);
                            MultiByteToWideChar(CP_UTF8, 0, sub.c_str(), -1, w.data(), len);
                            return w;
                        }
                    }
                }
            }
        }
    } catch (...) {}
    return {};
}

void saveConfigAudioDevice(const std::wstring& devId, const std::wstring& devName) {
    try {
        const auto path = getAppConfigPath();
        std::vector<std::string> lines;
        if (std::filesystem::exists(path)) {
            std::ifstream in(path);
            std::string l;
            while (std::getline(in, l)) {
                if (l.find("\"audioDeviceId\"") == std::string::npos &&
                    l.find("\"audioDeviceName\"") == std::string::npos) {
                    lines.push_back(l);
                }
            }
        }
        while (!lines.empty() && (lines.back().find('}') != std::string::npos || lines.back().empty())) {
            lines.pop_back();
        }
        if (lines.empty()) {
            lines.push_back("{");
        } else {
            auto& last = lines.back();
            while (!last.empty() && (last.back() == ' ' || last.back() == '\t' || last.back() == '\r')) last.pop_back();
            if (!last.empty() && last.back() != ',' && last.back() != '{') {
                last.push_back(',');
            }
        }
        auto toUtf8 = [](const std::wstring& w) -> std::string {
            if (w.empty()) return {};
            int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len <= 1) return {};
            std::string s(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
            return s;
        };
        lines.push_back("  \"audioDeviceId\": \"" + toUtf8(devId) + "\",");
        lines.push_back("  \"audioDeviceName\": \"" + toUtf8(devName) + "\"");
        lines.push_back("}");

        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        for (const auto& l : lines) {
            out << l << "\n";
        }
    } catch (...) {}
}

} // namespace

struct AudioMeter::Impl {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioMeterInformation> meter;

    std::wstring configuredDeviceId;
    std::wstring currentDeviceId;
    std::wstring currentDeviceName;

    AudioLevels levels{};

    float dispL{0.0f};
    float dispR{0.0f};
    float peakHoldL{0.0f};
    float peakHoldR{0.0f};
    float peakTimerL{0.0f};
    float peakTimerR{0.0f};

    float deviceCheckTimer{0.0f};
    bool comInitialized{false};

    Impl() {
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.GetAddressOf()));
        if (FAILED(hr)) {
            hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (SUCCEEDED(hr)) {
                comInitialized = true;
            }
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.GetAddressOf()));
        }
        configuredDeviceId = loadConfigAudioDeviceId();
        ensureEndpoint();
    }

    ~Impl() {
        meter.Reset();
        device.Reset();
        enumerator.Reset();
        if (comInitialized) {
            CoUninitialize();
        }
    }

    void ensureEndpoint() {
        if (!enumerator) return;
        ComPtr<IMMDevice> newDevice;
        HRESULT hr = E_FAIL;
        if (!configuredDeviceId.empty()) {
            hr = enumerator->GetDevice(configuredDeviceId.c_str(), newDevice.GetAddressOf());
        }
        if (FAILED(hr) || !newDevice) {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, newDevice.GetAddressOf());
        }
        if (SUCCEEDED(hr) && newDevice) {
            device = newDevice;
            meter.Reset();
            device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void**>(meter.GetAddressOf()));

            LPWSTR pId = nullptr;
            if (SUCCEEDED(device->GetId(&pId)) && pId) {
                currentDeviceId = pId;
                CoTaskMemFree(pId);
            }
            ComPtr<IPropertyStore> props;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv))) {
                    if (pv.vt == VT_LPWSTR && pv.pwszVal) {
                        currentDeviceName = pv.pwszVal;
                    }
                    PropVariantClear(&pv);
                }
            }
        }
    }

    void setDevice(const std::wstring& devId) {
        configuredDeviceId = devId;
        ensureEndpoint();
        saveConfigAudioDevice(configuredDeviceId, currentDeviceName);
    }

    void resetEndpoint() {
        meter.Reset();
        device.Reset();
        ensureEndpoint();
    }

    std::vector<AudioDeviceInfo> getAvailableDevices() const {
        std::vector<AudioDeviceInfo> list;
        if (!enumerator) return list;

        std::wstring defId;
        ComPtr<IMMDevice> defDev;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defDev.GetAddressOf()))) {
            LPWSTR pDefId = nullptr;
            if (SUCCEEDED(defDev->GetId(&pDefId)) && pDefId) {
                defId = pDefId;
                CoTaskMemFree(pDefId);
            }
        }

        ComPtr<IMMDeviceCollection> col;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, col.GetAddressOf()))) {
            UINT count = 0;
            col->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> dev;
                if (SUCCEEDED(col->Item(i, dev.GetAddressOf()))) {
                    AudioDeviceInfo info;
                    LPWSTR pId = nullptr;
                    if (SUCCEEDED(dev->GetId(&pId)) && pId) {
                        info.id = pId;
                        CoTaskMemFree(pId);
                    }
                    ComPtr<IPropertyStore> props;
                    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
                        PROPVARIANT pv;
                        PropVariantInit(&pv);
                        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv))) {
                            if (pv.vt == VT_LPWSTR && pv.pwszVal) {
                                info.name = pv.pwszVal;
                            }
                            PropVariantClear(&pv);
                        }
                    }
                    if (info.name.empty()) {
                        info.name = L"Аудиоустройство";
                    }
                    info.isDefault = (!defId.empty() && info.id == defId);
                    list.push_back(std::move(info));
                }
            }
        }
        return list;
    }

    // Convert linear peak amplitude (0.0 .. 1.0) to normalized UI display fraction [0.0 .. 1.0]
    // using Arturia / studio non-linear decibel scale from -60 dB (0.0) to 0 dB (1.0).
    struct DbPoint { float db; float frac; };
    static float amplitudeToFraction(float amp) {
        if (amp <= 0.0001f) return 0.0f;
        const float dB = 20.0f * std::log10(amp);
        if (dB <= -60.0f) return 0.0f;
        if (dB >= 0.0f) return 1.0f;

        constexpr DbPoint kScale[] = {
            {-60.0f, 0.00f},
            {-48.0f, 0.12f},
            {-36.0f, 0.28f},
            {-24.0f, 0.48f},
            {-18.0f, 0.62f},
            {-12.0f, 0.76f},
            {-6.0f,  0.90f},
            {  0.0f, 1.00f}
        };
        for (int i = 0; i < 7; ++i) {
            if (dB >= kScale[i].db && dB <= kScale[i + 1].db) {
                const float t = (dB - kScale[i].db) / (kScale[i + 1].db - kScale[i].db);
                return kScale[i].frac + t * (kScale[i + 1].frac - kScale[i].frac);
            }
        }
        return 0.0f;
    }

    void update(float dt) {
        if (dt <= 0.0f) dt = 0.016f;

        deviceCheckTimer += dt;
        if (deviceCheckTimer >= 2.0f || !meter) {
            deviceCheckTimer = 0.0f;
            ensureEndpoint();
        }

        float curL = 0.0f;
        float curR = 0.0f;
        bool gotValues = false;

        if (meter) {
            UINT channels = 0;
            if (SUCCEEDED(meter->GetMeteringChannelCount(&channels)) && channels >= 2) {
                float peaks[2] = {0.0f, 0.0f};
                if (SUCCEEDED(meter->GetChannelsPeakValues(2, peaks))) {
                    curL = peaks[0];
                    curR = peaks[1];
                    gotValues = true;
                }
            } else if (meter) {
                float peak = 0.0f;
                if (SUCCEEDED(meter->GetPeakValue(&peak))) {
                    curL = curR = peak;
                    gotValues = true;
                }
            }
        }

        if (!gotValues) {
            meter.Reset();
            device.Reset();
        }

        // Map to decibel fractions [0.0 .. 1.0]
        const float targetL = amplitudeToFraction(curL);
        const float targetR = amplitudeToFraction(curR);

        // Fast attack, smooth decay
        constexpr float decaySpeed = 1.6f; // fraction per second
        if (targetL >= dispL) {
            dispL = targetL; // instant attack on beats
        } else {
            dispL = std::max(0.0f, dispL - decaySpeed * dt);
        }

        if (targetR >= dispR) {
            dispR = targetR; // instant attack on beats
        } else {
            dispR = std::max(0.0f, dispR - decaySpeed * dt);
        }

        // Peak Hold logic for Left channel
        if (targetL >= peakHoldL) {
            peakHoldL = targetL;
            peakTimerL = 0.6f; // hold for 600ms
        } else {
            if (peakTimerL > 0.0f) {
                peakTimerL -= dt;
            } else {
                peakHoldL = std::max(dispL, peakHoldL - decaySpeed * 0.7f * dt);
            }
        }

        // Peak Hold logic for Right channel
        if (targetR >= peakHoldR) {
            peakHoldR = targetR;
            peakTimerR = 0.6f; // hold for 600ms
        } else {
            if (peakTimerR > 0.0f) {
                peakTimerR -= dt;
            } else {
                peakHoldR = std::max(dispR, peakHoldR - decaySpeed * 0.7f * dt);
            }
        }

        levels.left = dispL;
        levels.right = dispR;
        levels.peakLeft = peakHoldL;
        levels.peakRight = peakHoldR;
        levels.active = gotValues;
    }
};

AudioMeter::AudioMeter() : m_impl(new Impl()) {}
AudioMeter::~AudioMeter() { delete m_impl; }

void AudioMeter::update(float dt) {
    if (m_impl) m_impl->update(dt);
}

AudioLevels AudioMeter::getLevels() const {
    if (m_impl) return m_impl->levels;
    return {};
}

std::vector<AudioDeviceInfo> AudioMeter::getAvailableDevices() const {
    if (m_impl) return m_impl->getAvailableDevices();
    return {};
}

void AudioMeter::setDevice(const std::wstring& deviceId) {
    if (m_impl) m_impl->setDevice(deviceId);
}

std::wstring AudioMeter::getSelectedDeviceId() const {
    if (m_impl) return m_impl->configuredDeviceId;
    return {};
}

std::wstring AudioMeter::getSelectedDeviceName() const {
    if (m_impl) return m_impl->currentDeviceName;
    return {};
}

void AudioMeter::resetEndpoint() {
    if (m_impl) m_impl->resetEndpoint();
}

} // namespace edifier::audio
