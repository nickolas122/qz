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
    QStringLiteral("trainerState"), QStringLiteral("trainerName"),
    QStringLiteral("appState"), QStringLiteral("transport"),
    QStringLiteral("batteryLevel"), QStringLiteral("retrySeconds"),
    QStringLiteral("dataAgeSeconds"),
    // Ride
    QStringLiteral("gear"), QStringLiteral("resistance"), QStringLiteral("resistanceLevels"),
    QStringLiteral("power"), QStringLiteral("cadence"), QStringLiteral("speed"),
    QStringLiteral("heartRate"), QStringLiteral("rideMode"),
    QStringLiteral("targetPower"), QStringLiteral("autoResistance"),
};

const QSet<QString> kInvokables = {
    QStringLiteral("gearUp"), QStringLiteral("gearDown"), QStringLiteral("setGear"),
    QStringLiteral("setRideMode"), QStringLiteral("nudgeTargetPower"),
    QStringLiteral("toggleAutoResistance"), QStringLiteral("retryNow"),
};

/**
 * The complete vocabularies of the two state properties.
 *
 * They are strings rather than a Q_ENUM because RideState reaches QML as a context
 * property (see the header), which means nothing but this test stops a typo in a state
 * name from silently becoming a state no QML branch matches. So it is pinned here: a new
 * state must be added deliberately, in both places.
 */
const QSet<QString> kTrainerStates = {
    QStringLiteral("searching"), QStringLiteral("connecting"), QStringLiteral("discovering"),
    QStringLiteral("live"),      QStringLiteral("stale"),      QStringLiteral("lost"),
    QStringLiteral("gaveup"),
};

const QSet<QString> kAppStates = {
    QStringLiteral("idle"), QStringLiteral("live"), QStringLiteral("stale"), QStringLiteral("past"),
};

const QSet<QString> kRideModes = {
    QStringLiteral("sim"), QStringLiteral("erg"), QStringLiteral("manual"),
};

class RideStateContractTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // The test binary sets no QSettings scope, so an unqualified QSettings writes
        // nowhere on Windows and every toggle silently reads back its default. Give the
        // suite its own scope rather than the app's: rideMode() is a setting, and a test
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
 * The section 9.2 tripwire, mechanised: past the ceiling, something UI-shaped has leaked
 * back into the bridge. Failing here is not a licence to raise the number - it is the
 * moment to ask what the new member is really for.
 *
 * It was 20 until 2026-08-24, when the connection work moved it to 22, and the argument
 * was this: the five members added were a trainer
 * state, a training-app state, the trainer's battery, its resistance range and the
 * reconnect countdown - every one a fact about the bridge that a rider has to be able to
 * see, and none of them the tile-rendering plumbing the ceiling exists to keep out. It
 * cost 20 rather than 25 because two booleans were *replaced* by the two states rather
 * than joined by them, `appName` was deleted outright (it returned an empty string
 * unconditionally, and the QML branch reading it could never be taken), and one data-age
 * clock does the work that "how stale" and "how long lost" would otherwise have needed
 * two of.
 *
 * It moved to 24 on 2026-10-06 for ERG Manual: a target power the rider sets mid-ride and
 * the 1 W step that sets it. The third mode itself cost nothing - ergMode and toggleErg
 * were replaced by rideMode and setRideMode, not joined by them.
 *
 * The next member to arrive gets the same treatment or it does not go in.
 */
TEST_F(RideStateContractTest, SurfaceHasNotGrownPastTheCeiling) {
    const int members = propertyNames(state->metaObject()).size() + invokableNames(state->metaObject()).size();
    EXPECT_LE(members, 24) << "RideState is up to " << members
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
    // "searching", not "idle": with no device matched, discovery is what QZ is doing.
    EXPECT_EQ(state->trainerState(), QStringLiteral("searching"));
    EXPECT_TRUE(state->trainerName().isEmpty());
    EXPECT_EQ(state->appState(), QStringLiteral("idle"));
    EXPECT_TRUE(state->transport().isEmpty());
}

/**
 * The states are strings, so nothing but this stops a typo becoming a state no QML
 * branch matches - which would render as an empty chip rather than as an error.
 */
TEST_F(RideStateContractTest, StatesComeFromTheContractedVocabularies) {
    EXPECT_TRUE(kTrainerStates.contains(state->trainerState()))
        << "trainerState returned '" << state->trainerState().toStdString() << "'";
    EXPECT_TRUE(kAppStates.contains(state->appState()))
        << "appState returned '" << state->appState().toStdString() << "'";
    EXPECT_TRUE(kRideModes.contains(state->rideMode()))
        << "rideMode returned '" << state->rideMode().toStdString() << "'";
}

/**
 * The three that carry "no answer" as a value rather than as a zero. A battery of 0 and
 * a data age of 0 are both real readings, so none of them may default to one.
 */
TEST_F(RideStateContractTest, UnknownsReadAsMinusOneWithoutADevice) {
    EXPECT_EQ(state->batteryLevel(), -1);
    EXPECT_EQ(state->retrySeconds(), -1);
    EXPECT_EQ(state->dataAgeSeconds(), -1);
    EXPECT_EQ(state->resistanceLevels(), 0);
}

TEST_F(RideStateContractTest, RetryingWithoutADeviceIsANoOp) {
    state->retryNow();
    EXPECT_EQ(state->trainerState(), QStringLiteral("searching"));
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
 * The mode is a setting rather than a device property, so it is one of the things that do
 * work with nothing connected - and the ride screen colours its button from it.
 */
TEST_F(RideStateContractTest, CyclingWalksTheThreeModes) {
    EXPECT_EQ(state->rideMode(), QStringLiteral("sim"));

    state->cycleMode();
    EXPECT_EQ(state->rideMode(), QStringLiteral("erg"));

    state->cycleMode();
    EXPECT_EQ(state->rideMode(), QStringLiteral("manual"));

    state->cycleMode();
    EXPECT_EQ(state->rideMode(), QStringLiteral("sim"));
}

/**
 * zwift_erg is still what the slope and resistance paths read, so it has to be on in both
 * ERG modes and off in simulation - ERG Manual is ERG Auto plus the rider owning the target.
 */
TEST_F(RideStateContractTest, SettingTheModeWritesBothSettings) {
    QSettings settings;

    state->setRideMode(QStringLiteral("manual"));
    EXPECT_TRUE(settings.value(QZSettings::zwift_erg).toBool());
    EXPECT_TRUE(settings.value(QZSettings::erg_manual).toBool());

    state->setRideMode(QStringLiteral("erg"));
    EXPECT_TRUE(settings.value(QZSettings::zwift_erg).toBool());
    EXPECT_FALSE(settings.value(QZSettings::erg_manual).toBool());

    state->setRideMode(QStringLiteral("sim"));
    EXPECT_FALSE(settings.value(QZSettings::zwift_erg).toBool());
    EXPECT_FALSE(settings.value(QZSettings::erg_manual).toBool());

    state->setRideMode(QStringLiteral("bogus"));
    EXPECT_EQ(state->rideMode(), QStringLiteral("sim"));
}

TEST_F(RideStateContractTest, ShiftingInErgManualMovesTheTargetByTen) {
    state->setRideMode(QStringLiteral("manual"));
    const int before = state->targetPower();

    state->gearUp();
    EXPECT_EQ(state->targetPower(), before + 10);

    state->gearDown();
    state->gearDown();
    EXPECT_EQ(state->targetPower(), before - 10);
}

TEST_F(RideStateContractTest, NudgingMovesTheTargetByOneAndStaysInRange) {
    state->setRideMode(QStringLiteral("manual"));
    const int before = state->targetPower();

    state->nudgeTargetPower(1);
    EXPECT_EQ(state->targetPower(), before + 1);

    state->nudgeTargetPower(-100000);
    EXPECT_EQ(state->targetPower(), 0);

    state->nudgeTargetPower(100000);
    EXPECT_EQ(state->targetPower(), 1500);
}

/** The fine step is an ERG Manual control; anywhere else a stray press must not move it. */
TEST_F(RideStateContractTest, NudgingOutsideErgManualDoesNothing) {
    const int before = state->targetPower();

    state->nudgeTargetPower(5);
    EXPECT_EQ(state->targetPower(), before);

    state->setRideMode(QStringLiteral("erg"));
    state->nudgeTargetPower(5);
    state->gearUp();
    EXPECT_EQ(state->targetPower(), before);
}

TEST_F(RideStateContractTest, EmitsChangedWhenTheRiderShifts) {
    int fired = 0;
    QObject::connect(state, &RideState::changed, [&fired]() { fired++; });

    // No device, so nothing moves - but the signal is what the QML bindings hang off,
    // and a shift that never announces itself leaves the gear numeral stale.
    state->cycleMode();
    EXPECT_GE(fired, 1);
}

} // namespace
