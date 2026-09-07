#ifndef GAMEPADANDROID_H
#define GAMEPADANDROID_H

#include <QString>
#include <QtGlobal>

#ifdef Q_OS_ANDROID

/**
 * @brief The pad state Android hands QZ, kept for gamepadcontroller to poll.
 *
 * Android has no XInput and no HID access without root, but it does not need either: the system
 * already recognises a gamepad and delivers its buttons as KeyEvents and its hat and triggers as
 * MotionEvent axes. CustomQtActivity intercepts both and calls straight in here, which is why
 * this is a plain state box rather than a class - the events arrive on the Android UI thread and
 * gamepadcontroller reads them from Qt's, so the only thing that has to be right is that the
 * handover is atomic.
 *
 * One honest limitation, which the mapping screen says out loud: Android delivers input to the
 * focused app. On Windows the pad is read from the device, so shifting works while the training
 * app owns the screen; here it only works while QZ is the app on screen. A pad in its keyboard
 * mode is no better - those are key events too, and they go to whoever has focus.
 */
namespace gamepadandroid {

/** @brief Whether Android currently lists a gamepad among its input devices. */
bool connected();

/** @brief The buttons held right now, in padbuttons terms. */
quint32 buttons();

/** @brief The pad's name as Android reports it, empty when there is none. */
QString name();

} // namespace gamepadandroid

#endif // Q_OS_ANDROID

#endif // GAMEPADANDROID_H
