#include "ridestate.h"

#include "devices/bike.h"
#include "devices/bluetooth.h"
#include "devices/bluetoothdevice.h"
#include "qzsettings.h"
#include "virtualdevices/virtualbike.h"

#include <QSettings>

RideState::RideState(bluetooth *bl, QObject *parent) : QObject(parent), bluetoothManager(bl) {
    // The bridge has no single "something changed" signal - the metrics are pulled off
    // the device by whoever wants them. One second matches the rate the trainer sends
    // Indoor Bike Data at, so a faster poll would only re-read the same frame.
    connect(&poll, &QTimer::timeout, this, &RideState::changed);
    connect(&poll, &QTimer::timeout, this, &RideState::updateRtssOsd);
    poll.start(1000);

    if (bluetoothManager)
        connect(bluetoothManager, &bluetooth::bluetoothDeviceConnected, this,
                &RideState::restoreGear);
}

void RideState::updateRtssOsd() {
    bluetoothdevice *device = bluetoothManager ? bluetoothManager->device() : nullptr;
    if (!device) {
        rtssOsd.publish(QStringLiteral("QZ: no device"));
        return;
    }

    QSettings settings;
    const bool erg = settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool();

    // Only a bike carries a gear here. homeform also handled ROWING; group G takes the
    // rower, and this fork has one bike.
    QString gearLine;
    if (device->deviceType() == BIKE) {
        gearLine = QStringLiteral("Gear: %1\n").arg(static_cast<bike *>(device)->gears());
    }

    rtssOsd.publish(gearLine + QStringLiteral("ERG: %1\nResistance: %2")
                                   .arg(erg ? QStringLiteral("ON") : QStringLiteral("OFF"))
                                   .arg(device->currentResistance().value()));
}

void RideState::restoreGear(bluetoothdevice *device) {
    // bike::setGears persists the gear on every shift, but nothing put it back: the
    // restore half lived in homeform and went with it in 7c-2b, so every launch started
    // at m_gears 0 - below the minimum of 1, which made the first shift look like it did
    // nothing because gears() clamps the reported value up to 1 either way.
    if (!device || device->deviceType() != BIKE)
        return;
    QSettings settings;
    if (!settings.value(QZSettings::gears_restore_value, QZSettings::default_gears_restore_value).toBool() &&
        !settings.value(QZSettings::restore_specific_gear, QZSettings::default_restore_specific_gear).toBool())
        return;
    static_cast<bike *>(device)->setGears(
        settings.value(QZSettings::gears_current_value, QZSettings::default_gears_current_value).toDouble());
    emit changed();
}

bike *RideState::currentBike() const {
    if (!bluetoothManager)
        return nullptr;
    bluetoothdevice *dev = bluetoothManager->device();
    if (!dev || dev->deviceType() != BIKE)
        return nullptr;
    return static_cast<bike *>(dev);
}

bool RideState::trainerConnected() const { return currentBike() != nullptr; }

QString RideState::trainerName() const {
    bike *b = currentBike();
    return b ? b->bluetoothDevice.name() : QString();
}

bool RideState::appConnected() const { return !transport().isEmpty(); }

QString RideState::appName() const { return QString(); }

QString RideState::transport() const {
    bike *b = currentBike();
    if (!b)
        return QString();
    virtualbike *v = b->VirtualBike();
    if (!v || !v->ftmsDeviceConnected())
        return QString();
    // Both paths stamp their own timestamp; whichever is set is the one carrying the
    // ride. They fail differently, which is why the pill names the transport rather
    // than just saying "connected" - see STRIP-SPEC.md section 9.4.
    return v->isDirconFTMS() ? QStringLiteral("DIRCON") : QStringLiteral("BLE");
}

int RideState::gear() const {
    bike *b = currentBike();
    return b ? qRound(b->gears()) : 0;
}

double RideState::resistance() const {
    bike *b = currentBike();
    return b ? b->currentResistance().value() : 0.0;
}

double RideState::power() const {
    bike *b = currentBike();
    return b ? b->wattsMetricforUI() : 0.0;
}

double RideState::cadence() const {
    bike *b = currentBike();
    return b ? b->currentCadence().value() : 0.0;
}

double RideState::speed() const {
    bike *b = currentBike();
    return b ? b->currentSpeed().value() : 0.0;
}

double RideState::heartRate() const {
    bike *b = currentBike();
    return b ? b->currentHeart().value() : 0.0;
}

bool RideState::ergMode() const {
    QSettings settings;
    return settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool();
}

void RideState::gearUp() {
    if (bike *b = currentBike()) {
        b->gearUp();
        emit changed();
    }
}

void RideState::gearDown() {
    if (bike *b = currentBike()) {
        b->gearDown();
        emit changed();
    }
}

void RideState::setGear(int gear) {
    if (bike *b = currentBike()) {
        b->setGears(gear);
        emit changed();
    }
}

bool RideState::autoResistance() const {
    // The device owns this flag, not the UI and not a setting: it is what
    // bike::changeResistance actually gates on. No device means nothing is being
    // driven, which reads as off.
    if (bluetoothManager && bluetoothManager->device())
        return bluetoothManager->device()->autoResistance();
    return false;
}

void RideState::toggleAutoResistance() {
    if (bluetoothdevice *d = (bluetoothManager ? bluetoothManager->device() : nullptr)) {
        d->setAutoResistance(!d->autoResistance());
        emit changed();
    }
}

void RideState::toggleErg() {
    QSettings settings;
    settings.setValue(QZSettings::zwift_erg,
                      !settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool());
    emit changed();
}
