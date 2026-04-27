#pragma once
// GamepadReport.h — Cross-platform DS4 and XUSB (Xbox 360) report types.
// On Windows with ViGEm available, we delegate to the real ViGEm headers.
// On Linux/macOS we define compatible types ourselves.

#ifdef _WIN32
#  include <ViGEm/Client.h>
#  include <ViGEm/Common.h>
#else
// ─── POSIX / macOS compatible definitions ────────────────────────────────────
#include <cstdint>
#include <cstring>

// Basic integer typedefs that mirror Windows BYTE / USHORT / SHORT / ULONG / UCHAR
using BYTE   = uint8_t;
using UCHAR  = uint8_t;
using USHORT = uint16_t;
using SHORT  = int16_t;
using ULONG  = uint32_t;

// ── DS4 DPAD direction values (low nibble of wButtons) ──────────────────────
typedef enum _DS4_DPAD_DIRECTIONS {
    DS4_BUTTON_DPAD_NORTH     = 0x0,
    DS4_BUTTON_DPAD_NORTHEAST = 0x1,
    DS4_BUTTON_DPAD_EAST      = 0x2,
    DS4_BUTTON_DPAD_SOUTHEAST = 0x3,
    DS4_BUTTON_DPAD_SOUTH     = 0x4,
    DS4_BUTTON_DPAD_SOUTHWEST = 0x5,
    DS4_BUTTON_DPAD_WEST      = 0x6,
    DS4_BUTTON_DPAD_NORTHWEST = 0x7,
    DS4_BUTTON_DPAD_NONE      = 0x8,
} DS4_DPAD_DIRECTIONS;

// ── DS4 button bitmasks (bits 4-15 of wButtons) ─────────────────────────────
constexpr USHORT DS4_BUTTON_SQUARE         = 0x0010;
constexpr USHORT DS4_BUTTON_CROSS          = 0x0020;
constexpr USHORT DS4_BUTTON_CIRCLE         = 0x0040;
constexpr USHORT DS4_BUTTON_TRIANGLE       = 0x0080;
constexpr USHORT DS4_BUTTON_SHOULDER_LEFT  = 0x0100;
constexpr USHORT DS4_BUTTON_SHOULDER_RIGHT = 0x0200;
constexpr USHORT DS4_BUTTON_TRIGGER_LEFT   = 0x0400;
constexpr USHORT DS4_BUTTON_TRIGGER_RIGHT  = 0x0800;
constexpr USHORT DS4_BUTTON_SHARE          = 0x1000;
constexpr USHORT DS4_BUTTON_OPTIONS        = 0x2000;
constexpr USHORT DS4_BUTTON_THUMB_LEFT     = 0x4000;
constexpr USHORT DS4_BUTTON_THUMB_RIGHT    = 0x8000;

// ── DS4 special button bitmasks (bSpecial) ──────────────────────────────────
constexpr BYTE DS4_SPECIAL_BUTTON_PS       = 0x01;
constexpr BYTE DS4_SPECIAL_BUTTON_TOUCHPAD = 0x02;

// ── DS4 touch packet ────────────────────────────────────────────────────────
typedef struct _DS4_TOUCH {
    BYTE bPacketCounter;
    BYTE bIsUpTrackingNum1;
    BYTE bTouchData1[3];
    BYTE bIsUpTrackingNum2;
    BYTE bTouchData2[3];
    BYTE bPad[3];
} DS4_TOUCH, *PDS4_TOUCH;

// ── DS4 report ───────────────────────────────────────────────────────────────
typedef struct _DS4_REPORT {
    BYTE   bThumbLX;
    BYTE   bThumbLY;
    BYTE   bThumbRX;
    BYTE   bThumbRY;
    USHORT wButtons;
    BYTE   bSpecial;
    BYTE   bTriggerL;
    BYTE   bTriggerR;
    SHORT  wTimestamp;
    BYTE   bBatteryLvl;
    SHORT  wGyroX;
    SHORT  wGyroY;
    SHORT  wGyroZ;
    SHORT  wAccelX;
    SHORT  wAccelY;
    SHORT  wAccelZ;
    BYTE   _bUnknown1[5];
    BYTE   bBatteryLvlSpecial;
    BYTE   _bUnknown2[2];
    BYTE   bTouchPacketsN;
    DS4_TOUCH sCurrentTouch;
    DS4_TOUCH sPreviousTouch[2];
} DS4_REPORT, *PDS4_REPORT;

// ── DS4 extended report (union with raw bytes) ──────────────────────────────
typedef struct _DS4_REPORT_EX {
    union {
        DS4_REPORT Report;
        UCHAR      Report_raw[sizeof(DS4_REPORT)];
    };
} DS4_REPORT_EX, *PDS4_REPORT_EX;

// ── DS4 lightbar color ───────────────────────────────────────────────────────
typedef struct _DS4_LIGHTBAR_COLOR {
    BYTE Red;
    BYTE Green;
    BYTE Blue;
} DS4_LIGHTBAR_COLOR, *PDS4_LIGHTBAR_COLOR;

// ── XUSB (Xbox 360) button bitmasks ─────────────────────────────────────────
constexpr USHORT XUSB_GAMEPAD_DPAD_UP        = 0x0001;
constexpr USHORT XUSB_GAMEPAD_DPAD_DOWN      = 0x0002;
constexpr USHORT XUSB_GAMEPAD_DPAD_LEFT      = 0x0004;
constexpr USHORT XUSB_GAMEPAD_DPAD_RIGHT     = 0x0008;
constexpr USHORT XUSB_GAMEPAD_START          = 0x0010;
constexpr USHORT XUSB_GAMEPAD_BACK           = 0x0020;
constexpr USHORT XUSB_GAMEPAD_LEFT_THUMB     = 0x0040;
constexpr USHORT XUSB_GAMEPAD_RIGHT_THUMB    = 0x0080;
constexpr USHORT XUSB_GAMEPAD_LEFT_SHOULDER  = 0x0100;
constexpr USHORT XUSB_GAMEPAD_RIGHT_SHOULDER = 0x0200;
constexpr USHORT XUSB_GAMEPAD_GUIDE          = 0x0400;
constexpr USHORT XUSB_GAMEPAD_A              = 0x1000;
constexpr USHORT XUSB_GAMEPAD_B              = 0x2000;
constexpr USHORT XUSB_GAMEPAD_X              = 0x4000;
constexpr USHORT XUSB_GAMEPAD_Y              = 0x8000;

// ── XUSB report ─────────────────────────────────────────────────────────────
typedef struct _XUSB_REPORT {
    USHORT wButtons;
    BYTE   bLeftTrigger;
    BYTE   bRightTrigger;
    SHORT  sThumbLX;
    SHORT  sThumbLY;
    SHORT  sThumbRX;
    SHORT  sThumbRY;
} XUSB_REPORT, *PXUSB_REPORT;

// ── Initialization macros ────────────────────────────────────────────────────
inline void DS4_REPORT_INIT_FN(PDS4_REPORT r) {
    std::memset(r, 0, sizeof(DS4_REPORT));
    r->bThumbLX = 0x80;
    r->bThumbLY = 0x80;
    r->bThumbRX = 0x80;
    r->bThumbRY = 0x80;
    r->wButtons = (r->wButtons & ~0xF) | static_cast<USHORT>(DS4_BUTTON_DPAD_NONE);
}
#define DS4_REPORT_INIT(_Report_) DS4_REPORT_INIT_FN(_Report_)

#define XUSB_REPORT_INIT(_Report_) std::memset((_Report_), 0, sizeof(XUSB_REPORT))

#define DS4_SET_DPAD(_Report_, _Direction_) \
    (_Report_)->wButtons = ((_Report_)->wButtons & ~0xF) | static_cast<USHORT>(_Direction_)

#endif // _WIN32
