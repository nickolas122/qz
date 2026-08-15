#ifndef GAMEPADCONTROLLER_H
#define GAMEPADCONTROLLER_H

#include <QObject>
#include <QString>
#include <QTimer>

/**
 * @brief Turns an XInput gamepad into QZ actions, without QZ needing focus.
 *
 * QZ's keyboard shortcuts are declared with Qt.WindowShortcut, so they only fire while the QZ
 * window is in front. During a ride the training app owns the screen, which is exactly when the
 * shifting is wanted - so this polls the pad itself rather than synthesizing keys.
 *
 * XInput is the whole of the backend on purpose. A Bluetooth-paired Xbox Wireless Controller, and
 * any third-party pad in its X-input mode, both arrive as ordinary XInput devices on Windows, so
 * wired and wireless need no separate handling. Pads that only speak HID (DualSense, Switch Pro)
 * are invisible here and would need a Windows.Gaming.Input RawGameController backend beside this
 * one.
 *
 * xinput1_4.dll is resolved at runtime rather than linked, so a machine without it costs a log
 * line instead of a missing-DLL dialog at startup, and neither toolchain gains a link dependency.
 */
class gamepadcontroller : public QObject {
    Q_OBJECT

  public:
    explicit gamepadcontroller(QObject *parent = nullptr);

    /**
     * @brief Whether an XInput entry point was found. False on non-Windows, and on Windows without
     * any of the xinput DLLs - in which case the poll timer never starts.
     */
    bool available() const { return xinputAvailable; }

    /**
     * @brief Parse a mapping setting ("rt,lt") into a mask of XInput buttons.
     *
     * Comma-separated so one action can sit under either hand: the shifter is complete on both
     * shoulders, whichever way the pad ends up mounted on the bars. Unknown names are ignored, and
     * an empty string disables the action rather than binding it to everything.
     */
    static quint32 buttonMask(const QString &names);

    /** @brief The names buttonMask() accepts, in a stable order for the settings UI. */
    static QStringList buttonNames();

  signals:
    void gearUp();
    void gearDown();
    void ergToggle();

  private slots:
    void poll();

  private:
    /** @brief One mapped action, with the edge/repeat state that turns a held button into shifts. */
    struct action {
        quint32 mask = 0;
        bool repeats = false;
        bool down = false;
        qint64 nextRepeat = 0;
    };

    void refreshSettings();
    void releaseAll();
    bool readPad(quint32 *buttons);
    bool fired(action &a, quint32 buttons, qint64 now);

    QTimer timer;

    action gearUpAction;
    action gearDownAction;
    action ergAction;

    int repeatDelayMs = 0;
    int repeatRateMs = 0;
    bool enabled = false;
    bool xinputAvailable = false;

    qint64 lastSettingsRefresh = 0;
    qint64 lastSlotProbe = 0;
    int activeSlot = -1;
};

#endif // GAMEPADCONTROLLER_H
