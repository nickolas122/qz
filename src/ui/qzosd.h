#ifndef QZOSD_H
#define QZOSD_H

#include <QObject>
#include <QString>

#include "rtssosd.h"

class RideState;

/**
 * @brief What the overlay says, and every place it gets drawn.
 *
 * The overlay used to be one function on RideState that composed the lines and wrote them
 * into RTSS in the same breath. That was fine while RTSS was the only sink; it stopped
 * being fine the moment there were two, because "which lines" and "drawn where" are
 * different questions and only the first has anything to do with the ride.
 *
 * So this is one producer with N sinks. It composes the text once, from RideState's own
 * public API - it never reaches past it to a device - and then hands the same string to
 * whichever sinks the rider has switched on:
 *
 * - **RTSS** (`osd_rtss`, Windows). Draws inside the training app's own D3D frame, which
 *   is the only thing that survives exclusive fullscreen. Owned here, by value, so the
 *   slot is released when this object goes.
 * - **The floating window** (`osd_window`). `text` and `windowVisible` below are what
 *   OsdWindow.qml binds to. It is an ordinary always-on-top window, so an app in
 *   *exclusive* fullscreen covers it - windowed and borderless apps are fine.
 *
 * A third sink - an Android `TYPE_APPLICATION_OVERLAY` - would be another branch in
 * refresh() and nothing else. That is the point of the shape.
 *
 * Deliberately not a member of RideState. Section 9.2 caps that surface at 22 and it is
 * full, and this is the wrong kind of thing to spend one on: the overlay is a view of the
 * ride, not a fact about it. It reaches QML as its own context property, the same way
 * `language` and `gamepad` do.
 */
class QzOsd : public QObject {
    Q_OBJECT

    /**
     * @brief The overlay's text, lines separated by newlines.
     *
     * Empty when the overlay is switched off entirely, and also when it is on with every
     * line switched off - the rider wants it silent until something goes wrong.
     */
    Q_PROPERTY(QString text READ text NOTIFY changed)

    /** @brief Whether the floating window should be on screen right now. */
    Q_PROPERTY(bool windowVisible READ windowVisible NOTIFY changed)

  public:
    explicit QzOsd(RideState *state, QObject *parent = nullptr);

    QString text() const { return current; }
    bool windowVisible() const { return showWindow; }

  signals:
    /** @brief The text or the window's visibility changed. Not emitted on an idle tick. */
    void changed();

  private slots:
    /**
     * @brief Recompose, and push to every sink that is switched on.
     *
     * Driven off RideState::changed rather than a timer of its own: that already fires on
     * the 1 Hz poll, and additionally the instant the rider shifts, so the overlay follows
     * a gear change immediately instead of up to a second later.
     */
    void refresh();

  private:
    /** @brief Build the text from the ride and the per-line settings. */
    QString compose() const;

    RideState *ride = nullptr;
    RtssOsd rtss;

    QString current;
    bool showWindow = false;
};

#endif // QZOSD_H
