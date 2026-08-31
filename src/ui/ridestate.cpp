#include "ridestate.h"

#include "devices/bike.h"
#include "devices/bluetooth.h"
#include "devices/bluetoothdevice.h"
#include "qzsettings.h"
#include "virtualdevices/virtualbike.h"

#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QStringList>

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
    QSettings settings;

    // Off is not "stop writing". RTSS redraws the last text it was handed for as long as
    // the slot stays claimed, so switching the overlay off mid-ride would otherwise leave
    // a frozen gear on top of the training app. release() hands the slot back, and costs
    // nothing on the ticks after the first because there is then nothing mapped.
    if (!settings.value(QZSettings::osd_enabled, QZSettings::default_osd_enabled).toBool()) {
        rtssOsd.release();
        return;
    }

    bluetoothdevice *device = bluetoothManager ? bluetoothManager->device() : nullptr;
    if (!device) {
        rtssOsd.publish(QStringLiteral("QZ: no device"));
        return;
    }

    // A lost trainer displaces everything else. This overlay is the only QZ surface a
    // rider sees while the training app runs exclusive fullscreen (STRIP-SPEC.md 9.7),
    // which makes it the one place a silent five-minute reconnect can be announced to
    // somebody who is actually riding. Gear and resistance are meaningless anyway once
    // the numbers behind them have stopped arriving.
    //
    // Deliberately not one of the switchable lines: the per-line switches choose which
    // numbers are worth screen space, and this is not a number - it is the notice that
    // the numbers have stopped. Turning the whole overlay off is how a rider declines it.
    const QString link = trainerState();
    if (link == QStringLiteral("lost")) {
        const int secs = retrySeconds();
        rtssOsd.publish(QStringLiteral("QZ: TRAINER LOST\nRetrying%1")
                            .arg(secs > 0 ? QStringLiteral(" in %1s").arg(secs) : QStringLiteral("...")));
        return;
    }
    if (link == QStringLiteral("gaveup")) {
        rtssOsd.publish(QStringLiteral("QZ: TRAINER LOST\nGave up after 5 min"));
        return;
    }

    // Built as a list rather than concatenated, so a line that is switched off takes its
    // newline with it and the remaining ones do not end up separated by a blank row.
    QStringList lines;

    // Only a bike carries a gear here. homeform also handled ROWING; group G takes the
    // rower, and this fork has one bike.
    if (device->deviceType() == BIKE &&
        settings.value(QZSettings::osd_line_gear, QZSettings::default_osd_line_gear).toBool()) {
        lines << QStringLiteral("Gear: %1").arg(static_cast<bike *>(device)->gears());
    }

    if (settings.value(QZSettings::osd_line_erg, QZSettings::default_osd_line_erg).toBool()) {
        const bool erg = settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool();
        lines << QStringLiteral("ERG: %1").arg(erg ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }

    if (settings.value(QZSettings::osd_line_resistance, QZSettings::default_osd_line_resistance).toBool()) {
        lines << QStringLiteral("Resistance: %1").arg(device->currentResistance().value());
    }

    // Every line switched off publishes an empty string rather than releasing the slot:
    // the rider still wants the overlay, just silent until something goes wrong, and
    // keeping the slot is what lets the trainer-lost notice appear without re-attaching.
    rtssOsd.publish(lines.join(QStringLiteral("\n")));
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

QString RideState::trainerState() const {
    bike *b = currentBike();
    // No device object at all means discovery has not matched anything yet. QZ is always
    // scanning in that state, so "searching" is the honest word rather than "idle".
    if (!b)
        return QStringLiteral("searching");

    const LinkStatus s = b->linkStatus();
    switch (s.phase) {
    case LinkStatus::Connecting:
        return QStringLiteral("connecting");
    case LinkStatus::Discovering:
        return QStringLiteral("discovering");
    case LinkStatus::Lost:
        return QStringLiteral("lost");
    case LinkStatus::GaveUp:
        return QStringLiteral("gaveup");
    case LinkStatus::Live:
        // A link that is up but silent is its own state. The bike is still there and
        // the reconnect has nothing to do, but the numbers on screen stopped being
        // true - which is exactly the case TODO.md says must never be shown as normal.
        if (s.msSinceLastFrame >= 0 && s.msSinceLastFrame > TRAINER_STALE_MS)
            return QStringLiteral("stale");
        return QStringLiteral("live");
    case LinkStatus::Idle:
    default:
        // A driver that tracks no link of its own. Falling back to "is there an object"
        // is the old latching answer, so say what is certain instead: nothing is known
        // to be receiving yet.
        return QStringLiteral("searching");
    }
}

QString RideState::trainerName() const {
    bike *b = currentBike();
    return b ? b->bluetoothDevice.name() : QString();
}

QString RideState::appState() const {
    bike *b = currentBike();
    if (!b)
        return QStringLiteral("idle");
    virtualbike *v = b->VirtualBike();
    if (!v || !v->ftmsDeviceConnected())
        return QStringLiteral("idle");

    // ftmsDeviceConnected() only says a frame arrived once, ever - neither timestamp is
    // set back to zero when the app quits or the socket closes, which is why the pill
    // used to read "connected" for the life of the process. The timestamp it latched on
    // is the fix: ask how long ago instead of whether.
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - v->whenLastFTMSFrameReceived();
    if (age <= APP_STALE_MS)
        return QStringLiteral("live");
    if (age <= APP_GONE_MS)
        return QStringLiteral("stale");
    // Not a fault. Quitting the training app is how rides end, so this degrades to a
    // past-tense grey rather than an alarm - only the trainer goes red.
    return QStringLiteral("past");
}

QString RideState::transport() const {
    bike *b = currentBike();
    if (!b)
        return QString();
    virtualbike *v = b->VirtualBike();
    // Deliberately not gated on the app still being there: the chip has to be able to
    // say "DIRCON client left", which needs to know it was DIRCON.
    if (!v || !v->ftmsDeviceConnected())
        return QString();
    // Both paths stamp their own timestamp; whichever is set is the one carrying the
    // ride. They fail differently, which is why the chip names the transport rather
    // than just saying "connected" - see STRIP-SPEC.md section 9.4.
    return v->isDirconFTMS() ? QStringLiteral("DIRCON") : QStringLiteral("BLE");
}

int RideState::batteryLevel() const {
    bike *b = currentBike();
    return b ? b->linkStatus().batteryLevel : -1;
}

int RideState::retrySeconds() const {
    bike *b = currentBike();
    if (!b)
        return -1;
    const int ms = b->linkStatus().msToNextAttempt;
    // Rounded up, so a countdown never shows 0 while it is still waiting.
    return ms < 0 ? -1 : (ms + 999) / 1000;
}

int RideState::dataAgeSeconds() const {
    bike *b = currentBike();
    if (!b)
        return -1;
    const qint64 ms = b->linkStatus().msSinceLastFrame;
    return ms < 0 ? -1 : (int)(ms / 1000);
}

int RideState::gear() const {
    bike *b = currentBike();
    return b ? qRound(b->gears()) : 0;
}

double RideState::resistance() const {
    bike *b = currentBike();
    return b ? b->currentResistance().value() : 0.0;
}

int RideState::resistanceLevels() const {
    // Already virtual on bluetoothdevice and already overridden by ftmsbike, which
    // returns the value applyDeviceProfile() set from the device name - 32 for this
    // trainer. Nothing new had to be plumbed for the ladder under the gear.
    bike *b = currentBike();
    return b ? (int)b->maxResistance() : 0;
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

void RideState::retryNow() {
    // One invokable, two remedies, because the chip is one button whose label already
    // changes with the state - "Retry now" while it is still trying, "Search" once it
    // has stopped. Which of the two is possible is a question about the link, not about
    // the UI, so the UI does not get to ask it and the surface stays at 22 members.
    bike *b = currentBike();

    // Nothing has been claimed, or the driver has stopped trying. Reconnecting the
    // existing controller cannot help in either case: there is no controller in the
    // first, and in the second the bike may have come back on a different address or
    // the driver object may itself be wedged. Only another scan reaches those.
    //
    // Deliberately last resort. rescan() deletes the device, and the virtual bike goes
    // with it - see bluetooth::rescan(). After five minutes without a trainer there is
    // no ride left to protect.
    if (!b || b->linkStatus().phase == LinkStatus::GaveUp) {
        if (bluetoothManager) {
            qDebug() << QStringLiteral("RideState::retryNow - rider asked to search again");
            bluetoothManager->rescan();
            // b is dangling from here: rescan() deleted it. Nothing below may touch it.
        }
        emit changed();
        return;
    }

    b->retryNow();
    emit changed();
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
