#pragma once

#include <string>
#include <vector>

/**
 * @brief A ride scenario: what the rider is doing, second by second, in a file.
 *
 * The spine of the virtual bike (docs/fork/VIRTUAL-BIKE.md) is that one scenario file is
 * played by two different things - the simulated bike inside the app, which drives metrics
 * directly, and the frame harness in the test project, which encodes the same samples into
 * FTMS frames and feeds them to the real ftmsbike parser. The same file through both must
 * produce the same numbers; that round trip is what catches one of them lying.
 *
 * That is why this class carries no Qt: it is linked by the app and by the test project, and
 * one of the two players does not want a QObject anywhere near it. It is also why the format
 * is a plain line-oriented text file rather than JSON - these get hand-edited far more often
 * than they get parsed, and the parser is the same thirty lines either way.
 *
 * ## Format
 *
 * @code
 * # ramp.ride - 60s ramp from soft-pedal to threshold
 * mode        power           # what the file asserts on: power | resistance | speed
 * bike        YPBM1234        # optional: the device name to pretend to be
 * resistance  12              # optional: starting resistance level
 * erg_lag     2.0             # optional: seconds for simulated power to reach an ERG target
 * noise       0.03            # optional: +/- fraction of noise on simulated power
 *
 * t=0     watts=80   cadence=85  hr=110
 * t=30    watts=200  cadence=90  hr=147
 * t=45    watts=260  cadence=92  hr=161  silence=10
 * @endcode
 *
 * ## The two rules that are easy to get wrong
 *
 * **Each field is its own timeline.** A value is interpolated between the two nearest samples
 * that both state that field, is absent before the first one that states it, and holds after
 * the last. So `hr` can appear in one sample in ten without dragging the samples in between
 * to zero, and a bike that never reports resistance simply never has a resistance timeline.
 * "Absent" and "zero" are different things and the format keeps them different: `cadence=0` is
 * an explicit value that both players must reproduce exactly, because coasting is a case that
 * has to work, while a missing `cadence=` means the bike said nothing on that sample.
 *
 * **Interpolation is always linear.** There is no step mode, no easing, no per-field
 * override. A fast transition is expressed by putting two samples close together, which is
 * both readable and exactly what the recorded fixtures look like. One rule, applied
 * everywhere, is worth more here than expressiveness: every extra mode is a way for the two
 * players to disagree about what the file meant.
 *
 * `silence=<seconds>` is the exception that is not about values at all. It says the bike stops
 * reporting entirely from that sample's time for that many seconds - no frames at all, not
 * frames carrying stale numbers - which is the dropout case, and the only way to write it.
 */

/** What the file is asserting on. */
enum RideMode {
    RIDE_MODE_POWER,      ///< the ride is defined by watts; resistance follows if the bike has it
    RIDE_MODE_RESISTANCE, ///< the ride is defined by resistance level; power follows from it
    RIDE_MODE_SPEED       ///< the ride is defined by speed
};

/**
 * @brief One field of one sample.
 *
 * A plain aggregate with no default member initializers, for the same reason ServiceView has
 * none: the test project builds at -std=gnu++11, where a struct carrying them is not an
 * aggregate. Construct with both members supplied.
 */
struct RideValue {
    bool present;
    double value;
};

/** One line of the file. Fields the line did not state have present == false. */
struct RideSample {
    double t; ///< seconds from the start of the ride
    RideValue watts;
    RideValue cadence;
    RideValue hr;
    RideValue speed;
    RideValue resistance;
    /// Seconds of total silence beginning at t. 0 for an ordinary sample.
    double silence;
};

/**
 * @brief The value of every field at one instant, as a player should report it.
 *
 * Fields with present == false are ones the bike is not reporting at that moment, which a
 * player must pass on as "said nothing" rather than as a zero.
 */
struct RidePoint {
    double t;
    RideValue watts;
    RideValue cadence;
    RideValue hr;
    RideValue speed;
    RideValue resistance;
    /// True while a silence window is open: the bike is reporting nothing at all.
    bool silent;
};

class RideScenario {
  public:
    RideScenario();

    /**
     * @brief Parse a scenario from text.
     * @param text The whole file.
     * @param error Set to a message naming the offending line if parsing fails. May be null.
     * @return The scenario. Check valid().
     */
    static RideScenario parse(const std::string &text, std::string *error);

    /**
     * @brief Parse a scenario from a file on disk.
     * @param path Path to a .ride file.
     * @param error Set to a message if the file cannot be read or parsed. May be null.
     */
    static RideScenario load(const std::string &path, std::string *error);

    /** @brief Did this scenario parse? An invalid scenario has no samples and must not be played. */
    bool valid() const { return m_valid; }

    RideMode mode() const { return m_mode; }
    /** @brief The device name the scenario is pretending to be, or empty. */
    const std::string &bike() const { return m_bike; }
    /** @brief Starting resistance level, or -1 if the file did not state one. */
    double startResistance() const { return m_startResistance; }
    /** @brief Seconds for simulated power to converge on an ERG target. */
    double ergLag() const { return m_ergLag; }
    /** @brief Fraction of noise to apply to simulated power, e.g. 0.03 for +/-3%. */
    double noise() const { return m_noise; }

    const std::vector<RideSample> &samples() const { return m_samples; }
    /** @brief The t of the last sample, plus any silence it opens. */
    double duration() const;

    /**
     * @brief The state of the ride at an instant, with each field on its own timeline.
     *
     * Before the first sample and after the last, values hold rather than extrapolate: a ride
     * does not become negative because a player asked one tick early.
     */
    RidePoint at(double t) const;

    /**
     * @brief Write the scenario back out in canonical form.
     *
     * Exists so a test can prove parse(toText(x)) == x for every fixture, which is the cheap
     * way to find a field the parser reads but drops on the floor.
     */
    std::string toText() const;

    /** @brief Defaults, named so both players and the tests agree on them. */
    static double defaultErgLag() { return 2.0; }
    static double defaultNoise() { return 0.0; }

  private:
    bool m_valid;
    RideMode m_mode;
    std::string m_bike;
    double m_startResistance;
    double m_ergLag;
    double m_noise;
    std::vector<RideSample> m_samples;
};
