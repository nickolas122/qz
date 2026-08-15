#include "gamepadcontroller.h"

#include "qzsettings.h"

#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// The XInput button word, as documented for XINPUT_GAMEPAD. Declared here rather than pulled from
// xinput.h so the file needs neither the header nor the import library on either toolchain.
constexpr quint32 PAD_DPAD_UP = 0x0001;
constexpr quint32 PAD_DPAD_DOWN = 0x0002;
constexpr quint32 PAD_DPAD_LEFT = 0x0004;
constexpr quint32 PAD_DPAD_RIGHT = 0x0008;
constexpr quint32 PAD_START = 0x0010;
constexpr quint32 PAD_BACK = 0x0020;
constexpr quint32 PAD_LEFT_THUMB = 0x0040;
constexpr quint32 PAD_RIGHT_THUMB = 0x0080;
constexpr quint32 PAD_LB = 0x0100;
constexpr quint32 PAD_RB = 0x0200;
constexpr quint32 PAD_A = 0x1000;
constexpr quint32 PAD_B = 0x2000;
constexpr quint32 PAD_X = 0x4000;
constexpr quint32 PAD_Y = 0x8000;

// The analog triggers are not in the button word at all - they are separate 0-255 axes. They are
// folded in as two invented masks, deliberately above 16 bits so they can never collide with a real
// button, and everything downstream then treats them as ordinary buttons. Same convention as
// tools/xbox-mywhoosh-gears.
constexpr quint32 PAD_LT = 0x10000;
constexpr quint32 PAD_RT = 0x20000;

// Microsoft's own value for "the trigger is pressed", from XINPUT_GAMEPAD_TRIGGER_THRESHOLD.
constexpr int TRIGGER_THRESHOLD = 30;

constexpr int POLL_INTERVAL_MS = 50;
constexpr int IDLE_INTERVAL_MS = 1000;
constexpr int SETTINGS_REFRESH_MS = 2000;
// Querying an empty XInput slot is expensive by design, so an unknown slot is rescanned on a timer
// rather than every poll.
constexpr int SLOT_PROBE_MS = 2000;

struct namedButton {
    const char *name;
    quint32 mask;
};

const namedButton BUTTONS[] = {
    {"a", PAD_A},
    {"b", PAD_B},
    {"x", PAD_X},
    {"y", PAD_Y},
    {"lb", PAD_LB},
    {"rb", PAD_RB},
    {"lt", PAD_LT},
    {"rt", PAD_RT},
    {"start", PAD_START},
    {"back", PAD_BACK},
    {"l3", PAD_LEFT_THUMB},
    {"r3", PAD_RIGHT_THUMB},
    {"dpad_up", PAD_DPAD_UP},
    {"dpad_down", PAD_DPAD_DOWN},
    {"dpad_left", PAD_DPAD_LEFT},
    {"dpad_right", PAD_DPAD_RIGHT},
};

#ifdef Q_OS_WIN

struct QZ_XINPUT_GAMEPAD {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct QZ_XINPUT_STATE {
    DWORD dwPacketNumber;
    QZ_XINPUT_GAMEPAD Gamepad;
};

typedef DWORD(WINAPI *XInputGetState_t)(DWORD, QZ_XINPUT_STATE *);

XInputGetState_t resolveXInput() {
    static XInputGetState_t resolved = nullptr;
    static bool tried = false;
    if (tried) {
        return resolved;
    }
    tried = true;

    // Newest first: 1_4 ships with Windows 8 and later, 1_3 with the legacy DirectX redistributable,
    // 9_1_0 with everything back to Vista. The first one present wins.
    const wchar_t *dlls[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const wchar_t *dll : dlls) {
        HMODULE module = LoadLibraryW(dll);
        if (!module) {
            continue;
        }
        resolved = reinterpret_cast<XInputGetState_t>(
            reinterpret_cast<void *>(GetProcAddress(module, "XInputGetState")));
        if (resolved) {
            qDebug() << QStringLiteral("gamepadcontroller: using") << QString::fromWCharArray(dll);
            return resolved;
        }
        FreeLibrary(module);
    }
    return nullptr;
}

#endif

} // namespace

gamepadcontroller::gamepadcontroller(QObject *parent) : QObject(parent) {
#ifdef Q_OS_WIN
    xinputAvailable = resolveXInput() != nullptr;
#endif
    if (!xinputAvailable) {
        qDebug() << QStringLiteral("gamepadcontroller: no XInput available, gamepad support is off");
        return;
    }

    refreshSettings();

    timer.setTimerType(Qt::CoarseTimer);
    timer.setInterval(enabled ? POLL_INTERVAL_MS : IDLE_INTERVAL_MS);
    connect(&timer, &QTimer::timeout, this, &gamepadcontroller::poll);
    timer.start();
}

QStringList gamepadcontroller::buttonNames() {
    QStringList names;
    for (const namedButton &b : BUTTONS) {
        names.append(QString::fromLatin1(b.name));
    }
    return names;
}

quint32 gamepadcontroller::buttonMask(const QString &names) {
    quint32 mask = 0;
    const QStringList parts = names.split(QStringLiteral(","), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString name = part.trimmed().toLower();
        if (name.isEmpty()) {
            continue;
        }
        bool found = false;
        for (const namedButton &b : BUTTONS) {
            if (name == QLatin1String(b.name)) {
                mask |= b.mask;
                found = true;
                break;
            }
        }
        if (!found) {
            qDebug() << QStringLiteral("gamepadcontroller: unknown button") << name;
        }
    }
    return mask;
}

void gamepadcontroller::refreshSettings() {
    QSettings settings;
    lastSettingsRefresh = QDateTime::currentMSecsSinceEpoch();

    const bool wasEnabled = enabled;
    enabled = settings.value(QZSettings::gamepad_enabled, QZSettings::default_gamepad_enabled).toBool();

    gearUpAction.mask = buttonMask(
        settings.value(QZSettings::gamepad_gear_up, QZSettings::default_gamepad_gear_up).toString());
    gearDownAction.mask = buttonMask(
        settings.value(QZSettings::gamepad_gear_down, QZSettings::default_gamepad_gear_down).toString());
    ergAction.mask = buttonMask(
        settings.value(QZSettings::gamepad_erg_mode, QZSettings::default_gamepad_erg_mode).toString());

    // Shifting repeats while the paddle is held; ERG never does, because a toggle that repeats just
    // flickers the mode on and off.
    gearUpAction.repeats = true;
    gearDownAction.repeats = true;
    ergAction.repeats = false;

    repeatDelayMs =
        settings.value(QZSettings::gamepad_repeat_delay, QZSettings::default_gamepad_repeat_delay).toInt();
    repeatRateMs =
        settings.value(QZSettings::gamepad_repeat_rate, QZSettings::default_gamepad_repeat_rate).toInt();
    if (repeatRateMs < POLL_INTERVAL_MS) {
        repeatRateMs = POLL_INTERVAL_MS;
    }

    if (enabled != wasEnabled) {
        qDebug() << QStringLiteral("gamepadcontroller: enabled ->") << enabled;
        if (!enabled) {
            releaseAll();
        }
    }
}

void gamepadcontroller::releaseAll() {
    gearUpAction.down = false;
    gearDownAction.down = false;
    ergAction.down = false;
}

bool gamepadcontroller::fired(action &a, quint32 buttons, qint64 now) {
    const bool pressed = a.mask != 0 && (buttons & a.mask) != 0;

    if (!pressed) {
        a.down = false;
        return false;
    }
    if (!a.down) {
        a.down = true;
        a.nextRepeat = now + repeatDelayMs;
        return true;
    }
    // repeatDelayMs of 0 means "one action per press", so a held paddle stays quiet.
    if (a.repeats && repeatDelayMs > 0 && now >= a.nextRepeat) {
        a.nextRepeat = now + repeatRateMs;
        return true;
    }
    return false;
}

bool gamepadcontroller::readPad(quint32 *buttons) {
#ifdef Q_OS_WIN
    XInputGetState_t getState = resolveXInput();
    if (!getState) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QZ_XINPUT_STATE state;

    int slot = activeSlot;
    if (slot < 0) {
        if (now - lastSlotProbe < SLOT_PROBE_MS) {
            return false;
        }
        lastSlotProbe = now;
        for (int i = 0; i < 4; i++) {
            ZeroMemory(&state, sizeof(state));
            if (getState(static_cast<DWORD>(i), &state) == ERROR_SUCCESS) {
                slot = i;
                activeSlot = i;
                qDebug() << QStringLiteral("gamepadcontroller: pad connected on slot") << i;
                break;
            }
        }
        if (slot < 0) {
            return false;
        }
    } else {
        ZeroMemory(&state, sizeof(state));
        if (getState(static_cast<DWORD>(slot), &state) != ERROR_SUCCESS) {
            qDebug() << QStringLiteral("gamepadcontroller: pad disconnected from slot") << slot;
            activeSlot = -1;
            lastSlotProbe = now;
            return false;
        }
    }

    quint32 result = state.Gamepad.wButtons;
    if (state.Gamepad.bLeftTrigger > TRIGGER_THRESHOLD) {
        result |= PAD_LT;
    }
    if (state.Gamepad.bRightTrigger > TRIGGER_THRESHOLD) {
        result |= PAD_RT;
    }
    *buttons = result;
    return true;
#else
    Q_UNUSED(buttons)
    return false;
#endif
}

void gamepadcontroller::poll() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (now - lastSettingsRefresh >= SETTINGS_REFRESH_MS) {
        refreshSettings();
    }

    if (!enabled) {
        if (timer.interval() != IDLE_INTERVAL_MS) {
            timer.setInterval(IDLE_INTERVAL_MS);
        }
        return;
    }
    if (timer.interval() != POLL_INTERVAL_MS) {
        timer.setInterval(POLL_INTERVAL_MS);
    }

    quint32 buttons = 0;
    if (!readPad(&buttons)) {
        releaseAll();
        return;
    }

    if (fired(gearUpAction, buttons, now)) {
        qDebug() << QStringLiteral("gamepadcontroller: gear up");
        emit gearUp();
    }
    if (fired(gearDownAction, buttons, now)) {
        qDebug() << QStringLiteral("gamepadcontroller: gear down");
        emit gearDown();
    }
    if (fired(ergAction, buttons, now)) {
        qDebug() << QStringLiteral("gamepadcontroller: erg toggle");
        emit ergToggle();
    }
}
