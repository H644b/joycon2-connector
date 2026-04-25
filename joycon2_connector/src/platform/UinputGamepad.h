#pragma once
// UinputGamepad.h - Linux uinput virtual gamepad backend
#ifdef __linux__
#include "../VirtualGamepad.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <map>
#include <mutex>
#include <memory>

struct UinputTarget {
    int  fd = -1;
    bool isDS4 = false;
    VibrationCallback vibCb;
};

class UinputGamepad : public VirtualGamepad {
public:
    static UinputGamepad& Get() {
        static UinputGamepad g;
        return g;
    }

    bool Init() override { return true; }
    void Shutdown() override {}
    bool IsAvailable() const override { return true; }

    void* AllocDS4() override {
        auto* t = new UinputTarget();
        t->isDS4 = true;
        t->fd = CreateUinputDevice(true);
        return t;
    }

    void* AllocX360() override {
        auto* t = new UinputTarget();
        t->isDS4 = false;
        t->fd = CreateUinputDevice(false);
        return t;
    }

    bool AddTarget(void* target) override {
        if (!target) return false;
        auto* t = static_cast<UinputTarget*>(target);
        return t->fd >= 0;
    }

    void RemoveTarget(void* target) override {
        if (!target) return;
        auto* t = static_cast<UinputTarget*>(target);
        if (t->fd >= 0) {
            ioctl(t->fd, UI_DEV_DESTROY);
            close(t->fd);
            t->fd = -1;
        }
        delete t;
    }

    void UpdateDS4(void* target, const DS4_REPORT_EX& report) override {
        if (!target) return;
        auto* t = static_cast<UinputTarget*>(target);
        if (t->fd < 0) return;
        const auto& r = report.Report;

        // Map DS4 buttons to XUSB-like layout for uinput
        uint16_t btn = r.wButtons;
        EmitKey(t->fd, BTN_EAST,       (btn & DS4_BUTTON_CROSS) != 0);
        EmitKey(t->fd, BTN_SOUTH,      (btn & DS4_BUTTON_CIRCLE) != 0);
        EmitKey(t->fd, BTN_NORTH,      (btn & DS4_BUTTON_SQUARE) != 0);
        EmitKey(t->fd, BTN_WEST,       (btn & DS4_BUTTON_TRIANGLE) != 0);
        EmitKey(t->fd, BTN_TL,         (btn & DS4_BUTTON_SHOULDER_LEFT) != 0);
        EmitKey(t->fd, BTN_TR,         (btn & DS4_BUTTON_SHOULDER_RIGHT) != 0);
        EmitKey(t->fd, BTN_TL2,        (btn & DS4_BUTTON_TRIGGER_LEFT) != 0);
        EmitKey(t->fd, BTN_TR2,        (btn & DS4_BUTTON_TRIGGER_RIGHT) != 0);
        EmitKey(t->fd, BTN_SELECT,     (btn & DS4_BUTTON_SHARE) != 0);
        EmitKey(t->fd, BTN_START,      (btn & DS4_BUTTON_OPTIONS) != 0);
        EmitKey(t->fd, BTN_THUMBL,     (btn & DS4_BUTTON_THUMB_LEFT) != 0);
        EmitKey(t->fd, BTN_THUMBR,     (btn & DS4_BUTTON_THUMB_RIGHT) != 0);

        uint8_t dpad = btn & 0x0F;
        EmitAxis(t->fd, ABS_HAT0X, DpadX(dpad));
        EmitAxis(t->fd, ABS_HAT0Y, DpadY(dpad));

        EmitAxis(t->fd, ABS_X,  ScaleStick(r.bThumbLX));
        EmitAxis(t->fd, ABS_Y,  ScaleStick(r.bThumbLY));
        EmitAxis(t->fd, ABS_RX, ScaleStick(r.bThumbRX));
        EmitAxis(t->fd, ABS_RY, ScaleStick(r.bThumbRY));
        EmitAxis(t->fd, ABS_Z,  r.bTriggerL);
        EmitAxis(t->fd, ABS_RZ, r.bTriggerR);

        Sync(t->fd);
    }

    void UpdateX360(void* target, const XUSB_REPORT& report) override {
        if (!target) return;
        auto* t = static_cast<UinputTarget*>(target);
        if (t->fd < 0) return;

        uint16_t btn = report.wButtons;
        EmitKey(t->fd, BTN_EAST,       (btn & XUSB_GAMEPAD_A) != 0);
        EmitKey(t->fd, BTN_SOUTH,      (btn & XUSB_GAMEPAD_B) != 0);
        EmitKey(t->fd, BTN_NORTH,      (btn & XUSB_GAMEPAD_X) != 0);
        EmitKey(t->fd, BTN_WEST,       (btn & XUSB_GAMEPAD_Y) != 0);
        EmitKey(t->fd, BTN_TL,         (btn & XUSB_GAMEPAD_LEFT_SHOULDER) != 0);
        EmitKey(t->fd, BTN_TR,         (btn & XUSB_GAMEPAD_RIGHT_SHOULDER) != 0);
        EmitKey(t->fd, BTN_SELECT,     (btn & XUSB_GAMEPAD_BACK) != 0);
        EmitKey(t->fd, BTN_START,      (btn & XUSB_GAMEPAD_START) != 0);
        EmitKey(t->fd, BTN_THUMBL,     (btn & XUSB_GAMEPAD_LEFT_THUMB) != 0);
        EmitKey(t->fd, BTN_THUMBR,     (btn & XUSB_GAMEPAD_RIGHT_THUMB) != 0);

        int hx = 0, hy = 0;
        if (btn & XUSB_GAMEPAD_DPAD_LEFT)  hx = -1;
        if (btn & XUSB_GAMEPAD_DPAD_RIGHT) hx =  1;
        if (btn & XUSB_GAMEPAD_DPAD_UP)    hy = -1;
        if (btn & XUSB_GAMEPAD_DPAD_DOWN)  hy =  1;
        EmitAxis(t->fd, ABS_HAT0X, hx);
        EmitAxis(t->fd, ABS_HAT0Y, hy);

        EmitAxis(t->fd, ABS_X,  report.sThumbLX >> 7);
        EmitAxis(t->fd, ABS_Y,  -(report.sThumbLY >> 7));
        EmitAxis(t->fd, ABS_RX, report.sThumbRX >> 7);
        EmitAxis(t->fd, ABS_RY, -(report.sThumbRY >> 7));
        EmitAxis(t->fd, ABS_Z,  report.bLeftTrigger);
        EmitAxis(t->fd, ABS_RZ, report.bRightTrigger);

        Sync(t->fd);
    }

    void RegisterDS4VibrationCallback(void* target, VibrationCallback cb) override {
        if (!target) return;
        static_cast<UinputTarget*>(target)->vibCb = std::move(cb);
    }

    void RegisterX360VibrationCallback(void* target, VibrationCallback cb) override {
        if (!target) return;
        static_cast<UinputTarget*>(target)->vibCb = std::move(cb);
    }

    void UnregisterVibrationCallback(void* target) override {
        if (!target) return;
        static_cast<UinputTarget*>(target)->vibCb = nullptr;
    }

private:
    UinputGamepad() = default;

    static int CreateUinputDevice(bool /*isDS4*/) {
        int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
            if (fd < 0) return -1;
        }

        // Enable event types
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        ioctl(fd, UI_SET_EVBIT, EV_SYN);

        // Buttons
        for (int k : {BTN_EAST, BTN_SOUTH, BTN_NORTH, BTN_WEST,
                       BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
                       BTN_SELECT, BTN_START, BTN_MODE,
                       BTN_THUMBL, BTN_THUMBR}) {
            ioctl(fd, UI_SET_KEYBIT, k);
        }

        // Axes
        ioctl(fd, UI_SET_ABSBIT, ABS_X);
        ioctl(fd, UI_SET_ABSBIT, ABS_Y);
        ioctl(fd, UI_SET_ABSBIT, ABS_RX);
        ioctl(fd, UI_SET_ABSBIT, ABS_RY);
        ioctl(fd, UI_SET_ABSBIT, ABS_Z);
        ioctl(fd, UI_SET_ABSBIT, ABS_RZ);
        ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X);
        ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);

        struct uinput_user_dev uidev = {};
        strncpy(uidev.name, "JoyCon2 Virtual Gamepad", UINPUT_MAX_NAME_SIZE);
        uidev.id.bustype = BUS_USB;
        uidev.id.vendor  = 0x057E;
        uidev.id.product = 0x2009;
        uidev.id.version = 1;

        auto setAxis = [&](int ax, int min, int max, int flat, int fuzz) {
            uidev.absmin[ax] = min; uidev.absmax[ax] = max;
            uidev.absflat[ax] = flat; uidev.absfuzz[ax] = fuzz;
        };
        setAxis(ABS_X,  -128, 127, 16, 4);
        setAxis(ABS_Y,  -128, 127, 16, 4);
        setAxis(ABS_RX, -128, 127, 16, 4);
        setAxis(ABS_RY, -128, 127, 16, 4);
        setAxis(ABS_Z,    0, 255,  0, 0);
        setAxis(ABS_RZ,   0, 255,  0, 0);
        setAxis(ABS_HAT0X, -1, 1, 0, 0);
        setAxis(ABS_HAT0Y, -1, 1, 0, 0);

        if (write(fd, &uidev, sizeof(uidev)) < 0) { close(fd); return -1; }
        if (ioctl(fd, UI_DEV_CREATE) < 0) { close(fd); return -1; }
        return fd;
    }

    static void EmitKey(int fd, int code, bool pressed) {
        struct input_event ev = {};
        ev.type  = EV_KEY;
        ev.code  = static_cast<uint16_t>(code);
        ev.value = pressed ? 1 : 0;
        write(fd, &ev, sizeof(ev));
    }

    static void EmitAxis(int fd, int code, int value) {
        struct input_event ev = {};
        ev.type  = EV_ABS;
        ev.code  = static_cast<uint16_t>(code);
        ev.value = value;
        write(fd, &ev, sizeof(ev));
    }

    static void Sync(int fd) {
        struct input_event ev = {};
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        write(fd, &ev, sizeof(ev));
    }

    // Map DS4 dpad nibble to X axis value (-1, 0, 1)
    static int DpadX(uint8_t dpad) {
        switch (dpad) {
        case DS4_BUTTON_DPAD_EAST: case DS4_BUTTON_DPAD_NORTHEAST: case DS4_BUTTON_DPAD_SOUTHEAST: return 1;
        case DS4_BUTTON_DPAD_WEST: case DS4_BUTTON_DPAD_NORTHWEST: case DS4_BUTTON_DPAD_SOUTHWEST: return -1;
        default: return 0;
        }
    }

    static int DpadY(uint8_t dpad) {
        switch (dpad) {
        case DS4_BUTTON_DPAD_NORTH: case DS4_BUTTON_DPAD_NORTHEAST: case DS4_BUTTON_DPAD_NORTHWEST: return -1;
        case DS4_BUTTON_DPAD_SOUTH: case DS4_BUTTON_DPAD_SOUTHEAST: case DS4_BUTTON_DPAD_SOUTHWEST: return 1;
        default: return 0;
        }
    }

    // DS4 thumb byte (0-255, center=128) -> signed byte (-128..127)
    static int ScaleStick(uint8_t raw) {
        return static_cast<int>(raw) - 128;
    }
};

#endif // __linux__
