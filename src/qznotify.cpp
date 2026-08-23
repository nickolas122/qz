#include "qznotify.h"

QzNotify *QzNotify::singleton() {
    // Function-local static: constructed on first use, after QCoreApplication exists,
    // which a file-scope instance could not promise. Never deleted - it outlives every
    // device that posts to it and both UI trees that listen.
    static QzNotify *instance = new QzNotify();
    return instance;
}

void QzNotify::toast(const QString &message) {
    if (message.isEmpty())
        return;
    emit singleton()->toastRequested(message);
}
