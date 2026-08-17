#ifndef SIMULATEDBIKE_H
#define SIMULATEDBIKE_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QtCore/qtimer.h>

#include "devices/bike.h"
#include "devices/simulatedbike/ridescenario.h"

/**
 * @brief A bike that is not there: plays a ride scenario into QZ with no radio involved.
 *
 * Layer A of docs/fork/VIRTUAL-BIKE.md. Its whole reason to exist is that everything
 * downstream of the first correct packet - the tiles, the gear table, ERG, the DIRCON output,
 * the FIT file - currently needs the trainer powered and in range before it can be looked at
 * even once. With this selected QZ starts, connects to nothing, and rides.
 *
 * ## Named simulatedbike, not virtualbike
 *
 * `virtualbike` is taken: src/virtualdevices/virtualbike.cpp is the FTMS server QZ advertises
 * *towards* Zwift, which is the opposite direction from this. Two things called "virtual bike"
 * in one tree would be a permanent tax on every grep.
 *
 * ## What it deliberately does not do
 *
 * It computes no gear ratio, no ERG conversion, no peloton mapping and no metric bookkeeping
 * of its own. All of that lives in bike/bluetoothdevice and is reached through them - because
 * the moment this class reimplements one of them, the code being exercised stops being the
 * code that ships and the whole exercise turns into theatre. What it does is exactly what a
 * trainer does: report numbers, and move when it is told to.
 *
 * It also proves nothing whatever about parsing a real frame or about the bytes QZ writes
 * back. Those are Layer B and are tested against the real ftmsbike in the test project. A
 * green tile on this device is evidence about the tile, not about the bike.
 */
class simulatedbike : public bike {
    Q_OBJECT
  public:
    /**
     * @param noWriteResistance Passed to the virtual device, as the real drivers do.
     * @param noHeartService Passed to the virtual device, as the real drivers do.
     * @param scenarioPath A .ride file. Falls back to a built-in steady ride if empty or
     *        unreadable - a simulated bike that refuses to start because a path is wrong is
     *        the one failure mode that would make this useless at the moment it is needed.
     */
    simulatedbike(bool noWriteResistance, bool noHeartService, const QString &scenarioPath);

    bool connected() override;
    uint16_t watts() override;
    resistance_t maxResistance() override { return 100; }
    resistance_t resistanceFromPowerRequest(uint16_t power) override;
    double maxGears() override;
    double minGears() override;

    /** @brief The scenario being played, for the headless ride report. */
    const RideScenario &scenario() const { return m_scenario; }
    /** @brief Seconds into the current pass of the scenario. */
    double rideTime() const { return m_rideSeconds; }
    /** @brief True while the scenario is inside a silence window and the bike reports nothing. */
    bool silent() const { return m_silent; }

    /** @brief The ride played when no scenario file is given or the given one will not load. */
    static const char *builtInScenario();

  private:
    void createVirtualBike();
    /** @brief Power converging on an ERG request, the way a trainer's flywheel does. */
    double ergPower(double target, double current, double dtSeconds) const;
    double applyNoise(double watts) const;

    RideScenario m_scenario;
    QTimer *refresh;

    /// Where the ride has got to. Advanced by the measured tick rather than by counting ticks,
    /// so a stalled main thread does not silently slow the ride down and make a 60-second ramp
    /// take ninety.
    double m_rideSeconds = 0;
    bool m_silent = false;

    QDateTime lastRefresh = QDateTime::currentDateTime();
    uint8_t firstStateChanged = 0;
    bool noWriteResistance = false;
    bool noHeartService = false;

    /// Simulated power, kept as a double between ticks: m_watt is rounded on the way in and a
    /// convergence driven from a rounded value crawls.
    double m_simulatedWatts = 0;

  Q_SIGNALS:
    void disconnected();
    void debug(QString string);

  private slots:
    void update();
};

#endif // SIMULATEDBIKE_H
