#include "windowsblebond.h"

#ifdef Q_OS_WIN
// These come first and in this order on purpose. winsock2.h has to precede any
// windows.h, or the winsock 1 declarations it pulls in clash with it - and Qt
// headers can include windows.h themselves, so they cannot go above this block.
#include <winsock2.h>
#include <windows.h>
#include <bluetoothapis.h>

#include <QDesktopServices>
#include <QUrl>
#endif

namespace windowsblebond {

bool removeBond(quint64 address) {
#ifdef Q_OS_WIN
    if (address == 0)
        return false;

    BLUETOOTH_ADDRESS bta;
    ::ZeroMemory(&bta, sizeof(bta));
    bta.ullLong = address;

    return ::BluetoothRemoveDevice(&bta) == ERROR_SUCCESS;
#else
    Q_UNUSED(address)
    return false;
#endif
}

void openPairingSettings() {
#ifdef Q_OS_WIN
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:bluetooth")));
#endif
}

} // namespace windowsblebond
