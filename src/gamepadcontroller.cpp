#include "gamepadcontroller.h"

#include "gamepadandroid.h"
#include "gamepadbuttons.h"
#include "gamepadhid.h"
#include "qzsettings.h"

#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// Microsoft's own value for "the trigger is pressed", from XINPUT_GAMEPAD_TRIGGER_THRESHOLD.
constexpr int TRIGGER_THRESHOLD = 30;

constexpr int POLL_INTERVAL_MS = 50;
constexpr int IDLE_INTERVAL_MS = 1000;
constexpr int SETTINGS_REFRESH_MS = 2000;
// Querying an empty XInput slot is expensive by design, so an unknown slot is rescanned on a timer
// rather than every poll.
constexpr int SLOT_PROBE_MS = 2000;

// The button table moved to gamepadbuttons.h when the HID and Android backends arrived: all three
// have to build the same word, and two of them are in other files.
using namedButton = padbuttons::named;
using padbuttons::TABLE;

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
    // Built even when XInput answered: a rider can have an Xbox pad in one slot today and an
    // 8BitDo in D-input mode tomorrow, and the HID side costs nothing until it is polled.
    hidPad = new gamepadhid();
    padAvailable = xinputAvailable || hidPad->available();
#elif defined(Q_OS_ANDROID)
    // Android needs neither library: the system recognises the pad itself and CustomQtActivity
    // hands the events to gamepadandroid. There is nothing to resolve, so nothing to fail.
    padAvailable = true;
#endif

    // Read before the availability check, not after it. The mapping screen binds to the
    // bindings and the repeat timings on every platform - it just says the pad cannot be
    // read here - and an early return left those showing 0 ms and no buttons.
    refreshSettings();

    if (!padAvailable) {
        qDebug() << QStringLiteral("gamepadcontroller: no gamepad backend on this platform, support is off");
        return;
    }

    timer.setTimerType(Qt::CoarseTimer);
    timer.setInterval(enabled ? POLL_INTERVAL_MS : IDLE_INTERVAL_MS);
    connect(&timer, &QTimer::timeout, this, &gamepadcontroller::poll);
    timer.start();
}

gamepadcontroller::~gamepadcontroller() {
#ifdef Q_OS_WIN
    delete hidPad;
    hidPad = nullptr;
#endif
}

QStringList gamepadcontroller::buttonNames() {
    QStringList names;
    for (const namedButton &b : TABLE) {
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
        for (const namedButton &b : TABLE) {
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
    for (const namedButton &b : TABLE) {
        if (heldButtons & b.mask)
            out.append(QString::fromLatin1(b.name));
    }
    return out;
}

QStringList gamepadcontroller::pressedInputs() const {
    QStringList out;
    for (int b = 0; b < padraw::COUNT; b++) {
        if (heldRaw & padraw::mask(b))
            out.append(padraw::name(b));
    }
    return out;
}

bool gamepadcontroller::remappable() const {
#ifdef Q_OS_WIN
    // Only while HID is the backend answering. An Xbox pad in the other hand does not stop the
    // 8BitDo needing a map, but renaming buttons QZ is not currently reading would be a screen
    // that appears to do nothing.
    return hidPad != nullptr && backendName == QStringLiteral("HID");
#else
    return false;
#endif
}

quint64 gamepadcontroller::readRaw() const {
#ifdef Q_OS_WIN
    if (hidPad && backendName == QStringLiteral("HID"))
        return hidPad->physical();
#endif
    return 0;
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
    // One listener at a time - see beginRemap().
    remapButton.clear();
    remapBaseline = 0;
    remapPending = 0;
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

void gamepadcontroller::beginRemap(const QString &button) {
    const QString name = button.trimmed().toLower();
    if (buttonMask(name) == 0) {
        qDebug() << QStringLiteral("gamepadcontroller: refusing to remap unknown button") << button;
        return;
    }
    if (!remappable()) {
        qDebug() << QStringLiteral("gamepadcontroller: nothing to remap, the backend names its own buttons");
        return;
    }
    // One listener at a time, or a single press would both bind an action and rename a button.
    cancelCapture();
    remapButton = name;
    remapBaseline = heldRaw;
    remapPending = 0;
    timer.setInterval(POLL_INTERVAL_MS);
#ifdef Q_OS_WIN
    // The report bytes are what identify a control QZ has no name for. Worth the log lines for
    // the few seconds a rider is asking the question, and not for a second longer.
    if (hidPad)
        hidPad->setVerbose(true);
#endif
    qDebug() << QStringLiteral("gamepadcontroller: listening for the input that means") << name;
    emit statusChanged();
}

void gamepadcontroller::resetMap() {
#ifdef Q_OS_WIN
    if (hidPad)
        hidPad->resetMap();
#endif
    QSettings settings;
    settings.setValue(QZSettings::gamepad_hid_map, QZSettings::default_gamepad_hid_map);
    settings.setValue(QZSettings::gamepad_hid_map_pad, QZSettings::default_gamepad_hid_map_pad);
    qDebug() << QStringLiteral("gamepadcontroller: button map cleared, back to the positional guess");
    emit statusChanged();
}

void gamepadcontroller::cancelCapture() {
    if (captureAction.isEmpty() && remapButton.isEmpty())
        return;
    captureAction.clear();
    captureBaseline = 0;
    capturePending = 0;
    remapButton.clear();
    remapBaseline = 0;
    remapPending = 0;
#ifdef Q_OS_WIN
    if (hidPad)
        hidPad->setVerbose(false);
#endif
    emit statusChanged();
}

void gamepadcontroller::pollRemap(quint64 raw) {
    // Same three rules as pollCapture, on the un-named layer: what was already held does not
    // count, the first fresh input is the candidate, and nothing commits until it comes back up.
    remapBaseline &= raw;

    const quint64 fresh = raw & ~remapBaseline;

    if (remapPending == 0) {
        for (int b = 0; b < padraw::COUNT; b++) {
            if (fresh & padraw::mask(b)) {
                remapPending = padraw::mask(b);
                emit statusChanged();
                break;
            }
        }
        return;
    }

    if (raw & remapPending) {
        return;
    }

    int bit = -1;
    for (int b = 0; b < padraw::COUNT; b++) {
        if (remapPending == padraw::mask(b)) {
            bit = b;
            break;
        }
    }

    const QString name = remapButton;
    remapButton.clear();
    remapBaseline = 0;
    remapPending = 0;

#ifdef Q_OS_WIN
    if (hidPad) {
        hidPad->setVerbose(false);
    }
    if (bit >= 0 && hidPad) {
        hidPad->remap(bit, buttonMask(name));
        QSettings settings;
        // The pad's own name goes with the map, because button 11 means something else on the
        // next pad and a map that names the wrong buttons is worse than no map.
        settings.setValue(QZSettings::gamepad_hid_map, hidPad->mapSpec());
        settings.setValue(QZSettings::gamepad_hid_map_pad, hidPad->name());
        qDebug() << QStringLiteral("gamepadcontroller:") << padraw::name(bit) << QStringLiteral("on")
                 << hidPad->name() << QStringLiteral("is now") << name;
    }
#else
    Q_UNUSED(bit)
#endif

    emit statusChanged();
}

void gamepadcontroller::pollCapture(quint32 buttons) {
    // A button held since capture opened stops being excused the moment it comes up.
    captureBaseline &= buttons;

    const quint32 fresh = buttons & ~captureBaseline;

    if (capturePending == 0) {
        // TABLE order is the stable order buttonNames() promises, so two buttons
        // pressed in the same 50 ms frame resolve the same way every time.
        for (const namedButton &b : TABLE) {
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
    for (const namedButton &b : TABLE) {
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

#ifdef Q_OS_WIN
    // Cheap and idempotent: gamepadhid ignores a map it already has, so this can ride the ordinary
    // settings refresh rather than needing a signal of its own.
    if (hidPad) {
        hidPad->setMap(
            settings.value(QZSettings::gamepad_hid_map, QZSettings::default_gamepad_hid_map).toString(),
            settings.value(QZSettings::gamepad_hid_map_pad, QZSettings::default_gamepad_hid_map_pad).toString());
    }
#endif

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

void gamepadcontroller::setBackend(const QString &name, const QString &device) {
    if (backendName == name && deviceName == device) {
        return;
    }
    backendName = name;
    deviceName = device;
    if (!name.isEmpty()) {
        qDebug() << QStringLiteral("gamepadcontroller: reading the pad through") << name << device;
    }
    emit statusChanged();
}

bool gamepadcontroller::readXInput(quint32 *buttons) {
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
        result |= padbuttons::LT;
    }
    if (state.Gamepad.bRightTrigger > TRIGGER_THRESHOLD) {
        result |= padbuttons::RT;
    }
    *buttons = result;
    return true;
#else
    Q_UNUSED(buttons)
    return false;
#endif
}

bool gamepadcontroller::readPad(quint32 *buttons) {
#ifdef Q_OS_WIN
    // XInput first, and not only because it came first: it is the backend that knows an Xbox pad
    // is an Xbox pad, so its labels are the pad's own. HID is what is left over, and an XInput pad
    // is also a HID device, so asking in the other order would read the same pad through the
    // blinder.
    if (readXInput(buttons)) {
        if (hidPad) {
            // Hand the handle back rather than holding a file open on a pad nothing is reading.
            // A no-op unless a HID pad was answering before an XInput one turned up.
            hidPad->close();
        }
        setBackend(QStringLiteral("XInput"), QString());
        return true;
    }
    if (hidPad && hidPad->poll(buttons, QDateTime::currentMSecsSinceEpoch())) {
        setBackend(QStringLiteral("HID"), hidPad->name());
        return true;
    }
    setBackend(QString(), QString());
    return false;
#elif defined(Q_OS_ANDROID)
    if (gamepadandroid::connected()) {
        *buttons = gamepadandroid::buttons();
        setBackend(QStringLiteral("Android"), gamepadandroid::name());
        return true;
    }
    setBackend(QString(), QString());
    return false;
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
    // poll running and at full rate. Naming a button counts as capture for the same reason.
    const bool capturing = !captureAction.isEmpty();
    const bool remapping = !remapButton.isEmpty();

    if (!enabled && !capturing && !remapping) {
        if (timer.interval() != IDLE_INTERVAL_MS) {
            timer.setInterval(IDLE_INTERVAL_MS);
        }
        if (padPresent || heldButtons || heldRaw) {
            padPresent = false;
            heldButtons = 0;
            heldRaw = 0;
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
        if (padPresent || heldButtons || heldRaw) {
            padPresent = false;
            heldButtons = 0;
            heldRaw = 0;
            emit statusChanged();
        }
        return;
    }

    // readPad() has just settled which backend answered, so the un-named layer is readable now.
    const quint64 raw = readRaw();

    if (!padPresent || heldButtons != buttons || heldRaw != raw) {
        padPresent = true;
        heldButtons = buttons;
        heldRaw = raw;
        emit statusChanged();
    }

    if (remapping) {
        // Naming a button is checked before binding one, and both before any action fires: the
        // press that renames RT must not also shift.
        pollRemap(raw);
        return;
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
