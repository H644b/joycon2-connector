#pragma once
// BLEDevice.h - Cross-platform BLE abstraction using SimpleBLE
#include <simpleble/SimpleBLE.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

constexpr const char* INPUT_REPORT_UUID  = "ab7de9be-89fe-49ad-828f-118f09df7fd2";
constexpr const char* WRITE_COMMAND_UUID = "649d4ac9-8eb7-4e6c-af44-1ea54fe5f005";

// Find the service UUID that contains a given characteristic UUID
inline std::string FindServiceForChar(SimpleBLE::Peripheral& peripheral, const std::string& charUuid) {
    for (auto& service : peripheral.services()) {
        for (auto& characteristic : service.characteristics()) {
            if (characteristic.uuid() == charUuid) return service.uuid();
        }
    }
    return "";
}

struct BLECharacteristic {
    std::shared_ptr<SimpleBLE::Peripheral> peripheral;
    std::string serviceUuid;
    std::string charUuid;

    explicit operator bool() const { return peripheral && !charUuid.empty(); }

    void WriteWithoutResponse(const std::vector<uint8_t>& data) const {
        if (!peripheral) return;
        try {
            peripheral->write_command(serviceUuid, charUuid,
                SimpleBLE::ByteArray(data.begin(), data.end()));
        } catch (...) {}
    }

    void SubscribeNotify(std::function<void(const std::vector<uint8_t>&)> callback) const {
        if (!peripheral) return;
        try {
            peripheral->notify(serviceUuid, charUuid,
                [cb = std::move(callback)](SimpleBLE::ByteArray bytes) {
                    cb(std::vector<uint8_t>(bytes.begin(), bytes.end()));
                });
        } catch (...) {}
    }

    // SimpleBLE handles CCCD internally via notify(), so this is a no-op stub.
    void WriteDescriptor(const std::vector<uint8_t>& /*value*/) const {}
};

struct ConnectedJoyCon {
    std::shared_ptr<SimpleBLE::Peripheral> device;
    BLECharacteristic inputChar;
    BLECharacteristic writeChar;
    uint64_t bleAddress = 0;
};

enum class ScanState { Idle, Scanning, Found, Error, Timeout };
