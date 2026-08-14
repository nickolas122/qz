#include "windowsblebond.h"

#ifdef Q_OS_WIN
// These come first and in this order on purpose. winsock2.h has to precede any
// windows.h, or the winsock 1 declarations it pulls in clash with it - and Qt
// headers can include windows.h themselves, so they cannot go above this block.
#include <winsock2.h>
#include <windows.h>
#include <bluetoothapis.h>

#include <QDebug>
#include <QDesktopServices>
#include <QUrl>
#endif

namespace windowsblebond {

bool removeBond(quint64 address) {
#ifdef Q_OS_WIN
    if (address == 0) {
        qDebug() << QStringLiteral("windowsblebond::removeBond: refusing to remove the null address");
        return false;
    }

    BLUETOOTH_ADDRESS bta;
    ::ZeroMemory(&bta, sizeof(bta));
    bta.ullLong = address;

    const DWORD rc = ::BluetoothRemoveDevice(&bta);
    if (rc == ERROR_SUCCESS) {
        qDebug() << QStringLiteral("windowsblebond::removeBond: removed the pairing record for")
                 << QString::number(address, 16);
        return true;
    }

    // Worth naming, because the caller can only say "it did not work" and the
    // remedy differs by code. ERROR_NOT_FOUND (1168) is the one to expect if this
    // ever fires without an obvious cause: BluetoothRemoveDevice speaks to the
    // classic Bluetooth bond store, and a Bluetooth LE device paired through the
    // Settings app is held as a device association in Windows.Devices.Enumeration
    // instead - which only DeviceInformationPairing::UnpairAsync() can undo.
    qDebug() << QStringLiteral("windowsblebond::removeBond: BluetoothRemoveDevice failed for")
             << QString::number(address, 16) << QStringLiteral("with error") << (uint)rc
             << (rc == ERROR_NOT_FOUND ? QStringLiteral("(ERROR_NOT_FOUND - not in the classic bond store)")
                                       : QString());
    return false;
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
