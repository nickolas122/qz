#include "simulatedbike.h"

#include "homeform.h"
#include "virtualdevices/virtualbike.h"

#include <QDateTime>
#include <QSettings>
#include <math.h>

#include <chrono>

using namespace std::chrono_literals;

/**
 * The fallback ride. Deliberately dull and deliberately short: it exists so that a mistyped
 * path still gives a working bike rather than a dead app, and anyone who sees these exact
 * numbers should recognise that their scenario file did not load.
 */
const char *simulatedbike::builtInScenario() {
    return "mode power\n"
           "resistance 12\n"
           "t=0    watts=150 cadence=85 hr=118\n"
           "t=60   watts=150 cadence=85 hr=132\n";
}

simulatedbike::simulatedbike(bool noWriteResistance, bool noHeartService, const QString &scenarioPath) {
    m_watt.setType(metric::METRIC_WATT, deviceType());
    Speed.setType(metric::METRIC_SPEED);
    this->noWriteResistance = noWriteResistance;
    this->noHeartService = noHeartService;

    std::string error;
    if (!scenarioPath.isEmpty()) {
        m_scenario = RideScenario::load(scenarioPath.toStdString(), &error);
        if (m_scenario.valid())
            qDebug() << QStringLiteral("simulatedbike playing") << scenarioPath
                     << QStringLiteral("duration") << m_scenario.duration() << QStringLiteral("s");
        else
            qDebug() << QStringLiteral("simulatedbike could not load the scenario:")
                     << QString::fromStdString(error) << QStringLiteral("- falling back to the built-in ride");
    }
    if (!m_scenario.valid())
        m_scenario = RideScenario::parse(builtInScenario(), &error);

    if (m_scenario.startResistance() >= 0) {
        Resistance = m_scenario.startResistance();
        m_pelotonResistance = m_scenario.startResistance();
    }

    refresh = new QTimer(this);
    connect(refresh, &QTimer::timeout, this, &simulatedbike::update);
    refresh->start(200ms);
}

double simulatedbike::ergPower(double target, double current, double dtSeconds) const {
    const double lag = m_scenario.ergLag();
    if (lag <= 0 || dtSeconds <= 0)
        return target;

    // First-order convergence: the flywheel gets a fixed fraction of the way there each tick,
    // which is what a trainer changing its magnets actually looks like. A step to the target
    // would be simpler and would hide every ERG oscillation bug there is, which is the whole
    // reason the lag is configurable per scenario rather than fixed here.
    const double alpha = 1.0 - exp(-dtSeconds / lag);
    return current + (target - current) * alpha;
}

double simulatedbike::applyNoise(double watts) const {
    const double n = m_scenario.noise();
    if (n <= 0)
        return watts;
    const double r = (static_cast<double>(rand()) / RAND_MAX) * 2.0 - 1.0; // -1..1
    return watts * (1.0 + n * r);
}

void simulatedbike::update() {
    QSettings settings;
    const QDateTime now = QDateTime::currentDateTime();
    const double dt = fabs(lastRefresh.msecsTo(now)) / 1000.0;
    lastRefresh = now;

    if (!isPaused()) {
        m_rideSeconds += dt;
        // The ride loops. A two-minute scenario that stops dead two minutes in would leave the
        // app looking like the bike had dropped out, which is a different scenario with its own
        // file.
        if (m_scenario.duration() > 0 && m_rideSeconds > m_scenario.duration())
            m_rideSeconds = 0;
    }

    const RidePoint p = m_scenario.at(m_rideSeconds);
    m_silent = p.silent;

    if (!p.silent) {
        // The scenario is the rider. A control request from ERG or a training app moves the
        // resistance, and power follows from it - but only in the modes where the file is not
        // itself asserting the power, because otherwise the two fight and neither is testable.
        const bool controlledPower = requestPower != -1 && autoResistance();

        if (requestResistance != -1 && requestResistance != currentResistance().value()) {
            Resistance = requestResistance;
            m_pelotonResistance = requestResistance;
            emit resistanceRead(Resistance.value());
        } else if (p.resistance.present) {
            Resistance = p.resistance.value;
            m_pelotonResistance = p.resistance.value;
        }

        if (p.cadence.present)
            Cadence = p.cadence.value;

        double rideWatts = m_simulatedWatts;
        if (controlledPower) {
            rideWatts = ergPower(requestPower, m_simulatedWatts, dt);
        } else {
            switch (m_scenario.mode()) {
            case RIDE_MODE_POWER:
                if (p.watts.present)
                    rideWatts = p.watts.value;
                break;
            case RIDE_MODE_RESISTANCE:
                // Power from resistance and cadence, through the same erg table the real
                // drivers use rather than a curve of this class's own.
                rideWatts = _ergTable.estimateWattage(Cadence.value(), Resistance.value());
                break;
            case RIDE_MODE_SPEED:
                if (p.watts.present)
                    rideWatts = p.watts.value;
                break;
            }
        }
        m_simulatedWatts = rideWatts;

        // Coasting is not negotiable: no cadence means no power, whatever anyone requested.
        if (Cadence.value() <= 0)
            m_watt = 0;
        else
            m_watt = applyNoise(rideWatts);

        if (p.speed.present)
            Speed = p.speed.value;
        else if (Cadence.value() <= 0)
            Speed = 0;
        else
            Speed = metric::calculateSpeedFromPower(watts(), Inclination.value(), Speed.value(), dt, speedLimit());

        if (p.hr.present && !settings.value(QZSettings::heart_ignore_builtin,
                                            QZSettings::default_heart_ignore_builtin)
                                 .toBool())
            Heart = p.hr.value;

        if (Cadence.value() > 0) {
            CrankRevs++;
            LastCrankEventTime += (uint16_t)(1024.0 / (((double)(Cadence.value())) / 60.0));
        }

        Distance += ((Speed.value() / 3600.0) * dt);

        const double weight = settings.value(QZSettings::weight, QZSettings::default_weight).toFloat();
        if (watts())
            KCal += ((((0.048 * ((double)watts()) + 1.19) * weight * 3.5) / 200.0) / 60.0) * dt;
    }

    if (requestInclination != -100) {
        Inclination = requestInclination;
        requestInclination = -100;
    }

    update_metrics(false, watts());

    // ******************************************* virtual bike init *************************************
    if (!firstStateChanged && !this->hasVirtualDevice()) {
        if (settings.value(QZSettings::virtual_device_enabled, QZSettings::default_virtual_device_enabled).toBool())
            createVirtualBike();
    }
    if (!firstStateChanged)
        emit connectedAndDiscovered();
    firstStateChanged = 1;
    // ********************************************************************************************************

    if (settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name)
            .toString()
            .startsWith(QStringLiteral("Disabled")))
        update_hr_from_external();
}

void simulatedbike::createVirtualBike() {
    emit debug(QStringLiteral("creating virtual bike interface..."));
    auto virtualBike = new virtualbike(this, noWriteResistance, noHeartService, 4, 1.0);
    connect(virtualBike, &virtualbike::changeInclination, this, &simulatedbike::changeInclination);
    this->setVirtualDevice(virtualBike, VIRTUAL_DEVICE_MODE::PRIMARY);
}

uint16_t simulatedbike::watts() { return m_watt.value(); }

bool simulatedbike::connected() { return true; }

resistance_t simulatedbike::resistanceFromPowerRequest(uint16_t power) {
    return _ergTable.resistanceFromPowerRequest(power, Cadence.value(), maxResistance());
}

double simulatedbike::maxGears() { return 24; }

double simulatedbike::minGears() { return 1; }
