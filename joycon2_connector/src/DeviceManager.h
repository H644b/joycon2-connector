#pragma once
// DeviceManager - Async BLE scanning using SimpleBLE (cross-platform)
#include "BLEDevice.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <cstring>

constexpr uint16_t JOYCON_MANUFACTURER_ID = 1363; // 0x0553
inline const std::vector<uint8_t> JOYCON_MANUFACTURER_PREFIX = { 0x01, 0x00, 0x03, 0x7E };

class DeviceManager {
public:
    static DeviceManager& Instance() {
        static DeviceManager inst;
        return inst;
    }

    using ScanCallback = std::function<void(ConnectedJoyCon, ScanState)>;

    ScanState GetScanState() const { return state.load(); }

    void StartScan(ScanCallback callback) {
        if (state.load() == ScanState::Scanning) return;

        state.store(ScanState::Scanning);
        scanCallback = callback;

        if (scanThread.joinable()) scanThread.detach();
        scanThread = std::thread([this]() { RunScan(); });
    }

    void StopScan() {
        cancelScan.store(true);
        cv.notify_all();
        state.store(ScanState::Idle);
        if (scanThread.joinable()) scanThread.detach();
    }

    ~DeviceManager() { StopScan(); }

private:
    DeviceManager() = default;

    void RunScan() {
        cancelScan.store(false);

        auto adapters = SimpleBLE::Adapter::get_adapters();
        if (adapters.empty()) {
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }
        SimpleBLE::Adapter adapter = adapters[0];

        std::mutex mtx;
        std::shared_ptr<SimpleBLE::Peripheral> foundPeripheral;
        std::atomic<bool> found{ false };

        adapter.set_callback_on_scan_found([&](SimpleBLE::Peripheral peripheral) {
            if (found.load() || cancelScan.load()) return;

            auto mfgData = peripheral.manufacturer_data();
            auto it = mfgData.find(JOYCON_MANUFACTURER_ID);
            if (it == mfgData.end()) return;

            const auto& data = it->second;
            if (data.size() < JOYCON_MANUFACTURER_PREFIX.size()) return;
            if (!std::equal(JOYCON_MANUFACTURER_PREFIX.begin(), JOYCON_MANUFACTURER_PREFIX.end(),
                            data.begin())) return;

            bool expected = false;
            if (!found.compare_exchange_strong(expected, true)) return;

            {
                std::lock_guard<std::mutex> lock(mtx);
                foundPeripheral = std::make_shared<SimpleBLE::Peripheral>(peripheral);
            }
            cv.notify_one();
        });

        adapter.scan_start();

        {
            std::unique_lock<std::mutex> lock(mtx);
            if (!cv.wait_for(lock, std::chrono::seconds(30),
                [&]() { return found.load() || cancelScan.load(); })) {
                adapter.scan_stop();
                state.store(ScanState::Timeout);
                if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Timeout);
                return;
            }
        }
        adapter.scan_stop();

        if (cancelScan.load() || !foundPeripheral) {
            state.store(ScanState::Idle);
            return;
        }

        try {
            foundPeripheral->connect();
        } catch (...) {
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }

        if (cancelScan.load()) {
            try { foundPeripheral->disconnect(); } catch (...) {}
            state.store(ScanState::Idle);
            return;
        }

        ConnectedJoyCon cj;
        cj.device = foundPeripheral;

        // Derive uint64_t BLE address from the address string
        try {
            std::string addrStr = foundPeripheral->address();
            // Address format: "AA:BB:CC:DD:EE:FF"
            uint64_t addr = 0;
            for (char c : addrStr) {
                if (c == ':') continue;
                addr = (addr << 4);
                if (c >= '0' && c <= '9') addr |= (c - '0');
                else if (c >= 'A' && c <= 'F') addr |= (c - 'A' + 10);
                else if (c >= 'a' && c <= 'f') addr |= (c - 'a' + 10);
            }
            cj.bleAddress = addr;
        } catch (...) {}

        // Find input/write characteristics
        try {
            std::string inputSvcUuid  = FindServiceForChar(*foundPeripheral, INPUT_REPORT_UUID);
            std::string writeSvcUuid  = FindServiceForChar(*foundPeripheral, WRITE_COMMAND_UUID);

            if (!inputSvcUuid.empty()) {
                cj.inputChar.peripheral  = foundPeripheral;
                cj.inputChar.serviceUuid = inputSvcUuid;
                cj.inputChar.charUuid    = INPUT_REPORT_UUID;
            }
            if (!writeSvcUuid.empty()) {
                cj.writeChar.peripheral  = foundPeripheral;
                cj.writeChar.serviceUuid = writeSvcUuid;
                cj.writeChar.charUuid    = WRITE_COMMAND_UUID;
            }
        } catch (...) {
            state.store(ScanState::Error);
            if (scanCallback) scanCallback(ConnectedJoyCon{}, ScanState::Error);
            return;
        }

        if (cancelScan.load()) {
            try { foundPeripheral->disconnect(); } catch (...) {}
            state.store(ScanState::Idle);
            return;
        }

        state.store(ScanState::Found);
        if (scanCallback) scanCallback(cj, ScanState::Found);
    }

    std::atomic<ScanState> state{ ScanState::Idle };
    std::atomic<bool> cancelScan{ false };
    std::condition_variable cv;
    ScanCallback scanCallback;
    std::thread scanThread;
};
