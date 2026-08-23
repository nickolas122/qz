#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "devices/bike.h"
#include "qzsettings.h"

namespace {

/**
 * The cassette this fork rides: 15 gears on a trainer with 32 resistance levels, neutral
 * at gear 7, which is the level a flat road should feel like. The spacing is geometric
 * rather than linear - a real cassette's ratios are - and the integer values were chosen
 * by minimising squared log-ratio error under a strict-monotonicity constraint, because
 * naive rounding cascaded and pushed the low gears several percent out.
 *
 * These numbers were validated on the bike. That is why they are pinned here: they are
 * expensive to rederive and invisible until someone is riding.
 */
const int kNeutralGear = 7;
const int kNeutralResistance = 14;
const int kGearResistances[] = {7, 8, 9, 10, 11, 13, 14, 16, 17, 19, 21, 23, 26, 29, 32};
const int kGearCount = 15;

QString fifteenSpeedTable() {
    QStringList rows;
    for (int gear = 1; gear <= kGearCount; ++gear) {
        rows << QStringLiteral("%1|%2").arg(gear).arg(kGearResistances[gear - 1]);
    }
    return rows.join(QLatin1Char('\n'));
}

/**
 * bike reads QSettings through the default constructor, so the fixture redirects the
 * default store by name and clears it. Names are restored on teardown so suites that
 * run afterwards are unaffected.
 */
class GearTableTest : public ::testing::Test {
  protected:
    QString previousOrg, previousApp;

    void SetUp() override {
        previousOrg = QCoreApplication::organizationName();
        previousApp = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral("QZForkTests"));
        QCoreApplication::setApplicationName(QStringLiteral("GearTableTest"));

        QSettings settings;
        settings.clear();
        settings.setValue(QZSettings::gears_custom_table_enabled, true);
        settings.setValue(QZSettings::gears_custom_table, fifteenSpeedTable());
        settings.setValue(QZSettings::gears_neutral_gear, kNeutralGear);
        settings.sync();
    }

    void TearDown() override {
        QSettings settings;
        settings.clear();
        settings.sync();
        QCoreApplication::setOrganizationName(previousOrg);
        QCoreApplication::setApplicationName(previousApp);
    }

    static void disableNeutral() {
        QSettings settings;
        settings.setValue(QZSettings::gears_neutral_gear, 0);
        settings.sync();
    }
};

} // namespace

TEST_F(GearTableTest, RowCountIsTheGearCount) {
    bike b;

    // One row is one gear, so a 15-row table is a 15-speed. Upstream hard-codes 24.
    EXPECT_EQ(b.gearsTableSize(), kGearCount);
    EXPECT_EQ(b.gearsUpperBound(), kGearCount);
}

TEST_F(GearTableTest, NeutralGearIsRecognised) {
    bike b;

    EXPECT_EQ(b.gearsNeutral(), kNeutralGear);
    EXPECT_TRUE(b.gearsAbsoluteMode());
    EXPECT_DOUBLE_EQ(b.gearsNeutralResistance(), kNeutralResistance);
}

TEST_F(GearTableTest, NeutralGearRidesExactlyWhatTheAppAsked) {
    bike b;

    // The whole point of a neutral gear: at gear 7 QZ adds nothing to the training app's
    // demand. If this is not zero, every ride is silently offset.
    EXPECT_DOUBLE_EQ(b.gearsModifier(kNeutralGear), 0.0);
}

TEST_F(GearTableTest, EveryGearResolvesToItsMeasuredResistance) {
    bike b;

    for (int gear = 1; gear <= kGearCount; ++gear) {
        const double expected = kGearResistances[gear - 1] - kNeutralResistance;
        EXPECT_DOUBLE_EQ(b.gearsModifier(gear), expected) << "gear " << gear;
    }
}

TEST_F(GearTableTest, GearsAreStrictlyMonotonicAndInRange) {
    bike b;

    // A cassette that repeats a ratio has a dead shift, and the trainer has 32 levels.
    for (int gear = 2; gear <= kGearCount; ++gear) {
        EXPECT_GT(b.gearsModifier(gear), b.gearsModifier(gear - 1)) << "gear " << gear;
    }
    EXPECT_GE(kGearResistances[0], 1);
    EXPECT_LE(kGearResistances[kGearCount - 1], 32);
}

TEST_F(GearTableTest, RequestsOutsideTheTableClampToIt) {
    bike b;

    EXPECT_DOUBLE_EQ(b.gearsModifier(0), b.gearsModifier(1));
    EXPECT_DOUBLE_EQ(b.gearsModifier(kGearCount + 5), b.gearsModifier(kGearCount));
}

TEST_F(GearTableTest, IndexOffsetIsCountedFromNeutral) {
    bike b;

    // The slope path steers grade, which is measured in shifts rather than resistance
    // levels. Measuring from zero instead of from neutral is what made an ordinary gear
    // add a permanent climb.
    for (int gear = 1; gear <= kGearCount; ++gear) {
        b.setGears(gear);
        EXPECT_DOUBLE_EQ(b.gearsIndexOffset(), gear - kNeutralGear) << "gear " << gear;
    }

    b.setGears(kNeutralGear);
    EXPECT_DOUBLE_EQ(b.gearsIndexOffset(), 0.0);
}

TEST_F(GearTableTest, ShiftingStopsAtBothEndsOfTheCassette) {
    bike b;

    b.setGears(kGearCount);
    b.gearUp();
    EXPECT_DOUBLE_EQ(b.gears(), kGearCount);

    b.setGears(1);
    b.gearDown();
    EXPECT_DOUBLE_EQ(b.gears(), 1.0);
}

TEST_F(GearTableTest, ShiftingWalksTheCassetteOneGearAtATime) {
    bike b;
    b.setGears(kNeutralGear);

    b.gearUp();
    EXPECT_DOUBLE_EQ(b.gears(), kNeutralGear + 1);
    EXPECT_DOUBLE_EQ(b.gearsModifier(), kGearResistances[kNeutralGear] - kNeutralResistance);

    b.gearDown();
    b.gearDown();
    EXPECT_DOUBLE_EQ(b.gears(), kNeutralGear - 1);
    EXPECT_DOUBLE_EQ(b.gearsModifier(), kGearResistances[kNeutralGear - 2] - kNeutralResistance);
}

TEST_F(GearTableTest, WithoutANeutralGearTheRowsStayOffsets) {
    disableNeutral();
    bike b;

    // Backwards compatibility: with no neutral gear the table means what it always meant,
    // and the rows are added to whatever the app asked for rather than replacing it.
    EXPECT_FALSE(b.gearsAbsoluteMode());
    EXPECT_DOUBLE_EQ(b.gearsNeutralResistance(), 0.0);
    for (int gear = 1; gear <= kGearCount; ++gear) {
        EXPECT_DOUBLE_EQ(b.gearsModifier(gear), kGearResistances[gear - 1]) << "gear " << gear;
    }
}

TEST_F(GearTableTest, ANeutralGearOutsideTheTableIsIgnored) {
    QSettings settings;
    settings.setValue(QZSettings::gears_neutral_gear, kGearCount + 3);
    settings.sync();
    bike b;

    EXPECT_EQ(b.gearsNeutral(), 0);
    EXPECT_FALSE(b.gearsAbsoluteMode());
}

TEST_F(GearTableTest, ATableWithAGapIsTruncatedAtTheGap) {
    QSettings settings;
    settings.setValue(QZSettings::gears_custom_table, QStringLiteral("1|7\n2|8\n4|10"));
    settings.setValue(QZSettings::gears_neutral_gear, 0);
    settings.sync();
    bike b;

    // Gear 3 is missing, so gear 4 is unreachable and the table describes a 2-speed.
    EXPECT_EQ(b.gearsTableSize(), 2);
    EXPECT_EQ(b.gearsUpperBound(), 2);
}

TEST_F(GearTableTest, DisablingTheTableRestoresTheHistoricGearCount) {
    QSettings settings;
    settings.setValue(QZSettings::gears_custom_table_enabled, false);
    settings.sync();
    bike b;

    EXPECT_EQ(b.gearsTableSize(), 0);
    EXPECT_EQ(b.gearsUpperBound(), 24);
    EXPECT_FALSE(b.gearsAbsoluteMode());
}
