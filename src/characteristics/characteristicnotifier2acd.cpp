#include "characteristicnotifier2acd.h"

// 0x2ACD is Treadmill Data, and its whole body used to sit inside
// `if (dt == TREADMILL || dt == ELLIPTICAL)`. A bike has always fallen through to
// CN_INVALID here; phase 8 deleted the types that could reach the other side. The
// characteristic itself stays advertised - see dirconmanager.cpp, untouched - so what
// a client sees on the wire is what it saw before.
CharacteristicNotifier2ACD::CharacteristicNotifier2ACD(bluetoothdevice *Bike, QObject *parent)
    : CharacteristicNotifier(0x2acd, Bike, parent) {}

int CharacteristicNotifier2ACD::notify(QByteArray &value) {
    Q_UNUSED(value)
    return CN_INVALID;
}
