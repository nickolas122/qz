#include "qzosd.h"

#include "qzsettings.h"
#include "ui/ridestate.h"

#include <QSettings>
#include <QStringList>

QzOsd::QzOsd(RideState *state, QObject *parent) : QObject(parent), ride(state) {
    if (ride) {
        connect(ride, &RideState::changed, this, &QzOsd::refresh);
    }
    refresh();
}

QString QzOsd::compose() const {
    if (!ride) {
        return QString();
    }

    const QString link = ride->trainerState();

    // No trainer at all. "searching" is what RideState says both when nothing has been
    // matched yet and when the driver tracks no link of its own, which between them are
    // the cases the old code caught by testing for a null device.
    if (link == QStringLiteral("searching")) {
        return QStringLiteral("QZ: no device");
    }

    // A lost trainer displaces everything else. This overlay is the only QZ surface a rider
    // sees while the training app runs exclusive fullscreen (STRIP-SPEC.md 9.7), which makes
    // it the one place a silent five-minute reconnect can be announced to somebody who is
    // actually riding. Gear and resistance are meaningless anyway once the numbers behind
    // them have stopped arriving.
    //
    // Deliberately not one of the switchable lines: the per-line switches choose which
    // numbers are worth screen space, and this is not a number - it is the notice that the
    // numbers have stopped. Turning the whole overlay off is how a rider declines it.
    if (link == QStringLiteral("lost")) {
        const int secs = ride->retrySeconds();
        return QStringLiteral("QZ: TRAINER LOST\nRetrying%1")
            .arg(secs > 0 ? QStringLiteral(" in %1s").arg(secs) : QStringLiteral("..."));
    }
    if (link == QStringLiteral("gaveup")) {
        return QStringLiteral("QZ: TRAINER LOST\nGave up after 5 min");
    }

    QSettings settings;

    // Built as a list rather than concatenated, so a line that is switched off takes its
    // newline with it and the remaining ones do not end up separated by a blank row.
    QStringList lines;

    if (settings.value(QZSettings::osd_line_gear, QZSettings::default_osd_line_gear).toBool()) {
        lines << QStringLiteral("Gear: %1").arg(ride->gear());
    }
    if (settings.value(QZSettings::osd_line_erg, QZSettings::default_osd_line_erg).toBool()) {
        lines << QStringLiteral("ERG: %1").arg(ride->ergMode() ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }
    if (settings.value(QZSettings::osd_line_resistance, QZSettings::default_osd_line_resistance).toBool()) {
        lines << QStringLiteral("Resistance: %1").arg(ride->resistance());
    }

    return lines.join(QStringLiteral("\n"));
}

void QzOsd::refresh() {
    QSettings settings;
    const bool on = settings.value(QZSettings::osd_enabled, QZSettings::default_osd_enabled).toBool();

    const QString next = on ? compose() : QString();
    const bool wantWindow = on && settings.value(QZSettings::osd_window, QZSettings::default_osd_window).toBool();

    // Off is not "stop writing". RTSS redraws the last text it was handed for as long as the
    // slot stays claimed, so switching the sink off mid-ride would otherwise leave a frozen
    // gear on top of the training app. release() hands the slot back, and costs nothing on
    // the ticks after the first because there is then nothing mapped.
    if (on && settings.value(QZSettings::osd_rtss, QZSettings::default_osd_rtss).toBool()) {
        rtss.publish(next);
    } else {
        rtss.release();
    }

    if (next == current && wantWindow == showWindow) {
        return;
    }
    current = next;
    showWindow = wantWindow;
    emit changed();
}
