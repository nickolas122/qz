#include "gamepadandroid.h"

#ifdef Q_OS_ANDROID

#include "gamepadbuttons.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include <atomic>
#include <jni.h>

namespace {

// android.view.KeyEvent keycodes. Spelled out here rather than read through JNI because they are
// part of Android's frozen API and a constant is cheaper than a call on every press.
constexpr int KEYCODE_DPAD_UP = 19;
constexpr int KEYCODE_DPAD_DOWN = 20;
constexpr int KEYCODE_DPAD_LEFT = 21;
constexpr int KEYCODE_DPAD_RIGHT = 22;
constexpr int KEYCODE_BUTTON_A = 96;
constexpr int KEYCODE_BUTTON_B = 97;
constexpr int KEYCODE_BUTTON_X = 99;
constexpr int KEYCODE_BUTTON_Y = 100;
constexpr int KEYCODE_BUTTON_L1 = 102;
constexpr int KEYCODE_BUTTON_R1 = 103;
constexpr int KEYCODE_BUTTON_L2 = 104;
constexpr int KEYCODE_BUTTON_R2 = 105;
constexpr int KEYCODE_BUTTON_THUMBL = 106;
constexpr int KEYCODE_BUTTON_THUMBR = 107;
constexpr int KEYCODE_BUTTON_START = 108;
constexpr int KEYCODE_BUTTON_SELECT = 109;

// A stick or trigger axis at rest never reads exactly zero, so a press has to clear a threshold.
// The same fraction XInput uses for its triggers, 30 of 255.
constexpr float AXIS_THRESHOLD = 0.12f;

// The two halves are held apart on purpose: buttons arrive as key events and the hat and triggers
// as motion events, on the same thread but in different callbacks. Each writer owning its own word
// means neither has to read the other's, so there is no read-modify-write to lose.
std::atomic<quint32> keyBits{0};
std::atomic<quint32> axisBits{0};
std::atomic<bool> padPresent{false};

QMutex nameLock;
QString padName;

quint32 maskFor(int keyCode) {
    switch (keyCode) {
    case KEYCODE_BUTTON_A:
        return padbuttons::A;
    case KEYCODE_BUTTON_B:
        return padbuttons::B;
    case KEYCODE_BUTTON_X:
        return padbuttons::X;
    case KEYCODE_BUTTON_Y:
        return padbuttons::Y;
    case KEYCODE_BUTTON_L1:
        return padbuttons::LB;
    case KEYCODE_BUTTON_R1:
        return padbuttons::RB;
    // A pad with digital shoulder triggers sends these as ordinary keys; one with analog triggers
    // sends the axes instead, and those land in axisBits under the same two masks.
    case KEYCODE_BUTTON_L2:
        return padbuttons::LT;
    case KEYCODE_BUTTON_R2:
        return padbuttons::RT;
    case KEYCODE_BUTTON_THUMBL:
        return padbuttons::LEFT_THUMB;
    case KEYCODE_BUTTON_THUMBR:
        return padbuttons::RIGHT_THUMB;
    case KEYCODE_BUTTON_START:
        return padbuttons::START;
    case KEYCODE_BUTTON_SELECT:
        return padbuttons::BACK;
    case KEYCODE_DPAD_UP:
        return padbuttons::DPAD_UP;
    case KEYCODE_DPAD_DOWN:
        return padbuttons::DPAD_DOWN;
    case KEYCODE_DPAD_LEFT:
        return padbuttons::DPAD_LEFT;
    case KEYCODE_DPAD_RIGHT:
        return padbuttons::DPAD_RIGHT;
    default:
        // BUTTON_C, BUTTON_Z, BUTTON_MODE and the rest have no slot in the vocabulary. Ignoring
        // them is better than folding them onto a slot that already means something else.
        return 0;
    }
}

} // namespace

namespace gamepadandroid {

bool connected() { return padPresent.load(); }

quint32 buttons() { return keyBits.load() | axisBits.load(); }

QString name() {
    QMutexLocker locker(&nameLock);
    return padName;
}

} // namespace gamepadandroid

extern "C" JNIEXPORT void JNICALL Java_org_cagnulen_qdomyoszwift_CustomQtActivity_nativeGamepadButton(
    JNIEnv *env, jclass clazz, jint keyCode, jboolean down) {
    Q_UNUSED(env)
    Q_UNUSED(clazz)

    const quint32 mask = maskFor(int(keyCode));
    if (mask == 0) {
        return;
    }
    if (down) {
        keyBits.fetch_or(mask);
    } else {
        keyBits.fetch_and(~mask);
    }
}

extern "C" JNIEXPORT void JNICALL Java_org_cagnulen_qdomyoszwift_CustomQtActivity_nativeGamepadAxes(
    JNIEnv *env, jclass clazz, jfloat hatX, jfloat hatY, jfloat leftTrigger, jfloat rightTrigger) {
    Q_UNUSED(env)
    Q_UNUSED(clazz)

    quint32 bits = 0;
    // AXIS_HAT_Y is negative upwards, the opposite of the screen convention it looks like.
    if (hatY <= -AXIS_THRESHOLD) {
        bits |= padbuttons::DPAD_UP;
    } else if (hatY >= AXIS_THRESHOLD) {
        bits |= padbuttons::DPAD_DOWN;
    }
    if (hatX <= -AXIS_THRESHOLD) {
        bits |= padbuttons::DPAD_LEFT;
    } else if (hatX >= AXIS_THRESHOLD) {
        bits |= padbuttons::DPAD_RIGHT;
    }
    if (leftTrigger >= AXIS_THRESHOLD) {
        bits |= padbuttons::LT;
    }
    if (rightTrigger >= AXIS_THRESHOLD) {
        bits |= padbuttons::RT;
    }

    axisBits.store(bits);
}

extern "C" JNIEXPORT void JNICALL Java_org_cagnulen_qdomyoszwift_CustomQtActivity_nativeGamepadPresence(
    JNIEnv *env, jclass clazz, jboolean present, jstring deviceName) {
    Q_UNUSED(clazz)

    QString name;
    if (deviceName) {
        const char *utf = env->GetStringUTFChars(deviceName, nullptr);
        if (utf) {
            name = QString::fromUtf8(utf);
            env->ReleaseStringUTFChars(deviceName, utf);
        }
    }

    if (!present) {
        // A pad that goes away with a button held would otherwise leave that button held for
        // ever, and the next pad would arrive already shifting.
        keyBits.store(0);
        axisBits.store(0);
    }

    padPresent.store(present);
    {
        QMutexLocker locker(&nameLock);
        padName = present ? name : QString();
    }
    qDebug() << QStringLiteral("gamepadandroid: pad") << (present ? QStringLiteral("present") : QStringLiteral("gone"))
             << name;
}

#endif // Q_OS_ANDROID
