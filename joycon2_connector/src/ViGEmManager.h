#pragma once
// ViGEmManager.h — Windows ViGEm backend + cross-platform VirtualGamepad::Instance()
#include "VirtualGamepad.h"
#include <iostream>
#include <atomic>

#ifdef _WIN32
#include <ViGEm/Client.h>
#include <ViGEm/Common.h>
#include <map>
#include <mutex>

// Static maps for ViGEm callback dispatch
namespace ViGEmCB {
    inline std::map<PVIGEM_TARGET, VibrationCallback> ds4Callbacks;
    inline std::map<PVIGEM_TARGET, VibrationCallback> x360Callbacks;
    inline std::mutex cbMutex;

    inline VOID CALLBACK DS4Callback(
        PVIGEM_CLIENT, PVIGEM_TARGET target,
        UCHAR LargeMotor, UCHAR SmallMotor,
        DS4_LIGHTBAR_COLOR, LPVOID)
    {
        std::lock_guard<std::mutex> lk(cbMutex);
        auto it = ds4Callbacks.find(target);
        if (it != ds4Callbacks.end()) it->second(LargeMotor, SmallMotor);
    }

    inline VOID CALLBACK X360Callback(
        PVIGEM_CLIENT, PVIGEM_TARGET target,
        UCHAR LargeMotor, UCHAR SmallMotor,
        UCHAR, LPVOID)
    {
        std::lock_guard<std::mutex> lk(cbMutex);
        auto it = x360Callbacks.find(target);
        if (it != x360Callbacks.end()) it->second(LargeMotor, SmallMotor);
    }
} // namespace ViGEmCB

class ViGEmGamepad : public VirtualGamepad {
public:
    static ViGEmGamepad& Get() {
        static ViGEmGamepad inst;
        return inst;
    }

    bool Init() override {
        if (client) return true;
        client = vigem_alloc();
        if (!client) { connected = false; return false; }
        auto ret = vigem_connect(client);
        if (!VIGEM_SUCCESS(ret)) {
            vigem_free(client);
            client = nullptr;
            connected = false;
            return false;
        }
        connected = true;
        return true;
    }

    void Shutdown() override {
        if (client) {
            vigem_disconnect(client);
            vigem_free(client);
            client = nullptr;
        }
        connected = false;
    }

    bool IsAvailable() const override { return connected; }

    void* AllocDS4() override  { return vigem_target_ds4_alloc(); }
    void* AllocX360() override { return vigem_target_x360_alloc(); }

    bool AddTarget(void* target) override {
        if (!client || !target) return false;
        return VIGEM_SUCCESS(vigem_target_add(client, static_cast<PVIGEM_TARGET>(target)));
    }

    void RemoveTarget(void* target) override {
        if (client && target) {
            auto t = static_cast<PVIGEM_TARGET>(target);
            vigem_target_remove(client, t);
            vigem_target_free(t);
        }
    }

    void UpdateDS4(void* target, const DS4_REPORT_EX& report) override {
        if (!client || !target) return;
        vigem_target_ds4_update_ex(client, static_cast<PVIGEM_TARGET>(target), report);
    }

    void UpdateX360(void* target, const XUSB_REPORT& report) override {
        if (!client || !target) return;
        vigem_target_x360_update(client, static_cast<PVIGEM_TARGET>(target), report);
    }

    void RegisterDS4VibrationCallback(void* target, VibrationCallback cb) override {
        if (!client || !target) return;
        auto t = static_cast<PVIGEM_TARGET>(target);
        {
            std::lock_guard<std::mutex> lk(ViGEmCB::cbMutex);
            ViGEmCB::ds4Callbacks[t] = std::move(cb);
        }
        vigem_target_ds4_register_notification(client, t, ViGEmCB::DS4Callback, nullptr);
    }

    void RegisterX360VibrationCallback(void* target, VibrationCallback cb) override {
        if (!client || !target) return;
        auto t = static_cast<PVIGEM_TARGET>(target);
        {
            std::lock_guard<std::mutex> lk(ViGEmCB::cbMutex);
            ViGEmCB::x360Callbacks[t] = std::move(cb);
        }
        vigem_target_x360_register_notification(client, t, ViGEmCB::X360Callback, nullptr);
    }

    void UnregisterVibrationCallback(void* target) override {
        if (!target) return;
        auto t = static_cast<PVIGEM_TARGET>(target);
        // Try both — only one will be registered
        vigem_target_ds4_unregister_notification(t);
        vigem_target_x360_unregister_notification(t);
        {
            std::lock_guard<std::mutex> lk(ViGEmCB::cbMutex);
            ViGEmCB::ds4Callbacks.erase(t);
            ViGEmCB::x360Callbacks.erase(t);
        }
    }

    // Legacy accessor used by old code paths still transitioning
    PVIGEM_CLIENT GetClient() const { return client; }

    ~ViGEmGamepad() override { Shutdown(); }

private:
    ViGEmGamepad() = default;
    PVIGEM_CLIENT client = nullptr;
    std::atomic<bool> connected{ false };
};

// Legacy singleton alias so old call sites (ViGEmManager::Instance()) still compile
class ViGEmManager {
public:
    static ViGEmGamepad& Instance() { return ViGEmGamepad::Get(); }
};

inline VirtualGamepad& VirtualGamepad::Instance() {
    return ViGEmGamepad::Get();
}

#elif defined(__linux__)
// Forward declaration — UinputGamepad is defined in platform/UinputGamepad.h
#include "platform/UinputGamepad.h"
inline VirtualGamepad& VirtualGamepad::Instance() {
    return UinputGamepad::Get();
}
#else
// macOS / other: stub implementation
#include <cstdio>

class StubGamepad : public VirtualGamepad {
public:
    static StubGamepad& Get() { static StubGamepad g; return g; }
    bool Init() override { return true; }
    void Shutdown() override {}
    bool IsAvailable() const override { return false; }
    void* AllocDS4() override { return nullptr; }
    void* AllocX360() override { return nullptr; }
    bool AddTarget(void*) override { return false; }
    void RemoveTarget(void*) override {}
    void UpdateDS4(void*, const DS4_REPORT_EX&) override {}
    void UpdateX360(void*, const XUSB_REPORT&) override {}
    void RegisterDS4VibrationCallback(void*, VibrationCallback) override {}
    void RegisterX360VibrationCallback(void*, VibrationCallback) override {}
    void UnregisterVibrationCallback(void*) override {}
};

inline VirtualGamepad& VirtualGamepad::Instance() {
    return StubGamepad::Get();
}
#endif // _WIN32
