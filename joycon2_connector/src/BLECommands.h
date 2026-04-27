#pragma once
// BLECommands.h — Joy-Con BLE write helpers using SimpleBLE.
// Defines WriteHandle (lightweight cross-platform write endpoint)
// and all higher-level command helpers.

#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <memory>

// Forward: SimpleBLE::Peripheral (included by DeviceManager.h)
namespace SimpleBLE { class Peripheral; }

// ── WriteHandle — represents a writable BLE characteristic ───────────────────
struct WriteHandle {
    std::shared_ptr<SimpleBLE::Peripheral> peripheral;
    std::string serviceUuid;
    std::string charUuid;

    explicit operator bool() const {
        return peripheral && !charUuid.empty();
    }

    void write_command(const std::vector<uint8_t>& data) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Low-level write helper (defined in DeviceManager translation unit via include)
// These implementations reference SimpleBLE::Peripheral::write_command().
// ─────────────────────────────────────────────────────────────────────────────

// We include the simpleBLE header here because WriteHandle::write_command needs it.
// The header guard ensures it's included only once.
#include <simpleble/SimpleBLE.h>

inline void WriteHandle::write_command(const std::vector<uint8_t>& data) const {
    if (!*this) return;
    try {
        peripheral->write_command(serviceUuid, charUuid,
            std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    } catch (...) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE command helpers (all accept WriteHandle instead of GattCharacteristic)
// ─────────────────────────────────────────────────────────────────────────────

inline void SendGenericCommand(const WriteHandle& handle, uint8_t cmdId, uint8_t subCmdId,
                               const std::vector<uint8_t>& data) {
    if (!handle) return;
    std::vector<uint8_t> pkt;
    pkt.reserve(8 + data.size());
    pkt.push_back(cmdId);
    pkt.push_back(0x91);
    pkt.push_back(0x01);
    pkt.push_back(subCmdId);
    pkt.push_back(0x00);
    pkt.push_back(static_cast<uint8_t>(data.size()));
    pkt.push_back(0x00);
    pkt.push_back(0x00);
    pkt.insert(pkt.end(), data.begin(), data.end());
    handle.write_command(pkt);
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
}

inline void SendCustomCommands(const WriteHandle& handle) {
    if (!handle) return;
    const std::vector<uint8_t> cmds[] = {
        { 0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
        { 0x0c, 0x91, 0x01, 0x04, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
    };
    for (const auto& cmd : cmds) {
        handle.write_command(cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

inline void EmitSound(const WriteHandle& handle) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = 0x04;
    SendGenericCommand(handle, 0x0A, 0x02, data);
}

inline void SetPlayerLEDs(const WriteHandle& handle, uint8_t pattern) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = pattern;
    SendGenericCommand(handle, 0x09, 0x07, data);
}

// Vibration sample IDs
enum VibrationSample : uint8_t {
    VIB_NONE         = 0x00,
    VIB_BUZZ         = 0x01,
    VIB_FIND         = 0x02,
    VIB_CONNECT      = 0x03,
    VIB_PAIRING      = 0x04,
    VIB_STRONG_THUNK = 0x05,
    VIB_DUN          = 0x06,
    VIB_DING         = 0x07,
};

inline void SendVibrationSample(const WriteHandle& handle, uint8_t sampleId) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = sampleId;
    SendGenericCommand(handle, 0x0A, 0x02, data);
}

inline void SendRawVibration(const WriteHandle& handle, bool enabled,
                              const uint8_t vibData[12], uint8_t sequenceCounter) {
    if (!handle) return;
    std::vector<uint8_t> pkt(16, 0x00);
    pkt[0] = 0x00;
    pkt[1] = 0x50 | (sequenceCounter & 0x0F);
    pkt[2] = enabled ? 0x01 : 0x00;
    for (int i = 0; i < 12; ++i) pkt[3 + i] = vibData[i];
    pkt[15] = 0x00;
    handle.write_command(pkt);
}

inline void EncodeVibrationPayload(uint8_t largeMotor, uint8_t smallMotor, uint8_t outData[12]) {
    for (int i = 0; i < 12; ++i) outData[i] = 0;
    outData[0] = largeMotor;
    outData[1] = smallMotor;
    outData[2] = largeMotor;
    outData[3] = smallMotor;
}

// ── Async variants ────────────────────────────────────────────────────────────
inline void SetPlayerLEDsAsync(WriteHandle handle, uint8_t pattern) {
    std::thread([handle, pattern]() { SetPlayerLEDs(handle, pattern); }).detach();
}

inline void EmitSoundAsync(WriteHandle handle) {
    std::thread([handle]() { EmitSound(handle); }).detach();
}

inline void SendVibrationSampleAsync(WriteHandle handle, uint8_t sampleId) {
    std::thread([handle, sampleId]() { SendVibrationSample(handle, sampleId); }).detach();
}

inline void SendRawVibrationAsync(WriteHandle handle, bool enabled,
                                   const uint8_t vibData[12], uint8_t seq) {
    std::vector<uint8_t> data(vibData, vibData + 12);
    std::thread([handle, enabled, data, seq]() {
        SendRawVibration(handle, enabled, data.data(), seq);
    }).detach();
}
