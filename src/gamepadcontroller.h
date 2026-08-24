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

    /** @brief False on non-Windows and on Windows without any xinput DLL. */
    Q_PROPERTY(bool available READ available CONSTANT)
    /** @brief Whether a pad is answering on one of the four XInput slots right now. */
    Q_PROPERTY(bool padConnected READ padConnected NOTIFY statusChanged)
    /** @brief Which slot it answered on, or -1. Shown so two pads can be told apart. */
    Q_PROPERTY(int padSlot READ padSlot NOTIFY statusChanged)
    /** @brief Names of the buttons held down this instant, for the live pad diagram. */
    Q_PROPERTY(QStringList pressedButtons READ pressedButtons NOTIFY statusChanged)
    /** @brief The action being bound ("gearUp"/"gearDown"/"erg"), or empty when idle. */
    Q_PROPERTY(QString capturing READ capturing NOTIFY statusChanged)

    Q_PROPERTY(QStringList gearUpButtons READ gearUpButtons NOTIFY bindingsChanged)
    Q_PROPERTY(QStringList gearDownButtons READ gearDownButtons NOTIFY bindingsChanged)
    Q_PROPERTY(QStringList ergButtons READ ergButtons NOTIFY bindingsChanged)
    Q_PROPERTY(int repeatDelay READ repeatDelay NOTIFY bindingsChanged)
    Q_PROPERTY(int repeatRate READ repeatRate NOTIFY bindingsChanged)

  public:
    explicit gamepadcontroller(QObject *parent = nullptr);

    bool padConnected() const { return padPresent; }
    int padSlot() const { return activeSlot; }
    QStringList pressedButtons() const;
    QString capturing() const { return captureAction; }

    QStringList gearUpButtons() const;
    QStringList gearDownButtons() const;
    QStringList ergButtons() const;
    int repeatDelay() const { return repeatDelayMs; }
    int repeatRate() const { return repeatRateMs; }

    /**
     * @brief Start listening for a button to bind to one action.
     *
     * Runs the poll even when gamepad shifting is switched off, so a rider can set the
     * pad up before enabling it. Buttons already held when capture starts are ignored
     * until they are released - otherwise resting a thumb on a trigger binds it.
     *
     * The binding commits on release rather than on press, so a mis-press can be rolled
     * off before it lands.
     */
    Q_INVOKABLE void beginCapture(const QString &action);
    Q_INVOKABLE void cancelCapture();

    /** @brief Drop one button from one action. Removing the last one disables it. */
    Q_INVOKABLE void unbind(const QString &action, const QString &button);

    Q_INVOKABLE void setRepeatDelay(int ms);
    Q_INVOKABLE void setRepeatRate(int ms);

    /** @return the action currently bound to a button name, or empty. */
    Q_INVOKABLE QString actionFor(const QString &button) const;

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

    /** @brief Pad presence, pressed buttons or capture mode changed. */
    void statusChanged();
    /** @brief A binding or a repeat timing changed, from here or from settings. */
    void bindingsChanged();

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

    /** @brief Run one frame of the bind-on-release capture. @return true if it committed. */
    void pollCapture(quint32 buttons);
    /** @brief The QZSettings key an action name maps to, or empty for an unknown action. */
    static QString settingKeyFor(const QString &action);
    /** @brief The comma-separated setting for one action, split and trimmed. */
    static QStringList boundButtons(const QString &action);

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

    bool padPresent = false;
    quint32 heldButtons = 0;

    /** Empty when not capturing. Otherwise "gearUp", "gearDown" or "erg". */
    QString captureAction;
    /**
     * Buttons that were already down when capture started, and so do not count until
     * released. Cleared bit by bit as they come up.
     */
    quint32 captureBaseline = 0;
    /** The button that went down during capture and is waiting to be released. */
    quint32 capturePending = 0;
};

#endif // GAMEPADCONTROLLER_H
