#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>

#include <memory>

#include "Devices/ftmsframes.h"
#include "Devices/simulatedftmsbike.h"
#include "Tools/testsettings.h"
#include "qzsettings.h"

/**
 * The link went quiet but nobody hung up.
 *
 * On Windows with Qt 6 the Bluetooth backend is WinRT, and `QLowEnergyController::stateChanged`
 * does not always arrive when the peer goes away. Three sessions on 2026-08-24
 * (`C:\QZ\lite-version`, build `69009d2`) have the peripheral calling `cancelConnection()` and
 * QZ receiving nothing at all - no `UnconnectedState`, no controller error, no service state
 * change. The controller sat in `DiscoveredState` for two minutes while the rider watched
 * numbers that had stopped being true, and the reconnect ladder - which works, and is proven to
 * work by the same day's third log - was never armed. Restarting the app was the only exit.
 *
 * `ftmsbike::update()` now decides for itself: Indoor Bike Data is what a live link is made of,
 * and a Discovered link that has stopped delivering it gets hung up on from our side, which
 * produces the `UnconnectedState` the existing ladder is waiting for. No new retry logic is
 * under test here, because none was written - only the trigger Windows would not supply.
 *
 * ## Why this is a separate file from TestFtmsFrameHarness
 *
 * That file says, correctly, that decisions depending on elapsed time are deliberately not
 * tested there, because `ftmsbike` reads `QDateTime::currentDateTime()` throughout with no
 * clock seam. This decision is the exception that earned one: `msSinceLastFrame()` is a single
 * virtual over a single subtraction, and without it the only way to test a ten-second threshold
 * is to wait ten seconds. It is narrow on purpose - it does not thread a clock through the
 * file, it exposes the one quantity this one decision is made of.
 *
 * The other seam is `closeLink()`. There is no controller behind the harness, so the watchdog's
 * output is counted rather than performed - which is also the honest unit: what is under test
 * is the decision to hang up, not Qt's ability to carry it out.
 */
class FtmsLinkWatchdogTest : public ::testing::Test {
  protected:
    TestSettings testSettings{"Roberto Viola", "QZ Ftms Link Watchdog Test"};
    std::unique_ptr<simulatedFtmsBike> bike;

    void SetUp() override {
        testSettings.activate();
        testSettings.qsettings.setValue(QZSettings::speed_power_based, false);
        testSettings.qsettings.setValue(QZSettings::cadence_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::power_sensor_name, "Disabled");
        testSettings.qsettings.setValue(QZSettings::heart_rate_belt_name, "Disabled");
        // The bike end on its own: no virtual device, no DIRCON endpoint.
        testSettings.qsettings.setValue(QZSettings::virtual_device_enabled, false);
        testSettings.qsettings.setValue(QZSettings::virtual_device_bluetooth, false);
        testSettings.qsettings.setValue(QZSettings::dircon_yes, false);

        bike.reset(new simulatedFtmsBike());
        // See FtmsFrameHarness::burnTheFirstUpdate - the first update_metrics() discards
        // power, and racing the refresh timer for it is how a green suite goes red on CI.
        bike->tick();
        bike->clearWrites();
    }

    void TearDown() override {
        bike.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    /** A frame of the shape the YPBM actually sends, so the link is properly live. */
    void deliverFrame() {
        bike->notify(0x2AD2, ftmsframes::IndoorBikeData()
                                 .speed(30.0)
                                 .cadence(90)
                                 .resistance(16)
                                 .power(200)
                                 .heart(140)
                                 .bytes());
    }
};

// ---------------------------------------------------------------------------------------

/**
 * The case from the logs. A link that has been delivering data stops, the controller goes on
 * claiming DiscoveredState, and nothing external ever says otherwise.
 */
TEST_F(FtmsLinkWatchdogTest, HangsUpOnALinkThatHasStoppedDeliveringData) {
    deliverFrame();
    ASSERT_EQ(0, bike->closeLinkCount()) << "a live link must not be torn down";

    bike->setFrameAge(11000);
    bike->tick();

    EXPECT_EQ(1, bike->closeLinkCount());
}

/** Ten seconds is the budget, so nine is still an ordinary gap between frames. */
TEST_F(FtmsLinkWatchdogTest, LeavesALinkAloneInsideTheBudget) {
    deliverFrame();

    bike->setFrameAge(9000);
    bike->tick();

    EXPECT_EQ(0, bike->closeLinkCount());
}

/**
 * A link that has never delivered gets a longer budget, not an exemption - it is still doing
 * service discovery and the control-point handshake, both of which take about two seconds in
 * practice, so fifteen is generous rather than tight.
 */
TEST_F(FtmsLinkWatchdogTest, GivesALinkThatHasNotYetDeliveredALongerBudget) {
    bike->setFrameAge(11000);
    bike->tick();

    EXPECT_EQ(0, bike->closeLinkCount()) << "11 s is inside the first-frame grace";
}

/**
 * The regression the 2026-08-24 15:40 session found, and the reason the guard is a budget
 * rather than an exemption.
 *
 * The first version of this watchdog disarmed itself entirely until a frame had arrived. A
 * reconnect that landed on a link Windows called `Discovered` and that then delivered nothing
 * was therefore never torn down again: QZ sat in it for 45 seconds until the app was killed,
 * and pressing Play on the peripheral did nothing because QZ believed it was connected. That
 * is the same dead end the watchdog exists to remove, moved one step later.
 */
TEST_F(FtmsLinkWatchdogTest, HangsUpOnAReconnectThatNeverDeliversAnything) {
    // No frame has ever arrived on this link - exactly the state after a reconnect onto a
    // peripheral that has gone away.
    bike->setFrameAge(16000);
    bike->tick();

    EXPECT_EQ(1, bike->closeLinkCount())
        << "a link that is not delivering is not a link, however new it is";
}

/**
 * Writes going unanswered as well removes the doubt, so it need not be waited out. Three is the
 * threshold, and three is exactly what the 2026-08-24 log shows when the rider shifted gears
 * fifty-one seconds into a link that was already dead.
 */
TEST_F(FtmsLinkWatchdogTest, UnacknowledgedWritesShortenTheWait) {
    deliverFrame();

    // Two seconds is well inside the ordinary budget, so nothing happens on its own.
    bike->setFrameAge(2500);
    bike->tick();
    ASSERT_EQ(0, bike->closeLinkCount());

    // Three writes that the bike never answers. The queue's 300 ms timeout is the only way
    // out with nothing responding, which is what makes turning the loop the honest way to
    // produce them.
    bike->setGears(bike->gears() + 1);
    bike->tick();
    for (int i = 0; i < 3; i++) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 400;
        while (QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        bike->setGears(bike->gears() + 1);
        bike->tick();
    }

    EXPECT_GE(bike->closeLinkCount(), 1)
        << "three unanswered writes plus 2.5s of silence is a dead link, not a slow one";
}

/**
 * A frame arriving is the link answering for itself, so whatever the run of write timeouts was
 * suggesting stops being true. Without this, a console that ignores one write while streaming
 * perfectly well would accumulate a run and be torn down mid-ride.
 */
TEST_F(FtmsLinkWatchdogTest, AFrameClearsTheSuspicionRaisedByWrites) {
    deliverFrame();
    bike->setGears(bike->gears() + 1);
    bike->tick();
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 1300;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    // Data is flowing again. The corroborated, shorter budget must no longer apply.
    deliverFrame();
    bike->setFrameAge(2500);
    bike->tick();

    EXPECT_EQ(0, bike->closeLinkCount());
}

/**
 * One teardown per outage, not one per poll. `update()` runs five times a second at the default
 * poll interval, and re-issuing disconnectFromDevice() at that rate while the stack works
 * through the first one is how a fix becomes its own bug.
 */
TEST_F(FtmsLinkWatchdogTest, DoesNotReIssueTheTeardownOnEveryPoll) {
    deliverFrame();
    bike->setFrameAge(11000);

    for (int i = 0; i < 10; i++)
        bike->tick();

    EXPECT_EQ(1, bike->closeLinkCount())
        << "the teardown is rate-limited by stallTeardownAt; ten polls is still one hang-up";
}
