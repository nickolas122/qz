#ifndef LINKSTATUS_H
#define LINKSTATUS_H

#include <QtGlobal>

/**
 * @brief What a device's radio link is doing, as one snapshot.
 *
 * Read rather than signalled, because the UI already polls at 1 Hz and every field here
 * is derived from state the driver keeps anyway. Taken as one struct rather than six
 * accessors so a redraw cannot straddle a state change - a phase of Lost read next to an
 * attempt count from before the disconnect is worse than either alone.
 *
 * The default is a link that has never been up. Every device that does not track any of
 * this inherits it unchanged, which is why bluetoothdevice::linkStatus() has a body.
 *
 * See docs/fork/UI-INSTRUMENT-CLUSTER.md section 3 for what each phase looks like on
 * screen, and TODO.md ("both status indicators are latches, not state") for why a
 * boolean could not carry it.
 */
struct LinkStatus {
    enum Phase {
        /** @brief Nothing has connected this session. Not a fault - the normal start. */
        Idle,
        /** @brief A device was matched and the controller is opening the link. */
        Connecting,
        /** @brief Linked, enumerating services. Where the Windows stall in TODO.md lives. */
        Discovering,
        /** @brief Subscribed and receiving. */
        Live,
        /** @brief Was live, is not, and the backoff is still trying. */
        Lost,
        /** @brief Was live, is not, and the retry ceiling has been reached. */
        GaveUp,
    };

    Phase phase = Idle;

    /**
     * @brief Services enumerated so far, during Discovering.
     *
     * There is deliberately no denominator. How many services a bike has is not known
     * until discovery finishes, so "3 of 4" would be a number invented for the sake of
     * a progress bar. The count alone still distinguishes a discovery that is crawling
     * from one that has stopped.
     */
    int servicesFound = 0;

    /** @brief Milliseconds until the next reconnect attempt, or -1 when none is armed. */
    int msToNextAttempt = -1;

    /** @brief Consecutive failed attempts since the link was last up. */
    int attempts = 0;

    /**
     * @brief Age of the most recent data frame, or -1 when none has ever arrived.
     *
     * One clock does three jobs: it marks the metric chips stale, it says how long the
     * bike has been gone while Lost, and it is what the retry ceiling is measured
     * against. They are the same instant - when the link drops, the data stops - so
     * carrying two timestamps would only create a way for them to disagree.
     */
    qint64 msSinceLastFrame = -1;

    /** @brief 0-100, or -1 when the device does not report a battery at all. */
    int batteryLevel = -1;
};

#endif // LINKSTATUS_H
