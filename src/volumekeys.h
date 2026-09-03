#ifndef VOLUMEKEYS_H
#define VOLUMEKEYS_H

#include <QObject>
#include <QTimer>

/**
 * @brief Shifts from the Android volume keys, while the training app owns the screen.
 *
 * This exists because of what gamepadandroid cannot do. Android delivers key and motion events to
 * the focused app, so a pad read that way only shifts while QZ is in front - which during a ride
 * it is not. A volume change is different: the system handles the volume keys itself and
 * broadcasts VOLUME_CHANGED_ACTION to every registered receiver, whoever has focus. That makes it
 * the only input route on Android that works during a ride.
 *
 * It also gives the pad a way in. A pad with a keyboard mode - an 8BitDo Micro in K mode, say -
 * can be programmed to send volume up and volume down, and then two of its buttons shift while
 * the rider is looking at Zwift.
 *
 * MediaButtonReceiver.java has been in the tree the whole time, along with the
 * `volume_change_gears` setting, but the native method it calls went with homeform: nothing
 * implemented nativeOnMediaButtonEvent, nothing ever called registerReceiver, and the setting
 * only set QT_ANDROID_VOLUME_KEYS, which routes the keys to a focused QZ and so misses the point.
 * This is the missing half.
 *
 * The Java side parks the media volume back at 7 after every change, which is what makes the
 * shifting endless: without it a rider would run out of volume after a few gears.
 */
class volumekeys : public QObject {
    Q_OBJECT

  public:
    explicit volumekeys(QObject *parent = nullptr);
    ~volumekeys();

    /** @brief The instance the JNI callback reaches. Null off Android and before main builds one. */
    static volumekeys *instance() { return self; }

    /**
     * @brief One volume change, straight off the Android broadcast thread.
     *
     * Louder is a shift up and quieter a shift down. The signal is emitted through a queued
     * invocation because this runs on Android's thread and RideState lives on Qt's.
     */
    void onVolumeChanged(int previous, int current, int max);

  signals:
    void gearUp();
    void gearDown();

  private:
    /** @brief Re-read the setting, and register or unregister the receiver if it changed. */
    void refreshSettings();
    void setRegistered(bool wanted);

    static volumekeys *self;

    QTimer timer;
    bool enabled = false;
    bool debouncing = false;
    bool registered = false;
    qint64 lastShift = 0;
};

#endif // VOLUMEKEYS_H
