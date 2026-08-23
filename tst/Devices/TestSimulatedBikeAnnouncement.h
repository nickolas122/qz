#pragma once

#include <gtest/gtest.h>

#include <QBluetoothDeviceInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include "Tools/testsettings.h"
#include "devices/bluetooth.h"
#include "devices/simulatedbike/simulatedbike.h"
#include "qzsettings.h"

namespace {

/**
 * The bug this pins, in full, because it cost an install and a log to find and would be
 * invisible to every other test here.
 *
 * A real device is discovered from a radio callback. That happens once the event loop is
 * running, which is long after main() has built homeform and connected it to the two signals
 * that matter - deviceConnected(), which builds the session and clears the help label, and
 * bluetoothDeviceConnected(), which starts the template managers.
 *
 * The simulated bike is built in the bluetooth constructor instead, roughly a hundred lines
 * of main() before homeform exists. Announcing it there announced it to nobody: both signals
 * were emitted into an empty connection list, deviceConnected() was never emitted at all, and
 * the app came up with a working bike, a live DIRCON endpoint, a full metric stream - and no
 * tiles. The device log said it plainly: 219 metric points, and not one call to
 * homeform::deviceConnected().
 *
 * So the announcement is deferred to a zero timer, and this test is the thing that says so.
 * It asserts what the constructor cannot: that by the time the event loop has turned once,
 * the signals homeform needs have actually been emitted.
 */
class SimulatedBikeAnnouncement : public ::testing::Test {
  protected:
    TestSettings testSettings{"Roberto Viola", "QZ Simulated Bike Test"};

    void SetUp() override {
        // TestSettings does not activate itself, and an inactive one is a silent no-op: the
        // values land in its own file while the code under test goes on reading the default
        // one. The first version of this test failed for exactly that reason and looked like
        // a bug in the fix it was written to prove.
        testSettings.activate();

        // Nothing here may touch a radio or a socket: this runs on a CI box with neither.
        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);
        testSettings.qsettings.setValue(QZSettings::dircon_yes, false);
        testSettings.qsettings.setValue(QZSettings::simulated_bike, true);
        testSettings.qsettings.setValue(QZSettings::simulated_bike_ride, QLatin1String(""));
    }

    /**
     * Turn the event loop until the flag is set, or the deadline passes. Deliberately not
     * QSignalSpy: that lives in Qt's testlib, and this project does not link it - adding a Qt
     * module so one test can count signals would be paid for by every platform's build.
     */
    static void spinUntil(const bool &flag, int ms = 2000) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
        while (!flag && QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
};

TEST_F(SimulatedBikeAnnouncement, AnnouncesTheDeviceOnceTheEventLoopRuns) {
    bluetooth bt(false);
    int deviceConnected = 0;
    int bikeConnected = 0;
    QObject::connect(&bt, &bluetooth::deviceConnected,
                     [&deviceConnected](QBluetoothDeviceInfo) { ++deviceConnected; });
    QObject::connect(&bt, &bluetooth::bluetoothDeviceConnected,
                     [&bikeConnected](bluetoothdevice *) { ++bikeConnected; });

    // The device exists immediately - homeform::deviceConnected() returns early on a null
    // device, so being late with the object would break it just as thoroughly as being early
    // with the signal.
    ASSERT_NE(nullptr, bt.device());
    EXPECT_NE(nullptr, dynamic_cast<simulatedbike *>(bt.device()));

    // ...but nothing has been announced yet, which is the whole point: a listener connected
    // after this constructor returns must still hear about it.
    EXPECT_EQ(0, deviceConnected);
    EXPECT_EQ(0, bikeConnected);

    bool announced = false;
    QObject::connect(&bt, &bluetooth::deviceConnected, [&announced](QBluetoothDeviceInfo) { announced = true; });
    spinUntil(announced);

    EXPECT_EQ(1, deviceConnected) << "homeform would never build the session";
    EXPECT_EQ(1, bikeConnected) << "the template managers would never start";
}

TEST_F(SimulatedBikeAnnouncement, TheAnnouncedDeviceCarriesAUsableName) {
    bluetooth bt(false);
    bool announced = false;
    QBluetoothDeviceInfo info;
    QObject::connect(&bt, &bluetooth::deviceConnected, [&](QBluetoothDeviceInfo i) {
        info = i;
        announced = true;
    });
    spinUntil(announced);

    ASSERT_TRUE(announced);
    // homeform passes this to deviceFound(), so an empty name is a blank device label.
    EXPECT_FALSE(info.name().isEmpty());
    EXPECT_TRUE(info.isValid());
}

TEST_F(SimulatedBikeAnnouncement, NoSimulatedBikeWhenTheSettingIsOff) {
    testSettings.qsettings.setValue(QZSettings::simulated_bike, false);

    // startDiscovery false, so this builds nothing and starts no scan - the point is only
    // that the simulated bike is not conjured up when it was not asked for.
    bluetooth bt(true, QLatin1String(""), false, false, 200, false, false, 4, 1.0, false);
    EXPECT_EQ(nullptr, bt.device());
}

} // namespace
