#pragma once

#include <gtest/gtest.h>

#include <string>

#include "devices/simulatedbike/ridescenario.h"

#ifndef QZ_RIDE_FIXTURES
#error "QZ_RIDE_FIXTURES must name the directory holding the .ride fixtures"
#endif

namespace {

/**
 * Exercises the scenario format the way both players will (docs/fork/VIRTUAL-BIKE.md): parse a
 * file, then ask it for values at instants that fall between the samples, because that is all
 * either player ever does with it.
 *
 * The fixtures under tst/fixtures/rides are checked in and are the same files the simulated
 * bike and the frame harness play. They are asserted on here rather than being reduced to
 * literals in the test, so that a fixture edited without thinking about what it was for fails
 * something.
 */

std::string fixture(const char *name) { return std::string(QZ_RIDE_FIXTURES) + "/" + name; }

RideScenario loadFixture(const char *name) {
    std::string error;
    RideScenario s = RideScenario::load(fixture(name), &error);
    EXPECT_TRUE(s.valid()) << name << ": " << error;
    return s;
}

const char *const allFixtures[] = {"steady.ride", "ramp.ride", "sprint.ride", "coast.ride",
                                   "dropout.ride"};
const size_t allFixturesCount = sizeof(allFixtures) / sizeof(allFixtures[0]);

TEST(RideScenario, EveryFixtureParses) {
    for (size_t i = 0; i < allFixturesCount; ++i) {
        std::string error;
        const RideScenario s = RideScenario::load(fixture(allFixtures[i]), &error);
        EXPECT_TRUE(s.valid()) << allFixtures[i] << ": " << error;
        EXPECT_FALSE(s.samples().empty()) << allFixtures[i];
    }
}

/**
 * The round trip that Phase 0 is accepted on. A field the parser reads but toText() forgets -
 * or vice versa - shows up here and nowhere else, because every other test asks about the
 * fields it already knows are there.
 */
TEST(RideScenario, EveryFixtureRoundTrips) {
    for (size_t i = 0; i < allFixturesCount; ++i) {
        const RideScenario first = loadFixture(allFixtures[i]);
        std::string error;
        const RideScenario second = RideScenario::parse(first.toText(), &error);
        ASSERT_TRUE(second.valid()) << allFixtures[i] << ": " << error;
        EXPECT_EQ(first.toText(), second.toText()) << allFixtures[i];
        EXPECT_EQ(first.samples().size(), second.samples().size()) << allFixtures[i];
        EXPECT_EQ(first.mode(), second.mode()) << allFixtures[i];
        EXPECT_DOUBLE_EQ(first.startResistance(), second.startResistance()) << allFixtures[i];
        EXPECT_DOUBLE_EQ(first.duration(), second.duration()) << allFixtures[i];
    }
}

TEST(RideScenario, InterpolatesLinearlyBetweenSamples) {
    const RideScenario ramp = loadFixture("ramp.ride");

    // Halfway between t=0 (80 W, 85 rpm) and t=15 (140 W, 88 rpm).
    const RidePoint mid = ramp.at(7.5);
    ASSERT_TRUE(mid.watts.present);
    EXPECT_DOUBLE_EQ(110.0, mid.watts.value);
    ASSERT_TRUE(mid.cadence.present);
    EXPECT_DOUBLE_EQ(86.5, mid.cadence.value);

    // On a sample, the sample's own value and nothing smeared into it.
    const RidePoint onSample = ramp.at(30);
    EXPECT_DOUBLE_EQ(200.0, onSample.watts.value);
}

/**
 * Outside the stated samples a value holds. Extrapolating a ramp past its last sample is how a
 * 60-second ramp reaches 900 W at three minutes, which is not what anyone writing the file
 * meant.
 */
TEST(RideScenario, HoldsAfterTheLastSampleAndIsAbsentBeforeTheFirst) {
    const RideScenario ramp = loadFixture("ramp.ride");

    const RidePoint after = ramp.at(10000);
    ASSERT_TRUE(after.watts.present);
    EXPECT_DOUBLE_EQ(260.0, after.watts.value);

    std::string error;
    const RideScenario late = RideScenario::parse("mode power\n"
                                                 "t=0   watts=100\n"
                                                 "t=10  watts=200 hr=150\n",
                                                 &error);
    ASSERT_TRUE(late.valid()) << error;
    // hr is first stated at t=10, so before that the bike has said nothing about it. Holding
    // the first value backwards would invent a reading the ride never had.
    EXPECT_FALSE(late.at(0).hr.present);
    EXPECT_FALSE(late.at(9.9).hr.present);
    EXPECT_TRUE(late.at(10).hr.present);
}

/**
 * The distinction the format exists to keep: an explicit zero is a value the rider produced,
 * a missing key is the bike saying nothing. A player that conflates them either shows a stale
 * cadence through a coast or shows zero through a gap, and both have been real bugs.
 */
TEST(RideScenario, ExplicitZeroIsAValueAndNotAnAbsence) {
    const RideScenario coast = loadFixture("coast.ride");

    const RidePoint coasting = coast.at(28);
    ASSERT_TRUE(coasting.cadence.present);
    EXPECT_DOUBLE_EQ(0.0, coasting.cadence.value);
    ASSERT_TRUE(coasting.watts.present);
    EXPECT_DOUBLE_EQ(0.0, coasting.watts.value);

    // Heart rate is still being reported through the coast - the rider has not stopped
    // existing - so the zero belongs to cadence and power alone.
    ASSERT_TRUE(coasting.hr.present);
    EXPECT_GT(coasting.hr.value, 100.0);

    // coast.ride states no per-sample resistance at all, so that timeline is empty throughout.
    EXPECT_FALSE(coast.at(0).resistance.present);
    EXPECT_FALSE(coast.at(30).resistance.present);
}

/**
 * Each field is its own timeline: a sample that omits a field must not punch a hole in it.
 */
TEST(RideScenario, FieldsAreIndependentTimelines) {
    std::string error;
    const RideScenario s = RideScenario::parse("mode power\n"
                                               "t=0   watts=100 hr=120\n"
                                               "t=10  watts=200\n"
                                               "t=20  watts=300 hr=160\n",
                                               &error);
    ASSERT_TRUE(s.valid()) << error;

    // watts interpolates across its own samples...
    EXPECT_DOUBLE_EQ(150.0, s.at(5).watts.value);
    // ...and hr across its own, straight through the sample that omits it.
    ASSERT_TRUE(s.at(10).hr.present);
    EXPECT_DOUBLE_EQ(140.0, s.at(10).hr.value);
}

/**
 * Silence is total and bounded. Anything reported from inside the window is the bug
 * dropout.ride exists to catch; anything still missing after it has closed is the opposite bug.
 */
TEST(RideScenario, SilenceSwallowsItsWindowAndOnlyItsWindow) {
    const RideScenario drop = loadFixture("dropout.ride");

    // The window opens at t=20 and runs ten seconds.
    EXPECT_FALSE(drop.at(19.9).silent);
    EXPECT_TRUE(drop.at(20).silent);
    EXPECT_TRUE(drop.at(25).silent);
    EXPECT_TRUE(drop.at(29.9).silent);
    EXPECT_FALSE(drop.at(30).silent);

    // Inside it, nothing at all is reported - not stale values, not interpolated ones.
    const RidePoint inside = drop.at(25);
    EXPECT_FALSE(inside.watts.present);
    EXPECT_FALSE(inside.cadence.present);
    EXPECT_FALSE(inside.hr.present);

    // Either side, the ride is ordinary.
    EXPECT_TRUE(drop.at(10).watts.present);
    EXPECT_TRUE(drop.at(35).watts.present);
}

TEST(RideScenario, DurationIncludesATrailingSilence) {
    std::string error;
    const RideScenario s = RideScenario::parse("mode power\n"
                                               "t=0   watts=100\n"
                                               "t=30  watts=100 silence=15\n",
                                               &error);
    ASSERT_TRUE(s.valid()) << error;
    EXPECT_DOUBLE_EQ(45.0, s.duration());

    EXPECT_DOUBLE_EQ(50.0, loadFixture("dropout.ride").duration());
}

TEST(RideScenario, ReadsTheDirectives) {
    std::string error;
    const RideScenario s = RideScenario::parse("mode        resistance\n"
                                               "bike        YPBM1234\n"
                                               "resistance  17\n"
                                               "erg_lag     3.5\n"
                                               "noise       0.05\n"
                                               "t=0 resistance=17 cadence=80\n",
                                               &error);
    ASSERT_TRUE(s.valid()) << error;
    EXPECT_EQ(RIDE_MODE_RESISTANCE, s.mode());
    EXPECT_EQ(std::string("YPBM1234"), s.bike());
    EXPECT_DOUBLE_EQ(17.0, s.startResistance());
    EXPECT_DOUBLE_EQ(3.5, s.ergLag());
    EXPECT_DOUBLE_EQ(0.05, s.noise());
}

TEST(RideScenario, UnstatedDirectivesTakeTheirDefaults) {
    std::string error;
    const RideScenario s = RideScenario::parse("mode power\nt=0 watts=100\n", &error);
    ASSERT_TRUE(s.valid()) << error;
    EXPECT_TRUE(s.bike().empty());
    EXPECT_DOUBLE_EQ(-1.0, s.startResistance()); // "the file did not say", not "zero"
    EXPECT_DOUBLE_EQ(RideScenario::defaultErgLag(), s.ergLag());
    EXPECT_DOUBLE_EQ(RideScenario::defaultNoise(), s.noise());

    // A default that was never stated does not appear in the canonical form either, so a
    // round trip cannot quietly promote it to something the file asserts.
    EXPECT_EQ(std::string::npos, s.toText().find("erg_lag"));
    EXPECT_EQ(std::string::npos, s.toText().find("noise"));
}

TEST(RideScenario, IgnoresCommentsAndBlankLines) {
    std::string error;
    const RideScenario s = RideScenario::parse("# leading comment\n"
                                               "\n"
                                               "mode power   # trailing comment\n"
                                               "\n"
                                               "t=0  watts=100   # on a sample too\n"
                                               "   \n"
                                               "t=10 watts=200\n",
                                               &error);
    ASSERT_TRUE(s.valid()) << error;
    EXPECT_EQ(2u, s.samples().size());
    EXPECT_DOUBLE_EQ(150.0, s.at(5).watts.value);
}

/**
 * The rejections. A scenario that parses when it should not is worse than one that fails,
 * because it silently becomes a test that asserts the wrong ride.
 */
TEST(RideScenario, RejectsMalformedFiles) {
    struct Case {
        const char *text;
        const char *why;
    };
    const Case cases[] = {
        {"mode power\nt=0 watts=90rpm\n", "a number with trailing rubbish"},
        {"mode power\nt=0 watts=\n", "a key with no value"},
        {"mode power\nt=10 watts=100\nt=5 watts=100\n", "t going backwards"},
        {"mode power\nt=10 watts=100\nt=10 watts=200\n", "t not advancing"},
        {"mode power\nt=-5 watts=100\n", "negative t"},
        {"mode power\nt=0 cadence=900\n", "cadence outside its bounds"},
        {"mode power\nt=0 watts=9000\n", "watts outside its bounds"},
        {"mode power\nt=0 wats=100\n", "a misspelled field"},
        {"mode power\nt=0 watts=100 silence=0\n", "a silence of zero"},
        {"mode power\nt=0 watts=100 silence=-3\n", "a negative silence"},
        {"mode power\nwatts=100\n", "a sample with no t"},
        {"mode power\n", "no samples"},
        {"t=0 watts=100\n", "no mode"},
        {"mode sideways\nt=0 watts=100\n", "an unknown mode"},
        {"mode power\nspeeed 12\nt=0 watts=100\n", "an unknown directive"},
        {"mode power\nbike\nt=0 watts=100\n", "a directive with no value"},
        {"mode power\nbike one two\nt=0 watts=100\n", "a directive with two values"},
        {"mode power\nnoise 4\nt=0 watts=100\n", "a noise fraction above one"},
        {"mode power\nerg_lag -1\nt=0 watts=100\n", "a negative erg_lag"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        std::string error;
        const RideScenario s = RideScenario::parse(cases[i].text, &error);
        EXPECT_FALSE(s.valid()) << "accepted " << cases[i].why;
        EXPECT_FALSE(error.empty()) << "rejected " << cases[i].why << " without saying why";
    }
}

/** An invalid scenario is inert rather than dangerous: no samples, no values, no duration. */
TEST(RideScenario, AnInvalidScenarioReportsNothing) {
    std::string error;
    const RideScenario s = RideScenario::parse("mode nonsense\n", &error);
    ASSERT_FALSE(s.valid());
    EXPECT_TRUE(s.samples().empty());
    EXPECT_DOUBLE_EQ(0.0, s.duration());
    EXPECT_FALSE(s.at(0).watts.present);
    EXPECT_FALSE(s.at(0).silent);
    EXPECT_TRUE(s.toText().empty());
}

TEST(RideScenario, ReportsAMissingFileRatherThanPretendingItIsEmpty) {
    std::string error;
    const RideScenario s = RideScenario::load(fixture("no-such-ride-here.ride"), &error);
    EXPECT_FALSE(s.valid());
    EXPECT_FALSE(error.empty());
}

} // namespace
