#ifndef GAMEPADBUTTONS_H
#define GAMEPADBUTTONS_H

#include <QtGlobal>

/**
 * @brief The button vocabulary every gamepad backend speaks.
 *
 * The names are what the settings hold ("rt,lt"), what the capture screen shows and what
 * PadDiagram draws, so they have to mean the same thing whether the press arrived through
 * XInput, through a raw HID report or through an Android KeyEvent. The masks are XInput's
 * own button word simply because that was the first backend; the other two translate into
 * it rather than inventing a second numbering.
 *
 * Lives in a header, not in gamepadcontroller.cpp, since gamepadhid and gamepadandroid
 * both have to build the same word.
 */
namespace padbuttons {

// The XInput button word, as documented for XINPUT_GAMEPAD. Declared here rather than pulled
// from xinput.h so the file needs neither the header nor the import library on any toolchain.
constexpr quint32 DPAD_UP = 0x0001;
constexpr quint32 DPAD_DOWN = 0x0002;
constexpr quint32 DPAD_LEFT = 0x0004;
constexpr quint32 DPAD_RIGHT = 0x0008;
constexpr quint32 START = 0x0010;
constexpr quint32 BACK = 0x0020;
constexpr quint32 LEFT_THUMB = 0x0040;
constexpr quint32 RIGHT_THUMB = 0x0080;
constexpr quint32 LB = 0x0100;
constexpr quint32 RB = 0x0200;
constexpr quint32 A = 0x1000;
constexpr quint32 B = 0x2000;
constexpr quint32 X = 0x4000;
constexpr quint32 Y = 0x8000;

// The analog triggers are not in the button word at all - they are separate 0-255 axes. They are
// folded in as two invented masks, deliberately above 16 bits so they can never collide with a real
// button, and everything downstream then treats them as ordinary buttons. Same convention as
// tools/xbox-mywhoosh-gears.
constexpr quint32 LT = 0x10000;
constexpr quint32 RT = 0x20000;

struct named {
    const char *name;
    quint32 mask;
};

/**
 * @brief Every button, in the stable order the settings UI shows them.
 *
 * The first FACE_COUNT entries are the ones a HID pad's numbered buttons land on, in report
 * order, so the d-pad has to stay last: on a HID pad it arrives as a hat value, not as
 * buttons 13 to 16.
 */
constexpr named TABLE[] = {
    {"a", A},
    {"b", B},
    {"x", X},
    {"y", Y},
    {"lb", LB},
    {"rb", RB},
    {"lt", LT},
    {"rt", RT},
    {"start", START},
    {"back", BACK},
    {"l3", LEFT_THUMB},
    {"r3", RIGHT_THUMB},
    {"dpad_up", DPAD_UP},
    {"dpad_down", DPAD_DOWN},
    {"dpad_left", DPAD_LEFT},
    {"dpad_right", DPAD_RIGHT},
};

constexpr int COUNT = int(sizeof(TABLE) / sizeof(TABLE[0]));

/** @brief How many entries a HID pad's numbered buttons map onto: everything but the d-pad. */
constexpr int FACE_COUNT = COUNT - 4;

} // namespace padbuttons

#endif // GAMEPADBUTTONS_H
