#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>

#include <memory>

#include "Devices/DirconTestClient.h"
#include "Tools/testsettings.h"
#include "devices/dircon/dirconmanager.h"
#include "devices/simulatedbike/simulatedbike.h"
#include "qzsettings.h"

#ifndef QZ_RIDE_FIXTURES
#error "QZ_RIDE_FIXTURES must name the directory holding the .ride fixtures"
#endif

/**
 * Layer C of docs/fork/VIRTUAL-BIKE.md, phase 3: the asserting loop, and the endgoal of the
 * whole document.
 *
 * Phase 2 proved a client can connect and be told what is on offer. This proves the numbers
 * that come back are the ride: scenario file -> `simulatedbike` -> `bike`/`bluetoothdevice`
 * metrics -> `CharacteristicNotifier2AD2` -> DIRCON -> a fake training app, with no radio
 * and no hardware anywhere in the path. Then it steers: a target power written to 0x2AD9
 * has to change what comes back.
 *
 * ## These tests take real seconds, on purpose
 *
 * `simulatedbike` advances its ride by measured wall-clock time - deliberately, so that a
 * stalled main thread cannot silently make a sixty-second ramp take ninety. Nothing here can
 * skip ahead, so a test that needs the bike to reach `t=21` waits twenty-one seconds. That
 * is why `coast` and `dropout` are one test each rather than several: the wait is paid once.
 * Roughly a minute in total, which is nothing against the build that precedes it in CI.
 *
 * A clock seam in `simulatedbike` would remove the wait and would also remove the thing
 * being tested - that the ride advances in real time is half of what makes the stream
 * realistic. If this ever becomes intolerable, the answer is a shorter fixture, not a fake
 * clock.
 *
 * ## What "a dropout" turns out to look like
 *
 * The plan said `dropout.ride` should produce "a real gap rather than stale numbers". It
 * cannot, and the reason is structural rather than a bug: `DirconManager::bikeProvider()`
 * notifies on its own timer and sends whatever the metrics currently hold, so QZ pushes a
 * frame every tick whether or not the bike said anything. A silent bike therefore shows up
 * as **the same frame repeating, byte for byte** - which is still a sharp signal, and is the
 * one this file asserts. See `DropoutFreezesTheStreamAndResumesWithoutInterpolating`.
 */
namespace {

using namespace dircontest;

const char *const kSteadyRide = QZ_RIDE_FIXTURES "/steady.ride";
const char *const kCoastRide = QZ_RIDE_FIXTURES "/coast.ride";
const char *const kDropoutRide = QZ_RIDE_FIXTURES "/dropout.ride";
const char *const kErgHoldRide = QZ_RIDE_FIXTURES "/erg-hold.ride";

/// FTMS control point opcodes, from the enum in ftmsbike.h. Spelled out rather than included
/// so that renaming one in the product does not silently retarget the test.
const char kFtmsRequestControl = 0x00;
const char kFtmsSetTargetPower = 0x05;

class DirconRideLoop : public ::testing::Test {
  protected:
    TestSettings testSettings{"Roberto Viola", "QZ Dircon Ride Loop Test"};
    std::unique_ptr<simulatedbike> m_bike;

    void SetUp() override {
        testSettings.activate();

        testSettings.qsettings.setValue(QZSettings::dircon_yes, true);
        testSettings.qsettings.setValue(QZSettings::dircon_server_base_port, kBasePort);
        testSettings.qsettings.setValue(QZSettings::rouvy_compatibility, false);
        testSettings.qsettings.setValue(QZSettings::zwift_play_emulator, false);
        testSettings.qsettings.setValue(QZSettings::wahoo_rgt_dircon, false);

        // 1000ms between notifications rather than 50. The slow tick is the shipped default
        // and it is also what makes a ten-second freeze ten frames instead of two hundred.
        testSettings.qsettings.setValue(QZSettings::race_mode, false);

        // The scenario states heart rate and the stream has to carry it: with this on, the
        // device drops the file's value and the dropout assertion loses the field that moves
        // most obviously.
        testSettings.qsettings.setValue(QZSettings::heart_ignore_builtin, false);

        // ERG has to reach the bike unfiltered. A power offset would shift every assertion
        // below by a constant and look like a conversion bug.
        testSettings.qsettings.setValue(QZSettings::bike_power_offset, 0);

        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);

        DirconManager::releaseShared();
    }

    void TearDown() override {
        DirconManager::releaseShared();
        m_bike.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    /// A bike playing @p ride with the endpoint bound to it, and a client subscribed to
    /// Indoor Bike Data - the state every test here starts from.
    void ride(const char *scenario, FakeTrainingApp &app) {
        m_bike.reset(new simulatedbike(false, false, QString::fromUtf8(scenario)));
        DirconManager::shared(m_bike.get(), 4, 1.0);
        settle();

        ASSERT_TRUE(app.connectTo(kBikePort)) << "nothing is listening on the bike endpoint";
        ASSERT_EQ(std::string("01" "05" "01" "00" "0010"
                              "00002ad200001000800000805f9b34fb"),
                  hexOf(app.subscribe(0x2AD2)));
    }

    static void settle(int ms = 200) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    /// Turn the event loop until @p predicate holds, or give up. Returns whether it held.
    template <typename Predicate> static bool spinUntil(Predicate predicate, int ms) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            if (predicate())
                return true;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return predicate();
    }
};

// ---------------------------------------------------------------------------------------
// The ride on the wire
// ---------------------------------------------------------------------------------------

TEST_F(DirconRideLoop, SteadyRideArrivesOnTheWire) {
    FakeTrainingApp app;
    ASSERT_NO_FATAL_FAILURE(ride(kSteadyRide, app));

    const QList<IndoorBikeData> frames = app.watch(0x2AD2, 3000);
    ASSERT_FALSE(frames.isEmpty()) << "the endpoint never notified: the loop is not closed";

    // steady.ride states 200 W at 90 rpm for its whole two minutes and a starting resistance
    // of 12, so every one of these is exact rather than a range. This assertion is the
    // sentence the whole document is for: the file, on the wire, with no trainer.
    for (const IndoorBikeData &f : frames) {
        EXPECT_EQ(0x0264, f.flags) << "speed, cadence, resistance, power and heart rate";
        EXPECT_EQ(200, f.watts);
        EXPECT_DOUBLE_EQ(90.0, f.cadenceRpm);
        EXPECT_EQ(12, f.resistance) << "the file's resistance directive";
        EXPECT_GT(f.speedKmh, 0.0) << "speed is derived from power and must not be zero";

        // The file drifts heart rate 120 -> 145 over two minutes; a few seconds in it is at
        // the bottom of that. Asserting the range rather than a value keeps this from
        // depending on how long the test took to get here.
        EXPECT_GE(f.heart, 118);
        EXPECT_LE(f.heart, 146);
    }
}

TEST_F(DirconRideLoop, TheStreamKeepsComing) {
    FakeTrainingApp app;
    ASSERT_NO_FATAL_FAILURE(ride(kSteadyRide, app));

    // One notification a second, so three seconds is at least two. A notifier that sent one
    // frame and stopped would pass every assertion above and be useless.
    const QList<IndoorBikeData> frames = app.watch(0x2AD2, 3200);
    EXPECT_GE(frames.size(), 2) << "only " << frames.size() << " frames in 3.2 s";
}

TEST_F(DirconRideLoop, CoastDrivesTheRiderToZeroWithoutGoingStale) {
    FakeTrainingApp app;
    ASSERT_NO_FATAL_FAILURE(ride(kCoastRide, app));

    // coast.ride pedals until t=20 and is at a standstill from t=21 to t=35. Waiting on the
    // scenario position rather than on the cadence reaching zero: a freshly constructed bike
    // has not run a tick yet, so its cadence *is* zero, and waiting for that is a wait that
    // ends immediately at the wrong end of the ride.
    ASSERT_TRUE(spinUntil([this] { return m_bike->rideTime() >= 22.0; }, 40000))
        << "the ride never reached the standstill";

    const QList<IndoorBikeData> frames = app.watch(0x2AD2, 4000);
    ASSERT_GE(frames.size(), 3);

    bool heartMoved = false;
    for (const IndoorBikeData &f : frames) {
        // Zero is a value the rider produced, not a missing reading. Every one of these has
        // to be exactly zero: a player that holds the last good cadence here is the bug this
        // scenario exists to catch.
        EXPECT_DOUBLE_EQ(0.0, f.cadenceRpm);
        EXPECT_EQ(0, f.watts) << "no cadence means no power, whatever anyone requested";
        EXPECT_DOUBLE_EQ(0.0, f.speedKmh);
        if (f.heart != frames.first().heart)
            heartMoved = true;
    }

    // ...and the bike is still talking while it reports those zeroes. Heart rate falls from
    // 152 to 138 across the standstill, so a frozen stream would show it stuck. This is the
    // whole distinction between coasting and a dropout, and it is the only thing that
    // separates this test from the one below.
    EXPECT_TRUE(heartMoved) << "the frames were identical: this is a freeze, not a coast";
}

TEST_F(DirconRideLoop, DropoutFreezesTheStreamAndResumesWithoutInterpolating) {
    FakeTrainingApp app;
    ASSERT_NO_FATAL_FAILURE(ride(kDropoutRide, app));

    // dropout.ride goes quiet at t=20 for ten seconds. The rider keeps pedalling through it -
    // the samples either side say so - which is what makes a player that interpolates across
    // the gap look right and be wrong.
    ASSERT_TRUE(spinUntil([this] { return m_bike->silent(); }, 30000))
        << "the scenario never went silent";

    const QList<IndoorBikeData> during = app.watch(0x2AD2, 6000);
    ASSERT_GE(during.size(), 3) << "the endpoint stopped notifying entirely";
    ASSERT_TRUE(m_bike->silent()) << "the silence ended before the frames were collected";

    // Not a gap: QZ pushes on its own timer whatever the metrics hold, so silence is the
    // same frame arriving again and again. Byte for byte, heart rate included - anything
    // moving here is a value being invented while the bike says nothing.
    for (const IndoorBikeData &f : during)
        EXPECT_EQ(hexOf(during.first().body), hexOf(f.body))
            << "the stream moved while the bike was silent";

    const quint16 heldWatts = during.first().watts;

    ASSERT_TRUE(spinUntil([this] { return !m_bike->silent(); }, 20000))
        << "the scenario never came back";

    const QList<IndoorBikeData> after = app.watch(0x2AD2, 3000);
    ASSERT_FALSE(after.isEmpty());

    // The file says 200 W at t=20 and 205 W at t=30, and the ten seconds in between were
    // never ridden. So the stream steps from one to the other rather than arriving somewhere
    // in the middle, and the rider is still pedalling on the far side.
    EXPECT_NE(heldWatts, after.last().watts) << "the numbers stayed stale after the gap";
    EXPECT_GT(after.last().watts, 0) << "the bike did not come back";
    EXPECT_GT(after.last().cadenceRpm, 0.0);
}

// ---------------------------------------------------------------------------------------
// Control: the loop closing the other way
// ---------------------------------------------------------------------------------------

TEST_F(DirconRideLoop, AWrittenTargetPowerIsAcknowledgedAndMovesTheStream) {
    FakeTrainingApp app;
    ASSERT_NO_FATAL_FAILURE(ride(kErgHoldRide, app));

    // Request control first, as every real client does - Rouvy's recorded session opens with
    // exactly this write. The reply is FTMS_RESPONSE_CODE, the opcode echoed, FTMS_SUCCESS,
    // carried back inside the WRITE_CHARACTERISTIC response.
    EXPECT_EQ(std::string("01" "04" "02" "00" "0013"   // 16 + 3
                          "00002ad900001000800000805f9b34fb"
                          "800001"),
              hexOf(app.write(0x2AD9, QByteArray(1, kFtmsRequestControl))));

    const QList<IndoorBikeData> before = app.watch(0x2AD2, 2000);
    ASSERT_FALSE(before.isEmpty());
    // erg-hold.ride states a flat 160 W, so anything above it later came from the request.
    ASSERT_EQ(160, before.last().watts);
    const quint16 restingResistance = before.last().resistance;

    // Set Target Power, 300 W, little-endian.
    QByteArray target;
    target.append(kFtmsSetTargetPower);
    target.append(static_cast<char>(300 & 0xFF));
    target.append(static_cast<char>((300 >> 8) & 0xFF));
    EXPECT_EQ(std::string("01" "04" "03" "00" "0013"
                          "00002ad900001000800000805f9b34fb"
                          "800501"),
              hexOf(app.write(0x2AD9, target)));

    // The bike converges rather than stepping - erg_lag is a second in this scenario - so
    // give it five and then require it to be most of the way there. Asserting the exact
    // number would be asserting the shape of the convergence curve, which is Layer A's
    // business and not the wire's.
    const QList<IndoorBikeData> after = app.watch(0x2AD2, 5000);
    ASSERT_FALSE(after.isEmpty());
    EXPECT_GT(after.last().watts, 280) << "ERG never reached the target: last frame was "
                                       << after.last().watts << " W";
    EXPECT_LE(after.last().watts, 320) << "ERG overshot";

    // ...and the resistance moved with it, through the same erg table the real drivers use.
    EXPECT_NE(restingResistance, after.last().resistance)
        << "the target power never reached the resistance";
}

TEST_F(DirconRideLoop, ControlIsRefusedWhenNoBikeIsAttached) {
    // The endpoint outlives the bike, so a client can hold a connection open across the
    // device going away and then write into it. `CharacteristicWriteProcessor2AD9` refuses
    // rather than dereferencing a device that is not there - a stale pointer here turns a
    // client's power request into a use-after-free, not a stale reading.
    DirconManager::startIdleEndpoint();
    settle();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    QByteArray target;
    target.append(kFtmsSetTargetPower);
    target.append(static_cast<char>(300 & 0xFF));
    target.append(static_cast<char>((300 >> 8) & 0xFF));

    // A refused write produces no frame at all: `processPacket` sets the identifier to
    // DPKT_MSGID_ERROR and `DirconProcessor` sends nothing for it. Silence is the contract,
    // odd as it looks, so the assertion is that nothing comes back.
    EXPECT_EQ(std::string(), hexOf(app.write(0x2AD9, target, 1500)));
}

} // namespace
