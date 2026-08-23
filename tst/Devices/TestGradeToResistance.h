#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QVariant>

#include "Tools/testsettings.h"
#include "characteristics/characteristicwriteprocessor2ad9.h"
#include "devices/bike.h"
#include "qzsettings.h"

namespace {

/**
 * The road-feel calibration, pinned.
 *
 * A training app steers QZ by sending FTMS Set Indoor Bike Simulation Parameters
 * (0x11): wind speed, road grade in hundredths of a percent, a rolling-resistance
 * coefficient and a wind coefficient. QZ turns that into a resistance level with
 *
 *     round(grade% * 1.5 * bike_resistance_gain_f) + bike_resistance_offset + 1
 *                                                  + CRR_offset + CW_offset
 *
 * and that "+ 1" is not decoration - the trainer's levels start at 1, so the offset
 * alone would ride one level light. The whole expression is what decides whether a
 * flat road feels flat, which is a judgement made on the bike and invisible to every
 * other test in this suite. Phase 6 of the strip reshuffles the settings this reads;
 * these numbers are the evidence that the reshuffle did not move the road.
 *
 * The oracle is the raw argument to changeResistance(), taken before bike::changeResistance
 * folds in gears and difficulty. That keeps this test about the grade formula and lets
 * TestGearTable stay the one place the cassette is asserted.
 */
class RecordingBike : public bike {
  public:
    resistance_t requestedResistance = -1;
    int resistanceWrites = 0;

    void changeResistance(resistance_t res) override {
        requestedResistance = res;
        ++resistanceWrites;
    }
};

/** The offset this fork rides, from the flat-terrain calibration. */
const int kFlatOffset = 13;

class GradeToResistanceTest : public ::testing::Test {
  protected:
    TestSettings testSettings{QStringLiteral("Roberto Viola"), QStringLiteral("QZ Grade To Resistance Test")};

    void SetUp() override {
        testSettings.qsettings.clear();
        testSettings.activate();
    }

    void TearDown() override {
        testSettings.qsettings.clear();
        testSettings.qsettings.sync();
        testSettings.deactivate();
    }

    void set(const QString &key, const QVariant &value) {
        testSettings.qsettings.setValue(key, value);
        testSettings.qsettings.sync();
    }

    /**
     * The grade the trainer is steered with, in hundredths of a percent, as the wire
     * carries it. 100 is a 1% climb.
     */
    static int16_t hundredthsOfAPercent(double gradePercent) { return static_cast<int16_t>(qRound(gradePercent * 100.0)); }

    /** A 0x11 frame as Zwift, Rouvy and MyWhoosh send it. */
    static QByteArray simulationFrame(double gradePercent, uint8_t crr, uint8_t cw) {
        const int16_t grade = hundredthsOfAPercent(gradePercent);
        QByteArray frame;
        frame.append((char)0x11);
        frame.append((char)0x00); // wind speed, little endian
        frame.append((char)0x00);
        frame.append((char)(grade & 0xff));
        frame.append((char)((grade >> 8) & 0xff));
        frame.append((char)crr);
        frame.append((char)cw);
        return frame;
    }
};

} // namespace

TEST_F(GradeToResistanceTest, FlatGroundRidesTheOffsetPlusOne) {
    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    p.changeSlope(hundredthsOfAPercent(0.0), 40, 51);

    // The calibration in one number: on the flat, the trainer is asked for the offset
    // plus one and nothing else. Gear 7 lands here too (TestGearTable), which is what
    // makes neutral gear on flat road the identity.
    EXPECT_EQ(b.resistanceWrites, 1);
    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1);
}

TEST_F(GradeToResistanceTest, EachPercentOfGradeIsWorthOnePointFiveLevels) {
    struct Case {
        double grade;
        int expected;
    };
    // round() is half-away-from-zero, so 1% -> round(1.5) -> 2 and -1% -> round(-1.5) -> -2.
    const Case cases[] = {
        {0.0, kFlatOffset + 1},  {1.0, kFlatOffset + 3},   {2.0, kFlatOffset + 4},
        {4.0, kFlatOffset + 7},  {10.0, kFlatOffset + 16}, {-1.0, kFlatOffset - 1},
        {-4.0, kFlatOffset - 5},
    };

    for (const Case &c : cases) {
        RecordingBike b;
        CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

        p.changeSlope(hundredthsOfAPercent(c.grade), 40, 51);

        EXPECT_EQ(b.requestedResistance, c.expected) << "grade " << c.grade << "%";
    }
}

TEST_F(GradeToResistanceTest, TheResistanceGainScalesTheGradeAndNotTheOffset) {
    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(2.0, kFlatOffset, &b, nullptr);

    // 4% * 1.5 = 6 levels of climb, doubled by the gain; the offset is untouched by it.
    p.changeSlope(hundredthsOfAPercent(4.0), 40, 51);

    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1 + 12);
}

TEST_F(GradeToResistanceTest, TheGradeArrivesAsTheInclinationTheBikeShows) {
    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    // Deliberately a lambda rather than QSignalSpy: that lives in Qt's testlib, which
    // this project does not link, and one test is not worth a module on every platform.
    int announcements = 0;
    double announcedGrade = 0.0;
    QObject::connect(&p, &CharacteristicWriteProcessor::changeInclination,
                     [&announcements, &announcedGrade](double grade, double) {
                         ++announcements;
                         announcedGrade = grade;
                     });

    p.changeSlope(hundredthsOfAPercent(3.5), 40, 51);

    // The bike has no inclination hardware, so QZ carries the number itself. The
    // resistance path scales the grade by 1.5; the displayed inclination does not.
    EXPECT_DOUBLE_EQ(b.currentInclination().value(), 3.5);
    EXPECT_EQ(announcements, 1);
    EXPECT_DOUBLE_EQ(announcedGrade, 3.5);
}

TEST_F(GradeToResistanceTest, TheInclinationOffsetAndGainSteerTheGradeOnly) {
    set(QZSettings::zwift_inclination_gain, 0.5);
    set(QZSettings::zwift_inclination_offset, 1.0);

    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    p.changeSlope(hundredthsOfAPercent(4.0), 40, 51);

    // These two settings rescale what the rider is told the road is doing. They
    // deliberately do not reach the resistance the trainer is asked for, which still
    // reads the grade off the wire - a difference that has surprised people.
    EXPECT_DOUBLE_EQ(b.currentInclination().value(), 4.0 * 0.5 + 1.0);
    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1 + 6);
}

TEST_F(GradeToResistanceTest, DescentsCanBeDoubledForZwift) {
    set(QZSettings::zwift_negative_inclination_x2, true);

    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    p.changeSlope(hundredthsOfAPercent(-4.0), 40, 51);
    EXPECT_DOUBLE_EQ(b.currentInclination().value(), -8.0);

    // Climbs are left alone, and so is the resistance on the way down.
    p.changeSlope(hundredthsOfAPercent(4.0), 40, 51);
    EXPECT_DOUBLE_EQ(b.currentInclination().value(), 4.0);
}

TEST_F(GradeToResistanceTest, MinInclinationFloorsTheGradeAndNotTheResistance) {
    set(QZSettings::min_inclination, -2.0);

    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    p.changeSlope(hundredthsOfAPercent(-6.0), 40, 51);

    EXPECT_DOUBLE_EQ(b.currentInclination().value(), -2.0);
    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1 - 9);
}

TEST_F(GradeToResistanceTest, TheSurfaceGainsAreOffByDefault) {
    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    // CRRGain and CWGain both default to 0, so a gravel sector rides like tarmac
    // until someone opts in. Worth pinning: the surface terms are the only part of
    // this formula that is off unless asked for.
    p.changeSlope(hundredthsOfAPercent(0.0), 80, 90);

    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1);
    EXPECT_DOUBLE_EQ(b.currentInclination().value(), 0.0);
}

TEST_F(GradeToResistanceTest, TheSurfaceGainsBothReadTheRollingResistanceByte) {
    set(QZSettings::CRRGain, 1.0);
    set(QZSettings::CWGain, 1.0);

    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    // Both terms are (crr - 40) * 0.05 * gain. The wind term reading the *rolling*
    // resistance byte is upstream behaviour and looks like a typo - see docs/fork/TODO.md.
    // It is pinned rather than corrected because changing it changes how every gravel
    // ride feels, which is a decision for a session on the bike and not for this test.
    // cw is passed as 90 and would give 2.5 if it were read; 40 is what crr gives.
    p.changeSlope(hundredthsOfAPercent(0.0), 80, 90);

    const double expected = (80 - 40) * 0.05; // 2.0, once per gain
    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1 + 2 * (int)expected);
    EXPECT_DOUBLE_EQ(b.currentInclination().value(), 2 * expected);
}

TEST_F(GradeToResistanceTest, ErgModeLeavesTheResistanceAlone) {
    set(QZSettings::zwift_erg, true);

    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    p.changeSlope(hundredthsOfAPercent(6.0), 40, 51);

    // In ERG the app owns the power target, so a grade must not also push resistance
    // around underneath it. The inclination still updates - it is only being displayed.
    EXPECT_EQ(b.resistanceWrites, 0);
    EXPECT_DOUBLE_EQ(b.currentInclination().value(), 6.0);
}

TEST_F(GradeToResistanceTest, ForceResistanceOffLeavesTheResistanceAlone) {
    set(QZSettings::virtualbike_forceresistance, false);

    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    p.changeSlope(hundredthsOfAPercent(6.0), 40, 51);

    EXPECT_EQ(b.resistanceWrites, 0);
}

TEST_F(GradeToResistanceTest, AWholeSimulationFrameOffTheWireReachesTheSameNumber) {
    RecordingBike b;
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, &b, nullptr);

    QByteArray reply;
    const int rc = p.writeProcess(0x2AD9, simulationFrame(4.0, 40, 51), reply);

    // The byte layout is part of the calibration: grade is a signed little-endian
    // value at offset 3, and reading it from the wrong offset gives a plausible-looking
    // number rather than an error.
    EXPECT_EQ(rc, CP_OK);
    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1 + 6);
    ASSERT_EQ(reply.size(), 3);
    EXPECT_EQ((quint8)reply.at(0), 0x80); // response code
    EXPECT_EQ((quint8)reply.at(1), 0x11); // the opcode being answered
    EXPECT_EQ((quint8)reply.at(2), 0x01); // success

    // And the same frame with a descent, so the sign survives the two bytes.
    reply.clear();
    p.writeProcess(0x2AD9, simulationFrame(-4.0, 40, 51), reply);
    EXPECT_EQ(b.requestedResistance, kFlatOffset + 1 - 6);
}

TEST_F(GradeToResistanceTest, AWriteWithNoBikeAttachedIsRefused) {
    CharacteristicWriteProcessor2AD9 p(1.0, kFlatOffset, nullptr, nullptr);

    QByteArray reply;

    // The DIRCON endpoint outlives the bike, so a training app can steer a grade at
    // nothing at all. It must decline rather than dereference.
    EXPECT_EQ(p.writeProcess(0x2AD9, simulationFrame(4.0, 40, 51), reply), CP_INVALID);
    EXPECT_TRUE(reply.isEmpty());
}
