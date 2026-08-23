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
 * the trainer's connection, the numbers coming off it, and the four things a rider
 * does through QZ mid-ride. See STRIP-SPEC.md section 9.2.
 *
 * Keeping the surface small is the point of the exercise. If it grows past 20 members,
 * something UI-shaped has leaked back into the bridge - and TestRideState asserts that
 * bound rather than leaving it to judgement.
 */
class RideState : public QObject {
    Q_OBJECT

    // Connection
    Q_PROPERTY(bool trainerConnected READ trainerConnected NOTIFY changed)
    Q_PROPERTY(QString trainerName READ trainerName NOTIFY changed)
    Q_PROPERTY(bool appConnected READ appConnected NOTIFY changed)
    Q_PROPERTY(QString appName READ appName NOTIFY changed)
    Q_PROPERTY(QString transport READ transport NOTIFY changed)

    // Ride
    Q_PROPERTY(int gear READ gear NOTIFY changed)
    Q_PROPERTY(double resistance READ resistance NOTIFY changed)
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

    bool trainerConnected() const;
    QString trainerName() const;
    bool appConnected() const;
    /**
     * @brief The training app's name, when the bridge happens to know it.
     *
     * It usually does not. A BLE central never announces who it is, and the DIRCON
     * client's address is known only to DirconProcessor, which does not surface it.
     * Reaching it would mean widening the bridge to satisfy a status pill, which is
     * exactly the trade section 9.2 says not to make. Empty means "connected, name
     * unknown" - the UI shows the transport instead, which is the useful half anyway.
     */
    QString appName() const;
    /** @brief "BLE", "DIRCON", or empty when no training app is driving the bike. */
    QString transport() const;

    int gear() const;
    double resistance() const;
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
     * property would fire them all together and buy nothing but 12 more members.
     */
    void changed();

  private:
    bluetooth *bluetoothManager = nullptr;
    QTimer poll;

    /** @return the connected bike, or nullptr when there is none. */
    class bike *currentBike() const;
};

#endif // RIDESTATE_H
