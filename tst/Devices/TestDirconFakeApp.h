#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>

#include <memory>
#include <string>

#include "Devices/DirconTestClient.h"
#include "Tools/testsettings.h"
#include "devices/dircon/dirconmanager.h"
#include "devices/simulatedbike/simulatedbike.h"
#include "qzsettings.h"

#ifndef QZ_RIDE_FIXTURES
#error "QZ_RIDE_FIXTURES must name the directory holding the .ride fixtures"
#endif

/**
 * Layer C of docs/fork/VIRTUAL-BIKE.md, phase 2: the fake training app connects and
 * enumerates.
 *
 * A real TCP client talking to a real DirconManager built on a real simulatedbike, all
 * inside this binary. Nothing here touches a radio: the BLE half of the virtual device is
 * never built at all, because the manager is constructed directly rather than through
 * `virtualbike`, so there is no advertisement to fail politely.
 *
 * ## Why the expected bytes are literals
 *
 * The client is in `DirconTestClient.h` and shares no code with `DirconPacket`; that
 * header explains why and where the captures came from. Every assertion here is a complete
 * frame written out as hex, and the only thing this file derives is the 128-bit spelling of
 * a 16-bit UUID on the *request* side, where being wrong shows up as a failed exchange
 * rather than a false pass.
 *
 * The request sequence below is the one Rouvy actually sends, read off a recorded session:
 * discover services, discover the characteristics of 0x1826 and 0x1816, read 0x2ACC /
 * 0x2AD6 / 0x2A5C, subscribe to 0x2AD2 and 0x2A5B, then write 0x2AD9.
 *
 * ## What is deliberately not here
 *
 * The notification stream and the control path are phase 3, and mDNS discovery is phase 4
 * with an independent implementation. The endpoint does advertise while these run - it is
 * built the way the app builds it - but nothing here asserts on what goes out.
 */
namespace {

using namespace dircontest;

/// The .ride the bike plays unless a test says otherwise. Its numbers do not matter to
/// phase 2; a bike has to be playing something for the endpoint to have a device at all.
const char *const kDefaultRide = QZ_RIDE_FIXTURES "/steady.ride";

class DirconFakeApp : public ::testing::Test {
  protected:
    TestSettings testSettings{"Roberto Viola", "QZ Dircon Fake App Test"};
    std::unique_ptr<simulatedbike> m_bike;

    void SetUp() override {
        // An inactive TestSettings is a silent no-op - the values land in its own file
        // while the code under test reads the real one.
        testSettings.activate();

        testSettings.qsettings.setValue(QZSettings::dircon_yes, true);
        testSettings.qsettings.setValue(QZSettings::dircon_server_base_port, kBasePort);

        // Every setting the advertised service and characteristic set depends on is
        // written out rather than left to the default, because the assertions below are
        // exact byte strings: a default that moves would look like a protocol regression.
        testSettings.qsettings.setValue(QZSettings::rouvy_compatibility, false);
        testSettings.qsettings.setValue(QZSettings::zwift_play_emulator, false);
        testSettings.qsettings.setValue(QZSettings::bike_wheel_revs, false);
        testSettings.qsettings.setValue(QZSettings::wahoo_rgt_dircon, false);
        testSettings.qsettings.setValue(QZSettings::race_mode, false);

        // No BLE anywhere: the manager is built directly, so no virtualbike exists to try
        // to advertise, and the simulated bike must not build one behind our back either.
        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);

        // The endpoint has process lifetime, so a previous test's is still up unless it is
        // taken down here as well as in TearDown.
        DirconManager::releaseShared();
    }

    void TearDown() override {
        // Manager first: its notifiers and write processors hold the device pointer.
        DirconManager::releaseShared();
        m_bike.reset();
        // deleteLater() on the accepted sockets has to run before the next test binds the
        // same ports.
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    /// A bike playing @p ride, with the endpoint bound to it - the ordinary case.
    void startEndpointWithBike(const char *ride = kDefaultRide) {
        m_bike.reset(new simulatedbike(false, false, QString::fromUtf8(ride)));
        DirconManager::shared(m_bike.get(), 4, 1.0);
        settle();
    }

    /// Turn the event loop enough times for the listener to be accepting.
    static void settle(int ms = 200) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
};

// ---------------------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------------------

TEST_F(DirconFakeApp, ConnectsAndEnumeratesTheServices) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort)) << "nothing is listening on the bike endpoint";

    // Fitness Machine, Cycling Power, Cycling Speed and Cadence - in that order, each as a
    // full 128-bit UUID. Heart rate is not here; it is its own endpoint, below.
    EXPECT_EQ(std::string("01"     // message version
                          "01"     // DISCOVER_SERVICES
                          "01"     // sequence number, echoed from the request
                          "00"     // SUCCESS_REQUEST
                          "0030"   // 3 x 16 bytes
                          "0000182600001000800000805f9b34fb"   // 0x1826 Fitness Machine
                          "0000181800001000800000805f9b34fb"   // 0x1818 Cycling Power
                          "0000181600001000800000805f9b34fb"), // 0x1816 Speed and Cadence
              hexOf(app.exchange(0x01)));
}

TEST_F(DirconFakeApp, EnumeratesTheFitnessMachineCharacteristics) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // Each characteristic is its UUID followed by one property byte:
    // READ 0x01, WRITE 0x02, NOTIFY 0x04, INDICATE 0x08.
    EXPECT_EQ(std::string("01"     // message version
                          "02"     // DISCOVER_CHARACTERISTICS
                          "01"     // sequence number
                          "00"     // SUCCESS_REQUEST
                          "0076"   // 16 + 6 x 17
                          "0000182600001000800000805f9b34fb"     // the service asked about
                          "00002acc00001000800000805f9b34fb" "01"  // Fitness Machine Feature, read
                          "00002ad600001000800000805f9b34fb" "01"  // Supported Resistance Level Range, read
                          "00002ad900001000800000805f9b34fb" "0a"  // Control Point, write + indicate
                          "0000e00500001000800000805f9b34fb" "02"  // Wahoo extension, write
                          "00002ad200001000800000805f9b34fb" "04"  // Indoor Bike Data, notify
                          "00002ad300001000800000805f9b34fb" "01"),// Training Status, read
              hexOf(app.exchange(0x02, wireUuid(0x1826))));
}

TEST_F(DirconFakeApp, EnumeratesTheCyclingPowerCharacteristics) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    EXPECT_EQ(std::string("01" "02" "01" "00" "0043"   // 16 + 3 x 17
                          "0000181800001000800000805f9b34fb"
                          "00002a6500001000800000805f9b34fb" "01"  // Cycling Power Feature, read
                          "00002a5d00001000800000805f9b34fb" "01"  // Sensor Location, read
                          "00002a6300001000800000805f9b34fb" "04"),// Cycling Power Measurement, notify
              hexOf(app.exchange(0x02, wireUuid(0x1818))));
}

TEST_F(DirconFakeApp, EnumeratesTheSpeedAndCadenceCharacteristics) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    EXPECT_EQ(std::string("01" "02" "01" "00" "0054"   // 16 + 4 x 17
                          "0000181600001000800000805f9b34fb"
                          "00002a5c00001000800000805f9b34fb" "01"  // CSC Feature, read
                          "00002a5d00001000800000805f9b34fb" "01"  // Sensor Location, read
                          "00002a5b00001000800000805f9b34fb" "04"  // CSC Measurement, notify
                          "00002a5500001000800000805f9b34fb" "02"),// SC Control Point, write
              hexOf(app.exchange(0x02, wireUuid(0x1816))));
}

TEST_F(DirconFakeApp, TheHeartRateEndpointServesOnlyHeartRate) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kHeartPort)) << "the second endpoint did not come up";

    EXPECT_EQ(std::string("01" "01" "01" "00" "0010"
                          "0000180d00001000800000805f9b34fb"),
              hexOf(app.exchange(0x01)));

    EXPECT_EQ(std::string("01" "02" "02" "00" "0021"   // 16 + 1 x 17
                          "0000180d00001000800000805f9b34fb"
                          "00002a3700001000800000805f9b34fb" "04"),// Heart Rate Measurement, notify
              hexOf(app.exchange(0x02, wireUuid(0x180D))));
}

// ---------------------------------------------------------------------------------------
// Reads
// ---------------------------------------------------------------------------------------

TEST_F(DirconFakeApp, ReadsTheFitnessMachineFeature) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // The word that cost MyWhoosh a session. It decides what the trainer can report, and
    // the low half - 0x5483, little-endian in the first two bytes - has to carry bit 14,
    // power measurement. Without it a client that trusts this field saw 0 W all ride even
    // though 0x2AD2 has always carried power. Rouvy ignores the field and never noticed.
    // See FORK.md.
    EXPECT_EQ(std::string("01" "03" "01" "00" "0018"   // READ_CHARACTERISTIC, 16 + 8
                          "00002acc00001000800000805f9b34fb"
                          "835400000ce00000"),
              hexOf(app.exchange(0x03, wireUuid(0x2ACC))));
}

TEST_F(DirconFakeApp, ReadsTheOtherReadableCharacteristics) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // Supported Resistance Level Range: 1.0 to 15.0 in steps of 1.0, in 0.1 units.
    EXPECT_EQ(std::string("01" "03" "01" "00" "0016"
                          "00002ad600001000800000805f9b34fb"
                          "0a0096000a00"),
              hexOf(app.exchange(0x03, wireUuid(0x2AD6))));

    // Training Status: no string, status "idle".
    EXPECT_EQ(std::string("01" "03" "02" "00" "0012"
                          "00002ad300001000800000805f9b34fb"
                          "0001"),
              hexOf(app.exchange(0x03, wireUuid(0x2AD3))));

    // Cycling Power Feature: crank revolution data supported.
    EXPECT_EQ(std::string("01" "03" "03" "00" "0014"
                          "00002a6500001000800000805f9b34fb"
                          "08000000"),
              hexOf(app.exchange(0x03, wireUuid(0x2A65))));

    // Sensor Location: 0x0d, "rear hub".
    EXPECT_EQ(std::string("01" "03" "04" "00" "0011"
                          "00002a5d00001000800000805f9b34fb"
                          "0d"),
              hexOf(app.exchange(0x03, wireUuid(0x2A5D))));

    // CSC Feature: crank revolution data only. With bike_wheel_revs on this reads 0x0003
    // instead, which is why the setting is pinned in SetUp().
    EXPECT_EQ(std::string("01" "03" "05" "00" "0012"
                          "00002a5c00001000800000805f9b34fb"
                          "0200"),
              hexOf(app.exchange(0x03, wireUuid(0x2A5C))));
}

// ---------------------------------------------------------------------------------------
// The answers that are refusals
// ---------------------------------------------------------------------------------------

TEST_F(DirconFakeApp, AnswersServiceNotFoundForAServiceItDoesNotHave) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // Response code 0x03, and a refusal carries no body at all - not even the UUID that
    // was asked about.
    EXPECT_EQ(std::string("01" "02" "01" "03" "0000"), hexOf(app.exchange(0x02, wireUuid(0x1234))));
}

TEST_F(DirconFakeApp, AnswersCharacteristicNotFoundForOneItDoesNotHave) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    EXPECT_EQ(std::string("01" "03" "01" "04" "0000"), hexOf(app.exchange(0x03, wireUuid(0x2A99))));
}

TEST_F(DirconFakeApp, RefusesToReadANotifyOnlyCharacteristic) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // 0x05, operation not supported - 0x2AD2 is notify-only. A client that reads it
    // instead of subscribing has to be told so rather than handed a placeholder frame:
    // handing one out on connect is exactly what cost MyWhoosh its power source.
    EXPECT_EQ(std::string("01" "03" "01" "05" "0000"), hexOf(app.exchange(0x03, wireUuid(0x2AD2))));
}

TEST_F(DirconFakeApp, AcknowledgesANotificationSubscription) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // The acknowledgement echoes the UUID and drops the on/off byte the request carried.
    EXPECT_EQ(std::string("01" "05" "01" "00" "0010"
                          "00002ad200001000800000805f9b34fb"),
              hexOf(app.exchange(0x05, wireUuid(0x2AD2) + QByteArray(1, 0x01))));
}

TEST_F(DirconFakeApp, AnswersTheUndocumentedMessage07) {
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // Identifier 0x07 has no name in the DIRCON description and no payload either way.
    // Answering it is not optional: a client that sends it and gets silence back stalls.
    EXPECT_EQ(std::string("01" "07" "01" "00" "0000"), hexOf(app.exchange(0x07)));
}

// ---------------------------------------------------------------------------------------
// Lifetime - the change that gave the endpoint a life of its own, protected by nothing
// until now
// ---------------------------------------------------------------------------------------

TEST_F(DirconFakeApp, TheEndpointAnswersBeforeAnyBikeExists) {
    // No device anywhere. A client that caches discovery results and does not retry a
    // failed connect - Rouvy does both - has to find someone listening from launch, or it
    // stays broken until its cache is cleared by hand.
    DirconManager::startIdleEndpoint();
    settle();

    ASSERT_NE(nullptr, DirconManager::sharedIfAny());
    EXPECT_EQ(nullptr, DirconManager::sharedIfAny()->device());

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort)) << "the endpoint did not come up without a device";

    // The service set is fixed at construction from the machine type, and with nothing
    // attached that is the bike profile - identical to the one served with a bike bound.
    EXPECT_EQ(std::string("01" "01" "01" "00" "0030"
                          "0000182600001000800000805f9b34fb"
                          "0000181800001000800000805f9b34fb"
                          "0000181600001000800000805f9b34fb"),
              hexOf(app.exchange(0x01)));
}

TEST_F(DirconFakeApp, TheEndpointOutlivesTheBikeAndTheConnectionSurvivesTheRebind) {
    DirconManager::startIdleEndpoint();
    settle();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));
    ASSERT_FALSE(app.exchange(0x01).isEmpty());

    // A bike turns up. Rebinding must be invisible to the client already connected: the
    // listener and the advertisement are not rebuilt, so neither the socket nor the
    // discovery record it came from goes stale.
    m_bike.reset(new simulatedbike(false, false, QString::fromUtf8(kDefaultRide)));
    DirconManager::shared(m_bike.get(), 4, 1.0);
    settle();
    ASSERT_EQ(m_bike.get(), DirconManager::sharedIfAny()->device());

    EXPECT_EQ(std::string("01" "01" "02" "00" "0030"
                          "0000182600001000800000805f9b34fb"
                          "0000181800001000800000805f9b34fb"
                          "0000181600001000800000805f9b34fb"),
              hexOf(app.exchange(0x01)))
        << "the same connection stopped working when a device was bound";

    // ...and it goes away again. The endpoint must not go with it.
    DirconManager::shared(nullptr, 4, 1.0);
    m_bike.reset();
    settle();
    ASSERT_EQ(nullptr, DirconManager::sharedIfAny()->device());

    EXPECT_EQ(std::string("01" "01" "03" "00" "0030"
                          "0000182600001000800000805f9b34fb"
                          "0000181800001000800000805f9b34fb"
                          "0000181600001000800000805f9b34fb"),
              hexOf(app.exchange(0x01)))
        << "the endpoint died with the bike";
}

// ---------------------------------------------------------------------------------------
// The Rouvy profile
// ---------------------------------------------------------------------------------------

TEST_F(DirconFakeApp, RouvyCompatibilityServesEverythingFromOneEndpoint) {
    testSettings.qsettings.setValue(QZSettings::rouvy_compatibility, true);
    startEndpointWithBike();

    FakeTrainingApp app;
    ASSERT_TRUE(app.connectTo(kBikePort));

    // Same three services, same order: this profile changes the advertised name, the MAC
    // and what goes into the mDNS record, not what the endpoint serves.
    EXPECT_EQ(std::string("01" "01" "01" "00" "0030"
                          "0000182600001000800000805f9b34fb"
                          "0000181800001000800000805f9b34fb"
                          "0000181600001000800000805f9b34fb"),
              hexOf(app.exchange(0x01)));

    // The heart rate endpoint is folded away entirely - only the KICKR machine is built -
    // so there is nothing listening one port up.
    FakeTrainingApp heart;
    EXPECT_FALSE(heart.connectTo(kHeartPort, 2000))
        << "the Rouvy profile is meant to be a single endpoint";
}

} // namespace
