#pragma once
// VirtualController.h — Platform-agnostic virtual gamepad abstraction.
//
// IVirtualController is the common interface.
// Factory function CreateVirtualController() returns the right implementation:
//   Windows  — ViGEmController   (requires ViGEmBus driver)
//   Linux    — UInputController  (uses kernel uinput, no extra driver needed)
//   macOS    — StubController    (returns false; VirtualHIDDevice required)

#include "GamepadReport.h"
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <cstring>

// ─── Common interface ────────────────────────────────────────────────────────

class IVirtualController {
public:
    /// Initialize and register the virtual device.
    /// useXbox=true → emulate Xbox 360; false → emulate DS4.
    virtual bool Initialize(bool useXbox) = 0;

    /// Unregister and release the virtual device.
    virtual void Remove() = 0;

    /// Submit a DS4 state update (only valid when useXbox=false).
    virtual bool SubmitDS4(const DS4_REPORT_EX& report) = 0;

    /// Submit an Xbox 360 state update (only valid when useXbox=true).
    virtual bool SubmitXUSB(const XUSB_REPORT& report) = 0;

    /// Register a vibration callback invoked when the host sends rumble output.
    /// Callback receives (largeMotor 0-255, smallMotor 0-255).
    virtual void SetVibrationCallback(std::function<void(uint8_t, uint8_t)> cb) = 0;

    virtual bool IsXboxMode() const = 0;
    virtual std::string GetLastError() const = 0;

    virtual ~IVirtualController() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Windows — ViGEm implementation
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32

#include <ViGEm/Client.h>
#include <ViGEm/Common.h>

// Forward: each ViGEmController instance holds its own ViGEm target handle.
// The singleton ViGEmManager (ViGEmManager.h) still owns the client.

class ViGEmController final : public IVirtualController {
public:
    ViGEmController() = default;
    ~ViGEmController() override { Remove(); }

    bool Initialize(bool useXbox) override;
    void Remove() override;
    bool SubmitDS4(const DS4_REPORT_EX& report) override;
    bool SubmitXUSB(const XUSB_REPORT& report) override;
    void SetVibrationCallback(std::function<void(uint8_t, uint8_t)> cb) override {
        vibrationCallback_ = std::move(cb);
    }
    bool IsXboxMode() const override { return xboxMode_; }
    std::string GetLastError() const override { return lastError_; }

    // Internal — called by the static ViGEm callbacks below
    void OnVibration(uint8_t large, uint8_t small) {
        if (vibrationCallback_) vibrationCallback_(large, small);
    }

    PVIGEM_TARGET GetTarget() const { return target_; }

private:
    PVIGEM_TARGET target_  = nullptr;
    bool          xboxMode_ = false;
    std::string   lastError_;
    std::function<void(uint8_t, uint8_t)> vibrationCallback_;
};

// Static C-style callbacks needed by ViGEm
static VOID CALLBACK _ViGEm_DS4VibCb(
    PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small,
    DS4_LIGHTBAR_COLOR, LPVOID userData)
{
    if (userData) static_cast<ViGEmController*>(userData)->OnVibration(large, small);
}
static VOID CALLBACK _ViGEm_X360VibCb(
    PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small,
    UCHAR, LPVOID userData)
{
    if (userData) static_cast<ViGEmController*>(userData)->OnVibration(large, small);
}

// ViGEmManager forward declaration (defined in ViGEmManager.h)
class ViGEmManager;

inline bool ViGEmController::Initialize(bool useXbox) {
    // Import from ViGEmManager singleton
    extern PVIGEM_CLIENT _GetViGEmClient();
    PVIGEM_CLIENT client = _GetViGEmClient();
    if (!client) { lastError_ = "ViGEm client not initialized"; return false; }

    xboxMode_ = useXbox;
    target_ = useXbox ? vigem_target_x360_alloc() : vigem_target_ds4_alloc();
    if (!target_) { lastError_ = "vigem_target_alloc failed"; return false; }

    auto ret = vigem_target_add(client, target_);
    if (!VIGEM_SUCCESS(ret)) {
        vigem_target_free(target_); target_ = nullptr;
        lastError_ = "vigem_target_add failed (code " + std::to_string(ret) + ")";
        return false;
    }

    if (useXbox)
        vigem_target_x360_register_notification(client, target_, _ViGEm_X360VibCb, this);
    else
        vigem_target_ds4_register_notification(client, target_, _ViGEm_DS4VibCb, this);

    return true;
}

inline void ViGEmController::Remove() {
    if (!target_) return;
    extern PVIGEM_CLIENT _GetViGEmClient();
    PVIGEM_CLIENT client = _GetViGEmClient();
    if (client) {
        if (xboxMode_) vigem_target_x360_unregister_notification(target_);
        else           vigem_target_ds4_unregister_notification(target_);
        vigem_target_remove(client, target_);
    }
    vigem_target_free(target_);
    target_ = nullptr;
}

inline bool ViGEmController::SubmitDS4(const DS4_REPORT_EX& report) {
    if (!target_) return false;
    extern PVIGEM_CLIENT _GetViGEmClient();
    PVIGEM_CLIENT client = _GetViGEmClient();
    if (!client) return false;
    return VIGEM_SUCCESS(vigem_target_ds4_update_ex(client, target_, report));
}

inline bool ViGEmController::SubmitXUSB(const XUSB_REPORT& report) {
    if (!target_) return false;
    extern PVIGEM_CLIENT _GetViGEmClient();
    PVIGEM_CLIENT client = _GetViGEmClient();
    if (!client) return false;
    return VIGEM_SUCCESS(vigem_target_x360_update(client, target_, report));
}

// ─────────────────────────────────────────────────────────────────────────────
// Linux — uinput implementation
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(__linux__)

#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <stdexcept>
#include <mutex>
#include <algorithm>

// Axis calibration: DS4/XUSB sticks are -32768..32767, uinput ABS range matches.
// Triggers are 0..255 in DS4/XUSB, mapped to ABS_Z/ABS_RZ.

class UInputController final : public IVirtualController {
public:
    UInputController()  = default;
    ~UInputController() override { Remove(); }

    bool Initialize(bool useXbox) override;
    void Remove() override;
    bool SubmitDS4(const DS4_REPORT_EX& report) override;
    bool SubmitXUSB(const XUSB_REPORT& report) override;
    void SetVibrationCallback(std::function<void(uint8_t, uint8_t)> cb) override {
        std::lock_guard<std::mutex> lk(mtx_);
        vibrationCallback_ = std::move(cb);
    }
    bool IsXboxMode() const override { return xboxMode_; }
    std::string GetLastError() const override { return lastError_; }

private:
    int    fd_      = -1;
    bool   xboxMode_ = false;
    std::string lastError_;
    std::function<void(uint8_t, uint8_t)> vibrationCallback_;
    std::mutex   mtx_;
    std::thread  ffThread_;
    std::atomic<bool> ffRunning_{false};

    void EmitEvent(uint16_t type, uint16_t code, int32_t value);
    void SetupAbsAxis(uint16_t code, int32_t min, int32_t max, int32_t flat, int32_t fuzz);
    void StartFFThread();
};

inline void UInputController::EmitEvent(uint16_t type, uint16_t code, int32_t value) {
    if (fd_ < 0) return;
    struct input_event ev{};
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    write(fd_, &ev, sizeof(ev));
}

inline void UInputController::SetupAbsAxis(uint16_t code, int32_t min, int32_t max, int32_t flat, int32_t fuzz) {
    ioctl(fd_, UI_SET_ABSBIT, code);
    struct uinput_abs_setup abs{};
    abs.code         = code;
    abs.absinfo.minimum = min;
    abs.absinfo.maximum = max;
    abs.absinfo.flat    = flat;
    abs.absinfo.fuzz    = fuzz;
    ioctl(fd_, UI_ABS_SETUP, &abs);
}

inline bool UInputController::Initialize(bool useXbox) {
    xboxMode_ = useXbox;
    fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd_ < 0) {
        lastError_ = "Cannot open /dev/uinput: " + std::string(strerror(errno)) +
                     " (try: sudo modprobe uinput, or add user to 'input' group)";
        return false;
    }

    // Enable event types
    ioctl(fd_, UI_SET_EVBIT, EV_KEY);
    ioctl(fd_, UI_SET_EVBIT, EV_ABS);
    ioctl(fd_, UI_SET_EVBIT, EV_FF);  // force feedback (vibration)

    // ── Buttons ──────────────────────────────────────────────────────────────
    // Common gamepad buttons mapped to standard Linux BTN_* codes
    const int buttons[] = {
        BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH,
        BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
        BTN_SELECT, BTN_START, BTN_MODE,
        BTN_THUMBL, BTN_THUMBR,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
    };
    for (int b : buttons) ioctl(fd_, UI_SET_KEYBIT, b);

    // ── Axes ─────────────────────────────────────────────────────────────────
    // Left stick
    SetupAbsAxis(ABS_X,  -32768, 32767, 128, 16);
    SetupAbsAxis(ABS_Y,  -32768, 32767, 128, 16);
    // Right stick
    SetupAbsAxis(ABS_RX, -32768, 32767, 128, 16);
    SetupAbsAxis(ABS_RY, -32768, 32767, 128, 16);
    // Triggers
    SetupAbsAxis(ABS_Z,  0, 255, 0, 0);
    SetupAbsAxis(ABS_RZ, 0, 255, 0, 0);
    // D-pad hat (DS4 mode uses this; Xbox mode uses buttons above)
    if (!xboxMode_) {
        SetupAbsAxis(ABS_HAT0X, -1, 1, 0, 0);
        SetupAbsAxis(ABS_HAT0Y, -1, 1, 0, 0);
    }

    // ── FF / Rumble ───────────────────────────────────────────────────────────
    ioctl(fd_, UI_SET_FFBIT, FF_RUMBLE);

    // ── Device info ───────────────────────────────────────────────────────────
    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = useXbox ? 0x045e : 0x054c;  // Microsoft / Sony
    usetup.id.product = useXbox ? 0x028e : 0x05c4;  // Xbox 360 / DS4
    usetup.id.version = 0x0100;
    usetup.ff_effects_max = 1;
    strncpy(usetup.name,
            useXbox ? "JoyCon2 Xbox 360 Controller" : "JoyCon2 DualShock 4",
            UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(fd_, UI_DEV_SETUP, &usetup) < 0) {
        lastError_ = "UI_DEV_SETUP failed: " + std::string(strerror(errno));
        close(fd_); fd_ = -1; return false;
    }
    if (ioctl(fd_, UI_DEV_CREATE) < 0) {
        lastError_ = "UI_DEV_CREATE failed: " + std::string(strerror(errno));
        close(fd_); fd_ = -1; return false;
    }

    StartFFThread();
    return true;
}

inline void UInputController::Remove() {
    ffRunning_.store(false);
    if (ffThread_.joinable()) ffThread_.join();
    if (fd_ >= 0) {
        ioctl(fd_, UI_DEV_DESTROY);
        close(fd_);
        fd_ = -1;
    }
}

inline bool UInputController::SubmitDS4(const DS4_REPORT_EX& r) {
    if (fd_ < 0) return false;
    const DS4_REPORT& rep = r.Report;

    // Sticks (DS4: 0-255, centre=128 → Linux: -32768..32767)
    auto conv = [](uint8_t v) -> int32_t {
        return static_cast<int32_t>(v) * 257 - 32768;
    };
    EmitEvent(EV_ABS, ABS_X,  conv(rep.bThumbLX));
    EmitEvent(EV_ABS, ABS_Y,  conv(rep.bThumbLY));
    EmitEvent(EV_ABS, ABS_RX, conv(rep.bThumbRX));
    EmitEvent(EV_ABS, ABS_RY, conv(rep.bThumbRY));
    // Triggers
    EmitEvent(EV_ABS, ABS_Z,  rep.bTriggerL);
    EmitEvent(EV_ABS, ABS_RZ, rep.bTriggerR);

    // D-pad via HAT0
    uint8_t dpad = rep.wButtons & 0xF;
    int32_t hx = 0, hy = 0;
    switch (dpad) {
        case 0: hy = -1; break;                    // N
        case 1: hx =  1; hy = -1; break;           // NE
        case 2: hx =  1; break;                    // E
        case 3: hx =  1; hy =  1; break;           // SE
        case 4: hy =  1; break;                    // S
        case 5: hx = -1; hy =  1; break;           // SW
        case 6: hx = -1; break;                    // W
        case 7: hx = -1; hy = -1; break;           // NW
        default: break;                            // None (8)
    }
    EmitEvent(EV_ABS, ABS_HAT0X, hx);
    EmitEvent(EV_ABS, ABS_HAT0Y, hy);

    USHORT btn = rep.wButtons;
    EmitEvent(EV_KEY, BTN_SOUTH,  (btn & DS4_BUTTON_CROSS)          ? 1 : 0);
    EmitEvent(EV_KEY, BTN_EAST,   (btn & DS4_BUTTON_CIRCLE)         ? 1 : 0);
    EmitEvent(EV_KEY, BTN_WEST,   (btn & DS4_BUTTON_SQUARE)         ? 1 : 0);
    EmitEvent(EV_KEY, BTN_NORTH,  (btn & DS4_BUTTON_TRIANGLE)       ? 1 : 0);
    EmitEvent(EV_KEY, BTN_TL,     (btn & DS4_BUTTON_SHOULDER_LEFT)  ? 1 : 0);
    EmitEvent(EV_KEY, BTN_TR,     (btn & DS4_BUTTON_SHOULDER_RIGHT) ? 1 : 0);
    EmitEvent(EV_KEY, BTN_TL2,    (btn & DS4_BUTTON_TRIGGER_LEFT)   ? 1 : 0);
    EmitEvent(EV_KEY, BTN_TR2,    (btn & DS4_BUTTON_TRIGGER_RIGHT)  ? 1 : 0);
    EmitEvent(EV_KEY, BTN_SELECT, (btn & DS4_BUTTON_SHARE)          ? 1 : 0);
    EmitEvent(EV_KEY, BTN_START,  (btn & DS4_BUTTON_OPTIONS)        ? 1 : 0);
    EmitEvent(EV_KEY, BTN_THUMBL, (btn & DS4_BUTTON_THUMB_LEFT)     ? 1 : 0);
    EmitEvent(EV_KEY, BTN_THUMBR, (btn & DS4_BUTTON_THUMB_RIGHT)    ? 1 : 0);
    EmitEvent(EV_KEY, BTN_MODE,   (rep.bSpecial & DS4_SPECIAL_BUTTON_PS) ? 1 : 0);

    EmitEvent(EV_SYN, SYN_REPORT, 0);
    return true;
}

inline bool UInputController::SubmitXUSB(const XUSB_REPORT& r) {
    if (fd_ < 0) return false;

    EmitEvent(EV_ABS, ABS_X,  r.sThumbLX);
    EmitEvent(EV_ABS, ABS_Y,  -r.sThumbLY);   // flip Y: Linux up = negative
    EmitEvent(EV_ABS, ABS_RX, r.sThumbRX);
    EmitEvent(EV_ABS, ABS_RY, -r.sThumbRY);
    EmitEvent(EV_ABS, ABS_Z,  r.bLeftTrigger);
    EmitEvent(EV_ABS, ABS_RZ, r.bRightTrigger);

    USHORT btn = r.wButtons;
    EmitEvent(EV_KEY, BTN_DPAD_UP,    (btn & XUSB_GAMEPAD_DPAD_UP)        ? 1 : 0);
    EmitEvent(EV_KEY, BTN_DPAD_DOWN,  (btn & XUSB_GAMEPAD_DPAD_DOWN)      ? 1 : 0);
    EmitEvent(EV_KEY, BTN_DPAD_LEFT,  (btn & XUSB_GAMEPAD_DPAD_LEFT)      ? 1 : 0);
    EmitEvent(EV_KEY, BTN_DPAD_RIGHT, (btn & XUSB_GAMEPAD_DPAD_RIGHT)     ? 1 : 0);
    EmitEvent(EV_KEY, BTN_SOUTH,      (btn & XUSB_GAMEPAD_A)               ? 1 : 0);
    EmitEvent(EV_KEY, BTN_EAST,       (btn & XUSB_GAMEPAD_B)               ? 1 : 0);
    EmitEvent(EV_KEY, BTN_WEST,       (btn & XUSB_GAMEPAD_X)               ? 1 : 0);
    EmitEvent(EV_KEY, BTN_NORTH,      (btn & XUSB_GAMEPAD_Y)               ? 1 : 0);
    EmitEvent(EV_KEY, BTN_TL,         (btn & XUSB_GAMEPAD_LEFT_SHOULDER)   ? 1 : 0);
    EmitEvent(EV_KEY, BTN_TR,         (btn & XUSB_GAMEPAD_RIGHT_SHOULDER)  ? 1 : 0);
    EmitEvent(EV_KEY, BTN_SELECT,     (btn & XUSB_GAMEPAD_BACK)            ? 1 : 0);
    EmitEvent(EV_KEY, BTN_START,      (btn & XUSB_GAMEPAD_START)           ? 1 : 0);
    EmitEvent(EV_KEY, BTN_MODE,       (btn & XUSB_GAMEPAD_GUIDE)           ? 1 : 0);
    EmitEvent(EV_KEY, BTN_THUMBL,     (btn & XUSB_GAMEPAD_LEFT_THUMB)      ? 1 : 0);
    EmitEvent(EV_KEY, BTN_THUMBR,     (btn & XUSB_GAMEPAD_RIGHT_THUMB)     ? 1 : 0);

    EmitEvent(EV_SYN, SYN_REPORT, 0);
    return true;
}

inline void UInputController::StartFFThread() {
    // Find the uinput device's event file (/dev/input/eventN) by scanning
    // We do this in a background thread after a short delay so the device has time to appear.
    ffRunning_.store(true);
    ffThread_ = std::thread([this]() {
        // Wait for uinput to expose the event node
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Find the fd for reading FF events via the /dev/input/eventN that
        // corresponds to our uinput device (use sysfs to match)
        int evFd = -1;
        char sysfs_path[256];
        // Try up to event32
        for (int i = 0; i < 64 && ffRunning_.load(); ++i) {
            snprintf(sysfs_path, sizeof(sysfs_path), "/dev/input/event%d", i);
            int fd = open(sysfs_path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            // Check if this is our device by reading its name
            char name[256] = {};
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            bool match = (xboxMode_ && strstr(name, "JoyCon2 Xbox")) ||
                         (!xboxMode_ && strstr(name, "JoyCon2 Dual"));
            if (match) { evFd = fd; break; }
            close(fd);
        }

        if (evFd < 0) return;  // Could not find event node — FF not available

        struct input_event ev{};
        while (ffRunning_.load()) {
            ssize_t n = read(evFd, &ev, sizeof(ev));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                break;
            }
            if (n != sizeof(ev)) continue;
            if (ev.type == EV_FF && ev.code == FF_RUMBLE) {
                // ev.value: upper 8 bits = strong (large), lower 8 bits = weak (small)
                uint8_t large = static_cast<uint8_t>((ev.value >> 8) & 0xFF);
                uint8_t small = static_cast<uint8_t>(ev.value & 0xFF);
                std::lock_guard<std::mutex> lk(mtx_);
                if (vibrationCallback_) vibrationCallback_(large, small);
            }
        }
        close(evFd);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// macOS — stub implementation
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(__APPLE__)

class StubController final : public IVirtualController {
public:
    bool Initialize(bool useXbox) override {
        xboxMode_ = useXbox;
        lastError_ = "Virtual controller not supported on macOS without an additional driver "
                     "(e.g. Karabiner VirtualHIDDevice).";
        return false;
    }
    void Remove() override {}
    bool SubmitDS4(const DS4_REPORT_EX&) override { return false; }
    bool SubmitXUSB(const XUSB_REPORT&) override { return false; }
    void SetVibrationCallback(std::function<void(uint8_t, uint8_t)>) override {}
    bool IsXboxMode() const override { return xboxMode_; }
    std::string GetLastError() const override { return lastError_; }
private:
    bool        xboxMode_  = false;
    std::string lastError_;
};

#endif // platform

// ─────────────────────────────────────────────────────────────────────────────
// Factory function — returns the appropriate implementation for the host OS.
// ─────────────────────────────────────────────────────────────────────────────

inline std::unique_ptr<IVirtualController> CreateVirtualController() {
#ifdef _WIN32
    return std::make_unique<ViGEmController>();
#elif defined(__linux__)
    return std::make_unique<UInputController>();
#elif defined(__APPLE__)
    return std::make_unique<StubController>();
#else
    return nullptr;
#endif
}
