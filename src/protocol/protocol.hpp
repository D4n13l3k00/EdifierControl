#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace edifier {

inline constexpr wchar_t kNotifyUuid[]  = L"48090001-1a48-11e9-ab14-d663bd873d93";
inline constexpr wchar_t kWriteUuid[]   = L"48090002-1a48-11e9-ab14-d663bd873d93";

struct DeviceProfile {
    const wchar_t* name;
    const wchar_t* searchUuid;
    const wchar_t* serviceUuid;
    bool nineBandFamily;
};

inline constexpr std::array<DeviceProfile, 7> kStudioMonitors{{
    {L"MR3", L"0000f300-0000-1000-8000-00805f9b34fb", L"4809f301-1a48-11e9-ab14-d663bd873d93", false},
    {L"MR5", L"00003a01-0000-1000-8000-00805f9b34fb", L"48093a01-1a48-11e9-ab14-d663bd873d93", true},
    {L"MR4.5", L"00006303-0000-1000-8000-00805f9b34fb", L"48096303-1a48-11e9-ab14-d663bd873d93", true},
    {L"MR4 MKII", L"00006a03-0000-1000-8000-00805f9b34fb", L"48096a03-1a48-11e9-ab14-d663bd873d93", true},
    {L"MR4.5 (8625)", L"00007c03-0000-1000-8000-00805f9b34fb", L"48097c03-1a48-11e9-ab14-d663bd873d93", true},
    {L"MR4 MKII (8625)", L"00007d03-0000-1000-8000-00805f9b34fb", L"48097d03-1a48-11e9-ab14-d663bd873d93", true},
    {L"MR5 (8625)", L"00007e03-0000-1000-8000-00805f9b34fb", L"48097e03-1a48-11e9-ab14-d663bd873d93", true},
}};

enum class Command : std::uint8_t {
    ResetDevice       = 0x07,
    GetCustomEq       = 0x43,
    SetCustomEqBand   = 0x44,
    ResetCustomEq     = 0x45,
    StoreCustomEq     = 0x46,
    SetCustomEqName   = 0x47,
    GetInput          = 0x61,
    SetInput          = 0x62,
    GetVolume         = 0x66,
    SetVolume         = 0x67,
    SetEq             = 0xC4,
    GetVersion        = 0xC6,
    Shutdown          = 0xCE,
    GetEq             = 0xD5,
    SetAutoShutdown   = 0xD6,
    GetAutoShutdown   = 0xD7,
    GetSupport        = 0xD8,
    GetDeviceState    = 0xF2,
    GetLed            = 0xF7,
    SetLed            = 0xF8,
};

struct Frame {
    std::uint8_t appCode{};
    std::uint8_t command{};
    std::vector<std::uint8_t> payload;
    bool checksumValid{};
};

struct EqBand {
    std::uint8_t index{};
    std::uint8_t filter{};
    std::uint16_t frequency{};
    std::uint8_t gain{};
    std::uint8_t q{};
};

struct CustomEqState {
    bool valid{};
    std::uint8_t eqIndex{};
    std::uint8_t byte0{};
    std::vector<EqBand> bands;
};

struct EqCalibrationState {
    bool valid{false};
    std::uint8_t index{0};
    std::uint8_t byte0{0};          // 0 = normal, 1 = reset
    std::uint8_t lowCutoffFreq{20};  // 20 .. 100 Hz (step 5 Hz)
    std::uint8_t lowCutoffSlope{0}; // 0 = -6, 1 = -12, 2 = -18, 3 = -24 dB/oct
    std::uint8_t acousticSpace{0};  // 0 = 0 dB, 1 = -1 dB, 2 = -2 dB, 3 = -3 dB, 4 = -4 dB
    std::uint8_t desktopControl{0}; // 0 = Off, 1 = On
};

std::vector<std::uint8_t> makeV2(Command command, std::span<const std::uint8_t> payload = {}, std::uint8_t appCode = 0x0C);
std::vector<std::uint8_t> makeV1(Command command, std::span<const std::uint8_t> payload = {});
Frame parseV2(std::span<const std::uint8_t> bytes);
CustomEqState parseCustomEq(std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> makeCustomEqBand(const CustomEqState& state, std::size_t bandIndex);
EqCalibrationState parseEqCalibration(std::span<const std::uint8_t> payload);
std::vector<std::uint8_t> makeSetEq(std::uint8_t presetIndex, const EqCalibrationState* calib = nullptr);
std::vector<std::uint8_t> makeResetEqCalibration(std::uint8_t presetIndex, const EqCalibrationState& calib);
std::string hex(std::span<const std::uint8_t> bytes);
bool selfTest(std::string& report);

} // namespace edifier

