#ifndef GAMEPADCONTROLLER_H
#define GAMEPADCONTROLLER_H

#include <QObject>
#include <QString>
#include <QTimer>

/**
 * @brief Turns a gamepad into QZ actions, without QZ needing focus where the platform allows it.
 *
 * QZ's keyboard shortcuts are declared with Qt.WindowShortcut, so they only fire while the QZ
 * window is in front. During a ride the training app owns the screen, which is exactly when the
 * shifting is wanted - so this polls the pad itself rather than synthesizing keys.
 *
 * Three backends, asked in this order:
 *
 * - **XInput** (Windows). A Bluetooth-paired Xbox Wireless Controller, and any third-party pad in
 *   its X-input mode, both arrive as ordinary XInput devices, so wired and wireless need no
 *   separate handling. Asked first because it is the one that knows an Xbox pad's own labels.
 * - **HID** (Windows, gamepadhid). Everything XInput cannot see: a pad with no X-input mode at all,
 *   such as an 8BitDo Micro in D-input mode, or a DualSense or Switch Pro. Windows still enumerates
 *   these as HID gamepads and gamepadhid reads their reports.
 * - **Android** (gamepadandroid). No polling of a device at all - Android recognises the pad and
 *   CustomQtActivity forwards its key and motion events. The catch, which the mapping screen says
 *   out loud, is that Android delivers input to the focused app: shifting from the pad works while
 *   QZ is on screen, not while the training app is.
 *
 * xinput1_4.dll and hid.dll are both resolved at runtime rather than linked, so a machine without
 * one costs a log line instead of a missing-DLL dialog at startup, and neither toolchain gains a
 * link dependency.
 */
class gamepadcontroller : public QObject {
    Q_OBJECT

    /** @brief False where no backend can read a pad: iOS, macOS, Linux, or Windows without both DLLs. */
    Q_PROPERTY(bool available READ available CONSTANT)
    /** @brief Whether a pad is answering any backend right now. */
    Q_PROPERTY(bool padConnected READ padConnected NOTIFY statusChanged)
    /** @brief Which XInput slot it answered on, or -1 for no pad and for the other two backends. */
    Q_PROPERTY(int padSlot READ padSlot NOTIFY statusChanged)
    /** @brief Which backend is reading it: "XInput", "HID", "Android", or empty for none. */
    Q_PROPERTY(QString backend READ backend NOTIFY statusChanged)
    /** @brief The pad's own name where the backend knows one. Empty for XInput, which does not. */
    Q_PROPERTY(QString padName READ padName NOTIFY statusChanged)
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
    ~gamepadcontroller();

    bool padConnected() const { return padPresent; }
    int padSlot() const { return activeSlot; }
    QString backend() const { return backendName; }
    QString padName() const { return deviceName; }
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
     * @brief Whether any backend can read a pad here. False on iOS, macOS and Linux, and on Windows
     * with neither an xinput DLL nor a HID parser - in which case the poll timer never starts.
     */
    bool available() const { return padAvailable; }

    /**
     * @brief Parse a mapping setting ("rt,lt") into a mask of pad buttons.
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
    /** @brief Ask each backend in turn for the current button word. */
    bool readPad(quint32 *buttons);
    /** @brief The XInput half of readPad(), including the slot scan. Always false off Windows. */
    bool readXInput(quint32 *buttons);
    /** @brief Record which backend answered, and tell the screen when that changes. */
    void setBackend(const QString &name, const QString &device);
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
    bool padAvailable = false;

    qint64 lastSettingsRefresh = 0;
    qint64 lastSlotProbe = 0;
    int activeSlot = -1;

    bool padPresent = false;
    quint32 heldButtons = 0;

    /** Which backend answered last, and what it calls the pad. Both empty when nothing answers. */
    QString backendName;
    QString deviceName;

#ifdef Q_OS_WIN
    /** The pads XInput cannot see. Owned here, polled only when XInput has nothing. */
    class gamepadhid *hidPad = nullptr;
#endif

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
