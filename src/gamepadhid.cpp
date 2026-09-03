#include "gamepadhid.h"

#ifdef Q_OS_WIN

#include "gamepadbuttons.h"

#include <QByteArray>
#include <QDebug>
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

// An eight-position hat, in the order HID numbers them: north, then clockwise.
const quint32 HAT8[8] = {
    padbuttons::DPAD_UP,
    padbuttons::DPAD_UP | padbuttons::DPAD_RIGHT,
    padbuttons::DPAD_RIGHT,
    padbuttons::DPAD_DOWN | padbuttons::DPAD_RIGHT,
    padbuttons::DPAD_DOWN,
    padbuttons::DPAD_DOWN | padbuttons::DPAD_LEFT,
    padbuttons::DPAD_LEFT,
    padbuttons::DPAD_UP | padbuttons::DPAD_LEFT,
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
#endif
};

gamepadhid::gamepadhid() {
#ifdef QZ_HAS_HID
    hidAvailable = resolveHid() != nullptr;
#endif
    if (!hidAvailable) {
        qDebug() << QStringLiteral("gamepadhid: no HID parser available, HID pads are off");
    }
}

gamepadhid::~gamepadhid() { close(); }

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
                    const bool isHat = v.IsRange ? (v.Range.UsageMin <= USAGE_HAT && USAGE_HAT <= v.Range.UsageMax)
                                                 : (v.NotRange.Usage == USAGE_HAT);
                    if (!isHat) {
                        continue;
                    }
                    opened->hasHat = true;
                    opened->hatMin = v.LogicalMin;
                    opened->hatMax = v.LogicalMax;
                    break;
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
        qDebug() << QStringLiteral("gamepadhid: opened") << padName << QStringLiteral("buttons up to")
                 << opened->usages.size() << QStringLiteral("hat") << opened->hasHat;

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

    quint32 result = 0;

    ULONG usageLength = ULONG(dev->usages.size());
    if (usageLength > 0 && hid->GetUsages(HidP_Input, PAGE_BUTTON, 0, dev->usages.data(), &usageLength,
                                          dev->preparsed, const_cast<PCHAR>(report),
                                          ULONG(length)) == HIDP_STATUS_SUCCESS) {
        for (ULONG i = 0; i < usageLength; i++) {
            const int index = int(dev->usages[int(i)]) - 1; // HID numbers buttons from 1
            if (index >= 0 && index < padbuttons::FACE_COUNT) {
                result |= padbuttons::TABLE[index].mask;
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
                    result |= HAT8[(position * 2) % 8]; // a four-way hat: N, E, S, W
                } else if (positions >= 8) {
                    result |= HAT8[position % 8];
                }
            }
        }
    }

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
}
bool gamepadhid::open() { return false; }
bool gamepadhid::armRead() { return false; }
bool gamepadhid::decode(const char *, int, quint32 *) { return false; }
bool gamepadhid::poll(quint32 *, qint64) { return false; }

#endif // QZ_HAS_HID

#endif // Q_OS_WIN
