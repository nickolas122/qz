#ifndef RIDESTATE_H
#define RIDESTATE_H

#include <QObject>
#include <QString>
#include <QTimer>

class bluetooth;

/**
 * @brief The whole of what the new UI is allowed to know about a ride.
 *
 * QML must not talk to homeform. This object wraps the bridge core and nothing else:
 * the trainer's connection, the numbers coming off it, and the things a rider does
 * through QZ mid-ride. See STRIP-SPEC.md section 9.2.
 *
 * Keeping the surface small is the point of the exercise. The ceiling is 22 members and
 * TestRideState asserts it. It was 20 until 2026-08-24, and the reason it moved is worth
 * knowing before moving it again: section 9.2's limit exists to keep *tile-rendering
 * plumbing* out of the bridge, and the members that pushed it over are not that - they
 * are bridge facts a rider has to be able to see (what the radio link is doing, how old
 * the numbers are, how much battery the bike has). Two booleans became two state strings
 * rather than being joined by them, one always-empty property was deleted outright, and
 * one clock does the work of two. A member that cannot survive that kind of scrutiny is
 * the leak the test was written to catch.
 */
class RideState : public QObject {
    Q_OBJECT

    // Connection
    /**
     * @brief What the trainer's radio link is doing, as one of a fixed vocabulary:
     * "searching", "connecting", "discovering", "live", "stale", "lost", "gaveup".
     *
     * A string rather than a Q_ENUM because RideState reaches QML as a context property,
     * so an enum would need qmlRegisterUncreatableType and a module import - and section
     * 9.8 is explicit that the Qt 6 import rewriter is the easiest way to break the
     * Android build. TestRideState pins the vocabulary instead.
     *
     * This replaced a bool that was `currentBike() != nullptr`, which never went back
     * down because `bluetooth` never clears the device. See TODO.md, "both status
     * indicators are latches, not state".
     */
    Q_PROPERTY(QString trainerState READ trainerState NOTIFY changed)
    Q_PROPERTY(QString trainerName READ trainerName NOTIFY changed)
    /**
     * @brief The training app's link: "idle", "live", "stale", "past".
     *
     * Replaced a bool built on `lastFTMSFrameReceived != 0`, which latched true on the
     * first frame Zwift ever sent and stayed true through the app quitting and the
     * socket closing. The fix was a staleness test, not a new signal.
     */
    Q_PROPERTY(QString appState READ appState NOTIFY changed)
    /** @brief "BLE", "DIRCON", or empty when no training app has ever driven the bike. */
    Q_PROPERTY(QString transport READ transport NOTIFY changed)
    /** @brief Trainer battery, 0-100, or -1 when the bike does not report one. */
    Q_PROPERTY(int batteryLevel READ batteryLevel NOTIFY changed)
    /** @brief Seconds until the next reconnect attempt, or -1 when none is armed. */
    Q_PROPERTY(int retrySeconds READ retrySeconds NOTIFY changed)
    /**
     * @brief Age of the newest data from the bike, in seconds, or -1 when none has come.
     *
     * Does three jobs, which is why there is one of it rather than three: it marks the
     * metric chips stale, it is how long the bike has been gone while "lost", and it is
     * what "stale" is derived from.
     */
    Q_PROPERTY(int dataAgeSeconds READ dataAgeSeconds NOTIFY changed)

    // Ride
    Q_PROPERTY(int gear READ gear NOTIFY changed)
    Q_PROPERTY(double resistance READ resistance NOTIFY changed)
    /** @brief How many resistance levels this bike has, for the ladder under the gear. */
    Q_PROPERTY(int resistanceLevels READ resistanceLevels NOTIFY changed)
    Q_PROPERTY(double power READ power NOTIFY changed)
    Q_PROPERTY(double cadence READ cadence NOTIFY changed)
    Q_PROPERTY(double speed READ speed NOTIFY changed)
    Q_PROPERTY(double heartRate READ heartRate NOTIFY changed)
    Q_PROPERTY(bool ergMode READ ergMode NOTIFY changed)
    /**
     * @brief Whether QZ is applying the training app's resistance requests.
     *
     * Exposed rather than left to the invokable alone because it can be switched off
     * from outside the UI entirely - the QZWS socket carries the toggle, and ftmsbike
     * clears it outright for some consoles. A rider whose trainer has quietly stopped
     * responding needs somewhere to see why.
     */
    Q_PROPERTY(bool autoResistance READ autoResistance NOTIFY changed)

  public:
    explicit RideState(bluetooth *bl, QObject *parent = nullptr);

    QString trainerState() const;
    QString trainerName() const;
    QString appState() const;
    /** @brief "BLE", "DIRCON", or empty. See the property doc. */
    QString transport() const;
    int batteryLevel() const;
    int retrySeconds() const;
    int dataAgeSeconds() const;

    int gear() const;
    double resistance() const;
    int resistanceLevels() const;
    double power() const;
    double cadence() const;
    double speed() const;
    double heartRate() const;
    bool ergMode() const;
    bool autoResistance() const;

    Q_INVOKABLE void gearUp();
    Q_INVOKABLE void gearDown();
    Q_INVOKABLE void setGear(int gear);
    Q_INVOKABLE void toggleErg();

    /**
     * @brief Abandon the backoff and try the trainer again now.
     *
     * Behind "Retry now" and "Search" on the trainer chip. Resets the five-minute
     * ceiling as well as the delay - see bluetoothdevice::retryNow().
     */
    Q_INVOKABLE void retryNow();

    /** @brief Also a slot: the QZWS `autoResistance` command lands here. */
    Q_INVOKABLE void toggleAutoResistance();

  private slots:
    /**
     * @brief Put the rider's gear back when a bike connects.
     *
     * Not part of the section 9.2 surface - a private slot, invisible to QML and to
     * the contract test, because it is bridge bookkeeping rather than something the
     * UI asks for.
     */
    void restoreGear(class bluetoothdevice *device);

  signals:
    /**
     * @brief One signal for the lot.
     *
     * Every value here is polled off the device on the same tick, so a notify per
     * property would fire them all together and buy nothing but more members.
     */
    void changed();

  private:
    /** Data older than this, on a live link, is no longer presented as current. */
    static constexpr int TRAINER_STALE_MS = 5000;
    /** A training app that has sent nothing for this long is not driving the ride. */
    static constexpr int APP_STALE_MS = 5000;
    /** Past this, it has gone rather than paused - the ride is over, not faulty. */
    static constexpr int APP_GONE_MS = 30000;

    bluetooth *bluetoothManager = nullptr;
    QTimer poll;

    /** @return the connected bike, or nullptr when there is none. */
    class bike *currentBike() const;
};

#endif // RIDESTATE_H
