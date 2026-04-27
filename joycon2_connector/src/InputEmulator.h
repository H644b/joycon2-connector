#pragma once
// InputEmulator.h — Cross-platform mouse and keyboard input injection.
//
// Windows  : SendInput()
// Linux    : uinput virtual mouse/keyboard device
// macOS    : CoreGraphics CGEvent

#include <cstdint>
#include <string>

namespace InputEmulator {

// ── Key codes — a small portable subset used by this application ─────────────
enum class Key : uint16_t {
    F12 = 0xF12,
};

// ── Initialise (Linux: opens /dev/uinput; others: no-op) ─────────────────────
bool Init();
void Shutdown();

// ── Mouse movement (relative, pixels) ────────────────────────────────────────
void MoveMouse(int dx, int dy);

// ── Mouse buttons ─────────────────────────────────────────────────────────────
void SendMouseButton(int button, bool pressed);   // button: 0=left,1=right,2=middle
void SendMouseXButton(int xbutton, bool pressed); // xbutton: 1=X1, 2=X2

// ── Mouse wheel ───────────────────────────────────────────────────────────────
void SendMouseWheel(int delta);   // positive = up / away from user

// ── Keyboard ──────────────────────────────────────────────────────────────────
void SendKey(Key key, bool pressed);

} // namespace InputEmulator

// ─────────────────────────────────────────────────────────────────────────────
// Platform implementations
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
// ── Windows: SendInput ────────────────────────────────────────────────────────

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace InputEmulator {

inline bool Init()     { return true; }
inline void Shutdown() {}

inline void MoveMouse(int dx, int dy) {
    INPUT inp{};
    inp.type      = INPUT_MOUSE;
    inp.mi.dx     = dx;
    inp.mi.dy     = dy;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | 0x2000; // MOUSEEVENTF_VIRTUALDESK not strictly needed
    SendInput(1, &inp, sizeof(INPUT));
}

inline void SendMouseButton(int button, bool pressed) {
    INPUT inp{};
    inp.type = INPUT_MOUSE;
    if      (button == 0) inp.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN  : MOUSEEVENTF_LEFTUP;
    else if (button == 1) inp.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    else if (button == 2) inp.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN: MOUSEEVENTF_MIDDLEUP;
    SendInput(1, &inp, sizeof(INPUT));
}

inline void SendMouseXButton(int xbutton, bool pressed) {
    INPUT inp{};
    inp.type          = INPUT_MOUSE;
    inp.mi.mouseData  = (xbutton == 1) ? XBUTTON1 : XBUTTON2;
    inp.mi.dwFlags    = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    SendInput(1, &inp, sizeof(INPUT));
}

inline void SendMouseWheel(int delta) {
    INPUT inp{};
    inp.type          = INPUT_MOUSE;
    inp.mi.mouseData  = static_cast<DWORD>(delta);
    inp.mi.dwFlags    = MOUSEEVENTF_WHEEL;
    SendInput(1, &inp, sizeof(INPUT));
}

inline void SendKey(Key key, bool pressed) {
    INPUT inp{};
    inp.type    = INPUT_KEYBOARD;
    inp.ki.wVk  = static_cast<WORD>(key);
    inp.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &inp, sizeof(INPUT));
}

} // namespace InputEmulator

// ─────────────────────────────────────────────────────────────────────────────
#elif defined(__linux__)
// ── Linux: uinput virtual mouse + keyboard ────────────────────────────────────

#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <cstring>

namespace InputEmulator {

namespace _detail {
    inline int& mouseFd()    { static int fd = -1; return fd; }
    inline int& keyboardFd() { static int fd = -1; return fd; }

    inline void emit(int fd, uint16_t type, uint16_t code, int32_t value) {
        if (fd < 0) return;
        struct input_event ev{};
        ev.type  = type;
        ev.code  = code;
        ev.value = value;
        write(fd, &ev, sizeof(ev));
    }

    inline bool setupMouse() {
        int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0) return false;

        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        ioctl(fd, UI_SET_EVBIT, EV_REL);

        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
        ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
        ioctl(fd, UI_SET_KEYBIT, BTN_SIDE);   // X1
        ioctl(fd, UI_SET_KEYBIT, BTN_EXTRA);  // X2

        ioctl(fd, UI_SET_RELBIT, REL_X);
        ioctl(fd, UI_SET_RELBIT, REL_Y);
        ioctl(fd, UI_SET_RELBIT, REL_WHEEL);

        struct uinput_setup us{};
        us.id.bustype = BUS_USB;
        us.id.vendor  = 0x045e;
        us.id.product = 0x0001;
        us.id.version = 0x0100;
        strncpy(us.name, "JoyCon2 Virtual Mouse", UINPUT_MAX_NAME_SIZE - 1);

        if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
            close(fd); return false;
        }
        mouseFd() = fd;
        return true;
    }

    inline bool setupKeyboard() {
        int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd < 0) return false;

        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        ioctl(fd, UI_SET_KEYBIT, KEY_F12);

        struct uinput_setup us{};
        us.id.bustype = BUS_USB;
        us.id.vendor  = 0x045e;
        us.id.product = 0x0002;
        us.id.version = 0x0100;
        strncpy(us.name, "JoyCon2 Virtual Keyboard", UINPUT_MAX_NAME_SIZE - 1);

        if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
            close(fd); return false;
        }
        keyboardFd() = fd;
        return true;
    }
} // namespace _detail

inline bool Init() {
    bool ok = _detail::setupMouse();
    ok      = _detail::setupKeyboard() && ok;
    return ok;
}

inline void Shutdown() {
    if (_detail::mouseFd() >= 0) {
        ioctl(_detail::mouseFd(), UI_DEV_DESTROY);
        close(_detail::mouseFd());
        _detail::mouseFd() = -1;
    }
    if (_detail::keyboardFd() >= 0) {
        ioctl(_detail::keyboardFd(), UI_DEV_DESTROY);
        close(_detail::keyboardFd());
        _detail::keyboardFd() = -1;
    }
}

inline void MoveMouse(int dx, int dy) {
    int fd = _detail::mouseFd();
    _detail::emit(fd, EV_REL, REL_X, dx);
    _detail::emit(fd, EV_REL, REL_Y, dy);
    _detail::emit(fd, EV_SYN, SYN_REPORT, 0);
}

inline void SendMouseButton(int button, bool pressed) {
    int fd  = _detail::mouseFd();
    int btn = (button == 0) ? BTN_LEFT : (button == 1) ? BTN_RIGHT : BTN_MIDDLE;
    _detail::emit(fd, EV_KEY, static_cast<uint16_t>(btn), pressed ? 1 : 0);
    _detail::emit(fd, EV_SYN, SYN_REPORT, 0);
}

inline void SendMouseXButton(int xbutton, bool pressed) {
    int fd  = _detail::mouseFd();
    int btn = (xbutton == 1) ? BTN_SIDE : BTN_EXTRA;
    _detail::emit(fd, EV_KEY, static_cast<uint16_t>(btn), pressed ? 1 : 0);
    _detail::emit(fd, EV_SYN, SYN_REPORT, 0);
}

inline void SendMouseWheel(int delta) {
    int fd = _detail::mouseFd();
    _detail::emit(fd, EV_REL, REL_WHEEL, delta > 0 ? 1 : -1);
    _detail::emit(fd, EV_SYN, SYN_REPORT, 0);
}

inline void SendKey(Key key, bool pressed) {
    int fd = _detail::keyboardFd();
    uint16_t linuxKey = KEY_F12;  // only key used currently
    (void)key;
    _detail::emit(fd, EV_KEY, linuxKey, pressed ? 1 : 0);
    _detail::emit(fd, EV_SYN, SYN_REPORT, 0);
}

} // namespace InputEmulator

// ─────────────────────────────────────────────────────────────────────────────
#elif defined(__APPLE__)
// ── macOS: CoreGraphics CGEvent ───────────────────────────────────────────────

#include <ApplicationServices/ApplicationServices.h>

namespace InputEmulator {

namespace _detail {
    inline CGPoint& curPos() {
        static CGPoint p{0, 0};
        return p;
    }
}

inline bool Init()     { return true; }
inline void Shutdown() {}

inline void MoveMouse(int dx, int dy) {
    _detail::curPos().x += dx;
    _detail::curPos().y += dy;
    CGEventRef ev = CGEventCreateMouseEvent(
        nullptr, kCGEventMouseMoved, _detail::curPos(), kCGMouseButtonLeft);
    if (ev) { CGEventPost(kCGHIDEventTap, ev); CFRelease(ev); }
}

inline void SendMouseButton(int button, bool pressed) {
    CGEventType type;
    CGMouseButton mb;
    if (button == 0) { type = pressed ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;   mb = kCGMouseButtonLeft; }
    else if (button == 1) { type = pressed ? kCGEventRightMouseDown : kCGEventRightMouseUp; mb = kCGMouseButtonRight; }
    else { type = pressed ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; mb = kCGMouseButtonCenter; }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type, _detail::curPos(), mb);
    if (ev) { CGEventPost(kCGHIDEventTap, ev); CFRelease(ev); }
}

inline void SendMouseXButton(int /*xbutton*/, bool pressed) {
    CGEventType type = pressed ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, type, _detail::curPos(), kCGMouseButtonCenter);
    if (ev) { CGEventPost(kCGHIDEventTap, ev); CFRelease(ev); }
}

inline void SendMouseWheel(int delta) {
    CGEventRef ev = CGEventCreateScrollWheelEvent(
        nullptr, kCGScrollEventUnitLine, 1, delta > 0 ? 1 : -1);
    if (ev) { CGEventPost(kCGHIDEventTap, ev); CFRelease(ev); }
}

inline void SendKey(Key key, bool pressed) {
    // F12 = macOS virtual key code 111
    CGKeyCode code = 111;
    (void)key;
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, code, pressed);
    if (ev) { CGEventPost(kCGHIDEventTap, ev); CFRelease(ev); }
}

} // namespace InputEmulator

#else
// ── Unsupported platform — no-ops ─────────────────────────────────────────────

namespace InputEmulator {
inline bool Init()     { return false; }
inline void Shutdown() {}
inline void MoveMouse(int, int)           {}
inline void SendMouseButton(int, bool)    {}
inline void SendMouseXButton(int, bool)   {}
inline void SendMouseWheel(int)           {}
inline void SendKey(Key, bool)            {}
} // namespace InputEmulator

#endif // platform
