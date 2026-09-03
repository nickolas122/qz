#include "volumekeys.h"

#include "qzsettings.h"

#include <QDateTime>
#include <QDebug>
#include <QSettings>

#ifdef Q_OS_ANDROID
#include <QAndroidJniObject>
#include <QtAndroid>
#include <jni.h>
#endif

namespace {

// The setting is re-read rather than watched, the same way gamepadcontroller re-reads its own:
// there is no change signal to connect to, and two seconds is quick enough for a switch a rider
// has just flicked.
constexpr int SETTINGS_REFRESH_MS = 2000;

// What "debouncing" means here: a volume key held down repeats fast enough to run through the
// whole cassette in a second. With it on, changes closer together than this are one shift.
constexpr int DEBOUNCE_MS = 300;

} // namespace

volumekeys *volumekeys::self = nullptr;

volumekeys::volumekeys(QObject *parent) : QObject(parent) {
    self = this;

    refreshSettings();

    timer.setTimerType(Qt::CoarseTimer);
    timer.setInterval(SETTINGS_REFRESH_MS);
    connect(&timer, &QTimer::timeout, this, &volumekeys::refreshSettings);
    timer.start();
}

volumekeys::~volumekeys() {
    setRegistered(false);
    if (self == this) {
        self = nullptr;
    }
}

void volumekeys::refreshSettings() {
    QSettings settings;
    enabled = settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool();
    debouncing =
        settings.value(QZSettings::gears_volume_debouncing, QZSettings::default_gears_volume_debouncing).toBool();

    // Registering parks the media volume in the middle, which is not something to do to a rider
    // who has the feature switched off - so the receiver comes and goes with the setting.
    setRegistered(enabled);
}

void volumekeys::setRegistered(bool wanted) {
    if (registered == wanted) {
        return;
    }

#ifdef Q_OS_ANDROID
    QAndroidJniObject activity = QtAndroid::androidActivity();
    if (!activity.isValid()) {
        return;
    }
    QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/MediaButtonReceiver",
                                              wanted ? "registerReceiver" : "unregisterReceiver",
                                              "(Landroid/content/Context;)V", activity.object());
    registered = wanted;
    qDebug() << QStringLiteral("volumekeys: volume shifting") << (wanted ? QStringLiteral("on") : QStringLiteral("off"));
#else
    Q_UNUSED(wanted)
#endif
}

void volumekeys::onVolumeChanged(int previous, int current, int max) {
    Q_UNUSED(max)

    if (!enabled || previous < 0 || current < 0 || previous == current) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (debouncing && now - lastShift < DEBOUNCE_MS) {
        return;
    }
    lastShift = now;

    const bool up = current > previous;
    qDebug() << QStringLiteral("volumekeys: gear") << (up ? QStringLiteral("up") : QStringLiteral("down"))
             << QStringLiteral("from volume") << previous << QStringLiteral("to") << current;

    // The broadcast arrives on Android's thread; RideState lives on Qt's, and a gear change that
    // crossed threads unqueued would be a data race on the bike.
    QMetaObject::invokeMethod(
        this,
        [this, up]() {
            if (up) {
                emit gearUp();
            } else {
                emit gearDown();
            }
        },
        Qt::QueuedConnection);
}

#ifdef Q_OS_ANDROID

// Declared in MediaButtonReceiver.java as an instance method, so the second argument is the
// receiver rather than its class.
extern "C" JNIEXPORT void JNICALL Java_org_cagnulen_qdomyoszwift_MediaButtonReceiver_nativeOnMediaButtonEvent(
    JNIEnv *env, jobject thiz, jint prev, jint current, jint max) {
    Q_UNUSED(env)
    Q_UNUSED(thiz)

    if (volumekeys::instance()) {
        volumekeys::instance()->onVolumeChanged(int(prev), int(current), int(max));
    }
}

#endif // Q_OS_ANDROID
