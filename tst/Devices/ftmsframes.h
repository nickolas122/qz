#pragma once

#include <QByteArray>
#include <QtGlobal>

/**
 * An Indoor Bike Data encoder, for Layer B of docs/fork/VIRTUAL-BIKE.md.
 *
 * ## Why a builder rather than a struct
 *
 * The bug this exists to catch is field-offset arithmetic. Every field in Indoor Bike Data is
 * optional, the flags word says which are present, and the reader has to walk the payload
 * adding the right width for each - so a missing Average Speed pushes cadence into the
 * resistance slot and every number after it is silently wrong. Frames that differ *only* in
 * which optional fields are present are the whole point of the encoder.
 *
 * So presence is set by calling a setter, the flag bit follows from that, and the bytes are
 * always emitted in spec order regardless of the order the setters were called in. A caller
 * cannot produce a frame whose flags and layout disagree, which is exactly the mistake a
 * hand-written `QByteArray` makes.
 *
 * ## Field order and widths
 *
 * From the FTMS Indoor Bike Data characteristic. Bit 0 is "More Data", and it is inverted:
 * when it is **set** the instantaneous speed field is *absent*. That is how the real YPBM
 * trainer splits its data across two notifications - see
 * `tst/fixtures/recorded/ypbm-32min-ride.frames`.
 *
 * | bit | field | width | unit |
 * | --- | --- | --- | --- |
 * | 0 (inverted) | Instantaneous Speed | 2 | 0.01 km/h |
 * | 1 | Average Speed | 2 | 0.01 km/h |
 * | 2 | Instantaneous Cadence | 2 | 0.5 rpm |
 * | 3 | Average Cadence | 2 | 0.5 rpm |
 * | 4 | Total Distance | 3 | m |
 * | 5 | Resistance Level | 2 | unitless |
 * | 6 | Instantaneous Power | 2 | W |
 * | 7 | Average Power | 2 | W |
 * | 8 | Expended Energy | 2+2+1 | kcal, kcal/h, kcal/min |
 * | 9 | Heart Rate | 1 | bpm |
 * | 10 | Metabolic Equivalent | 1 | 0.1 |
 * | 11 | Elapsed Time | 2 | s |
 * | 12 | Remaining Time | 2 | s |
 *
 * ## What keeps this honest
 *
 * An encoder bug and a parser bug that agree with each other cancel out and pass. So this is
 * not checked against our own decoder: it is checked against **bytes a real trainer sent**,
 * replayed out of a recorded fixture. If the encoder can reproduce `f5 01 …` and `00 2a …`
 * from the values the log says those frames carried, it agrees with a device rather than with
 * us.
 */
namespace ftmsframes {

class IndoorBikeData {
  public:
    IndoorBikeData &speed(double kmh) { return set(F_SPEED, qRound(kmh * 100)); }
    IndoorBikeData &avgSpeed(double kmh) { return set(F_AVG_SPEED, qRound(kmh * 100)); }
    IndoorBikeData &cadence(double rpm) { return set(F_CADENCE, qRound(rpm * 2)); }
    IndoorBikeData &avgCadence(double rpm) { return set(F_AVG_CADENCE, qRound(rpm * 2)); }
    IndoorBikeData &distance(quint32 metres) { return set(F_DISTANCE, int(metres)); }
    IndoorBikeData &resistance(int level) { return set(F_RESISTANCE, level); }
    IndoorBikeData &power(int watts) { return set(F_POWER, watts); }
    IndoorBikeData &avgPower(int watts) { return set(F_AVG_POWER, watts); }
    IndoorBikeData &heart(int bpm) { return set(F_HEART, bpm); }
    IndoorBikeData &metabolic(double equivalent) { return set(F_METABOLIC, qRound(equivalent * 10)); }
    IndoorBikeData &elapsed(int seconds) { return set(F_ELAPSED, seconds); }
    IndoorBikeData &remaining(int seconds) { return set(F_REMAINING, seconds); }

    /// The three energy fields share one flag bit, so they are set together or not at all.
    IndoorBikeData &energy(int total, int perHour, int perMinute) {
        set(F_ENERGY_TOTAL, total);
        set(F_ENERGY_HOUR, perHour);
        return set(F_ENERGY_MINUTE, perMinute);
    }

    /**
     * @brief Add bytes the flags do not account for.
     *
     * The real trainer does this - every one of its 10-byte frames carries three bytes more
     * than its flags describe - so a harness has to be able to reproduce it. A reader must
     * ignore them, and this is how that gets tested.
     */
    IndoorBikeData &unflaggedTrailer(const QByteArray &extra) {
        m_trailer = extra;
        return *this;
    }

    /// Set a raw flag bit without adding a field, for malformed-frame tests.
    IndoorBikeData &rawFlagBit(int bit) {
        m_extraFlags |= quint16(1u << bit);
        return *this;
    }

    quint16 flags() const {
        quint16 out = m_extraFlags;
        for (int i = 0; i < F_COUNT; ++i) {
            // Speed is skipped here on purpose. Its bit is inverted, so presence must not set
            // it - and the loop setting bit 0 for a *present* speed field, immediately before
            // the inverted rule below decided not to, is exactly the bug this comment exists
            // to stop coming back.
            if (i != F_SPEED && m_present[i]) {
                out |= quint16(1u << kFields[i].bit);
            }
        }
        // Bit 0 is inverted: set means the speed field is NOT there.
        if (!m_present[F_SPEED]) {
            out |= 0x0001;
        }
        return out;
    }

    QByteArray bytes() const {
        QByteArray out;
        const quint16 f = flags();
        out.append(char(f & 0xFF));
        out.append(char((f >> 8) & 0xFF));
        for (int i = 0; i < F_COUNT; ++i) {
            if (!m_present[i]) {
                continue;
            }
            const quint32 value = quint32(m_value[i]);
            for (int b = 0; b < kFields[i].width; ++b) {
                out.append(char((value >> (8 * b)) & 0xFF));
            }
        }
        out.append(m_trailer);
        return out;
    }

  private:
    // Declaration order is spec order, and bytes() walks it in that order. Adding a field in
    // the wrong place here is the one mistake that would make the encoder lie, which is why
    // the table carries the bit number explicitly rather than implying it from position.
    enum Field {
        F_SPEED, F_AVG_SPEED, F_CADENCE, F_AVG_CADENCE, F_DISTANCE, F_RESISTANCE,
        F_POWER, F_AVG_POWER, F_ENERGY_TOTAL, F_ENERGY_HOUR, F_ENERGY_MINUTE,
        F_HEART, F_METABOLIC, F_ELAPSED, F_REMAINING, F_COUNT
    };

    struct Spec {
        int bit;
        int width;
    };

    IndoorBikeData &set(Field field, int value) {
        m_present[field] = true;
        m_value[field] = value;
        return *this;
    }

    static const Spec kFields[F_COUNT];
    bool m_present[F_COUNT] = {false};
    int m_value[F_COUNT] = {0};
    QByteArray m_trailer;
    quint16 m_extraFlags = 0;
};

// Defined inline in the header because only the test project uses it, and a .cpp for fifteen
// rows would be one more file to keep in step with the enum above.
inline const IndoorBikeData::Spec IndoorBikeData::kFields[IndoorBikeData::F_COUNT] = {
    {0, 2},  // instantaneous speed (bit 0 inverted - see flags())
    {1, 2},  // average speed
    {2, 2},  // instantaneous cadence
    {3, 2},  // average cadence
    {4, 3},  // total distance
    {5, 2},  // resistance level
    {6, 2},  // instantaneous power
    {7, 2},  // average power
    {8, 2},  // expended energy: total
    {8, 2},  // expended energy: per hour
    {8, 1},  // expended energy: per minute
    {9, 1},  // heart rate
    {10, 1}, // metabolic equivalent
    {11, 2}, // elapsed time
    {12, 2}, // remaining time
};

} // namespace ftmsframes
