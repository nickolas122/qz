#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>

#include <memory>

#include "Devices/DirconTestClient.h"
#include "Devices/ftmsframes.h"
#include "Devices/simulatedftmsbike.h"
#include "Tools/testsettings.h"
#include "devices/dircon/dirconmanager.h"
#include "qzsettings.h"

/**
 * Layer B of docs/fork/VIRTUAL-BIKE.md: the shipped `ftmsbike` parser, driven byte-exact.
 *
 * Until the seams went in, `ftmsbike::characteristicChanged` - roughly nine hundred lines that
 * turn a 0x2AD2 frame into a speed - had no test at all, in either direction. Nothing
 * exercised the parse, and nothing asserted on the bytes QZ writes back. The reason was one
 * fact: a test cannot build a `QLowEnergyCharacteristic` with a UUID in it, so every branch of
 * the handler missed and the object was unreachable.
 *
 * ## What is worth testing here, and what is not
 *
 * The value is in **field-offset arithmetic**. Every field in Indoor Bike Data is optional and
 * the flags word says which are present, so a reader has to walk the payload adding the right
 * width for each. Get one wrong and a missing Average Speed pushes cadence into the resistance
 * slot - numbers that are plausible, wrong, and silent. `TheSameValuesSurviveADifferentFlagSet`
 * is the test that catches it, and it is the reason the encoder is a builder rather than a
 * literal.
 *
 * Anything whose *decision* depends on elapsed time is deliberately not here. `ftmsbike` reads
 * `QDateTime::currentDateTime()` throughout and there is no clock seam; threading one through
 * that file is a larger and riskier change than every seam in this phase combined. Accumulators
 * are asserted as monotonic rather than to a figure, and time-dependent decisions keep being
 * tested the way this fork already tests them - lifted into a plain class with an injected
 * clock, as the handshake and the slew limiter already are.
 *
 * ## Why the encoder is checked against a real bike
 *
 * An encoder bug and a parser bug that agree cancel out and pass. So the encoder is not
 * validated against our own decoder: it is validated against bytes the trainer actually sent,
 * read out of `tst/fixtures/recorded/ypbm-32min-ride.frames`. If it can reproduce those frames
 * from the values the log says they carried, it agrees with a device rather than with us.
 */
namespace {

using ftmsframes::IndoorBikeData;

/// The two frames the real YPBM trainer alternates, once a second each, copied from the
/// recording. Neither is a complete picture on its own - it splits its data by field.
const char *const kRealLongFrame = "f50100000000000100000000000000ffffff";
const char *const kRealShortFrame = "002a0000003c00000000";

std::string hexOf(const QByteArray &bytes) { return std::string(bytes.toHex(' ').constData()); }

class FtmsFrameHarness : public ::testing::Test {
  protected:
    TestSettings testSettings{"Roberto Viola", "QZ Ftms Frame Harness Test"};
    std::unique_ptr<simulatedFtmsBike> bike;

    void SetUp() override {
        testSettings.activate();

        // Everything the parse depends on, written out rather than left to the default: these
        // assertions are exact numbers, and a default that moves would look like a parser bug.
        testSettings.qsettings.setValue(QZSettings::speed_power_based, false);
        testSettings.qsettings.setValue(QZSettings::heart_ignore_builtin, false);
        testSettings.qsettings.setValue(QZSettings::cadence_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::power_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::heart_rate_belt_name, "Disabled");

        // No virtual device and no DIRCON endpoint: this is the bike end on its own.
        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);
        testSettings.qsettings.setValue(QZSettings::dircon_yes, false);

        bike.reset(new simulatedFtmsBike());
        burnTheFirstUpdate();
    }

    void TearDown() override {
        bike.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    /**
     * Run one `update()` before any test touches the bike, and drop what it wrote.
     *
     * `bluetoothdevice::update_metrics()` guards its body with `_firstUpdate`, which starts
     * true and is cleared at the end of that first call - so the first call falls through to
     * the tail branch, and that branch zeroes `m_watt` without consulting `watt_calc`. Power
     * received before the first update is therefore discarded; nothing else is.
     *
     * Production never notices: the refresh timer has been running for seconds by the time
     * service discovery finishes and the first notification arrives. A fixture that creates
     * the bike and notifies it milliseconds later is racing that timer, and `poll_device_time`
     * defaults to 200 ms - the same figure the DIRCON fixture below was turning the loop for.
     * It won that race on Windows and lost it on a loaded CI runner, where a frame carrying
     * 200 W reached the wire carrying nought.
     *
     * So the first update happens here, deterministically, where there is nothing to lose.
     */
    void burnTheFirstUpdate() {
        bike->tick();
        bike->clearWrites();
    }

    static void turnEventLoop(int ms) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    /**
     * Let the write queue drain. `processWriteQueue()` holds `isWriting` from the moment it
     * writes until `completeCurrentWrite()`, which production reaches from the device's
     * response or from a 300 ms timeout. With nothing answering, the timeout is the only way
     * out, so a test that wants to see two writes has to wait for it.
     */
    void settleWrites(int writes = 1) { turnEventLoop(350 * writes + 100); }

    void notify2AD2(const IndoorBikeData &frame) { bike->notify(0x2AD2, frame.bytes()); }
};

// ---------------------------------------------------------------------------------------
// The encoder, against a real trainer's bytes
// ---------------------------------------------------------------------------------------

TEST_F(FtmsFrameHarness, TheEncoderReproducesTheRealBikesLongFrame) {
    // What the recording says this frame carried: cadence 0, distance 0, resistance 1, power 0,
    // average power 0, and an energy triplet whose per-hour and per-minute fields are the
    // "not available" sentinels. Speed is absent, which is what sets bit 0.
    const IndoorBikeData frame = IndoorBikeData()
                                     .cadence(0)
                                     .distance(0)
                                     .resistance(1)
                                     .power(0)
                                     .avgPower(0)
                                     .energy(0, 0xFFFF, 0xFF);

    EXPECT_EQ(0x01F5, frame.flags());
    EXPECT_EQ(std::string(kRealLongFrame), std::string(frame.bytes().toHex().constData()))
        << "the encoder no longer agrees with the bytes the trainer sent";
}

TEST_F(FtmsFrameHarness, TheEncoderReproducesTheRealBikesShortFrame) {
    // The other half of the pair: speed, heart rate and elapsed time. This one is three bytes
    // longer than its flags account for, and it sets bit 13, which Indoor Bike Data does not
    // define - the field it presumably means is bit 12, Remaining Time. Reproducing the fault
    // is the only way to test that a reader survives it.
    const IndoorBikeData frame = IndoorBikeData()
                                     .speed(0)
                                     .heart(0)
                                     .elapsed(60)
                                     .rawFlagBit(13)
                                     .unflaggedTrailer(QByteArray(3, 0x00));

    EXPECT_EQ(0x2A00, frame.flags());
    EXPECT_EQ(std::string(kRealShortFrame), std::string(frame.bytes().toHex().constData()));
}

// ---------------------------------------------------------------------------------------
// The parse
// ---------------------------------------------------------------------------------------

TEST_F(FtmsFrameHarness, AFrameSetsTheMetricsItCarries) {
    notify2AD2(IndoorBikeData().speed(32.5).cadence(90).resistance(12).power(200).heart(141));

    EXPECT_DOUBLE_EQ(32.5, bike->currentSpeed().value());
    EXPECT_DOUBLE_EQ(90.0, bike->currentCadence().value());
    EXPECT_EQ(200, bike->wattsValue());
    EXPECT_DOUBLE_EQ(12.0, bike->currentResistance().value());
    EXPECT_EQ(141, bike->currentHeart().value());
}

TEST_F(FtmsFrameHarness, TheSameValuesSurviveADifferentFlagSet) {
    // The test this layer exists for. Two frames stating the same five things, one minimal and
    // one with every other optional field present around them. Every value has moved to a
    // different byte offset, and the flags are the only thing that says where. A reader that
    // gets one width wrong reads cadence out of the resistance slot and reports numbers that
    // are plausible, wrong, and silent.
    notify2AD2(IndoorBikeData().speed(32.5).cadence(90).resistance(12).power(200).heart(141));

    const double speed = bike->currentSpeed().value();
    const double cadence = bike->currentCadence().value();
    const uint16_t watts = bike->wattsValue();
    const double resistance = bike->currentResistance().value();
    const uint8_t heart = bike->currentHeart().value();

    notify2AD2(IndoorBikeData()
                   .speed(32.5)
                   .avgSpeed(30.0)
                   .cadence(90)
                   .avgCadence(88)
                   .distance(1234)
                   .resistance(12)
                   .power(200)
                   .avgPower(190)
                   .energy(50, 600, 10)
                   .heart(141)
                   .metabolic(7.5)
                   .elapsed(600)
                   .remaining(300));

    EXPECT_DOUBLE_EQ(speed, bike->currentSpeed().value()) << "speed moved with the flags";
    EXPECT_DOUBLE_EQ(cadence, bike->currentCadence().value()) << "cadence moved with the flags";
    EXPECT_EQ(watts, bike->wattsValue()) << "power moved with the flags";
    EXPECT_DOUBLE_EQ(resistance, bike->currentResistance().value())
        << "resistance moved with the flags";
    EXPECT_EQ(heart, bike->currentHeart().value()) << "heart rate moved with the flags";
}

TEST_F(FtmsFrameHarness, SpeedIsAbsentWhenMoreDataIsSet) {
    // Bit 0 is inverted, which is the one place in this characteristic where a set bit means a
    // field is missing. A reader that treats it like every other flag reads the cadence bytes
    // as a speed and is wrong about everything after it.
    notify2AD2(IndoorBikeData().speed(20.0).cadence(80).power(150));
    const double speedBefore = bike->currentSpeed().value();
    EXPECT_DOUBLE_EQ(20.0, speedBefore);

    // Same cadence and power, no speed field at all.
    notify2AD2(IndoorBikeData().cadence(80).power(150));
    EXPECT_DOUBLE_EQ(80.0, bike->currentCadence().value());
    EXPECT_EQ(150, bike->wattsValue());
}

TEST_F(FtmsFrameHarness, TheRealBikesTwoFramePatternParses) {
    // Replayed from the recording, byte for byte. The pair is what the trainer actually sends,
    // once a second each, and neither frame is a complete reading on its own.
    bike->notify(0x2AD2, QByteArray::fromHex(kRealLongFrame));
    EXPECT_DOUBLE_EQ(1.0, bike->currentResistance().value())
        << "resistance sits at bytes 6-7 of this frame and nowhere else";
    EXPECT_EQ(0, bike->wattsValue());

    bike->notify(0x2AD2, QByteArray::fromHex(kRealShortFrame));
    EXPECT_DOUBLE_EQ(0.0, bike->currentSpeed().value());
    // ...and the three bytes the flags never accounted for changed nothing.
    EXPECT_DOUBLE_EQ(1.0, bike->currentResistance().value());
}

TEST_F(FtmsFrameHarness, AFrameTooShortForItsFlagsIsSurvived) {
    notify2AD2(IndoorBikeData().speed(32.5).cadence(90).resistance(12).power(200).heart(141));
    const double cadence = bike->currentCadence().value();
    const uint16_t watts = bike->wattsValue();

    // A frame whose flags promise heart rate and power but which stops after the cadence
    // field. `ftmsbike.cpp` guards this; the point of the test is that the guard holds rather
    // than reading past the end and reporting whatever was next in memory.
    QByteArray truncated =
        IndoorBikeData().speed(32.5).cadence(90).resistance(12).power(200).heart(141).bytes();
    truncated.truncate(6);
    bike->notify(0x2AD2, truncated);

    EXPECT_DOUBLE_EQ(cadence, bike->currentCadence().value())
        << "a truncated frame overwrote a good reading";
    EXPECT_EQ(watts, bike->wattsValue()) << "a truncated frame overwrote a good reading";
}

TEST_F(FtmsFrameHarness, AFrameWithNothingButFlagsIsSurvived) {
    notify2AD2(IndoorBikeData().speed(32.5).cadence(90).power(200));
    const uint16_t watts = bike->wattsValue();

    bike->notify(0x2AD2, QByteArray::fromHex("0000"));
    bike->notify(0x2AD2, QByteArray());
    bike->notify(0x2AD2, QByteArray::fromHex("64"));

    EXPECT_EQ(watts, bike->wattsValue());
}

TEST_F(FtmsFrameHarness, DistanceAccumulatesRatherThanJumping) {
    // An accumulator, so asserted as monotonic rather than to a figure - there is no clock
    // seam in ftmsbike and a test that pinned the number would be pinning the wall clock.
    notify2AD2(IndoorBikeData().speed(30.0).cadence(90).power(200).distance(0));
    const double first = bike->odometer();

    notify2AD2(IndoorBikeData().speed(30.0).cadence(90).power(200).distance(500));
    const double second = bike->odometer();

    EXPECT_GE(second, first) << "the odometer went backwards";
}

// ---------------------------------------------------------------------------------------
// The bytes QZ writes back
// ---------------------------------------------------------------------------------------

TEST_F(FtmsFrameHarness, ARideCommandReachesTheControlPointAsAWellFormedFtmsFrame) {
    // Let the init handshake finish first: update() drives it, and its request-control and
    // start-simulation writes would otherwise be mixed in with the one under test.
    bike->tick();
    settleWrites(3);
    bike->clearWrites();

    notify2AD2(IndoorBikeData().speed(30.0).cadence(90).power(200).resistance(10));
    bike->changeResistance(14);
    bike->tick();
    settleWrites(2);

    ASSERT_FALSE(bike->writes().isEmpty())
        << "a resistance request produced no control point write at all";

    // Which opcode a ride command turns into is a policy decision, not a parse one: it depends
    // on gears, inclination, the virtual device and half a dozen settings, and in this
    // configuration the request comes out as Set Indoor Bike Simulation Parameters rather than
    // Set Target Resistance. That policy is not what Layer B is for, so what is asserted here
    // is that whatever QZ chose is a *well-formed* FTMS frame of the length its opcode
    // requires - which is the part a byte-level bug would break.
    //
    // The three-byte Set Target Resistance spelling this trainer actually wants
    // (level x 10, 16-bit) is gated on a device-name flag private to ftmsbike and is not
    // reachable from here. See TODO.md.
    for (const QByteArray &write : bike->writes()) {
        ASSERT_FALSE(write.isEmpty());
        switch (write.at(0)) {
        case 0x00: // Request Control
        case 0x01: // Reset
        case 0x07: // Start or Resume
            EXPECT_EQ(1, write.size()) << hexOf(write);
            break;
        case 0x04: // Set Target Resistance Level
            EXPECT_TRUE(write.size() == 2 || write.size() == 3) << hexOf(write);
            break;
        case 0x05: // Set Target Power
            EXPECT_EQ(3, write.size()) << hexOf(write);
            break;
        case 0x11: // Set Indoor Bike Simulation Parameters: wind, grade, crr, cw
            EXPECT_EQ(7, write.size()) << hexOf(write);
            break;
        default:
            ADD_FAILURE() << "unrecognised FTMS opcode in " << hexOf(write);
            break;
        }
    }
}

TEST_F(FtmsFrameHarness, TheInitHandshakeAsksForControlBeforeAnythingElse) {
    // The one test here that wants a bike nothing has touched: the assertion is about the
    // *first* thing QZ says to a trainer, and SetUp's first update has already said it. So
    // this starts again rather than asserting on the second-best write.
    bike.reset(new simulatedFtmsBike());

    bike->tick();
    settleWrites(2);

    ASSERT_FALSE(bike->writes().isEmpty()) << "the handshake wrote nothing at all";
    // Request Control is opcode 0x00 and has to come first: a trainer that has not granted
    // control ignores everything after it.
    EXPECT_EQ(std::string("00"), std::string(bike->writes().first().toHex().constData()))
        << "handshake began with " << bike->writesHex().join(", ").toStdString();
}

// ---------------------------------------------------------------------------------------
// The second bike end of the same loop
// ---------------------------------------------------------------------------------------

/**
 * The whole point of Layer B, and the reason it moved to the end of the plan rather than the
 * middle: the real driver standing where the simulated bike stood.
 *
 * `DirconRideLoop` asserts that a `.ride` file arrives on the DIRCON wire with the right
 * numbers, through `simulatedbike`. That proves the output side and says nothing about the code
 * that talks to a trainer. This runs the same assertion with `ftmsbike` as the bike end, fed
 * byte-exact FTMS frames - so the loop covers the shipped parser, the metric plumbing, the
 * virtual device and the wire in one pass.
 */
class FtmsBikeAsTheBikeEnd : public ::testing::Test {
  protected:
    /// Clear of both the shipped port and Layer C's, so a running QZ and the other suites
    /// cannot collide with this one.
    static const quint16 kBasePort = 47840;

    TestSettings testSettings{"Roberto Viola", "QZ Ftms Bike End Test"};
    std::unique_ptr<simulatedFtmsBike> bike;

    void SetUp() override {
        testSettings.activate();
        testSettings.qsettings.setValue(QZSettings::speed_power_based, false);
        testSettings.qsettings.setValue(QZSettings::heart_ignore_builtin, false);
        testSettings.qsettings.setValue(QZSettings::cadence_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::power_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::heart_rate_belt_name, "Disabled");

        testSettings.qsettings.setValue(QZSettings::dircon_yes, true);
        testSettings.qsettings.setValue(QZSettings::dircon_server_base_port, kBasePort);
        testSettings.qsettings.setValue(QZSettings::rouvy_compatibility, false);
        testSettings.qsettings.setValue(QZSettings::zwift_play_emulator, false);
        testSettings.qsettings.setValue(QZSettings::bike_wheel_revs, false);
        testSettings.qsettings.setValue(QZSettings::wahoo_rgt_dircon, false);
        testSettings.qsettings.setValue(QZSettings::race_mode, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);

        DirconManager::releaseShared();
        bike.reset(new simulatedFtmsBike());

        // Before anything else, and for the reason written out in FtmsFrameHarness above:
        // the first update_metrics() throws away any power the bike already has, so it
        // happens now rather than racing the notification a test is about to send.
        bike->tick();
        bike->clearWrites();

        DirconManager::shared(bike.get(), 4, 1.0);
        turn(200);
    }

    void TearDown() override {
        DirconManager::releaseShared();
        bike.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    static void turn(int ms) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
};

TEST_F(FtmsBikeAsTheBikeEnd, AFrameTheDriverParsedArrivesOnTheDirconWire) {
    dircontest::FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBasePort));
    ASSERT_EQ(std::string("01" "05" "01" "00" "0010"
                          "00002ad200001000800000805f9b34fb"),
              dircontest::hexOf(app.subscribe(0x2AD2)));

    // One frame in, from the trainer's side...
    bike->notify(0x2AD2,
                 IndoorBikeData().speed(32.5).cadence(90).resistance(12).power(200).heart(141).bytes());

    // ...and the same numbers out, on the other side of the whole stack: ftmsbike's parse,
    // bike/bluetoothdevice's metrics, CharacteristicNotifier2AD2, and DIRCON.
    const QList<dircontest::IndoorBikeData> frames = app.watch(0x2AD2, 3000);
    ASSERT_FALSE(frames.isEmpty()) << "nothing reached the wire";

    const dircontest::IndoorBikeData &last = frames.last();
    EXPECT_EQ(0x0264, last.flags);
    EXPECT_EQ(200, last.watts);
    EXPECT_DOUBLE_EQ(90.0, last.cadenceRpm);
    EXPECT_EQ(12, last.resistance);
    EXPECT_EQ(141, last.heart);
    EXPECT_GT(last.speedKmh, 0.0);
}

TEST_F(FtmsBikeAsTheBikeEnd, TheWireFollowsTheFrames) {
    dircontest::FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBasePort));
    ASSERT_FALSE(app.subscribe(0x2AD2).isEmpty());

    bike->notify(0x2AD2, IndoorBikeData().speed(20.0).cadence(80).resistance(8).power(150).bytes());
    const QList<dircontest::IndoorBikeData> first = app.watch(0x2AD2, 2500);
    ASSERT_FALSE(first.isEmpty());
    EXPECT_EQ(150, first.last().watts);

    // A second frame with different numbers has to move the wire, not just the metrics - which
    // is the assertion that a stale notifier or a cached value would fail.
    bike->notify(0x2AD2, IndoorBikeData().speed(35.0).cadence(95).resistance(16).power(280).bytes());
    const QList<dircontest::IndoorBikeData> second = app.watch(0x2AD2, 2500);
    ASSERT_FALSE(second.isEmpty());
    EXPECT_EQ(280, second.last().watts);
    EXPECT_DOUBLE_EQ(95.0, second.last().cadenceRpm);
    EXPECT_EQ(16, second.last().resistance);
}

} // namespace
