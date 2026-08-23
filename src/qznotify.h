#ifndef QZNOTIFY_H
#define QZNOTIFY_H

#include <QObject>
#include <QString>

/**
 * @brief Where the bridge posts short messages for whichever UI is loaded.
 *
 * Every one of these used to go through homeform::singleton()->setToastRequested(),
 * which meant a battery reading in ftmsbike.cpp had to know the UI class existed and
 * had to null-check it, twenty times over. That is backwards: a device driver has
 * something to say and no business knowing who listens.
 *
 * So it signals into here instead, and a UI attaches if there is one. With no UI - the
 * headless smoke test, the gtest suite - the signal goes nowhere and costs nothing,
 * which is why toast() has no return value and no null check at its call sites.
 *
 * Main-thread only, like the calls it replaces: Qt delivers BLE characteristic
 * notifications on the thread that owns the controller, and that is where every
 * existing caller already ran.
 *
 * See STRIP-SPEC.md section 7 group F - this is one of the four things that had to be
 * unpicked before homeform could be deleted.
 */
class QzNotify : public QObject {
    Q_OBJECT

  public:
    /** @brief The instance the UI connects to. Created on first use. */
    static QzNotify *singleton();

    /** @brief Post a message. Safe to call with no UI loaded, and from anywhere. */
    static void toast(const QString &message);

  signals:
    void toastRequested(const QString &message);

  private:
    explicit QzNotify(QObject *parent = nullptr) : QObject(parent) {}
};

#endif // QZNOTIFY_H
