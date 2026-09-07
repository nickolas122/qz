#ifndef GAMEPADBUTTONS_H
#define GAMEPADBUTTONS_H

#include <QString>
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

/**
 * @brief What a pad physically sent, before anything claims to know what it is called.
 *
 * A HID report carries numbers, not labels: button 11 is button 11, and only the rider knows
 * whether it is under their left thumb or on the back of the pad. padbuttons is the vocabulary
 * of *labels*; this is the vocabulary of *sources*, and the map between them is what the pad
 * screen captures.
 *
 * Three kinds of source, because a pad will use any of them for the same physical control and
 * an 8BitDo Micro proves it: it declares a hat switch, parks that hat at its null value forever,
 * and reports its d-pad on the X and Y *axes* instead - 0, 127 and 255. A backend that reads
 * only buttons and hats does not merely mislabel those presses, it never sees them, which is
 * why an axis is a source here rather than a stick position.
 *
 * One bit per source in a quint64: 32 numbered buttons, the four hat directions, then each
 * analog axis twice, once for each end of its travel. Sixty-four rather than the old
 * thirty-two because the axes did not fit, and a source that does not fit is a control the
 * rider cannot reach.
 */
namespace padraw {

constexpr int BUTTON_COUNT = 32;

constexpr int HAT_UP = 32;
constexpr int HAT_DOWN = 33;
constexpr int HAT_LEFT = 34;
constexpr int HAT_RIGHT = 35;

/** X, Y, Z, Rx, Ry, Rz - the HID generic-desktop axes a gamepad actually uses. */
constexpr int AXIS_COUNT = 6;
/** The first axis bit. Each axis owns two: the low end of its travel, then the high end. */
constexpr int AXIS_BASE = 36;

constexpr int COUNT = AXIS_BASE + AXIS_COUNT * 2;

/** @brief HID usages for the axes above, in the same order. Generic-desktop page throughout. */
inline quint16 axisUsage(int axis) {
    return quint16(0x30 + axis); // 0x30 X, 0x31 Y, 0x32 Z, 0x33 Rx, 0x34 Ry, 0x35 Rz
}

inline QString axisName(int axis) {
    switch (axis) {
    case 0:
        return QStringLiteral("x");
    case 1:
        return QStringLiteral("y");
    case 2:
        return QStringLiteral("z");
    case 3:
        return QStringLiteral("rx");
    case 4:
        return QStringLiteral("ry");
    default:
        return QStringLiteral("rz");
    }
}

/** @brief The token one source is written as in the saved map: "b13", "hat_left", "x_lo". */
inline QString name(int bit) {
    if (bit >= 0 && bit < BUTTON_COUNT) {
        return QStringLiteral("b%1").arg(bit + 1); // HID numbers its buttons from 1
    }
    switch (bit) {
    case HAT_UP:
        return QStringLiteral("hat_up");
    case HAT_DOWN:
        return QStringLiteral("hat_down");
    case HAT_LEFT:
        return QStringLiteral("hat_left");
    case HAT_RIGHT:
        return QStringLiteral("hat_right");
    default:
        break;
    }
    if (bit >= AXIS_BASE && bit < COUNT) {
        const int axis = (bit - AXIS_BASE) / 2;
        const bool high = ((bit - AXIS_BASE) % 2) != 0;
        return axisName(axis) + (high ? QStringLiteral("_hi") : QStringLiteral("_lo"));
    }
    return QString();
}

/** @brief The bit for one end of one axis. Low is left and up; high is right and down. */
inline int axisBit(int axis, bool high) { return AXIS_BASE + axis * 2 + (high ? 1 : 0); }

/** @brief The reverse of name(), or -1 for a token this build does not know. */
inline int bit(const QString &token) {
    // Forty-eight comparisons, run when a map is loaded or a button is bound - never in a poll.
    for (int i = 0; i < COUNT; i++) {
        if (name(i) == token) {
            return i;
        }
    }
    return -1;
}

/** @brief One source as a mask, spelled once so nothing has to remember the width. */
inline quint64 mask(int bit) { return Q_UINT64_C(1) << bit; }

} // namespace padraw

#endif // GAMEPADBUTTONS_H
