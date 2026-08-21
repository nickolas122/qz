#pragma once

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QList>

#include <memory>

#include "Devices/ftmsframes.h"
#include "Devices/simulatedftmsbike.h"
#include "Tools/testsettings.h"
#include "characteristics/characteristicwriteprocessor2ad9.h"
#include "qzsettings.h"

/**
 * Two control loops, one bike: the regression cover for the phantom resistance drops.
 *
 * A training app steers a trainer one of two ways - a target power (FTMS `0x05`, ERG) or a road
 * gradient (`0x11`, simulation). They are alternatives, and the app does not announce the
 * switch: it stops sending one kind of packet and starts sending the other.
 *
 * QZ used to believe both at once. `RequestedPower` is cleared only by a session reset, so a
 * single `0x05` at 19:48:03 of a measured 61-minute ride left `ftmsbike`'s continuous-ERG block
 * chasing 100 W for the rest of it - across 2,507 simulation packets and no further power
 * packets - overwriting the gradient resistance about once a second. Before that packet: zero
 * large resistance drops. After it, in the identical riding mode: 253. The worst was 24 -> 1 on
 * an 8 % climb, held for 2.5 s until the simulation loop wrote 24 again.
 *
 * ## What these tests drive
 *
 * The app's actual bytes, through the actual control point. `CharacteristicWriteProcessor2AD9`
 * is what both the virtual bike and the DIRCON endpoint hand a client's writes to, so a `0x05`
 * or `0x11` here takes the same path it takes from Zwift - and the assertions are on the `0x04`
 * frames `ftmsbike` writes back, which is what the trainer would have received.
 *
 * ## What they deliberately do not assert
 *
 * Exact ERG table outputs. `estimateWattage()` depends on collected samples, so pinning "100 W
 * at 87 rpm is level 4" would fail on unrelated table work while proving nothing about the
 * conflict. Every assertion below is structural - *which loop wrote*, and *whether it moved* -
 * and every level it compares against is read back from the run rather than written down here.
 */
namespace {

class ErgSimConflict : public ::testing::Test {
  protected:
    TestSettings testSettings{"Roberto Viola", "QZ Erg Sim Conflict Test"};
    std::unique_ptr<simulatedFtmsBike> bike;
    /// The training app's end of the FTMS control point.
    std::unique_ptr<CharacteristicWriteProcessor2AD9> app;

    void SetUp() override {
        testSettings.activate();

        testSettings.qsettings.setValue(QZSettings::speed_power_based, false);
        testSettings.qsettings.setValue(QZSettings::heart_ignore_builtin, false);
        testSettings.qsettings.setValue(QZSettings::cadence_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::power_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::heart_rate_belt_name, "Disabled");

        // No virtual device and no DIRCON endpoint: the processor below stands in for both.
        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);
        testSettings.qsettings.setValue(QZSettings::dircon_yes, false);

        // The gradient path only reaches changeResistance() with these two as they were on the
        // ride the log came from. zwift_erg is the *setting* that says "convert gradient to
        // resistance"; it is not the app's live mode, which is the whole point of the bug.
        testSettings.qsettings.setValue(QZSettings::virtualbike_forceresistance, true);
        testSettings.qsettings.setValue(QZSettings::zwift_erg, false);

        // The slew limiter would spread one target over several polls, so a test asserting on
        // levels would be reading the ramp rather than the decision. Zero is off.
        testSettings.qsettings.setValue(QZSettings::resistance_slew_up, 0.0);
        testSettings.qsettings.setValue(QZSettings::resistance_slew_down, 0.0);

        // Every assertion here is on what one poll decided, so the poll has to be the test's.
        // ftmsbike's constructor starts a poll_device_time timer on update() - 200 ms by
        // default - and a test that turns the event loop is then being polled behind its own
        // back. That stays invisible until the assertions are about *writes*: a queued write
        // is held for a 300 ms timeout when nothing answers, so five updates a second feeding
        // a queue that drains three a second backs up without bound, and what the test reads
        // after its own tick is whatever the backlog had got round to. Out of reach of the
        // ride, the interval is the honest way to stop it: the timer is private.
        testSettings.qsettings.setValue(QZSettings::poll_device_time, 60000);

        bike.reset(new simulatedFtmsBike());
        app.reset(new CharacteristicWriteProcessor2AD9(1.0, 4, bike.get(), nullptr));

        burnTheFirstUpdate();
    }

    void TearDown() override {
        app.reset();
        bike.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    /// See TestFtmsFrameHarness: the first update() discards power, deterministically here.
    void burnTheFirstUpdate() {
        bike->tick();
        bike->clearWrites();
    }

    static void turnEventLoop(int ms) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    /// `processWriteQueue()` holds `isWriting` until a 300 ms timeout when nothing answers.
    void settleWrites(int writes = 1) { turnEventLoop(350 * writes + 100); }

    /** @brief What the trainer is reporting, as a 0x2AD2 notification. */
    void reportsCadence(double rpm) {
        bike->notify(0x2AD2, ftmsframes::IndoorBikeData().cadence(rpm).power(0).bytes());
    }

    /** @brief The app asks for a target power: FTMS 0x05, the packet that starts ERG. */
    void appSetsTargetPower(uint16_t watts) {
        QByteArray packet;
        packet.append((char)FTMS_SET_TARGET_POWER);
        packet.append((char)(watts & 0xFF));
        packet.append((char)(watts >> 8));
        QByteArray reply;
        app->writeProcess(0x2AD9, packet, reply);
    }

    /** @brief The app asks for a road gradient: FTMS 0x11, the packet that starts simulation. */
    void appSetsGrade(double percent) {
        const int16_t grade = (int16_t)qRound(percent * 100.0);
        QByteArray packet;
        packet.append((char)FTMS_SET_INDOOR_BIKE_SIMULATION_PARAMS);
        packet.append((char)0x00); // wind speed
        packet.append((char)0x00);
        packet.append((char)(grade & 0xFF));
        packet.append((char)((grade >> 8) & 0xFF));
        packet.append((char)40); // crr, at the value that makes its offset term zero
        packet.append((char)40); // cw
        QByteArray reply;
        app->writeProcess(0x2AD9, packet, reply);
    }

    /**
     * @brief The app writes a resistance level directly: FTMS 0x04. Does not end ERG.
     *
     * The level travels in tenths in a single byte, and `writeProcess()` reads that byte as a
     * signed char - so anything above 12 comes out negative on the other side. Keep test
     * levels inside that range rather than working around it here.
     */
    void appSetsResistanceLevel(int level) {
        Q_ASSERT(level >= 0 && level <= 12);
        QByteArray packet;
        packet.append((char)FTMS_SET_TARGET_RESISTANCE_LEVEL);
        packet.append((char)(level * 10));
        QByteArray reply;
        app->writeProcess(0x2AD9, packet, reply);
    }

    /**
     * @brief Every resistance level QZ has commanded the trainer, in order.
     *
     * A YPBM takes 0x04 with the level in tenths, little-endian - `04 f0 00` is level 24, the
     * frame the log records against "forceResistance 24".
     */
    QList<int> commandedLevels() const {
        QList<int> levels;
        for (const QByteArray &write : bike->writes()) {
            if (write.size() >= 3 && (quint8)write.at(0) == FTMS_SET_TARGET_RESISTANCE_LEVEL)
                levels.append((((quint8)write.at(1)) | (((quint8)write.at(2)) << 8)) / 10);
        }
        return levels;
    }

    int lastCommandedLevel() const {
        const QList<int> levels = commandedLevels();
        return levels.isEmpty() ? -1 : levels.last();
    }

    /// Run one poll of the driver and let everything it decided reach the wire.
    void poll() {
        bike->tick();
        settleWrites(2);
    }
};

// ---------------------------------------------------------------------------------------
// D1 - the power target that outlived its mode
// ---------------------------------------------------------------------------------------

TEST_F(ErgSimConflict, AGradientPacketRetiresTheTargetItReplaces) {
    reportsCadence(90);
    appSetsTargetPower(100);
    EXPECT_GT(bike->lastRequestedPower().value(), 0.0);
    EXPECT_EQ((int)bike->lastControlMode(), (int)bike::control_mode::erg);

    appSetsGrade(7.6);
    EXPECT_EQ(bike->lastRequestedPower().value(), 0.0) << "the power target survived the mode that set it";
    EXPECT_EQ((int)bike->lastControlMode(), (int)bike::control_mode::simulation);
}

TEST_F(ErgSimConflict, ASimModeRideIgnoresAStaleTargetPower) {
    bike->seedErgTable();

    // Period A of the log, 19:19-19:48: a climb, steered by gradient. The app repeats itself
    // about once a second - 2,507 of these against 62 power packets across the whole ride.
    for (int rpm = 85; rpm <= 95; rpm += 2) {
        appSetsGrade(7.6);
        reportsCadence(rpm);
        poll();
    }
    const int gradientLevel = lastCommandedLevel();
    ASSERT_GT(gradientLevel, 1) << "the gradient never reached the trainer at all";

    // 19:48:03. One target power, and never another.
    appSetsTargetPower(100);
    settleWrites(3);
    bike->clearWrites();

    // Period B: the identical riding mode. The only thing that changed is that one packet.
    // The cadence sweep is load-bearing - ergResistanceAccepted() only fires when the table's
    // answer moves, so a constant cadence would let a broken build through.
    for (int rpm = 85; rpm <= 95; rpm += 2) {
        appSetsGrade(7.6);
        reportsCadence(rpm);
        poll();
    }

    const QList<int> levels = commandedLevels();
    ASSERT_FALSE(levels.isEmpty()) << "the gradient stopped reaching the trainer";
    for (int level : levels) {
        EXPECT_EQ(level, gradientLevel)
            << "the gradient level was overwritten mid-climb - commanded: "
            << bike->writesHex().join(", ").toStdString();
    }
}

// ---------------------------------------------------------------------------------------
// D2 - the damping gate's idea of where the bike is
// ---------------------------------------------------------------------------------------

TEST_F(ErgSimConflict, TheErgGateMeasuresAgainstTheBikeNotItself) {
    // A genuine ERG session: a target power, and no gradient anywhere near it.
    bike->seedErgTable();
    reportsCadence(90);
    appSetsTargetPower(200);
    settleWrites(2);
    poll();

    const int ergLevel = lastCommandedLevel();
    ASSERT_GT(ergLevel, 1) << "ERG never commanded a level to measure against";

    // Something else moves the bike. Here it is the app's own 0x04, which - unlike a gradient
    // packet - does not end ERG mode, so both writers stay live. Far enough from the ERG
    // answer that the gate cannot read the difference as a wobble either way.
    const int elsewhere = ergLevel < 7 ? 12 : 2;
    bike->clearWrites();
    appSetsResistanceLevel(elsewhere);
    poll();

    const QList<int> levels = commandedLevels();
    ASSERT_TRUE(levels.contains(elsewhere))
        << "the second writer never reached the trainer - commanded: "
        << bike->writesHex().join(", ").toStdString();

    // Within the same poll, the ERG loop has to notice the bike is no longer where it left
    // it. A gate comparing against its own last write sees "still <ergLevel>, nothing to do"
    // and abandons the trainer at the other writer's level for the rest of the session.
    EXPECT_GT(levels.lastIndexOf(ergLevel), levels.indexOf(elsewhere))
        << "ERG never re-asserted its target after the bike moved under it - commanded: "
        << bike->writesHex().join(", ").toStdString();
}

// ---------------------------------------------------------------------------------------
// The guard: period C of the log, where ERG is real and working
// ---------------------------------------------------------------------------------------

TEST_F(ErgSimConflict, AnActiveErgSessionStillTracksCadence) {
    bike->seedErgTable();
    reportsCadence(100);
    appSetsTargetPower(200);
    settleWrites(2);
    poll();

    const int atHighCadence = lastCommandedLevel();
    ASSERT_GT(atHighCadence, 0);

    bike->clearWrites();

    // Same target, and no further packet from the app - that is the point. Holding 200 W as
    // the cadence falls takes *more* resistance, and the continuous-ERG block is the only
    // thing that can notice. This is the guard against a fix that deletes that block: with no
    // app asking for anything the fallback commands the neutral gear, which is at the bottom
    // of the range, so nothing but ERG can push the level up.
    for (int rpm : {90, 80, 70, 65}) {
        reportsCadence(rpm);
        poll();
    }

    const QList<int> levels = commandedLevels();
    ASSERT_FALSE(levels.isEmpty()) << "nothing was commanded at all once the cadence moved";
    bool climbed = false;
    for (int level : levels)
        climbed = climbed || level > atHighCadence;
    EXPECT_TRUE(climbed) << "resistance never followed the cadence down - commanded: "
                         << bike->writesHex().join(", ").toStdString();
}

} // namespace
