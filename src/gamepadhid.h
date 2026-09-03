#ifndef GAMEPADHID_H
#define GAMEPADHID_H

#include <QString>
#include <QtGlobal>

#ifdef Q_OS_WIN

/**
 * @brief Reads a plain HID gamepad on Windows - the pads XInput cannot see.
 *
 * XInput only answers for Xbox pads and for third-party pads in their X-input mode. A pad that
 * has no X-input mode at all - an 8BitDo Micro, which offers Switch, D-input and keyboard modes
 * and nothing else - is invisible to it however it is connected, Bluetooth or cable. Windows
 * still enumerates it as an ordinary HID gamepad, which is what this reads.
 *
 * The device is found through the raw input device list (usage page 1, usage 4 or 5), opened
 * with CreateFile, and its input reports are read with overlapped I/O so a poll never blocks on
 * a pad that is sitting still. The reports are decoded with the HID parser rather than by hand,
 * so a pad that lays its buttons out differently still comes out right.
 *
 * hid.dll is resolved at runtime, for the same reason gamepadcontroller resolves xinput: a
 * missing DLL should cost a log line, not a startup dialog, and neither toolchain should gain a
 * link dependency for a fallback backend.
 *
 * Numbered HID buttons are mapped positionally onto padbuttons::TABLE - button 1 becomes "a",
 * button 2 "b", and so on. There is no honest alternative: HID reports carry numbers, not
 * labels, and no two pads number them the same way. It does not matter in practice because the
 * mapping screen binds by pressing: the rider presses the paddle they want and whatever slot it
 * lands on is the slot that gets bound.
 */
class gamepadhid {
  public:
    gamepadhid();
    ~gamepadhid();

    gamepadhid(const gamepadhid &) = delete;
    gamepadhid &operator=(const gamepadhid &) = delete;

    /** @brief False when hid.dll has no HID parser in it, in which case poll() never succeeds. */
    bool available() const { return hidAvailable; }

    /**
     * @brief Take one frame from the pad, opening or reopening one if needed.
     *
     * @param buttons receives the current button word, in padbuttons terms.
     * @param now the poll's own timestamp, so the rescan is on the caller's clock.
     * @return true while a pad is open and readable. A pad that is simply idle still returns
     *         true with its last state - HID pads report on change, not on a schedule.
     */
    bool poll(quint32 *buttons, qint64 now);

    /** @brief The pad's own product string, or a generic name if it has none. Empty when closed. */
    QString name() const { return padName; }

    /** @brief Drop the device, so the next poll() looks for one again. */
    void close();

  private:
    struct device;

    /** @brief Find and open the first HID gamepad or joystick. @return true if one opened. */
    bool open();
    /** @brief Turn one input report into a button word. */
    bool decode(const char *report, int length, quint32 *buttons);
    /** @brief Start one overlapped read. @return false if the device has gone. */
    bool armRead();

    // Opaque so the header does not drag windows.h into everything that includes it.
    device *dev = nullptr;

    bool hidAvailable = false;
    QString padName;
    quint32 state = 0;
    qint64 lastScan = 0;
};

#endif // Q_OS_WIN

#endif // GAMEPADHID_H
