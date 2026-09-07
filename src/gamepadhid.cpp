#include "gamepadhid.h"

#ifdef Q_OS_WIN

#include "gamepadbuttons.h"

#include <QByteArray>
#include <QDebug>
#include <QStringList>
#include <QVector>

#include <windows.h>

// The HID parser headers ship with both toolchains (mingw-w64 and the Windows SDK), but this is a
// fallback backend: if they are ever missing the app should still build, with HID pads simply
// unavailable, rather than failing the build over a pad nobody has plugged in.
#if defined(__has_include)
#if __has_include(<hidsdi.h>)
#define QZ_HAS_HID 1
#endif
#else
#define QZ_HAS_HID 1
#endif

#ifdef QZ_HAS_HID
extern "C" {
#include <hidsdi.h>
}
#endif

namespace {

constexpr int SCAN_INTERVAL_MS = 2000;
// How many completed reports one poll drains before giving up its turn. A pad rattled hard enough
// to outrun 50 ms of reads must not be able to hold the timer inside this loop.
constexpr int MAX_REPORTS_PER_POLL = 16;

#ifdef QZ_HAS_HID

constexpr USAGE PAGE_GENERIC = 0x01;
constexpr USAGE PAGE_BUTTON = 0x09;
constexpr USAGE USAGE_JOYSTICK = 0x04;
constexpr USAGE USAGE_GAMEPAD = 0x05;
constexpr USAGE USAGE_HAT = 0x39;

// The hat's four directions as padraw sources, not as d-pad names: what a hat is called is the
// map's business, and on a pad whose d-pad is on numbered buttons the hat may be something else
// entirely.
const quint64 RAW_HAT_UP = padraw::mask(padraw::HAT_UP);
const quint64 RAW_HAT_DOWN = padraw::mask(padraw::HAT_DOWN);
const quint64 RAW_HAT_LEFT = padraw::mask(padraw::HAT_LEFT);
const quint64 RAW_HAT_RIGHT = padraw::mask(padraw::HAT_RIGHT);

// How far an axis has to leave the middle of its declared range before it counts as pressed, as a
// fraction of that range. A d-pad on an axis goes all the way to an end stop, so this only decides
// how hard a *stick* has to be pushed - and a shift paddle that fires on a nudge would be worse
// than one that needs a shove.
//
// Measured from the declared midpoint rather than from an observed resting value, which is what
// this did first and got wrong: a Bluetooth pad's first input report after connecting is often all
// zeroes, so "wherever it sat on the first report" learned 0 as the centre of an 0..255 axis, and
// a d-pad pushed fully left then read as no movement at all. The descriptor is not guessing.
//
// The cost is an analog trigger, which rests at its minimum rather than its middle, and so reads
// as held towards the low end whenever it is not pulled. Nothing acts on that - the default map
// names only X and Y, and an unnamed source drives nothing - so it is a stray line in the pad
// screen's readout rather than a stuck shift.
constexpr double AXIS_THRESHOLD = 0.3;

// An eight-position hat, in the order HID numbers them: north, then clockwise.
const quint64 HAT8[8] = {
    RAW_HAT_UP,
    RAW_HAT_UP | RAW_HAT_RIGHT,
    RAW_HAT_RIGHT,
    RAW_HAT_DOWN | RAW_HAT_RIGHT,
    RAW_HAT_DOWN,
    RAW_HAT_DOWN | RAW_HAT_LEFT,
    RAW_HAT_LEFT,
    RAW_HAT_UP | RAW_HAT_LEFT,
};

/**
 * The entry points this backend needs, none of them linked.
 *
 * hid.dll for the parser, for the reason the class comment gives. The two raw input calls come
 * the same way rather than through user32.lib because the Qt 6 / MSVC target does not link user32
 * at all, and adding a library to every platform's link line to enumerate pads on one of them is
 * the wrong trade - user32.dll is already loaded in a GUI process, so this only asks for its
 * address.
 */
struct hidapi {
    UINT(WINAPI *RawDeviceList)(PRAWINPUTDEVICELIST, PUINT, UINT) = nullptr;
    UINT(WINAPI *RawDeviceInfo)(HANDLE, UINT, LPVOID, PUINT) = nullptr;

    BOOLEAN(NTAPI *GetPreparsedData)(HANDLE, PHIDP_PREPARSED_DATA *) = nullptr;
    BOOLEAN(NTAPI *FreePreparsedData)(PHIDP_PREPARSED_DATA) = nullptr;
    BOOLEAN(NTAPI *GetProductString)(HANDLE, PVOID, ULONG) = nullptr;
    NTSTATUS(NTAPI *GetCaps)(PHIDP_PREPARSED_DATA, PHIDP_CAPS) = nullptr;
    NTSTATUS(NTAPI *GetButtonCaps)(HIDP_REPORT_TYPE, PHIDP_BUTTON_CAPS, PUSHORT, PHIDP_PREPARSED_DATA) = nullptr;
    NTSTATUS(NTAPI *GetValueCaps)(HIDP_REPORT_TYPE, PHIDP_VALUE_CAPS, PUSHORT, PHIDP_PREPARSED_DATA) = nullptr;
    NTSTATUS(NTAPI *GetUsages)
    (HIDP_REPORT_TYPE, USAGE, USHORT, PUSAGE, PULONG, PHIDP_PREPARSED_DATA, PCHAR, ULONG) = nullptr;
    NTSTATUS(NTAPI *GetUsageValue)
    (HIDP_REPORT_TYPE, USAGE, USHORT, USAGE, PULONG, PHIDP_PREPARSED_DATA, PCHAR, ULONG) = nullptr;
};

const hidapi *resolveHid() {
    static hidapi api;
    static bool tried = false;
    static bool ok = false;
    if (tried) {
        return ok ? &api : nullptr;
    }
    tried = true;

    HMODULE module = LoadLibraryW(L"hid.dll");
    if (!module) {
        return nullptr;
    }

    // Every cast below is to the signature hidsdi.h declares for that same export; the trip
    // through void * is only to keep -Wcast-function-type quiet on mingw.
    auto sym = [module](const char *name) { return reinterpret_cast<void *>(GetProcAddress(module, name)); };

    api.GetPreparsedData = reinterpret_cast<decltype(api.GetPreparsedData)>(sym("HidD_GetPreparsedData"));
    api.FreePreparsedData = reinterpret_cast<decltype(api.FreePreparsedData)>(sym("HidD_FreePreparsedData"));
    api.GetProductString = reinterpret_cast<decltype(api.GetProductString)>(sym("HidD_GetProductString"));
    api.GetCaps = reinterpret_cast<decltype(api.GetCaps)>(sym("HidP_GetCaps"));
    api.GetButtonCaps = reinterpret_cast<decltype(api.GetButtonCaps)>(sym("HidP_GetButtonCaps"));
    api.GetValueCaps = reinterpret_cast<decltype(api.GetValueCaps)>(sym("HidP_GetValueCaps"));
    api.GetUsages = reinterpret_cast<decltype(api.GetUsages)>(sym("HidP_GetUsages"));
    api.GetUsageValue = reinterpret_cast<decltype(api.GetUsageValue)>(sym("HidP_GetUsageValue"));

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        api.RawDeviceList = reinterpret_cast<decltype(api.RawDeviceList)>(
            reinterpret_cast<void *>(GetProcAddress(user32, "GetRawInputDeviceList")));
        api.RawDeviceInfo = reinterpret_cast<decltype(api.RawDeviceInfo)>(
            reinterpret_cast<void *>(GetProcAddress(user32, "GetRawInputDeviceInfoW")));
    }

    ok = api.RawDeviceList && api.RawDeviceInfo && api.GetPreparsedData && api.FreePreparsedData && api.GetCaps &&
         api.GetButtonCaps && api.GetValueCaps && api.GetUsages && api.GetUsageValue;
    if (!ok) {
        FreeLibrary(module);
        if (user32) {
            FreeLibrary(user32);
        }
        return nullptr;
    }
    // The module is deliberately left loaded: the pointers above outlive this call.
    return &api;
}

#endif // QZ_HAS_HID

} // namespace

/** One open HID pad, and the overlapped read that keeps its reports coming. */
struct gamepadhid::device {
#ifdef QZ_HAS_HID
    HANDLE handle = INVALID_HANDLE_VALUE;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    OVERLAPPED overlapped = {};
    QByteArray report;
    QVector<USAGE> usages;
    bool pending = false;

    bool hasHat = false;
    LONG hatMin = 0;
    LONG hatMax = 7;

    /** One generic-desktop axis the pad declares, in padraw's axis order. */
    struct axis {
        bool present = false;
        LONG min = 0;
        LONG max = 255;
        /** The two trip points, worked out once from the declared range. See AXIS_THRESHOLD. */
        LONG low = 0;
        LONG high = 0;
    };
    axis axes[padraw::AXIS_COUNT];

    /** The last report seen, so the verbose log fires on change rather than at 20 Hz. */
    QByteArray lastReport;
#endif
};

gamepadhid::gamepadhid() {
#ifdef QZ_HAS_HID
    hidAvailable = resolveHid() != nullptr;
#endif
    // The guess, until the controller hands over a saved map. Without this every input would be
    // nameless on the first poll, which is a worse first run than the wrong label it replaced.
    rebuildMap();
    if (!hidAvailable) {
        qDebug() << QStringLiteral("gamepadhid: no HID parser available, HID pads are off");
    }
}

gamepadhid::~gamepadhid() { close(); }

// Bits are quint64 from here down: see padraw. The named side stays quint32 - padbuttons has not
// grown, and it is not the layer that has to describe whatever a pad happens to be built from.

namespace {

/**
 * @brief The guess, for one physical source.
 *
 * Positional for the buttons, because there is nothing else to go on: a HID report says
 * "button 11", never "the left shoulder". It is right on the many pads that number their buttons
 * the way an Xbox pad lays them out, and wrong on the rest - which is what the map is for.
 *
 * The d-pad is guessed twice over, from the hat *and* from the X and Y axes, because a pad uses
 * one or the other and there is no way to tell which from the descriptor alone - the Micro
 * declares a hat it never moves. Two sources for one name is harmless: only one of them ever
 * moves, they cannot disagree, and the first rename resolves the duplicate anyway. The cost of
 * guessing only one would be a d-pad that does nothing until the rider finds this screen.
 */
quint32 guessedMask(int bit) {
    if (bit >= 0 && bit < padbuttons::FACE_COUNT) {
        return padbuttons::TABLE[bit].mask;
    }
    switch (bit) {
    case padraw::HAT_UP:
        return padbuttons::DPAD_UP;
    case padraw::HAT_DOWN:
        return padbuttons::DPAD_DOWN;
    case padraw::HAT_LEFT:
        return padbuttons::DPAD_LEFT;
    case padraw::HAT_RIGHT:
        return padbuttons::DPAD_RIGHT;
    default:
        break;
    }
    // X is left and right, Y is up and down; low is left and up, the way HID orients a screen.
    if (bit == padraw::axisBit(0, false))
        return padbuttons::DPAD_LEFT;
    if (bit == padraw::axisBit(0, true))
        return padbuttons::DPAD_RIGHT;
    if (bit == padraw::axisBit(1, false))
        return padbuttons::DPAD_UP;
    if (bit == padraw::axisBit(1, true))
        return padbuttons::DPAD_DOWN;
    return 0;
}

/** @brief One padbuttons name to its mask, or 0. */
quint32 maskForName(const QString &name) {
    for (const padbuttons::named &b : padbuttons::TABLE) {
        if (name == QLatin1String(b.name)) {
            return b.mask;
        }
    }
    return 0;
}

} // namespace

void gamepadhid::rebuildMap() {
    for (int i = 0; i < padraw::COUNT; i++) {
        map[i] = guessedMask(i);
    }

    if (savedSpec.isEmpty()) {
        return;
    }
    // A map made on another pad is worse than no map: button 11 means something else there, so it
    // would name this pad's buttons confidently and wrongly. padName is empty until a pad opens,
    // and this runs again from open() once it is known.
    if (!savedPad.isEmpty() && !padName.isEmpty() && savedPad.compare(padName, Qt::CaseInsensitive) != 0) {
        qDebug() << QStringLiteral("gamepadhid: ignoring the button map, it was captured on") << savedPad;
        return;
    }

    // The saved map is the whole truth rather than a patch over the guess. A rider who moves a
    // name onto another source has also said the old source no longer carries it, and a patch
    // that only listed differences could not say that without a way to spell "this one has no
    // name" - which is more format than the problem is worth.
    //
    // The price is that a saved map does not learn: widening the guess later reaches new pads
    // only. That price was paid once already, when the axes were added and an existing map kept
    // a d-pad that could not move. If the guess changes again, "Back to the guess" is the fix,
    // and this is the comment that says so.
    for (int i = 0; i < padraw::COUNT; i++) {
        map[i] = 0;
    }
    const QStringList entries = savedSpec.split(QStringLiteral(","), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const int split = entry.indexOf(QLatin1Char('='));
        if (split <= 0) {
            continue;
        }
        const int bit = padraw::bit(entry.left(split).trimmed().toLower());
        const quint32 mask = maskForName(entry.mid(split + 1).trimmed().toLower());
        if (bit >= 0 && mask != 0) {
            map[bit] = mask;
        }
    }
}

void gamepadhid::setMap(const QString &spec, const QString &forPad) {
    if (savedSpec == spec && savedPad == forPad) {
        return; // called off the settings refresh every couple of seconds
    }
    savedSpec = spec;
    savedPad = forPad;
    rebuildMap();
}

QString gamepadhid::mapSpec() const {
    QStringList parts;
    for (int i = 0; i < padraw::COUNT; i++) {
        if (map[i] == 0) {
            continue;
        }
        for (const padbuttons::named &b : padbuttons::TABLE) {
            if (b.mask == map[i]) {
                parts.append(padraw::name(i) + QStringLiteral("=") + QLatin1String(b.name));
                break;
            }
        }
    }
    return parts.join(QStringLiteral(","));
}

void gamepadhid::remap(int rawBit, quint32 buttonMask) {
    if (rawBit < 0 || rawBit >= padraw::COUNT) {
        return;
    }
    if (buttonMask != 0) {
        // One name, one source. Leaving the old source in place would fire the action twice, once
        // from the button the rider meant and once from whatever the guess had put there - and on
        // a pad where the guess named both the hat and an axis, twice over.
        for (int i = 0; i < padraw::COUNT; i++) {
            if (map[i] == buttonMask) {
                map[i] = 0;
            }
        }
    }
    map[rawBit] = buttonMask;
    // savedSpec is what rebuildMap() would restore, so it has to follow the edit or the next
    // settings refresh would undo it.
    savedSpec = mapSpec();
    savedPad = padName;
}

void gamepadhid::resetMap() {
    savedSpec.clear();
    savedPad.clear();
    rebuildMap();
}

#ifdef QZ_HAS_HID

void gamepadhid::close() {
    if (dev) {
        if (dev->pending) {
            CancelIo(dev->handle);
        }
        if (dev->preparsed) {
            resolveHid()->FreePreparsedData(dev->preparsed);
        }
        if (dev->overlapped.hEvent) {
            CloseHandle(dev->overlapped.hEvent);
        }
        if (dev->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(dev->handle);
        }
        delete dev;
        dev = nullptr;
    }
    padName.clear();
    state = 0;
    physicalState = 0;
}

bool gamepadhid::open() {
    const hidapi *hid = resolveHid();
    if (!hid) {
        return false;
    }

    // The raw input device list is the shortest route to "every HID device present".
    UINT count = 0;
    if (hid->RawDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) {
        return false;
    }
    QVector<RAWINPUTDEVICELIST> devices(static_cast<int>(count));
    const UINT got = hid->RawDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (got == UINT(-1)) {
        return false;
    }
    devices.resize(int(got));

    for (const RAWINPUTDEVICELIST &entry : qAsConst(devices)) {
        if (entry.dwType != RIM_TYPEHID) {
            continue;
        }

        RID_DEVICE_INFO info;
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        UINT size = sizeof(info);
        if (hid->RawDeviceInfo(entry.hDevice, RIDI_DEVICEINFO, &info, &size) == UINT(-1)) {
            continue;
        }
        // Joystick and gamepad both, because plenty of pads in a generic HID mode call themselves
        // a joystick.
        if (info.hid.usUsagePage != PAGE_GENERIC ||
            (info.hid.usUsage != USAGE_JOYSTICK && info.hid.usUsage != USAGE_GAMEPAD)) {
            continue;
        }

        UINT chars = 0;
        if (hid->RawDeviceInfo(entry.hDevice, RIDI_DEVICENAME, nullptr, &chars) == UINT(-1) || chars == 0) {
            continue;
        }
        QVector<wchar_t> path(int(chars) + 1, 0);
        if (hid->RawDeviceInfo(entry.hDevice, RIDI_DEVICENAME, path.data(), &chars) == UINT(-1)) {
            continue;
        }

        // Shared read and write: a pad is not ours alone, and asking for exclusive access is how
        // an open fails on a device something else is already watching.
        HANDLE handle = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps;
        ZeroMemory(&caps, sizeof(caps));
        if (!hid->GetPreparsedData(handle, &preparsed) || !preparsed ||
            hid->GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS || caps.InputReportByteLength == 0) {
            if (preparsed) {
                hid->FreePreparsedData(preparsed);
            }
            CloseHandle(handle);
            continue;
        }

        device *opened = new device;
        opened->handle = handle;
        opened->preparsed = preparsed;
        opened->report.resize(int(caps.InputReportByteLength));
        opened->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        // How many buttons can be down at once, which is the length HidP_GetUsages fills.
        USHORT buttonCapsLength = caps.NumberInputButtonCaps;
        int maxUsages = 0;
        if (buttonCapsLength > 0) {
            QVector<HIDP_BUTTON_CAPS> buttonCaps(static_cast<int>(buttonCapsLength));
            if (hid->GetButtonCaps(HidP_Input, buttonCaps.data(), &buttonCapsLength, preparsed) ==
                HIDP_STATUS_SUCCESS) {
                for (int i = 0; i < int(buttonCapsLength); i++) {
                    const HIDP_BUTTON_CAPS &b = buttonCaps[i];
                    if (b.UsagePage != PAGE_BUTTON) {
                        continue;
                    }
                    maxUsages += b.IsRange ? (b.Range.UsageMax - b.Range.UsageMin + 1) : 1;
                }
            }
        }
        opened->usages.resize(qBound(16, maxUsages, 128));

        USHORT valueCapsLength = caps.NumberInputValueCaps;
        if (valueCapsLength > 0) {
            QVector<HIDP_VALUE_CAPS> valueCaps(static_cast<int>(valueCapsLength));
            if (hid->GetValueCaps(HidP_Input, valueCaps.data(), &valueCapsLength, preparsed) == HIDP_STATUS_SUCCESS) {
                for (int i = 0; i < int(valueCapsLength); i++) {
                    const HIDP_VALUE_CAPS &v = valueCaps[i];
                    if (v.UsagePage != PAGE_GENERIC) {
                        continue;
                    }
                    const USAGE first = v.IsRange ? v.Range.UsageMin : v.NotRange.Usage;
                    const USAGE last = v.IsRange ? v.Range.UsageMax : v.NotRange.Usage;

                    if (first <= USAGE_HAT && USAGE_HAT <= last) {
                        opened->hasHat = true;
                        opened->hatMin = v.LogicalMin;
                        opened->hatMax = v.LogicalMax;
                    }
                    // A ranged cap covers several usages at once, so every axis in the range is
                    // claimed rather than only the first - which is also why this no longer stops
                    // at the hat: on the Micro the hat is the first cap listed, and breaking there
                    // is what hid the X and Y axes the d-pad actually lives on.
                    for (int a = 0; a < padraw::AXIS_COUNT; a++) {
                        const USAGE usage = padraw::axisUsage(a);
                        if (usage < first || usage > last) {
                            continue;
                        }
                        opened->axes[a].present = true;
                        opened->axes[a].min = v.LogicalMin;
                        opened->axes[a].max = v.LogicalMax;
                        const double middle = (double(v.LogicalMin) + double(v.LogicalMax)) / 2.0;
                        const double reach = (double(v.LogicalMax) - double(v.LogicalMin)) * AXIS_THRESHOLD;
                        opened->axes[a].low = LONG(middle - reach);
                        opened->axes[a].high = LONG(middle + reach);
                    }
                }
            }
        }

        wchar_t product[128];
        ZeroMemory(product, sizeof(product));
        QString name;
        if (hid->GetProductString && hid->GetProductString(handle, product, sizeof(product))) {
            name = QString::fromWCharArray(product).trimmed();
        }
        if (name.isEmpty()) {
            name = QStringLiteral("HID gamepad");
        }

        dev = opened;
        padName = name;
        state = 0;
        physicalState = 0;
        // The pad's name is only known now, and it decides whether the saved map is this pad's.
        rebuildMap();
        QStringList found;
        for (int a = 0; a < padraw::AXIS_COUNT; a++) {
            if (opened->axes[a].present) {
                // The trip points are logged rather than only the range: a d-pad that reads as
                // motionless is almost always one whose travel does not cross them.
                found.append(QStringLiteral("%1 %2..%3 trips %4/%5")
                                 .arg(padraw::axisName(a))
                                 .arg(opened->axes[a].min)
                                 .arg(opened->axes[a].max)
                                 .arg(opened->axes[a].low)
                                 .arg(opened->axes[a].high));
            }
        }
        qDebug() << QStringLiteral("gamepadhid: opened") << padName << QStringLiteral("buttons up to")
                 << opened->usages.size() << QStringLiteral("hat") << opened->hasHat << QStringLiteral("axes")
                 << (found.isEmpty() ? QStringLiteral("none") : found.join(QStringLiteral(", ")));

        if (!armRead()) {
            close();
            continue;
        }
        return true;
    }

    return false;
}

bool gamepadhid::armRead() {
    if (!dev || dev->pending) {
        return dev != nullptr;
    }

    ResetEvent(dev->overlapped.hEvent);
    DWORD read = 0;
    if (ReadFile(dev->handle, dev->report.data(), DWORD(dev->report.size()), &read, &dev->overlapped)) {
        // Completed inside the call, which is what happens when a report was already queued.
        decode(dev->report.constData(), int(read), &state);
        return true;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        dev->pending = true;
        return true;
    }
    return false;
}

bool gamepadhid::decode(const char *report, int length, quint32 *buttons) {
    const hidapi *hid = resolveHid();
    if (!hid || !dev || length <= 0) {
        return false;
    }

    // Physical first, named second. Every source the pad reports gets a bit, including the ones
    // no name is pointed at yet: an input dropped here cannot be mapped later, which is what made
    // the 8BitDo Micro's d-pad unreachable rather than merely mislabelled.
    quint64 raw = 0;

    ULONG usageLength = ULONG(dev->usages.size());
    if (usageLength > 0 && hid->GetUsages(HidP_Input, PAGE_BUTTON, 0, dev->usages.data(), &usageLength,
                                          dev->preparsed, const_cast<PCHAR>(report),
                                          ULONG(length)) == HIDP_STATUS_SUCCESS) {
        for (ULONG i = 0; i < usageLength; i++) {
            const int index = int(dev->usages[int(i)]) - 1; // HID numbers buttons from 1
            if (index >= 0 && index < padraw::BUTTON_COUNT) {
                raw |= padraw::mask(index);
            }
        }
    }

    if (dev->hasHat) {
        ULONG value = 0;
        if (hid->GetUsageValue(HidP_Input, PAGE_GENERIC, 0, USAGE_HAT, &value, dev->preparsed,
                               const_cast<PCHAR>(report), ULONG(length)) == HIDP_STATUS_SUCCESS) {
            const LONG position = LONG(value) - dev->hatMin;
            const LONG positions = dev->hatMax - dev->hatMin + 1;
            // Anything outside the declared range is the null state: the hat is centred.
            if (position >= 0 && position < positions) {
                if (positions == 4) {
                    raw |= HAT8[(position * 2) % 8]; // a four-way hat: N, E, S, W
                } else if (positions >= 8) {
                    raw |= HAT8[position % 8];
                }
            }
        }
    }

    QStringList seen; // only filled while verbose, for the log below
    for (int a = 0; a < padraw::AXIS_COUNT; a++) {
        device::axis &ax = dev->axes[a];
        if (!ax.present) {
            continue;
        }
        ULONG value = 0;
        if (hid->GetUsageValue(HidP_Input, PAGE_GENERIC, 0, padraw::axisUsage(a), &value, dev->preparsed,
                               const_cast<PCHAR>(report), ULONG(length)) != HIDP_STATUS_SUCCESS) {
            continue;
        }
        const LONG now = LONG(value);
        if (verbose) {
            seen.append(QStringLiteral("%1=%2").arg(padraw::axisName(a)).arg(now));
        }
        if (now <= ax.low) {
            raw |= padraw::mask(padraw::axisBit(a, false));
        } else if (now >= ax.high) {
            raw |= padraw::mask(padraw::axisBit(a, true));
        }
    }

    if (verbose && dev->lastReport != QByteArray(report, length)) {
        dev->lastReport = QByteArray(report, length);
        qDebug() << QStringLiteral("gamepadhid: report") << dev->lastReport.toHex(' ')
                 << QStringLiteral("sources") << QString::number(raw, 16) << QStringLiteral("axes")
                 << seen.join(QStringLiteral(" "));
    }

    quint32 result = 0;
    for (int b = 0; b < padraw::COUNT; b++) {
        if (raw & padraw::mask(b)) {
            result |= map[b]; // 0 for an input this pad's map has no name for
        }
    }

    physicalState = raw;
    *buttons = result;
    return true;
}

bool gamepadhid::poll(quint32 *buttons, qint64 now) {
    if (!hidAvailable) {
        return false;
    }

    if (!dev) {
        // Opening every HID device on the machine is not something to do at 20 Hz.
        if (now - lastScan < SCAN_INTERVAL_MS) {
            return false;
        }
        lastScan = now;
        if (!open()) {
            return false;
        }
    }

    for (int i = 0; i < MAX_REPORTS_PER_POLL; i++) {
        if (!dev->pending) {
            if (!armRead()) {
                qDebug() << QStringLiteral("gamepadhid: lost") << padName;
                close();
                lastScan = now;
                return false;
            }
            if (!dev->pending) {
                continue; // completed synchronously, state is already updated
            }
        }

        DWORD read = 0;
        if (GetOverlappedResult(dev->handle, &dev->overlapped, &read, FALSE)) {
            dev->pending = false;
            decode(dev->report.constData(), int(read), &state);
            continue;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) {
            break; // nothing new this frame, the normal case for a pad held still
        }
        qDebug() << QStringLiteral("gamepadhid: read failed on") << padName << QStringLiteral("error") << error;
        close();
        lastScan = now;
        return false;
    }

    *buttons = state;
    return true;
}

#else // no HID headers on this toolchain

void gamepadhid::close() {
    padName.clear();
    state = 0;
    physicalState = 0;
}
bool gamepadhid::open() { return false; }
bool gamepadhid::armRead() { return false; }
bool gamepadhid::decode(const char *, int, quint32 *) { return false; }
bool gamepadhid::poll(quint32 *, qint64) { return false; }

#endif // QZ_HAS_HID

#endif // Q_OS_WIN
