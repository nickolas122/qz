#pragma once

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QList>
#include <QMetaObject>

#include "devices/ftmsbike/ftmsbike.h"
#include "devices/ftmsbike/speedracex_defaults.h"

/**
 * The shipped `ftmsbike`, driven from a test: frames in, writes out, no radio.
 *
 * Layer B of docs/fork/VIRTUAL-BIKE.md. This is the *second bike end* of the same loop Layer C
 * asserts on - the point being that the code under test is the driver that talks to the real
 * trainer, not a simulation of it. Everything below is a subclass overriding the four seams
 * `ftmsbike` grew for exactly this purpose; no product behaviour is replaced.
 *
 * - `linkExists()` / `linkState()` say the controller is there and discovered, so `update()`
 *   runs its body instead of returning at the first gate.
 * - `writeTargetReady()` says the target is writable, so `processWriteQueue()` proceeds.
 * - `performWrite()` records the bytes instead of putting them on a wire.
 *
 * ## The write queue has to be let go of
 *
 * `processWriteQueue()` sets `isWriting` before the write and only clears it in
 * `completeCurrentWrite()`, which production reaches either from the device's response or from
 * a 300 ms timeout. A test that writes twice in a row and does not turn the event loop sees
 * only the first: the second is still queued. `settleWrites()` turns the loop long enough for
 * the timeout to fire, which is the honest way to do it - the alternative is reaching into
 * private state and pretending a response arrived.
 */
class simulatedFtmsBike : public ftmsbike {
  public:
    /**
     * @param name What the bike is called. The name-derived profile is applied, so this is
     *        the bike it says it is: the default is the YPBM the fork is built around, and
     *        it arrives with resistance_lvl_mode set, ERG unsupported and 32 levels, exactly
     *        as discovery would have left it.
     */
    explicit simulatedFtmsBike(const QString &name = QStringLiteral("YPBM123456"))
        : ftmsbike(false, false, 4, 1.0) {
        // update() checks this before doing anything, and a default-constructed
        // QBluetoothDeviceInfo is not valid.
        bluetoothDevice = QBluetoothDeviceInfo(QBluetoothAddress(QStringLiteral("11:22:33:44:55:66")),
                                               name, 0);
        // The half of deviceDiscovered() that does not touch the radio. Without it every
        // name-gated flag stays at its default and the object is a bike in general rather
        // than any bike in particular.
        applyDeviceProfile(bluetoothDevice);
    }

    /** @brief Deliver a notification, as the radio would. */
    void notify(quint16 uuid, const QByteArray &frame) {
        handleNotification(QBluetoothUuid(uuid), frame, nullptr);
    }

    /** @brief Deliver a notification on a 128-bit UUID. */
    void notify(const QBluetoothUuid &uuid, const QByteArray &frame) {
        handleNotification(uuid, frame, nullptr);
    }

    /**
     * @brief Run one `update()`.
     *
     * `update()` is a private slot, so this goes through the metaobject rather than calling it
     * - which is also what the refresh timer does, so the path is the production one.
     */
    void tick() { QMetaObject::invokeMethod(this, "update", Qt::DirectConnection); }

    /**
     * @brief The current power.
     *
     * `ftmsbike` narrows `watts()` to private, which its bases declare public - so the call
     * has to go through a base's static type. Access is checked against that, and virtual
     * dispatch still lands on `ftmsbike`'s override, so this reads the real value.
     */
    uint16_t wattsValue() { return static_cast<bike *>(this)->watts(); }

    /** @brief The resistance the app is currently asking for, before gears and limiter. */
    resistance_t requestedResistance() const { return requestResistance; }

    /**
     * @brief Seed the ERG table with a real bike's calibration.
     *
     * An empty table answers 1 for every target at every cadence, so a test about ERG
     * *choosing* a level has nothing to observe. These are the SpeedRaceX defaults - measured
     * points, 9 cadences x 32 levels - which is a resistance-level bike of the same shape as
     * the YPBM.
     *
     * The reset() is not belt and braces. `~ergTable()` writes the table back to QSettings and
     * the constructor reads it, so points collected by an earlier test in the same run - or on
     * this machine last week, since QSettings is the real registry on Windows - arrive here
     * ahead of the defaults, and `loadDefaultData()` skips whenever anything is already there.
     * One junk point is enough to make every answer level 1.
     */
    void seedErgTable() {
        _ergTable.reset();
        _ergTable.loadDefaultData(kSpeedRaceXDefaultErgData);
    }

    /** @brief Every payload `ftmsbike` has tried to write, in order. */
    const QList<QByteArray> &writes() const { return m_writes; }

    QByteArray lastWrite() const { return m_writes.isEmpty() ? QByteArray() : m_writes.last(); }

    void clearWrites() { m_writes.clear(); }

    /// Hex of every write, for a readable assertion failure.
    QStringList writesHex() const {
        QStringList out;
        for (const QByteArray &write : m_writes) {
            out.append(QString::fromLatin1(write.toHex(' ')));
        }
        return out;
    }

  protected:
    bool linkExists() const override { return true; }

    QLowEnergyController::ControllerState linkState() const override {
        return QLowEnergyController::DiscoveredState;
    }

    // The outbound path asks three separate questions, and every one of them ultimately wants
    // a QLowEnergyCharacteristic that a test cannot build. Answering all three is what makes
    // the bytes QZ writes back observable at all.
    bool controlPointReady() const override { return true; }

    bool enqueueTargetValid(QLowEnergyService *service,
                            const QLowEnergyCharacteristic &characteristic) const override {
        Q_UNUSED(service)
        Q_UNUSED(characteristic)
        return true;
    }

    bool writeTargetReady(const WriteRequest &request) const override {
        Q_UNUSED(request)
        return true;
    }

    void performWrite(const WriteRequest &request, const QByteArray &data) override {
        Q_UNUSED(request)
        m_writes.append(data);
    }

  private:
    QList<QByteArray> m_writes;
};
