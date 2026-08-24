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
    // Read before the availability check, not after it. The mapping screen binds to the
    // bindings and the repeat timings on every platform - it just says the pad cannot be
    // read here - and an early return left those showing 0 ms and no buttons.
    refreshSettings();

    if (!xinputAvailable) {
        qDebug() << QStringLiteral("gamepadcontroller: no XInput available, gamepad support is off");
        return;
    }

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

QString gamepadcontroller::settingKeyFor(const QString &action) {
    if (action == QStringLiteral("gearUp"))
        return QZSettings::gamepad_gear_up;
    if (action == QStringLiteral("gearDown"))
        return QZSettings::gamepad_gear_down;
    if (action == QStringLiteral("erg"))
        return QZSettings::gamepad_erg_mode;
    return QString();
}

QStringList gamepadcontroller::boundButtons(const QString &action) {
    const QString key = settingKeyFor(action);
    if (key.isEmpty())
        return QStringList();

    QSettings settings;
    QString value;
    if (key == QZSettings::gamepad_gear_up)
        value = settings.value(key, QZSettings::default_gamepad_gear_up).toString();
    else if (key == QZSettings::gamepad_gear_down)
        value = settings.value(key, QZSettings::default_gamepad_gear_down).toString();
    else
        value = settings.value(key, QZSettings::default_gamepad_erg_mode).toString();

    QStringList out;
    const QStringList parts = value.split(QStringLiteral(","), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const QString name = p.trimmed().toLower();
        if (!name.isEmpty() && !out.contains(name))
            out.append(name);
    }
    return out;
}

QStringList gamepadcontroller::gearUpButtons() const { return boundButtons(QStringLiteral("gearUp")); }
QStringList gamepadcontroller::gearDownButtons() const { return boundButtons(QStringLiteral("gearDown")); }
QStringList gamepadcontroller::ergButtons() const { return boundButtons(QStringLiteral("erg")); }

QStringList gamepadcontroller::pressedButtons() const {
    QStringList out;
    for (const namedButton &b : BUTTONS) {
        if (heldButtons & b.mask)
            out.append(QString::fromLatin1(b.name));
    }
    return out;
}

QString gamepadcontroller::actionFor(const QString &button) const {
    const QString name = button.trimmed().toLower();
    if (gearUpButtons().contains(name))
        return QStringLiteral("gearUp");
    if (gearDownButtons().contains(name))
        return QStringLiteral("gearDown");
    if (ergButtons().contains(name))
        return QStringLiteral("erg");
    return QString();
}

void gamepadcontroller::beginCapture(const QString &action) {
    if (settingKeyFor(action).isEmpty()) {
        qDebug() << QStringLiteral("gamepadcontroller: refusing to capture for unknown action") << action;
        return;
    }
    captureAction = action;
    // Whatever is held right now does not count. A rider reaching for the screen with a
    // thumb resting on a trigger would otherwise bind that trigger the instant capture
    // opened, which is the one thing a capture UI must not do.
    captureBaseline = heldButtons;
    capturePending = 0;
    // The poll idles at 1 Hz when shifting is off, which is too slow to feel like it is
    // listening. Capture needs the fast rate whether or not the feature is enabled.
    timer.setInterval(POLL_INTERVAL_MS);
    qDebug() << QStringLiteral("gamepadcontroller: capturing for") << action;
    emit statusChanged();
}

void gamepadcontroller::cancelCapture() {
    if (captureAction.isEmpty())
        return;
    captureAction.clear();
    captureBaseline = 0;
    capturePending = 0;
    emit statusChanged();
}

void gamepadcontroller::pollCapture(quint32 buttons) {
    // A button held since capture opened stops being excused the moment it comes up.
    captureBaseline &= buttons;

    const quint32 fresh = buttons & ~captureBaseline;

    if (capturePending == 0) {
        // BUTTONS order is the stable order buttonNames() promises, so two buttons
        // pressed in the same 50 ms frame resolve the same way every time.
        for (const namedButton &b : BUTTONS) {
            if (fresh & b.mask) {
                capturePending = b.mask;
                emit statusChanged();
                break;
            }
        }
        return;
    }

    if (buttons & capturePending) {
        return; // still held - nothing commits until it comes up
    }

    QString name;
    for (const namedButton &b : BUTTONS) {
        if (b.mask == capturePending) {
            name = QString::fromLatin1(b.name);
            break;
        }
    }

    const QString action = captureAction;
    captureAction.clear();
    captureBaseline = 0;
    capturePending = 0;

    if (name.isEmpty()) {
        emit statusChanged();
        return;
    }

    // A button may drive exactly one action. Binding it somewhere new takes it away from
    // wherever it was, rather than firing two actions off one press.
    const QString previous = actionFor(name);
    if (!previous.isEmpty() && previous != action) {
        unbind(previous, name);
    }

    QStringList names = boundButtons(action);
    if (!names.contains(name)) {
        names.append(name);
        QSettings settings;
        settings.setValue(settingKeyFor(action), names.join(QStringLiteral(",")));
    }

    qDebug() << QStringLiteral("gamepadcontroller: bound") << name << QStringLiteral("to") << action;
    refreshSettings();
    emit statusChanged();
    emit bindingsChanged();
}

void gamepadcontroller::unbind(const QString &action, const QString &button) {
    const QString key = settingKeyFor(action);
    if (key.isEmpty())
        return;

    QStringList names = boundButtons(action);
    if (!names.removeAll(button.trimmed().toLower()))
        return;

    QSettings settings;
    // An empty string is what buttonMask() already reads as "this action is off", so
    // clearing the last binding disables the action rather than leaving it bound to
    // everything. That behaviour predates this screen; the screen just exposes it.
    settings.setValue(key, names.join(QStringLiteral(",")));
    refreshSettings();
    emit bindingsChanged();
}

void gamepadcontroller::setRepeatDelay(int ms) {
    QSettings settings;
    settings.setValue(QZSettings::gamepad_repeat_delay, qMax(0, ms));
    refreshSettings();
    emit bindingsChanged();
}

void gamepadcontroller::setRepeatRate(int ms) {
    QSettings settings;
    settings.setValue(QZSettings::gamepad_repeat_rate, qMax(POLL_INTERVAL_MS, ms));
    refreshSettings();
    emit bindingsChanged();
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

    // The mapping screen has to work with shifting switched off - that is the state a
    // rider is in while setting the pad up for the first time - so capture keeps the
    // poll running and at full rate.
    const bool capturing = !captureAction.isEmpty();

    if (!enabled && !capturing) {
        if (timer.interval() != IDLE_INTERVAL_MS) {
            timer.setInterval(IDLE_INTERVAL_MS);
        }
        if (padPresent || heldButtons) {
            padPresent = false;
            heldButtons = 0;
            emit statusChanged();
        }
        return;
    }
    if (timer.interval() != POLL_INTERVAL_MS) {
        timer.setInterval(POLL_INTERVAL_MS);
    }

    quint32 buttons = 0;
    if (!readPad(&buttons)) {
        releaseAll();
        if (padPresent || heldButtons) {
            padPresent = false;
            heldButtons = 0;
            emit statusChanged();
        }
        return;
    }

    if (!padPresent || heldButtons != buttons) {
        padPresent = true;
        heldButtons = buttons;
        emit statusChanged();
    }

    if (capturing) {
        // No actions fire while binding. Pressing RT to map it must not also shift.
        pollCapture(buttons);
        return;
    }

    if (!enabled) {
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
