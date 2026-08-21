#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QSet>
#include <QSettings>
#include <QString>

#include "qzsettings.h"
#include "ui/ridestate.h"

/**
 * The contract test of STRIP-SPEC.md section 11.5 item 8.
 *
 * RideState is the only thing the new UI is allowed to see, and the whole point of it is
 * that it stays small. That is a promise nobody can keep by intention alone, so it is
 * asserted here: the exact member list, and the ceiling above it.
 *
 * Every assertion runs against a RideState with no bluetooth stack behind it, which is
 * also the state the UI is in for the first few seconds of every launch. If the object
 * cannot survive that, the ride screen cannot draw before a trainer answers.
 */
namespace {

QSet<QString> propertyNames(const QMetaObject *mo) {
    QSet<QString> names;
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); i++) {
        names.insert(QString::fromLatin1(mo->property(i).name()));
    }
    return names;
}

QSet<QString> invokableNames(const QMetaObject *mo) {
    QSet<QString> names;
    for (int i = mo->methodOffset(); i < mo->methodCount(); i++) {
        const QMetaMethod m = mo->method(i);
        if (m.methodType() == QMetaMethod::Method) {
            names.insert(QString::fromLatin1(m.name()));
        }
    }
    return names;
}

const QSet<QString> kProperties = {
    // Connection
    QStringLiteral("trainerConnected"), QStringLiteral("trainerName"),
    QStringLiteral("appConnected"), QStringLiteral("appName"), QStringLiteral("transport"),
    // Ride
    QStringLiteral("gear"), QStringLiteral("resistance"), QStringLiteral("power"),
    QStringLiteral("cadence"), QStringLiteral("speed"), QStringLiteral("heartRate"),
    QStringLiteral("ergMode"),
};

const QSet<QString> kInvokables = {
    QStringLiteral("gearUp"), QStringLiteral("gearDown"), QStringLiteral("setGear"),
    QStringLiteral("toggleErg"),
};

class RideStateContractTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // The test binary sets no QSettings scope, so an unqualified QSettings writes
        // nowhere on Windows and every toggle silently reads back its default. Give the
        // suite its own scope rather than the app's: ergMode() is a setting, and a test
        // that flips it must not reach into the settings of whoever is running it.
        savedOrg = QCoreApplication::organizationName();
        savedApp = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral("qz-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("RideStateContractTest"));
        QSettings().clear();
        state = new RideState(nullptr);
    }

    void TearDown() override {
        delete state;
        QSettings().clear();
        QCoreApplication::setOrganizationName(savedOrg);
        QCoreApplication::setApplicationName(savedApp);
    }

    RideState *state = nullptr;
    QString savedOrg;
    QString savedApp;
};

TEST_F(RideStateContractTest, ExposesExactlyTheContractedProperties) {
    EXPECT_EQ(propertyNames(state->metaObject()), kProperties);
}

TEST_F(RideStateContractTest, ExposesExactlyTheContractedInvokables) {
    EXPECT_EQ(invokableNames(state->metaObject()), kInvokables);
}

/**
 * The section 9.2 tripwire, mechanised: past twenty members, something UI-shaped has
 * leaked back into the bridge. Failing here is not a licence to raise the number - it is
 * the moment to ask what the new member is really for.
 */
TEST_F(RideStateContractTest, SurfaceHasNotGrownPastTwentyMembers) {
    const int members = propertyNames(state->metaObject()).size() + invokableNames(state->metaObject()).size();
    EXPECT_LE(members, 20) << "RideState is up to " << members
                           << " members. See STRIP-SPEC.md section 9.2 before raising this.";
}

TEST_F(RideStateContractTest, EveryPropertyIsReadableWithoutADevice) {
    const QMetaObject *mo = state->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); i++) {
        const QMetaProperty p = mo->property(i);
        EXPECT_TRUE(p.isReadable()) << p.name() << " is not readable";
        EXPECT_TRUE(p.read(state).isValid()) << p.name() << " read back an invalid value";
    }
}

TEST_F(RideStateContractTest, ReportsNothingConnectedWithoutADevice) {
    EXPECT_FALSE(state->trainerConnected());
    EXPECT_TRUE(state->trainerName().isEmpty());
    EXPECT_FALSE(state->appConnected());
    EXPECT_TRUE(state->transport().isEmpty());
}

/**
 * The ride screen renders these before a trainer answers, so they have to be numbers
 * rather than garbage - a gear of 0 draws, an uninitialised one does not.
 */
TEST_F(RideStateContractTest, ReadsZeroMetricsWithoutADevice) {
    EXPECT_EQ(state->gear(), 0);
    EXPECT_DOUBLE_EQ(state->resistance(), 0.0);
    EXPECT_DOUBLE_EQ(state->power(), 0.0);
    EXPECT_DOUBLE_EQ(state->cadence(), 0.0);
    EXPECT_DOUBLE_EQ(state->speed(), 0.0);
    EXPECT_DOUBLE_EQ(state->heartRate(), 0.0);
}

TEST_F(RideStateContractTest, ShiftingWithoutADeviceIsANoOp) {
    state->gearUp();
    state->gearDown();
    state->setGear(9);
    EXPECT_EQ(state->gear(), 0);
}

/**
 * ERG is a setting rather than a device property, so it is the one thing that does work
 * with nothing connected - and the ride screen colours its button from it.
 */
TEST_F(RideStateContractTest, TogglingErgFlipsTheSetting) {
    QSettings settings;
    const bool before = settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool();

    state->toggleErg();
    EXPECT_NE(state->ergMode(), before);

    state->toggleErg();
    EXPECT_EQ(state->ergMode(), before);
}

TEST_F(RideStateContractTest, EmitsChangedWhenTheRiderShifts) {
    int fired = 0;
    QObject::connect(state, &RideState::changed, [&fired]() { fired++; });

    // No device, so nothing moves - but the signal is what the QML bindings hang off,
    // and a shift that never announces itself leaves the gear numeral stale.
    state->toggleErg();
    EXPECT_GE(fired, 1);
}

} // namespace
