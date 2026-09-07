#ifndef GAMEPADHID_H
#define GAMEPADHID_H

#include "gamepadbuttons.h"

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
 * A HID report carries numbers, not labels, and no two pads number them the same way, so what
 * comes out of decode() is a padraw word - one bit per physical input, named nothing. A map turns
 * that into padbuttons names. Its default is a guess: button 1 is "a", button 2 is "b" down the
 * list, and the d-pad is whichever of the hat and the X/Y axes the pad actually moves. The guess
 * is right often enough to be worth starting from and wrong often enough that the rider can
 * replace it, which the pad screen does by asking them to press the button they just pointed at.
 *
 * Buttons, hat and axes are all read, because a pad will use any of the three for the same
 * control: an 8BitDo Micro declares a hat, leaves it at its null value forever, and puts the
 * d-pad on X and Y. Reading only buttons and hats did not mislabel those four presses, it lost
 * them - and a press that never arrives is one the rider cannot even remap.
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

    /**
     * @brief What the pad physically sent last frame, one bit per padraw source.
     *
     * The un-named layer, for the screen that does the naming: it is the only way to tell a
     * rider "something arrived, it was button 13" about an input no map has a name for yet.
     */
    quint64 physical() const { return physicalState; }

    /**
     * @brief Log every report while the rider is naming a button, and nothing otherwise.
     *
     * The report bytes and axis values are what identified the Micro's d-pad. They are also far
     * too noisy to carry through a ride, and the one moment they are worth having is the one
     * moment a rider is asking QZ what it can see.
     */
    void setVerbose(bool on) { verbose = on; }

    /**
     * @brief Load a saved map, or ignore it when it belongs to a different pad.
     *
     * @param spec "b1=a,hat_up=dpad_up", exactly as mapSpec() writes it. Empty restores the
     *        positional guess.
     * @param forPad the product name the map was captured on. A map applied to the wrong pad
     *        would name its buttons confidently and wrongly, so it is only used on a match.
     */
    void setMap(const QString &spec, const QString &forPad);

    /** @brief The map as it stands, ready for the setting. Always complete, never a diff. */
    QString mapSpec() const;

    /**
     * @brief Make one physical input mean one button.
     *
     * The button is taken away from whatever used to produce it: a label has one source, or a
     * single press would fire two actions.
     *
     * @param rawBit a padraw bit index.
     * @param buttonMask a padbuttons mask, or 0 to leave the input unnamed.
     */
    void remap(int rawBit, quint32 buttonMask);

    /** @brief Throw the rider's map away and go back to the positional guess. */
    void resetMap();

    /** @brief Drop the device, so the next poll() looks for one again. */
    void close();

  private:
    struct device;

    /** @brief Find and open the first HID gamepad or joystick. @return true if one opened. */
    bool open();
    /** @brief Turn one input report into a button word, and into the padraw word behind it. */
    bool decode(const char *report, int length, quint32 *buttons);
    /** @brief Start one overlapped read. @return false if the device has gone. */
    bool armRead();
    /** @brief Rebuild map[] from the guess, then from the saved spec if it is for this pad. */
    void rebuildMap();

    // Opaque so the header does not drag windows.h into everything that includes it.
    device *dev = nullptr;

    bool hidAvailable = false;
    bool verbose = false;
    QString padName;
    quint32 state = 0;
    quint64 physicalState = 0;
    qint64 lastScan = 0;

    /** What each physical source is called, in padbuttons masks. 0 is "this input has no name". */
    quint32 map[padraw::COUNT] = {0};
    /** The saved map and the pad it was captured on, kept so open() can re-decide on a new pad. */
    QString savedSpec;
    QString savedPad;
};

#endif // Q_OS_WIN

#endif // GAMEPADHID_H
