#include "qzpaths.h"

#include "qzsettings.h"
#include "simplecrypt.h"

#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QQmlFile>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_ANDROID
#include <QAndroidJniEnvironment>
#include <QAndroidJniObject>
#include <QtAndroid>
#include <jni.h>
#endif

#ifdef Q_OS_ANDROID
namespace {

// Both of these were file-static in homeform.cpp, inside its own Q_OS_ANDROID block,
// and the seven call sites that need them are in the bodies that moved here. Windows
// never compiled either side of that, which is why the move looked clean locally and
// broke only on the Android runner.

/** @return true if a JNI call left an exception pending; clears it either way. */
bool clearAndroidJniException(const char *context) {
    QAndroidJniEnvironment env;
    if (!env->ExceptionCheck()) {
        return false;
    }

    env->ExceptionDescribe();
    env->ExceptionClear();
    qWarning() << "Android JNI exception cleared during" << context;
    return true;
}

/** @return a usable file name when the content resolver will not give one up. */
QString fallbackFileNameFromUri(const QString &uriString) {
    QUrl url(uriString);
    QString fileName = url.fileName();
    if (!fileName.isEmpty()) {
        return fileName;
    }

    const QString lastSegment = url.path().section('/', -1);
    if (!lastSegment.isEmpty()) {
        return lastSegment;
    }

    return QStringLiteral("imported_file");
}

} // namespace
#endif

QString QzPaths::getWritableAppDir() {
    QString path = QLatin1String("");
#if defined(Q_OS_ANDROID)
    QSettings settings;
    bool android_documents_folder = settings.value(QZSettings::android_documents_folder, QZSettings::default_android_documents_folder).toBool();
    if (android_documents_folder || QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Android, 14)) {
        path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/QZ/";
        QDir().mkdir(path);
        // Create .nomedia file to prevent gallery indexing
        QFile nomediaFile(path + ".nomedia");
        if (!nomediaFile.exists()) {
            nomediaFile.open(QIODevice::WriteOnly);
            nomediaFile.close();
        }
    } else {
        path = getAndroidDataAppDir() + "/";
    }
#elif defined(Q_OS_MACOS) || defined(Q_OS_OSX)
    path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/";
#elif defined(Q_OS_IOS)
    path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/";
#elif defined(Q_OS_WINDOWS)
    path = QDir::currentPath() + "/";
#endif
    return path;
}

QString QzPaths::getProfileDir() {
    QString path = getWritableAppDir() + "profiles";
    QDir().mkdir(path);
    return path;
}

QString QzPaths::getAndroidDataAppDir() {
#ifdef Q_OS_ANDROID
    static QString path = "";

    if (path.length()) {
        return path;
    }

    QAndroidJniObject filesArr = QtAndroid::androidActivity().callObjectMethod(
        "getExternalFilesDirs", "(Ljava/lang/String;)[Ljava/io/File;", nullptr);
    jobjectArray dataArray = filesArr.object<jobjectArray>();
    QString out;
    if (dataArray) {
        QAndroidJniEnvironment env;
        jsize dataSize = env->GetArrayLength(dataArray);
        if (dataSize) {
            QAndroidJniObject mediaPath;
            QAndroidJniObject file;
            for (int i = 0; i < dataSize; i++) {
                file = env->GetObjectArrayElement(dataArray, i);
                if (!file.isValid())
                    continue;
                // isExternalStorageRemovable throws IllegalArgumentException on Waydroid/emulators
                // where vold can't resolve the storage volume — clear any pending exception.
                jboolean val = QAndroidJniObject::callStaticMethod<jboolean>(
                    "android/os/Environment", "isExternalStorageRemovable", "(Ljava/io/File;)Z", file.object());
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    val = JNI_FALSE;
                }
                mediaPath = file.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;");
                out = mediaPath.toString();
                if (!val)
                    break;
            }
        }
    }
    // Fallback to internal storage when external storage is unavailable (e.g. Waydroid)
    if (out.isEmpty()) {
        QAndroidJniObject internalDir = QtAndroid::androidActivity().callObjectMethod(
            "getFilesDir", "()Ljava/io/File;");
        if (internalDir.isValid()) {
            QAndroidJniObject internalPath = internalDir.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;");
            out = internalPath.toString();
        }
    }
    path = out;
    return out;
#else
    return QString();
#endif
}

quint64 QzPaths::cryptoKeySettingsProfiles() {
    QSettings settings;
    quint64 v = settings.value(QZSettings::cryptoKeySettingsProfiles, QZSettings::default_cryptoKeySettingsProfiles)
                    .toULongLong();
    if (!v) {
        QRandomGenerator r = QRandomGenerator();
        r.seed(QDateTime::currentMSecsSinceEpoch());
        v = r.generate64();
        settings.setValue(QZSettings::cryptoKeySettingsProfiles, v);
    }
    return v;
}

QString QzPaths::getFileNameFromContentUri(const QString &uriString) {
    qDebug() << "getFileNameFromContentUri" << uriString;
    if(!uriString.startsWith("content")) {
        return uriString;
    }
#ifdef Q_OS_ANDROID

    QAndroidJniObject jUriString = QAndroidJniObject::fromString(uriString);
    QAndroidJniObject jUri = QAndroidJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", jUriString.object<jstring>());
    if (clearAndroidJniException("Uri.parse") || !jUri.isValid()) {
        return fallbackFileNameFromUri(uriString);
    }
    QAndroidJniObject result = QAndroidJniObject::callStaticObjectMethod(
        "org/cagnulen/qdomyoszwift/ContentHelper",
        "getFileName",
        "(Landroid/content/Context;Landroid/net/Uri;)Ljava/lang/String;",
        QtAndroid::androidContext().object(),
        jUri.object());
    if (clearAndroidJniException("ContentHelper.getFileName") || !result.isValid()) {
        return fallbackFileNameFromUri(uriString);
    }

    QString fileName = result.toString();
    if (fileName.isEmpty()) {
        fileName = fallbackFileNameFromUri(uriString);
    }
    return fileName;
#else
    return uriString;
#endif
}

QString QzPaths::copyAndroidContentsURI(QUrl file, QString subfolder) {
#ifdef Q_OS_ANDROID        
    qDebug() << "Android Version:" << QOperatingSystemVersion::current();
    const QString sourcePath = QQmlFile::urlToLocalFileOrQrc(file);
    const QString destinationDir = getWritableAppDir() + subfolder + "/";
    QDir().mkpath(destinationDir);

    if (!sourcePath.isEmpty() && sourcePath.startsWith(destinationDir)) {
        qDebug() << "no need to copy file, the file is already in QZ subfolder" << file << subfolder;
        return sourcePath;
    }

    QString filename;
    if (file.toString().startsWith(QStringLiteral("content"))) {
        filename = getFileNameFromContentUri(file.toString());
    }
    if (filename.isEmpty() && !sourcePath.isEmpty()) {
        filename = QFileInfo(sourcePath).fileName();
    }
    if (filename.isEmpty()) {
        filename = QFileInfo(file.fileName()).fileName();
    }
    if (filename.isEmpty()) {
        filename = QStringLiteral("imported_file");
    }

    const QString dest = destinationDir + filename;
    qDebug() << file.fileName() << sourcePath << filename;
    QFile::remove(dest);

    if (file.toString().startsWith(QStringLiteral("content"))) {
        QAndroidJniObject jUriString = QAndroidJniObject::fromString(file.toString());
        QAndroidJniObject jUri = QAndroidJniObject::callStaticObjectMethod(
            "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", jUriString.object<jstring>());
        if (clearAndroidJniException("Uri.parse for copy") || !jUri.isValid()) {
            qWarning() << "Unable to parse content URI for copy" << file;
            return QString();
        }

        QAndroidJniObject jDest = QAndroidJniObject::fromString(dest);
        jboolean copied = QAndroidJniObject::callStaticMethod<jboolean>(
            "org/cagnulen/qdomyoszwift/ContentHelper",
            "copyContentToFile",
            "(Landroid/content/Context;Landroid/net/Uri;Ljava/lang/String;)Z",
            QtAndroid::androidContext().object(),
            jUri.object(),
            jDest.object<jstring>());
        if (clearAndroidJniException("ContentHelper.copyContentToFile")) {
            QFile::remove(dest);
            return QString();
        }

        qDebug() << "copyContentToFile" << dest << static_cast<bool>(copied);
        if (!copied || !QFile::exists(dest)) {
            QFile::remove(dest);
            return QString();
        }
        return dest;
    }

    QFile fileFile(sourcePath);
    bool copy = fileFile.copy(dest);
    qDebug() << "copy" << dest << copy << fileFile.exists() << fileFile.isReadable();
    return copy ? dest : QString();
#endif
    return file.toString();
}

void QzPaths::loadSettings(const QUrl &filename) {

    QFile file(QQmlFile::urlToLocalFileOrQrc(filename));
    QString settingsFile = file.fileName();
#ifdef Q_OS_ANDROID
    const QString copiedSettingsFile = copyAndroidContentsURI(filename, "settings");
    if (!copiedSettingsFile.isEmpty()) {
        settingsFile = copiedSettingsFile;
    }
#endif

    qDebug() << "QzPaths::loadSettings" << file.fileName();

    QSettings settings;
    QSettings settings2Load(settingsFile, QSettings::IniFormat);
    auto settings2LoadAllKeys = settings2Load.allKeys();
    for (const QString &s : qAsConst(settings2LoadAllKeys)) {
        if (!s.contains(QZSettings::cryptoKeySettingsProfiles)) {
            if (!s.contains(QStringLiteral("password")) && !s.contains(QStringLiteral("token"))) {
                settings.setValue(s, settings2Load.value(s));
            } else {
                SimpleCrypt crypt;
                crypt.setKey(cryptoKeySettingsProfiles());
                settings.setValue(s, crypt.decryptToString(settings2Load.value(s).toString()));
            }
        }
    }
    
}
