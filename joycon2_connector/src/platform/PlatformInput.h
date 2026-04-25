#pragma once
// PlatformInput.h - Cross-platform mouse/keyboard injection
#include <cstdint>

namespace PlatformInput {

void MoveMouse(int dx, int dy);
void MouseButtonDown(int button); // 0=left 1=right 2=middle 3=xbutton1 4=xbutton2
void MouseButtonUp(int button);
void MouseScroll(int clicks);     // positive=up negative=down
void KeyDown(int virtualKey);     // use Windows VK_ codes everywhere; mapped on other platforms
void KeyUp(int virtualKey);

void SetHighPriority();
void SetAboveNormalPriority();
void SetTimeCriticalPriority();

} // namespace PlatformInput

// ─────────────────────────────────────────────────────────────────
// Implementations
// ─────────────────────────────────────────────────────────────────

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>

namespace PlatformInput {

inline void MoveMouse(int dx, int dy) {
    INPUT input = {};
    input.type    = INPUT_MOUSE;
    input.mi.dx   = dx;
    input.mi.dy   = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | 0x2000; // 0x2000 = MOUSEEVENTF_VIRTUALDESK
    SendInput(1, &input, sizeof(INPUT));
}

inline void MouseButtonDown(int button) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (button) {
    case 0: input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;   break;
    case 1: input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;  break;
    case 2: input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; break;
    case 3: input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON1; break;
    case 4: input.mi.dwFlags = MOUSEEVENTF_XDOWN; input.mi.mouseData = XBUTTON2; break;
    default: return;
    }
    SendInput(1, &input, sizeof(INPUT));
}

inline void MouseButtonUp(int button) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (button) {
    case 0: input.mi.dwFlags = MOUSEEVENTF_LEFTUP;   break;
    case 1: input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;  break;
    case 2: input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; break;
    case 3: input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON1; break;
    case 4: input.mi.dwFlags = MOUSEEVENTF_XUP; input.mi.mouseData = XBUTTON2; break;
    default: return;
    }
    SendInput(1, &input, sizeof(INPUT));
}

inline void MouseScroll(int clicks) {
    INPUT input = {};
    input.type          = INPUT_MOUSE;
    input.mi.mouseData  = static_cast<DWORD>(clicks * 120);
    input.mi.dwFlags    = MOUSEEVENTF_WHEEL;
    SendInput(1, &input, sizeof(INPUT));
}

inline void KeyDown(int virtualKey) {
    INPUT input = {};
    input.type      = INPUT_KEYBOARD;
    input.ki.wVk    = static_cast<WORD>(virtualKey);
    input.ki.dwFlags= 0;
    SendInput(1, &input, sizeof(INPUT));
}

inline void KeyUp(int virtualKey) {
    INPUT input = {};
    input.type      = INPUT_KEYBOARD;
    input.ki.wVk    = static_cast<WORD>(virtualKey);
    input.ki.dwFlags= KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

inline void SetHighPriority()        { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST); }
inline void SetAboveNormalPriority() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); }
inline void SetTimeCriticalPriority(){ SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL); }

} // namespace PlatformInput

// ─────────────────────────────────────────────────────────────────
#elif defined(__linux__)
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <pthread.h>

namespace PlatformInput {
namespace detail {

// Map a Windows VK_ code to a Linux KEY_ code
inline int VKtoLinuxKey(int vk) {
    switch (vk) {
    case 0x7B: return KEY_F12;
    case 0x70: return KEY_F1;  case 0x71: return KEY_F2;
    case 0x72: return KEY_F3;  case 0x73: return KEY_F4;
    case 0x74: return KEY_F5;  case 0x75: return KEY_F6;
    case 0x76: return KEY_F7;  case 0x77: return KEY_F8;
    case 0x78: return KEY_F9;  case 0x79: return KEY_F10;
    case 0x7A: return KEY_F11;
    default:   return KEY_RESERVED;
    }
}

inline int& MouseFd() {
    static int fd = -1;
    return fd;
}

inline int& KbdFd() {
    static int fd = -1;
    return fd;
}

inline void EnsureMouseDevice() {
    if (MouseFd() >= 0) return;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return;

    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);
    ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
    for (int k : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA})
        ioctl(fd, UI_SET_KEYBIT, k);

    struct uinput_user_dev uidev = {};
    strncpy(uidev.name, "JoyCon2 Virtual Mouse", UINPUT_MAX_NAME_SIZE);
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;
    write(fd, &uidev, sizeof(uidev));
    if (ioctl(fd, UI_DEV_CREATE) < 0) { close(fd); return; }
    MouseFd() = fd;
}

inline void EnsureKbdDevice() {
    if (KbdFd() >= 0) return;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return;

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int k = KEY_F1; k <= KEY_F12; ++k) ioctl(fd, UI_SET_KEYBIT, k);

    struct uinput_user_dev uidev = {};
    strncpy(uidev.name, "JoyCon2 Virtual Keyboard", UINPUT_MAX_NAME_SIZE);
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5679;
    uidev.id.version = 1;
    write(fd, &uidev, sizeof(uidev));
    if (ioctl(fd, UI_DEV_CREATE) < 0) { close(fd); return; }
    KbdFd() = fd;
}

inline void EmitEvent(int fd, int type, int code, int value) {
    struct input_event ev = {};
    ev.type  = static_cast<uint16_t>(type);
    ev.code  = static_cast<uint16_t>(code);
    ev.value = value;
    write(fd, &ev, sizeof(ev));
}

inline void SyncDev(int fd) { EmitEvent(fd, EV_SYN, SYN_REPORT, 0); }

} // namespace detail

inline void MoveMouse(int dx, int dy) {
    detail::EnsureMouseDevice();
    int fd = detail::MouseFd();
    if (fd < 0) return;
    detail::EmitEvent(fd, EV_REL, REL_X, dx);
    detail::EmitEvent(fd, EV_REL, REL_Y, dy);
    detail::SyncDev(fd);
}

inline void MouseButtonDown(int button) {
    detail::EnsureMouseDevice();
    int fd = detail::MouseFd();
    if (fd < 0) return;
    int code;
    switch (button) {
    case 0: code = BTN_LEFT;   break;
    case 1: code = BTN_RIGHT;  break;
    case 2: code = BTN_MIDDLE; break;
    case 3: code = BTN_SIDE;   break;
    case 4: code = BTN_EXTRA;  break;
    default: return;
    }
    detail::EmitEvent(fd, EV_KEY, code, 1);
    detail::SyncDev(fd);
}

inline void MouseButtonUp(int button) {
    detail::EnsureMouseDevice();
    int fd = detail::MouseFd();
    if (fd < 0) return;
    int code;
    switch (button) {
    case 0: code = BTN_LEFT;   break;
    case 1: code = BTN_RIGHT;  break;
    case 2: code = BTN_MIDDLE; break;
    case 3: code = BTN_SIDE;   break;
    case 4: code = BTN_EXTRA;  break;
    default: return;
    }
    detail::EmitEvent(fd, EV_KEY, code, 0);
    detail::SyncDev(fd);
}

inline void MouseScroll(int clicks) {
    detail::EnsureMouseDevice();
    int fd = detail::MouseFd();
    if (fd < 0) return;
    detail::EmitEvent(fd, EV_REL, REL_WHEEL, clicks);
    detail::SyncDev(fd);
}

inline void KeyDown(int vk) {
    detail::EnsureKbdDevice();
    int fd = detail::KbdFd();
    if (fd < 0) return;
    int code = detail::VKtoLinuxKey(vk);
    if (!code) return;
    detail::EmitEvent(fd, EV_KEY, code, 1);
    detail::SyncDev(fd);
}

inline void KeyUp(int vk) {
    detail::EnsureKbdDevice();
    int fd = detail::KbdFd();
    if (fd < 0) return;
    int code = detail::VKtoLinuxKey(vk);
    if (!code) return;
    detail::EmitEvent(fd, EV_KEY, code, 0);
    detail::SyncDev(fd);
}

inline void SetHighPriority()        { struct sched_param sp{10}; pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp); }
inline void SetAboveNormalPriority() { struct sched_param sp{5};  pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp); }
inline void SetTimeCriticalPriority(){ struct sched_param sp{15}; pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp); }

} // namespace PlatformInput

// ─────────────────────────────────────────────────────────────────
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <pthread.h>

namespace PlatformInput {

inline void MoveMouse(int dx, int dy) {
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, {0,0}, kCGMouseButtonLeft);
    CGEventSetIntegerValueField(ev, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(ev, kCGMouseEventDeltaY, dy);
    CGEventPost(kCGSessionEventTap, ev);
    CFRelease(ev);
}

inline void MouseButtonDown(int button) {
    CGEventType t; CGMouseButton b;
    switch (button) {
    case 0: t = kCGEventLeftMouseDown;  b = kCGMouseButtonLeft;  break;
    case 1: t = kCGEventRightMouseDown; b = kCGMouseButtonRight; break;
    default: t = kCGEventOtherMouseDown; b = static_cast<CGMouseButton>(button); break;
    }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, t, {0,0}, b);
    CGEventPost(kCGSessionEventTap, ev);
    CFRelease(ev);
}

inline void MouseButtonUp(int button) {
    CGEventType t; CGMouseButton b;
    switch (button) {
    case 0: t = kCGEventLeftMouseUp;  b = kCGMouseButtonLeft;  break;
    case 1: t = kCGEventRightMouseUp; b = kCGMouseButtonRight; break;
    default: t = kCGEventOtherMouseUp; b = static_cast<CGMouseButton>(button); break;
    }
    CGEventRef ev = CGEventCreateMouseEvent(nullptr, t, {0,0}, b);
    CGEventPost(kCGSessionEventTap, ev);
    CFRelease(ev);
}

inline void MouseScroll(int clicks) {
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, clicks);
    CGEventPost(kCGSessionEventTap, ev);
    CFRelease(ev);
}

// VK_ to CGKeyCode mappings (subset)
inline CGKeyCode VKtoCGKey(int vk) {
    switch (vk) {
    case 0x70: return 122; // F1
    case 0x71: return 120; // F2
    case 0x72: return 99;  // F3
    case 0x73: return 118; // F4
    case 0x74: return 96;  // F5
    case 0x75: return 97;  // F6
    case 0x76: return 98;  // F7
    case 0x77: return 100; // F8
    case 0x78: return 101; // F9
    case 0x79: return 109; // F10
    case 0x7A: return 103; // F11
    case 0x7B: return 111; // F12
    default:   return 0xFFFF;
    }
}

inline void KeyDown(int vk) {
    CGKeyCode code = VKtoCGKey(vk);
    if (code == 0xFFFF) return;
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, code, true);
    CGEventPost(kCGSessionEventTap, ev);
    CFRelease(ev);
}

inline void KeyUp(int vk) {
    CGKeyCode code = VKtoCGKey(vk);
    if (code == 0xFFFF) return;
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, code, false);
    CGEventPost(kCGSessionEventTap, ev);
    CFRelease(ev);
}

inline void SetHighPriority()        {}
inline void SetAboveNormalPriority() {}
inline void SetTimeCriticalPriority(){}

} // namespace PlatformInput
#endif
