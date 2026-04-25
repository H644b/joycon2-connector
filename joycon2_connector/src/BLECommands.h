#pragma once
// BLE Commands for Joy-Con / Pro Controller communication — cross-platform (SimpleBLE)
#include "BLEDevice.h"
#include <vector>
#include <thread>
#include <chrono>

inline void SendGenericCommand(const BLECharacteristic& characteristic, uint8_t cmdId, uint8_t subCmdId, const std::vector<uint8_t>& data) {
    if (!characteristic) return;

    std::vector<uint8_t> packet;
    packet.push_back(cmdId);
    packet.push_back(0x91);
    packet.push_back(0x01);
    packet.push_back(subCmdId);
    packet.push_back(0x00);
    packet.push_back(static_cast<uint8_t>(data.size()));
    packet.push_back(0x00);
    packet.push_back(0x00);
    for (uint8_t b : data) packet.push_back(b);

    characteristic.WriteWithoutResponse(packet);
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
}

inline void SendCustomCommands(const BLECharacteristic& characteristic) {
    std::vector<std::vector<uint8_t>> commands = {
        { 0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
        { 0x0c, 0x91, 0x01, 0x04, 0x00, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 }
    };
    for (const auto& cmd : commands) {
        characteristic.WriteWithoutResponse(cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

inline void EmitSound(const BLECharacteristic& characteristic) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = 0x04;
    SendGenericCommand(characteristic, 0x0A, 0x02, data);
}

inline void SetPlayerLEDs(const BLECharacteristic& characteristic, uint8_t pattern) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = pattern;
    SendGenericCommand(characteristic, 0x09, 0x07, data);
}

// Vibration sample IDs (from protocol reverse engineering)
enum VibrationSample : uint8_t {
    VIB_NONE        = 0x00,
    VIB_BUZZ        = 0x01,
    VIB_FIND        = 0x02,
    VIB_CONNECT     = 0x03,
    VIB_PAIRING     = 0x04,
    VIB_STRONG_THUNK= 0x05,
    VIB_DUN         = 0x06,
    VIB_DING        = 0x07,
};

inline void SendVibrationSample(const BLECharacteristic& characteristic, uint8_t sampleId) {
    std::vector<uint8_t> data(8, 0x00);
    data[0] = sampleId;
    SendGenericCommand(characteristic, 0x0A, 0x02, data);
}

inline void SendRawVibration(const BLECharacteristic& characteristic,
                             bool enabled, const uint8_t vibData[12],
                             uint8_t sequenceCounter) {
    if (!characteristic) return;

    std::vector<uint8_t> packet;
    packet.push_back(0x00);
    packet.push_back(0x50 | (sequenceCounter & 0x0F));
    packet.push_back(enabled ? 0x01 : 0x00);
    for (int i = 0; i < 12; ++i) packet.push_back(vibData[i]);
    packet.push_back(0x00);

    characteristic.WriteWithoutResponse(packet);
}

inline void EncodeVibrationPayload(uint8_t largeMotor, uint8_t smallMotor, uint8_t outData[12]) {
    for (int i = 0; i < 12; ++i) outData[i] = 0;
    outData[0] = largeMotor;
    outData[1] = smallMotor;
    outData[2] = largeMotor;
    outData[3] = smallMotor;
}

// Non-blocking async versions (copy BLECharacteristic by value — safe because it's a shared_ptr wrapper)
inline void SetPlayerLEDsAsync(BLECharacteristic characteristic, uint8_t pattern) {
    std::thread([characteristic, pattern]() {
        SetPlayerLEDs(characteristic, pattern);
    }).detach();
}

inline void EmitSoundAsync(BLECharacteristic characteristic) {
    std::thread([characteristic]() {
        EmitSound(characteristic);
    }).detach();
}

inline void SendVibrationSampleAsync(BLECharacteristic characteristic, uint8_t sampleId) {
    std::thread([characteristic, sampleId]() {
        SendVibrationSample(characteristic, sampleId);
    }).detach();
}

inline void SendRawVibrationAsync(BLECharacteristic characteristic,
                                   bool enabled, const uint8_t vibData[12],
                                   uint8_t sequenceCounter) {
    std::vector<uint8_t> data(vibData, vibData + 12);
    std::thread([characteristic, enabled, data, sequenceCounter]() {
        SendRawVibration(characteristic, enabled, data.data(), sequenceCounter);
    }).detach();
}
