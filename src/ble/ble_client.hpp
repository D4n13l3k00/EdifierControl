#pragma once

#include "protocol/protocol.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace edifier {

enum class LinkState { Idle, Scanning, Found, Connecting, Ready, Error };

struct DiscoveredDevice {
    std::wstring deviceName;
    std::wstring modelName;
    std::wstring serviceUuid;
    std::uint64_t address{};
    short rssi{-127};
    bool nineBandFamily{false};
};

struct BleSnapshot {
    LinkState state{LinkState::Idle};
    std::wstring deviceName;
    std::uint64_t address{};
    short rssi{-127};
    std::string detail{"Ready to scan"};
    std::string lastPacket;
    CustomEqState customEq;
    std::wstring modelName{L"MR3"};
    std::wstring serviceUuid;
    bool nineBandFamily{false};
    std::vector<DiscoveredDevice> discovered;
    std::uint64_t selectedAddress{};
    int volumeMax{30};
    int volumeCurrent{};
    bool volumeKnown{};
    int inputMax{};
    int inputCurrent{};
    bool inputKnown{};
    int eqCurrent{};
    bool eqKnown{};
    bool ledCurrent{};
    bool ledKnown{};
    std::string firmwareVersion;
    std::uint64_t firmwareRevision{};
    bool autoReconnect{true};
    EqCalibrationState calibration;
    std::uint64_t volumeRevision{}, eqRevision{}, customEqRevision{}, inputRevision{}, ledRevision{}, calibrationRevision{};
};

class BleClient {
public:
    BleClient();
    ~BleClient();
    BleClient(const BleClient&) = delete;
    BleClient& operator=(const BleClient&) = delete;

    void scan();
    void connect(std::uint64_t address = 0);
    void disconnect();
    void sendRead(Command command);
    void sendMutation(Command command, std::vector<std::uint8_t> payload);
    void changeVolume(int delta);
    void setAutoReconnect(bool enable);
    void setEqPreset(int preset);
    void setCustomEqBase(std::uint8_t baseProfile);
    void setEqCalibration(const EqCalibrationState& calib);
    void resetEqCalibration();
    BleSnapshot snapshot() const;

private:
    struct Impl;
    Impl* impl_{};
};

} // namespace edifier
