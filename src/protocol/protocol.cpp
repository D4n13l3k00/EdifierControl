#include "protocol.hpp"

#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace edifier {

std::vector<std::uint8_t> makeV2(Command command, std::span<const std::uint8_t> payload, std::uint8_t appCode) {
    if (payload.size() > 0xFFFF) throw std::invalid_argument("payload too large");
    std::vector<std::uint8_t> out{0xAA, appCode, static_cast<std::uint8_t>(command),
        static_cast<std::uint8_t>(payload.size() >> 8), static_cast<std::uint8_t>(payload.size())};
    out.insert(out.end(), payload.begin(), payload.end());
    const auto sum = std::accumulate(out.begin(), out.end(), 0u);
    out.push_back(static_cast<std::uint8_t>(sum & 0xFF));
    return out;
}

std::vector<std::uint8_t> makeV1(Command command, std::span<const std::uint8_t> payload) {
    if (payload.size() > 0xFE) throw std::invalid_argument("payload too large");
    std::vector<std::uint8_t> out{0xAA, static_cast<std::uint8_t>(payload.size() + 1), static_cast<std::uint8_t>(command)};
    out.insert(out.end(), payload.begin(), payload.end());
    const auto sum = 0x2019u + std::accumulate(out.begin(), out.end(), 0u);
    out.push_back(static_cast<std::uint8_t>((sum >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(sum & 0xFF));
    return out;
}

Frame parseV2(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 6) throw std::invalid_argument("not an EDIFIER v2 response");
    if (bytes[0] != 0xAA && bytes[0] != 0xBB && bytes[0] != 0xCC && bytes[0] != 0xDD) {
        throw std::invalid_argument("not an EDIFIER v2 response");
    }
    const auto length = (static_cast<std::size_t>(bytes[3]) << 8) | bytes[4];
    if (bytes.size() != length + 6) throw std::invalid_argument("invalid frame length");
    const auto sum = std::accumulate(bytes.begin(), bytes.end() - 1, 0u);
    return {bytes[1], bytes[2], {bytes.begin() + 5, bytes.end() - 1}, static_cast<std::uint8_t>(sum & 0xFF) == bytes.back()};
}

CustomEqState parseCustomEq(std::span<const std::uint8_t> payload) {
    CustomEqState state;
    if (payload.size() < 2) return state;
    state.eqIndex = payload[0];
    const std::size_t start = 2;
    std::size_t count = 4, stride = 6, bytesRequired = 24;
    if (state.eqIndex == 2) { count = 6; stride = 4; bytesRequired = 24; }
    else if (state.eqIndex == 7) { count = 6; stride = 4; bytesRequired = 25; state.byte0 = payload.size() > start ? payload[start] : 0; }
    else if (state.eqIndex == 12) { count = 9; stride = 4; bytesRequired = 37; state.byte0 = payload.size() > start ? payload[start] : 0; }
    else if (state.eqIndex == 16) { count = 9; stride = 4; bytesRequired = 36; }
    else if (state.eqIndex == 17) { count = 10; stride = 4; bytesRequired = 40; }
    if (payload.size() < start + bytesRequired) return state;
    state.bands.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto base = start + ((state.eqIndex == 7 || state.eqIndex == 12) ? 1 : 0) + i * stride;
        EqBand band{};
        band.index = static_cast<std::uint8_t>(i);
        if (state.eqIndex == 7 || state.eqIndex == 12) {
            band.index = payload[base];
            band.frequency = static_cast<std::uint16_t>((payload[base + 1] << 8) | payload[base + 2]);
            band.gain = payload[base + 3];
        } else if (stride == 4) {
            band.index = payload[base];
            band.frequency = static_cast<std::uint16_t>((payload[base + 1] << 8) | payload[base + 2]);
            band.gain = payload[base + 3];
        } else {
            band.index = payload[base];
            band.filter = payload[base + 1];
            band.frequency = static_cast<std::uint16_t>((payload[base + 2] << 8) | payload[base + 3]);
            band.gain = payload[base + 4];
            band.q = payload[base + 5];
        }
        state.bands.push_back(band);
    }
    state.valid = true;
    return state;
}

std::vector<std::uint8_t> makeCustomEqBand(const CustomEqState& state, std::size_t bandIndex) {
    if (!state.valid || bandIndex >= state.bands.size()) throw std::invalid_argument("invalid custom EQ band");
    const auto& band = state.bands[bandIndex];
    const auto hi = static_cast<std::uint8_t>(band.frequency >> 8);
    const auto lo = static_cast<std::uint8_t>(band.frequency & 0xFF);
    if (state.eqIndex == 7 || state.eqIndex == 12) return {state.byte0, band.index, hi, lo, band.gain};
    if (state.eqIndex == 2 || state.eqIndex == 16 || state.eqIndex == 17) return {band.index, hi, lo, band.gain};
    return {band.index, band.filter, hi, lo, band.gain, band.q};
}

EqCalibrationState parseEqCalibration(std::span<const std::uint8_t> payload) {
    EqCalibrationState state;
    if (payload.size() == 6) {
        state.index = payload[1];
        state.byte0 = 0xFF; // matches (byte) -1 in Android APK
        state.lowCutoffFreq = payload[2];
        state.lowCutoffSlope = payload[3];
        state.acousticSpace = payload[4];
        state.desktopControl = payload[5];
        state.valid = true;
    } else if (payload.size() >= 7) {
        state.index = payload[1];
        state.byte0 = payload[2];
        state.lowCutoffFreq = payload[3];
        state.lowCutoffSlope = payload[4];
        state.acousticSpace = payload[5];
        state.desktopControl = payload[6];
        state.valid = true;
    }
    if (state.valid) {
        if (state.lowCutoffFreq < 20) state.lowCutoffFreq = 20;
        else if (state.lowCutoffFreq > 100) state.lowCutoffFreq = 100;

        if (state.lowCutoffSlope == 6) state.lowCutoffSlope = 0;
        else if (state.lowCutoffSlope == 12) state.lowCutoffSlope = 1;
        else if (state.lowCutoffSlope == 18) state.lowCutoffSlope = 2;
        else if (state.lowCutoffSlope == 24) state.lowCutoffSlope = 3;
        else if (state.lowCutoffSlope > 3) state.lowCutoffSlope = 0;

        if (state.acousticSpace > 4) state.acousticSpace = 4;
        state.desktopControl = state.desktopControl ? 1 : 0;
    }
    return state;
}

std::vector<std::uint8_t> makeSetEq(std::uint8_t presetIndex, const EqCalibrationState* calib) {
    if (calib && calib->valid) {
        return {
            presetIndex,
            calib->index,
            calib->byte0,
            calib->lowCutoffFreq,
            calib->lowCutoffSlope,
            calib->acousticSpace,
            calib->desktopControl
        };
    }
    return { presetIndex };
}

std::vector<std::uint8_t> makeResetEqCalibration(std::uint8_t presetIndex, const EqCalibrationState& calib) {
    return {
        presetIndex,
        calib.index,
        1, // byte0 = 1 signals DSP reset for calibration
        calib.lowCutoffFreq ? calib.lowCutoffFreq : static_cast<std::uint8_t>(20),
        calib.lowCutoffSlope,
        calib.acousticSpace,
        calib.desktopControl
    };
}

std::string hex(std::span<const std::uint8_t> bytes) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return out.str();
}

bool selfTest(std::string& report) {
    const auto getVolume = makeV2(Command::GetVolume);
    const std::vector<std::uint8_t> expected{0xAA, 0x0C, 0x66, 0x00, 0x00, 0x1C};
    const bool ok1 = getVolume == expected;
    const std::uint8_t volume = 30;
    const auto setVolume = makeV2(Command::SetVolume, {&volume, 1});
    const bool ok2 = setVolume.size() == 7 && setVolume[5] == 30 && setVolume.back() == 0x3C;
    const std::vector<std::uint8_t> response{0xBB, 0x0C, 0x66, 0x00, 0x01, 0x2A, 0x58};
    const auto parsed = parseV2(response);
    const bool ok3 = parsed.checksumValid && parsed.command == 0x66 && parsed.payload == std::vector<std::uint8_t>{0x2A};
    std::vector<std::uint8_t> eqPayload{7, 0, 1};
    const std::array<std::uint16_t, 6> freqs{50, 200, 800, 3200, 8000, 16000};
    for (std::size_t i = 0; i < freqs.size(); ++i) {
        eqPayload.push_back(static_cast<std::uint8_t>(i));
        eqPayload.push_back(static_cast<std::uint8_t>(freqs[i] >> 8));
        eqPayload.push_back(static_cast<std::uint8_t>(freqs[i]));
        eqPayload.push_back(6);
    }
    const auto custom = parseCustomEq(eqPayload);
    const auto bandPacket = makeCustomEqBand(custom, 2);
    const bool ok4 = custom.valid && custom.bands.size() == 6 && custom.bands[2].frequency == 800 &&
        bandPacket == std::vector<std::uint8_t>({1, 2, 0x03, 0x20, 6});

    const std::vector<std::uint8_t> mockCalibPayload{0, 1, 0, 80, 2, 3, 1};
    const auto calib = parseEqCalibration(mockCalibPayload);
    const auto setEqPacket = makeSetEq(2, &calib);
    const auto resetCalibPacket = makeResetEqCalibration(2, calib);
    const bool ok5 = calib.valid && calib.index == 1 && calib.lowCutoffFreq == 80 &&
        calib.lowCutoffSlope == 2 && calib.acousticSpace == 3 && calib.desktopControl == 1 &&
        setEqPacket == std::vector<std::uint8_t>({2, 1, 0, 80, 2, 3, 1}) &&
        resetCalibPacket == std::vector<std::uint8_t>({2, 1, 1, 80, 2, 3, 1});

    const std::vector<std::uint8_t> mockCalib6{0, 1, 80, 18, 3, 1};
    const auto calib6 = parseEqCalibration(mockCalib6);
    const bool ok6 = calib6.valid && calib6.index == 1 && calib6.byte0 == 0xFF &&
        calib6.lowCutoffFreq == 80 && calib6.lowCutoffSlope == 2 && calib6.acousticSpace == 3 &&
        calib6.desktopControl == 1;

    report = "v2 get-volume: " + hex(getVolume) + "\nv2 set-volume(30): " + hex(setVolume) +
             "\nparse response: " + std::string(ok3 ? "ok" : "failed") +
             "\ncustom EQ dynamic parser: " + std::string(ok4 ? "ok" : "failed") +
             "\nacoustic calibration parser & builder: " + std::string((ok5 && ok6) ? "ok" : "failed") + "\n";
    return ok1 && ok2 && ok3 && ok4 && ok5 && ok6;
}

} // namespace edifier

