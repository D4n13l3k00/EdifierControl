#include "ble_client.hpp"

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <chrono>
#include <memory>
#include <deque>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <windows.h>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

namespace {
guid parseGuid(const wchar_t* value) { return guid{value}; }

IBuffer toBuffer(const std::vector<std::uint8_t>& bytes) {
    DataWriter writer;
    writer.WriteBytes(bytes);
    return writer.DetachBuffer();
}

std::vector<std::uint8_t> fromBuffer(const IBuffer& buffer) {
    DataReader reader = DataReader::FromBuffer(buffer);
    std::vector<std::uint8_t> bytes(reader.UnconsumedBufferLength());
    reader.ReadBytes(bytes);
    return bytes;
}

std::string toUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring toWide(std::string_view s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::string trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(start, end - start + 1));
}

std::filesystem::path getConfigFilePath() {
    wchar_t appData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return std::filesystem::path(appData) / L"EdifierControl" / L"config.json";
}

struct AppConfig {
    std::uint64_t lastAddress{};
    std::wstring deviceName;
    std::wstring modelName;
    std::wstring serviceUuid;
};

AppConfig loadDeviceConfig() {
    AppConfig cfg;
    const auto path = getConfigFilePath();
    if (!std::filesystem::exists(path)) return cfg;
    try {
        std::ifstream file(path);
        if (!file.is_open()) return cfg;
        std::string line;
        while (std::getline(file, line)) {
            if (auto pos = line.find("\"lastAddress\""); pos != std::string::npos) {
                if (auto colon = line.find(':', pos); colon != std::string::npos) {
                    std::string val = trim(line.substr(colon + 1));
                    if (!val.empty() && val.back() == ',') val.pop_back();
                    val = trim(val);
                    if (!val.empty() && val.front() == '"') val = val.substr(1, val.size() >= 2 ? val.size() - 2 : 0);
                    try { cfg.lastAddress = std::stoull(val, nullptr, 0); } catch (...) {}
                }
            }
            auto readStringProp = [&](const char* key, std::wstring& out) {
                if (auto pos = line.find(key); pos != std::string::npos) {
                    if (auto firstQuote = line.find('"', pos + std::strlen(key)); firstQuote != std::string::npos) {
                        if (auto secondQuote = line.find('"', firstQuote + 1); secondQuote != std::string::npos) {
                            out = toWide(line.substr(firstQuote + 1, secondQuote - firstQuote - 1));
                        }
                    }
                }
            };
            readStringProp("\"deviceName\"", cfg.deviceName);
            readStringProp("\"modelName\"", cfg.modelName);
            readStringProp("\"serviceUuid\"", cfg.serviceUuid);
        }
    } catch (...) {}
    return cfg;
}

void saveDeviceConfig(const AppConfig& cfg) {
    try {
        const auto path = getConfigFilePath();
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file.is_open()) return;
        file << "{\n";
        file << "  \"lastAddress\": " << cfg.lastAddress << ",\n";
        file << "  \"deviceName\": \"" << toUtf8(cfg.deviceName) << "\",\n";
        file << "  \"modelName\": \"" << toUtf8(cfg.modelName) << "\",\n";
        file << "  \"serviceUuid\": \"" << toUtf8(cfg.serviceUuid) << "\"\n";
        file << "}\n";
    } catch (...) {}
}
}

namespace edifier {

struct BleClient::Impl {
    mutable std::mutex lock;
    BleSnapshot info;
    BluetoothLEAdvertisementWatcher watcher;
    BluetoothLEDevice device{nullptr};
    GattCharacteristic notify{nullptr};
    GattCharacteristic write{nullptr};
    event_token receivedToken{};
    event_token statusToken{};
    std::jthread worker;
    std::deque<std::vector<std::uint8_t>> pending;
    std::condition_variable wake;
    std::atomic<bool> reconnectRequested{false};
    std::atomic<bool> autoReconnect{true};

    void update(LinkState state, std::string detail) {
        std::scoped_lock guard(lock);
        info.state = state;
        info.detail = std::move(detail);
    }

    void cleanupGatt() {
        try {
            if (notify) {
                notify.ValueChanged(receivedToken);
                notify.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::None).get();
            }
            if (device) {
                device.ConnectionStatusChanged(statusToken);
            }
        } catch (...) {}
        notify = nullptr;
        write = nullptr;
        device = nullptr;
    }

    void stopWorker() {
        if (worker.joinable()) { worker.request_stop(); wake.notify_all(); worker.join(); }
    }
};

BleClient::BleClient() : impl_(new Impl) {
    auto cfg = loadDeviceConfig();
    if (cfg.lastAddress != 0) {
        {
            std::scoped_lock guard(impl_->lock);
            impl_->info.selectedAddress = cfg.lastAddress;
            impl_->info.address = cfg.lastAddress;
            impl_->info.deviceName = cfg.deviceName.empty() ? L"EDIFIER MR3" : cfg.deviceName;
            impl_->info.modelName = cfg.modelName.empty() ? L"MR3" : cfg.modelName;
            impl_->info.serviceUuid = cfg.serviceUuid.empty() ? kStudioMonitors[0].serviceUuid : cfg.serviceUuid;
            for (const auto& profile : kStudioMonitors) {
                if (profile.name == impl_->info.modelName) {
                    impl_->info.nineBandFamily = profile.nineBandFamily;
                    break;
                }
            }
            impl_->info.detail = "Saved speaker found. Connecting...";
        }
        connect(cfg.lastAddress);
    }
}

BleClient::~BleClient() {
    disconnect();
    delete impl_;
}

BleSnapshot BleClient::snapshot() const {
    std::scoped_lock guard(impl_->lock);
    return impl_->info;
}

void BleClient::scan() {
    disconnect();
    {
        std::scoped_lock guard(impl_->lock);
        impl_->info.discovered.clear();
        impl_->info.selectedAddress = 0;
    }
    impl_->update(LinkState::Scanning, "Listening for EDIFIER studio monitors...");
    impl_->worker = std::jthread([this](std::stop_token stop) {
        try {
            init_apartment(apartment_type::multi_threaded);
            BluetoothLEAdvertisementWatcher watcher;
            watcher.ScanningMode(BluetoothLEScanningMode::Active);
            auto token = watcher.Received([this](auto const&, BluetoothLEAdvertisementReceivedEventArgs const& args) {
                const auto name = args.Advertisement().LocalName();
                const std::wstring_view nameView{name.c_str(), name.size()};
                const DeviceProfile* matched{};
                for (const auto& profile : kStudioMonitors) {
                    for (auto const& uuid : args.Advertisement().ServiceUuids()) if (uuid == guid{profile.searchUuid}) matched = &profile;
                }
                if (!matched) {
                    for (const auto& profile : kStudioMonitors) if (nameView.find(profile.name) != std::wstring_view::npos) { matched = &profile; break; }
                }
                if (!matched) return;

                DiscoveredDevice deviceItem;
                deviceItem.deviceName = name.empty() ? matched->name : std::wstring{name};
                deviceItem.modelName = matched->name;
                deviceItem.serviceUuid = matched->serviceUuid;
                deviceItem.address = args.BluetoothAddress();
                deviceItem.rssi = args.RawSignalStrengthInDBm();
                deviceItem.nineBandFamily = matched->nineBandFamily;

                std::scoped_lock guard(impl_->lock);
                auto it = std::find_if(impl_->info.discovered.begin(), impl_->info.discovered.end(),
                    [addr = deviceItem.address](const DiscoveredDevice& d) { return d.address == addr; });
                if (it != impl_->info.discovered.end()) {
                    it->rssi = deviceItem.rssi;
                    it->nineBandFamily = deviceItem.nineBandFamily;
                    if (!name.empty()) it->deviceName = deviceItem.deviceName;
                } else {
                    impl_->info.discovered.push_back(deviceItem);
                }

                impl_->info.state = LinkState::Found;
                if (impl_->info.selectedAddress == 0 || impl_->info.selectedAddress == deviceItem.address) {
                    impl_->info.selectedAddress = deviceItem.address;
                    impl_->info.deviceName = deviceItem.deviceName;
                    impl_->info.modelName = deviceItem.modelName;
                    impl_->info.serviceUuid = deviceItem.serviceUuid;
                    impl_->info.address = deviceItem.address;
                    impl_->info.rssi = deviceItem.rssi;
                    impl_->info.nineBandFamily = deviceItem.nineBandFamily;
                }
                impl_->info.detail = "Studio monitor found. Ready to connect.";
            });
            impl_->watcher = watcher;
            watcher.Start();
            while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            watcher.Stop();
            watcher.Received(token);
        } catch (const hresult_error& e) {
            impl_->update(LinkState::Error, to_string(e.message()));
        }
    });
}

void BleClient::connect(std::uint64_t address) {
    {
        std::scoped_lock guard(impl_->lock);
        if (address != 0) {
            impl_->info.selectedAddress = address;
            auto it = std::find_if(impl_->info.discovered.begin(), impl_->info.discovered.end(),
                [address](const DiscoveredDevice& d) { return d.address == address; });
            if (it != impl_->info.discovered.end()) {
                impl_->info.deviceName = it->deviceName;
                impl_->info.modelName = it->modelName;
                impl_->info.serviceUuid = it->serviceUuid;
                impl_->info.address = it->address;
                impl_->info.rssi = it->rssi;
                impl_->info.nineBandFamily = it->nineBandFamily;
            }
        }
    }
    const auto current = snapshot();
    if (!current.address) { impl_->update(LinkState::Error, "Scan and select a device first."); return; }
    impl_->autoReconnect = true;
    impl_->stopWorker();
    impl_->update(LinkState::Connecting, "Opening EDIFIER GATT service...");
    impl_->worker = std::jthread([this, address = current.address](std::stop_token stop) {
        while (!stop.stop_requested()) {
            try {
                init_apartment(apartment_type::multi_threaded);
                auto device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
                if (!device) {
                    if (!impl_->autoReconnect || stop.stop_requested()) {
                        impl_->update(LinkState::Error, "Windows could not open the BLE device.");
                        break;
                    }
                    impl_->update(LinkState::Connecting, "Speaker unavailable. Waiting to reconnect...");
                    for (int i = 0; i < 25 && !stop.stop_requested(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue;
                }
                const auto selected = snapshot();
                if (selected.serviceUuid.empty()) {
                    impl_->update(LinkState::Error, "No model profile selected.");
                    break;
                }
                const auto servicesResult = device.GetGattServicesForUuidAsync(parseGuid(selected.serviceUuid.c_str()), BluetoothCacheMode::Uncached).get();
                if (servicesResult.Status() != GattCommunicationStatus::Success || servicesResult.Services().Size() == 0) {
                    if (!impl_->autoReconnect || stop.stop_requested()) {
                        impl_->update(LinkState::Error, "EDIFIER service was not exposed. Pairing may be required.");
                        break;
                    }
                    impl_->update(LinkState::Connecting, "Speaker in standby. Waiting for reconnect...");
                    for (int i = 0; i < 25 && !stop.stop_requested(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    continue;
                }
                auto service = servicesResult.Services().GetAt(0);
                auto notifyResult = service.GetCharacteristicsForUuidAsync(parseGuid(kNotifyUuid), BluetoothCacheMode::Uncached).get();
                auto writeResult = service.GetCharacteristicsForUuidAsync(parseGuid(kWriteUuid), BluetoothCacheMode::Uncached).get();
                if (notifyResult.Characteristics().Size() == 0 || writeResult.Characteristics().Size() == 0) {
                    impl_->update(LinkState::Error, "Expected EDIFIER characteristics were not found.");
                    break;
                }
                impl_->device = device;
                impl_->notify = notifyResult.Characteristics().GetAt(0);
                impl_->write = writeResult.Characteristics().GetAt(0);
                impl_->receivedToken = impl_->notify.ValueChanged([this](auto const&, GattValueChangedEventArgs const& args) {
                    const auto bytes = fromBuffer(args.CharacteristicValue());
                    std::scoped_lock guard(impl_->lock);
                    impl_->info.lastPacket = hex(bytes);
                    try {
                        const auto frame = parseV2(bytes);
                        if (frame.checksumValid && frame.command == static_cast<std::uint8_t>(Command::GetCustomEq)) {
                            impl_->info.customEq = parseCustomEq(frame.payload);
                            if (impl_->info.customEq.bands.size() >= 9) {
                                impl_->info.nineBandFamily = true;
                            }
                            ++impl_->info.customEqRevision;
                        }
                        if (frame.checksumValid && frame.command == static_cast<std::uint8_t>(Command::GetVolume) && frame.payload.size() >= 2) {
                            impl_->info.volumeMax = frame.payload[0];
                            impl_->info.volumeCurrent = frame.payload[1];
                            impl_->info.volumeKnown = true;
                            ++impl_->info.volumeRevision;
                        }
                        if (frame.checksumValid && frame.command == static_cast<std::uint8_t>(Command::GetInput) && !frame.payload.empty()) {
                            if (frame.payload.size() >= 2) {
                                impl_->info.inputMax = frame.payload[0];
                                impl_->info.inputCurrent = frame.payload[1];
                            } else {
                                impl_->info.inputMax = 1;
                                impl_->info.inputCurrent = frame.payload[0];
                            }
                            impl_->info.inputKnown = true;
                            ++impl_->info.inputRevision;
                        }
                        if (frame.checksumValid && !frame.payload.empty() &&
                            (frame.command == static_cast<std::uint8_t>(Command::GetEq) ||
                             frame.command == static_cast<std::uint8_t>(Command::SetEq))) {
                            impl_->info.eqCurrent = frame.payload[0];
                            impl_->info.eqKnown = true;
                            ++impl_->info.eqRevision;
                            if (frame.payload.size() >= 6) {
                                impl_->info.calibration = parseEqCalibration(frame.payload);
                                ++impl_->info.calibrationRevision;
                            }
                        }
                        if (frame.checksumValid && frame.command == static_cast<std::uint8_t>(Command::SetEq)) {
                            sendRead(Command::GetEq);
                        }
                        if (frame.checksumValid && !frame.payload.empty() && frame.command == static_cast<std::uint8_t>(Command::GetLed)) {
                            impl_->info.ledCurrent = frame.payload[0] == 1;
                            impl_->info.ledKnown = true;
                            ++impl_->info.ledRevision;
                        }
                        if (frame.checksumValid && frame.command == static_cast<std::uint8_t>(Command::GetVersion) && !frame.payload.empty()) {
                            std::string ver;
                            bool isAscii = true;
                            for (auto b : frame.payload) {
                                if (b < 0x20 || b > 0x7E) { isAscii = false; break; }
                            }
                            if (isAscii && !frame.payload.empty()) {
                                ver = std::string(frame.payload.begin(), frame.payload.end());
                            } else {
                                for (std::size_t i = 0; i < frame.payload.size(); ++i) {
                                    if (i) ver += '.';
                                    ver += std::to_string(frame.payload[i]);
                                }
                                if (!ver.empty() && ver[0] != 'v' && ver[0] != 'V') ver = "v" + ver;
                            }
                            impl_->info.firmwareVersion = ver;
                            ++impl_->info.firmwareRevision;
                        }
                    } catch (...) {}
                });

                impl_->statusToken = device.ConnectionStatusChanged([this](auto const& dev, auto const&) {
                    if (dev.ConnectionStatus() == BluetoothConnectionStatus::Disconnected) {
                        std::scoped_lock guard(impl_->lock);
                        if (impl_->info.state == LinkState::Ready) {
                            impl_->info.state = LinkState::Connecting;
                            impl_->info.detail = "Speaker disconnected / standby. Reconnecting...";
                            impl_->reconnectRequested = true;
                            impl_->wake.notify_all();
                        }
                    }
                });

                const auto status = impl_->notify.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
                if (status != GattCommunicationStatus::Success) {
                    impl_->cleanupGatt();
                    if (!impl_->autoReconnect || stop.stop_requested()) {
                        impl_->update(LinkState::Error, "Could not enable MR3 notifications.");
                        break;
                    }
                    impl_->update(LinkState::Connecting, "Retrying notifications setup...");
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }
                impl_->update(LinkState::Ready, "Connected. Loading settings.");
                {
                    AppConfig cfg;
                    std::scoped_lock guard(impl_->lock);
                    cfg.lastAddress = impl_->info.address;
                    cfg.deviceName = impl_->info.deviceName;
                    cfg.modelName = impl_->info.modelName;
                    cfg.serviceUuid = impl_->info.serviceUuid;
                    saveDeviceConfig(cfg);
                }
                for (auto command : {Command::GetVolume, Command::GetEq,
                        Command::GetCustomEq, Command::GetVersion,
                        Command::GetSupport, Command::GetDeviceState}) sendRead(command);

                BluetoothLEAdvertisementWatcher rssiWatcher;
                rssiWatcher.ScanningMode(BluetoothLEScanningMode::Passive);
                auto rssiToken = rssiWatcher.Received([this, address](auto const&, auto const& args) {
                    if (args.BluetoothAddress() == address) {
                        std::scoped_lock guard(impl_->lock);
                        impl_->info.rssi = args.RawSignalStrengthInDBm();
                    }
                });
                try { rssiWatcher.Start(); } catch (...) {}

                while (!stop.stop_requested() && !impl_->reconnectRequested) {
                    std::vector<std::uint8_t> packet;
                    {
                        std::unique_lock guard(impl_->lock);
                        impl_->wake.wait_for(guard, std::chrono::milliseconds(100),
                            [&] { return stop.stop_requested() || impl_->reconnectRequested || !impl_->pending.empty(); });
                        if (stop.stop_requested() || impl_->reconnectRequested) break;
                        if (impl_->pending.empty()) continue;
                        packet = std::move(impl_->pending.front());
                        impl_->pending.pop_front();
                    }
                    const auto result = impl_->write.WriteValueAsync(toBuffer(packet), GattWriteOption::WriteWithResponse).get();
                    if (result != GattCommunicationStatus::Success) {
                        impl_->reconnectRequested = true;
                        break;
                    }
                    std::unique_lock guard(impl_->lock);
                    impl_->wake.wait_for(guard, std::chrono::milliseconds(120),
                        [&] { return stop.stop_requested() || impl_->reconnectRequested; });
                }

                try {
                    rssiWatcher.Stop();
                    rssiWatcher.Received(rssiToken);
                } catch (...) {}

                impl_->cleanupGatt();
                if (stop.stop_requested()) break;

                if (impl_->reconnectRequested) {
                    impl_->reconnectRequested = false;
                    impl_->update(LinkState::Connecting, "Speaker reconnecting...");
                    for (int i = 0; i < 20 && !stop.stop_requested(); ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            } catch (const hresult_error& e) {
                impl_->cleanupGatt();
                if (!impl_->autoReconnect || stop.stop_requested()) {
                    impl_->update(LinkState::Error, to_string(e.message()));
                    break;
                }
                impl_->update(LinkState::Connecting, "Reconnecting...");
                for (int i = 0; i < 25 && !stop.stop_requested(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
    });
}

void BleClient::disconnect() {
    if (!impl_) return;
    impl_->autoReconnect = false;
    impl_->stopWorker();
    impl_->cleanupGatt();
    impl_->watcher = nullptr;
    {
        std::scoped_lock guard(impl_->lock);
        impl_->pending.clear();
        impl_->info.customEq = {};
        impl_->info.calibration = {};
        impl_->info.volumeKnown = false;
        impl_->info.inputKnown = false;
        impl_->info.eqKnown = false;
        impl_->info.ledKnown = false;
        impl_->info.lastPacket.clear();
    }
    impl_->update(LinkState::Idle, "Disconnected");
}

void BleClient::sendRead(Command command) { sendMutation(command, {}); }

void BleClient::sendMutation(Command command, std::vector<std::uint8_t> payload) {
    if (snapshot().state != LinkState::Ready) return;
    const auto packet = makeV2(command, payload);
    std::scoped_lock guard(impl_->lock);
    if (impl_->info.state != LinkState::Ready) return;
    impl_->pending.push_back(packet);
    impl_->wake.notify_one();
}

void BleClient::changeVolume(int delta) {
    int target = 0;
    {
        std::scoped_lock guard(impl_->lock);
        if (impl_->info.state != LinkState::Ready || !impl_->info.volumeKnown) return;
        target = std::clamp(impl_->info.volumeCurrent + delta, 0, impl_->info.volumeMax);
        if (target == impl_->info.volumeCurrent) return;
        impl_->info.volumeCurrent = target;
        ++impl_->info.volumeRevision;
    }
    sendMutation(Command::SetVolume, {static_cast<std::uint8_t>(target)});
    sendRead(Command::GetVolume);
}

void BleClient::setAutoReconnect(bool enable) {
    impl_->autoReconnect = enable;
    std::scoped_lock guard(impl_->lock);
    impl_->info.autoReconnect = enable;
}

void BleClient::setEqPreset(int preset) {
    std::vector<std::uint8_t> payload;
    {
        std::scoped_lock guard(impl_->lock);
        impl_->info.eqCurrent = preset;
        impl_->info.eqKnown = true;
        ++impl_->info.eqRevision;
        payload = makeSetEq(static_cast<std::uint8_t>(preset), impl_->info.calibration.valid ? &impl_->info.calibration : nullptr);
    }
    sendMutation(Command::SetEq, payload);
    sendRead(Command::GetEq);
}

void BleClient::setCustomEqBase(std::uint8_t baseProfile) {
    std::vector<std::uint8_t> payload;
    {
        std::scoped_lock guard(impl_->lock);
        impl_->info.customEq.byte0 = baseProfile;
        ++impl_->info.customEqRevision;
        if (impl_->info.customEq.valid && !impl_->info.customEq.bands.empty()) {
            payload = makeCustomEqBand(impl_->info.customEq, 0);
        }
    }
    if (!payload.empty()) {
        sendMutation(Command::SetCustomEqBand, payload);
        sendRead(Command::GetCustomEq);
    }
}

void BleClient::setEqCalibration(const EqCalibrationState& calib) {
    std::vector<std::uint8_t> payload;
    {
        std::scoped_lock guard(impl_->lock);
        impl_->info.calibration = calib;
        impl_->info.calibration.valid = true;
        ++impl_->info.calibrationRevision;
        payload = makeSetEq(static_cast<std::uint8_t>(impl_->info.eqCurrent), &calib);
    }
    sendMutation(Command::SetEq, payload);
}

void BleClient::resetEqCalibration() {
    std::vector<std::uint8_t> payload;
    {
        std::scoped_lock guard(impl_->lock);
        payload = makeResetEqCalibration(static_cast<std::uint8_t>(impl_->info.eqCurrent), impl_->info.calibration);
    }
    sendMutation(Command::SetEq, payload);
    sendRead(Command::GetEq);
}

} // namespace edifier

