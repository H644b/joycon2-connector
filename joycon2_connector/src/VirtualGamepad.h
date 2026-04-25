#pragma once
// VirtualGamepad.h - Cross-platform virtual gamepad interface
#include <cstdint>
#include <functional>

#ifdef _WIN32
#include <ViGEm/Client.h>
// DS4_REPORT_EX, XUSB_REPORT, DS4_BUTTON_*, XUSB_GAMEPAD_* already defined by ViGEm headers
#else
// Define equivalent structs so JoyConDecoder.h can use them unchanged on all platforms

#pragma pack(push, 1)
struct DS4_REPORT {
    uint8_t  bThumbLX;
    uint8_t  bThumbLY;
    uint8_t  bThumbRX;
    uint8_t  bThumbRY;
    uint16_t wButtons;
    uint8_t  bSpecial;
    uint8_t  bTriggerL;
    uint8_t  bTriggerR;
};

struct DS4_REPORT_EX {
    union {
        DS4_REPORT Report;
        uint8_t    Reserved[63];
    };
};

struct XUSB_REPORT {
    uint16_t wButtons;
    uint8_t  bLeftTrigger;
    uint8_t  bRightTrigger;
    int16_t  sThumbLX;
    int16_t  sThumbLY;
    int16_t  sThumbRX;
    int16_t  sThumbRY;
};
#pragma pack(pop)

// DS4 button bitmask constants (identical values to ViGEm)
#define DS4_BUTTON_THUMB_RIGHT        0x0080U
#define DS4_BUTTON_THUMB_LEFT         0x0040U
#define DS4_BUTTON_OPTIONS            0x0020U
#define DS4_BUTTON_SHARE              0x0010U
#define DS4_BUTTON_TRIGGER_RIGHT      0x0008U
#define DS4_BUTTON_TRIGGER_LEFT       0x0004U
#define DS4_BUTTON_SHOULDER_RIGHT     0x0002U
#define DS4_BUTTON_SHOULDER_LEFT      0x0001U
#define DS4_BUTTON_TRIANGLE           0x0800U
#define DS4_BUTTON_CIRCLE             0x0400U
#define DS4_BUTTON_CROSS              0x0200U
#define DS4_BUTTON_SQUARE             0x0100U
#define DS4_BUTTON_DPAD_WEST          0x06U
#define DS4_BUTTON_DPAD_NORTHWEST     0x07U
#define DS4_BUTTON_DPAD_NORTH         0x00U
#define DS4_BUTTON_DPAD_NORTHEAST     0x01U
#define DS4_BUTTON_DPAD_EAST          0x02U
#define DS4_BUTTON_DPAD_SOUTHEAST     0x03U
#define DS4_BUTTON_DPAD_SOUTH         0x04U
#define DS4_BUTTON_DPAD_SOUTHWEST     0x05U
#define DS4_BUTTON_DPAD_NONE          0x08U

// XUSB (Xbox 360) button bitmask constants
#define XUSB_GAMEPAD_DPAD_UP          0x0001U
#define XUSB_GAMEPAD_DPAD_DOWN        0x0002U
#define XUSB_GAMEPAD_DPAD_LEFT        0x0004U
#define XUSB_GAMEPAD_DPAD_RIGHT       0x0008U
#define XUSB_GAMEPAD_START            0x0010U
#define XUSB_GAMEPAD_BACK             0x0020U
#define XUSB_GAMEPAD_LEFT_THUMB       0x0040U
#define XUSB_GAMEPAD_RIGHT_THUMB      0x0080U
#define XUSB_GAMEPAD_LEFT_SHOULDER    0x0100U
#define XUSB_GAMEPAD_RIGHT_SHOULDER   0x0200U
#define XUSB_GAMEPAD_A                0x1000U
#define XUSB_GAMEPAD_B                0x2000U
#define XUSB_GAMEPAD_X                0x4000U
#define XUSB_GAMEPAD_Y                0x8000U

// Windows primitive types used in JoyConDecoder
using BYTE   = uint8_t;
using USHORT = uint16_t;
using SHORT  = int16_t;

#endif // _WIN32

using VibrationCallback = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

// Abstract cross-platform virtual gamepad interface
class VirtualGamepad {
public:
    virtual ~VirtualGamepad() = default;
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsAvailable() const = 0;

    // Allocate a new virtual DS4 or Xbox 360 target; returns opaque handle (never nullptr on success)
    virtual void* AllocDS4() = 0;
    virtual void* AllocX360() = 0;

    virtual bool AddTarget(void* target) = 0;
    virtual void RemoveTarget(void* target) = 0;

    virtual void UpdateDS4(void* target, const DS4_REPORT_EX& report) = 0;
    virtual void UpdateX360(void* target, const XUSB_REPORT& report) = 0;

    virtual void RegisterDS4VibrationCallback(void* target, VibrationCallback cb) = 0;
    virtual void RegisterX360VibrationCallback(void* target, VibrationCallback cb) = 0;
    virtual void UnregisterVibrationCallback(void* target) = 0;

    // Returns the platform-appropriate singleton
    static VirtualGamepad& Instance();
};
