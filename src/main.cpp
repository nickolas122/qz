#include <QApplication>
#include <QStyleFactory>
#include <stdio.h>
#include <stdlib.h>
#ifdef Q_OS_LINUX
#ifndef Q_OS_ANDROID
#include <unistd.h> // getuid
#include "EventHandler.h"
#endif
#endif
#include <QQmlContext>
#include "logwriter.h"
#include "qzforkversion.h"
#include "bluetooth.h"
#include "devices/dircon/dirconmanager.h"
// Not Q_OS_WIN-guarded any more. The class is compiled on every platform - the .pri has
// always listed it unconditionally - and reports available() == false where XInput is
// not there, which is what the mapping screen needs an object to ask.
#include "gamepadcontroller.h"
#include "ui/qzosd.h"
#include "volumekeys.h"
#include "qznotify.h"
#include "qzpaths.h"
// Reached through homeform.h until 7c-2b deleted it. The dark-palette block below has
// always needed these.
#include <QColor>
#include <QPalette>
#include <QThread>
#include "templateinfosenderbuilder.h"
#include <QDir>
#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QList>
#ifdef CHARTJS
#include <QtWebView/QtWebView>
#endif

// qdomyos-zwift.pri stamps the real one in. This keeps any build path that does
// not go through it compiling rather than failing on an undefined symbol.
#ifndef QZ_GIT_SHA
#define QZ_GIT_SHA "unknown"
#endif

#include "androidstatusbar.h"
#include "fontmanager.h"
#include "filesearcher.h"

#ifdef Q_OS_ANDROID
#include "keepawakehelper.h"
#include <QtAndroid>
#endif

#ifdef Q_OS_MACOS
#include "macos/lockscreen.h"
#endif

#ifdef Q_OS_IOS
#include "ios/lockscreen.h"
#endif


#include "ui/language.h"
#include "ui/ridestate.h"
#include "handleurl.h"
#include "mywhooshlink.h"

bool logs = true;
bool noWriteResistance = false;
bool noHeartService = true;
bool noConsole = false;
bool onlyVirtualBike = false;
bool testResistance = false;
bool forceQml = true;
bool miles = false;
bool bluetooth_no_reconnection = false;
bool bluetooth_relaxed = false;
bool bike_cadence_sensor = false;
bool bike_power_sensor = false;
bool battery_service = false;
bool service_changed = false;
bool bike_wheel_revs = false;
bool zwift_play = false;
bool zwift_click = false;
bool zwift_play_emulator = false;
bool virtual_device_bluetooth = true;
QString eventGearDevice = QStringLiteral("");
QString deviceName = QLatin1String("");
uint32_t pollDeviceTime = 200;
int8_t bikeResistanceOffset = 4;
double bikeResistanceGain = 1.0;
QString power_sensor_name = QStringLiteral("Disabled");
bool smokeTest = false;
// The bike that is not there - docs/fork/VIRTUAL-BIKE.md. Flags rather than settings-only so a
// simulated session can be started from a shortcut without touching a saved profile.
bool simulatedBike = false;
QString simulatedBikeRide = QLatin1String("");
QString logfilename = QStringLiteral("debug-") +
                      QDateTime::currentDateTime()
                          .toString()
                          .replace(QStringLiteral(":"), QStringLiteral("_"))
                          .replace(QStringLiteral(" "), QStringLiteral("_"))
                          .replace(QStringLiteral("."), QStringLiteral("_")) +
                      QStringLiteral(".log");
QUrl profileToLoad;
static const QtMessageHandler QT_DEFAULT_MESSAGE_HANDLER = qInstallMessageHandler(0);

// Function to display help information and exit
void displayHelp() {
    // Test string for translation workflow - will be extracted by lupdate
    QString testTranslation = QCoreApplication::translate("main", "QDomyos-Zwift - Fitness Equipment Bridge");
    Q_UNUSED(testTranslation); // Suppress unused variable warning

    printf("qDomyos-Zwift Usage:\n");
    printf("General options:\n");
    printf("  -h, --help                    Display this help message and exit\n");
    printf("  -no-gui                       Run in non-GUI mode\n");
    printf("  -qml                          Force QML mode\n");
    printf("  -noqml                        Disable QML mode\n");
    printf("  -miles                        Use miles instead of kilometers\n");
    printf("  -no-console                   Disable console output\n");
    printf("  -no-log                       Disable logging\n");
    printf("  -profile <name>               Load specific profile\n");

    printf("\nDevice configuration:\n");
    printf("  -name <device_name>           Set device name\n");
    printf("  -simulated-bike               Run against a simulated bike instead of a real one\n");
    printf("  -ride <file.ride>             Ride scenario for -simulated-bike\n");
    printf("  -poll-device-time <ms>        Set device polling time in milliseconds\n");
    printf("  -no-write-resistance          Disable resistance writing\n");
    printf("  -no-heart-service             Disable heart rate service\n");
    printf("  -heart-service                Enable heart rate service\n");
    printf("  -no-virtual-device-bluetooth  Disable virtual device bluetooth\n");

    printf("\nBike specific options:\n");
    printf("  -only-virtualbike             Run only virtual bike mode\n");
    printf("  -bike-resistance-gain <value> Set bike resistance gain\n");
    printf("  -bike-resistance-offset <value> Set bike resistance offset\n");
    printf("  -bike-cadence-sensor          Enable bike cadence sensor\n");
    printf("  -bike-power-sensor            Enable bike power sensor\n");
    printf("  -bike-wheel-revs              Enable bike wheel revolution tracking\n");
    printf("  -power-sensor-name <name>     Set power sensor name\n");

    printf("\nBluetooth options:\n");
    printf("  -no-reconnection              Disable bluetooth reconnection\n");
    printf("  -bluetooth_relaxed            Enable relaxed bluetooth mode\n");
    printf("  -battery-service              Enable battery service\n");
    printf("  -service-changed              Enable service changed notifications\n");
    printf("  -bluetooth-event-gear-device <device> Set bluetooth event gear device\n");

    printf("\nIntegration options:\n");
    printf("  -zwift_play                   Enable Zwift Play\n");
    printf("  -zwift_click                  Enable Zwift Click\n");
    printf("  -zwift_play_emulator          Enable Zwift Play emulator\n");
    printf("  -smoke-test                   Run smoke test (verify Qt loads, print SMOKE_OK, exit)\n");

    printf("\nOther options:\n");
    printf("  -test-resistance              Enable resistance testing\n");

    exit(0);
}


#ifdef Q_CC_MSVC
#include <windows.h>
#include <dbghelp.h>
#include <rtcapi.h>
#include <cstdio>

void PrintStack() {
    CONTEXT context = {};
    RtlCaptureContext(&context);

    STACKFRAME64 stackFrame = {};
    stackFrame.AddrPC.Offset = context.Rip;  // Per x64, usa Rip
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymInitialize(process, NULL, TRUE);

    while (StackWalk64(
        IMAGE_FILE_MACHINE_AMD64, process, thread, &stackFrame, &context,
        NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
        printf("Address: 0x%llx\n", stackFrame.AddrPC.Offset);
    }

    SymCleanup(process);
}

int __cdecl CustomRTCErrorHandler(int errorType, const wchar_t* filename, int linenumber, 
                           const wchar_t* moduleName, const wchar_t* format, ...)
{
    // Buffer for the formatted error message
    wchar_t errorMessage[512];
    va_list args;
    
    // Format the error message using varargs
    va_start(args, format);
    vswprintf(errorMessage, sizeof(errorMessage)/sizeof(wchar_t), format, args);
    va_end(args);
    
    // Print complete error information
    fwprintf(stderr, L"Runtime Error Check Failed!\n");
    fwprintf(stderr, L"Error Type: %d\n", errorType);
    fwprintf(stderr, L"File: %ls\n", filename ? filename : L"Unknown");
    fwprintf(stderr, L"Line: %d\n", linenumber);
    fwprintf(stderr, L"Module: %ls\n", moduleName ? moduleName : L"Unknown");
    fwprintf(stderr, L"Error Message: %ls\n", errorMessage);
    fwprintf(stderr, L"----------------------------------------\n");
    
    #ifdef _DEBUG
        __debugbreak();  // Break into debugger in debug builds
    #endif
  
    PrintStack();

    return 1;  // Return non-zero to indicate error was handled    
}
#endif

QCoreApplication *createApplication(int &argc, char *argv[]) {

    QSettings settings;
    bool nogui = false;

    for (int i = 1; i < argc; ++i) {
        if (!qstrcmp(argv[i], "-h") || !qstrcmp(argv[i], "--help")) {
            displayHelp();
            // displayHelp() will exit the program
        }
        if (!qstrcmp(argv[i], "-no-gui")) {
            nogui = true;
            forceQml = false;
        }
        if (!qstrcmp(argv[i], "-qml"))
            forceQml = true;
        if (!qstrcmp(argv[i], "-noqml"))
            forceQml = false;
        if (!qstrcmp(argv[i], "-miles"))
            miles = true;
        if (!qstrcmp(argv[i], "-no-console"))
            noConsole = true;
        if (!qstrcmp(argv[i], "-test-resistance"))
            testResistance = true;
        if (!qstrcmp(argv[i], "-simulated-bike"))
            simulatedBike = true;
        if (!qstrcmp(argv[i], "-ride")) {
            simulatedBikeRide = argv[++i];
            simulatedBike = true;
        }
        if (!qstrcmp(argv[i], "-no-virtual-device-bluetooth"))
            virtual_device_bluetooth = false;
        if (!qstrcmp(argv[i], "-no-log"))
            logs = false;
        if (!qstrcmp(argv[i], "-no-write-resistance"))
            noWriteResistance = true;
        if (!qstrcmp(argv[i], "-no-heart-service"))
            noHeartService = true;
        if (!qstrcmp(argv[i], "-heart-service"))
            noHeartService = false;
        if (!qstrcmp(argv[i], "-only-virtualbike"))
            onlyVirtualBike = true;
        if (!qstrcmp(argv[i], "-no-reconnection"))
            bluetooth_no_reconnection = true;
        if (!qstrcmp(argv[i], "-bluetooth_relaxed"))
            bluetooth_relaxed = true;
        if (!qstrcmp(argv[i], "-bike-cadence-sensor"))
            bike_cadence_sensor = true;
        if (!qstrcmp(argv[i], "-bike-power-sensor"))
            bike_power_sensor = true;
        if (!qstrcmp(argv[i], "-battery-service"))
            battery_service = true;
        if (!qstrcmp(argv[i], "-service-changed"))
            service_changed = true;
        if (!qstrcmp(argv[i], "-bike-wheel-revs"))
            bike_wheel_revs = true;
        if (!qstrcmp(argv[i], "-zwift_play"))
            zwift_play = true;
        if (!qstrcmp(argv[i], "-zwift_click"))
            zwift_click = true;
        if (!qstrcmp(argv[i], "-zwift_play_emulator"))
            zwift_play_emulator = true;
        if (!qstrcmp(argv[i], "-smoke-test")) {
            smokeTest = true;
            nogui = true;
        }
        if (!qstrcmp(argv[i], "-name")) {

            deviceName = argv[++i];
        }
        if (!qstrcmp(argv[i], "-bluetooth-event-gear-device")) {

            eventGearDevice = argv[++i];
        }
        if (!qstrcmp(argv[i], "-poll-device-time")) {

            pollDeviceTime = atol(argv[++i]);
        }
        if (!qstrcmp(argv[i], "-bike-resistance-gain")) {

            bikeResistanceGain = atof(argv[++i]);
        }
        if (!qstrcmp(argv[i], "-bike-resistance-offset")) {

            bikeResistanceOffset = atoi(argv[++i]);
        }
        if (!qstrcmp(argv[i], "-profile")) {
            QString profileName = argv[++i];
            if (QFile::exists(QzPaths::getProfileDir() + "/" + profileName + ".qzs")) {
                profileToLoad = QUrl::fromLocalFile(QzPaths::getProfileDir() + "/" + profileName + ".qzs");
            } else {
                qDebug() << QzPaths::getProfileDir() + "/" + profileName << "not found!";
            }
        }
        if (!qstrcmp(argv[i], "-power-sensor-name")) {
            power_sensor_name = argv[++i];
        }
    }

    if (nogui) {
        return new QCoreApplication(argc, argv);
    } else if (forceQml) {
        return new QApplication(argc, argv);
    } else {

        QApplication *a = new QApplication(argc, argv);

        a->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

        /*QFont defaultFont = QApplication::font();
        defaultFont.setPointSize(defaultFont.pointSize()+2);
        qApp->setFont(defaultFont);*/

        // modify palette to dark
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
        darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::Dark, QColor(35, 35, 35));
        darkPalette.setColor(QPalette::Shadow, QColor(20, 20, 20));
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(127, 127, 127));

        qApp->setPalette(darkPalette);

        return a;
    }
}

// Global thread and writer instance
static QThread *logThread = nullptr;
static LogWriter *logWriter = nullptr;

void initializeLogThread() {
    if (!logThread) {
        logThread = new QThread();
        logWriter = new LogWriter();
        logWriter->moveToThread(logThread);
        logThread->start();
    }
}

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg) {

    QSettings settings;
    static bool logdebug = settings.value(QZSettings::log_debug, QZSettings::default_log_debug).toBool();
#if defined(Q_OS_LINUX) // Linux OS does not read settings file for now
    if ( (logs == false && !forceQml) || (logdebug == false && forceQml))
#else
    if (logdebug == false)
#endif
        return;

    // QByteArray localMsg = msg.toLocal8Bit(); // NOTE: clazy-unused-non-trivial-variable
    const char *file = context.file ? context.file : "";
    const char *function = context.function ? context.function : "";
    QString txt = QDateTime::currentDateTime().toString() + QStringLiteral(" ") +
                  QString::number(QDateTime::currentMSecsSinceEpoch()) + QStringLiteral(" ");
    switch (type) {
    case QtInfoMsg:
        txt += QStringLiteral("Info: %1 %2 %3\n").arg(file, function, msg); // NOTE: clazy-qstring-arg
        break;
    case QtDebugMsg:
        txt += QStringLiteral("Debug: %1 %2 %3\n").arg(file, function, msg); // NOTE: clazy-qstring-arg
        break;
    case QtWarningMsg:
        txt += QStringLiteral("Warning: %1 %2 %3\n").arg(file, function, msg); // NOTE: clazy-qstring-arg
        break;
    case QtCriticalMsg:
        txt += QStringLiteral("Critical: %1 %2 %3\n").arg(file, function, msg); // NOTE: clazy-qstring-arg
        break;
    case QtFatalMsg:
        txt += QStringLiteral("Fatal: %1 %2 %3\n").arg(file, function, msg); // NOTE: clazy-qstring-arg
        abort();
    }

    if (logs == true || logdebug == true) {

        QString path = QzPaths::getWritableAppDir();

        // Ensure thread is initialized
        initializeLogThread();

        // Write log in the worker thread
        QMetaObject::invokeMethod(logWriter, "writeLog",
                                 Qt::QueuedConnection,
                                 Q_ARG(QString, path + logfilename),
                                 Q_ARG(QString, txt));

    }
    (*QT_DEFAULT_MESSAGE_HANDLER)(type, context, msg);
}

int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN32
    qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", "windowsmediafoundation");
#endif

#ifdef Q_CC_MSVC
  _RTC_SetErrorFuncW(CustomRTCErrorHandler);
#endif
  
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QScopedPointer<QCoreApplication> app(createApplication(argc, argv));
#else
#ifdef Q_OS_IOS
    HandleURL *URLHandler = new HandleURL();
    QDesktopServices::setUrlHandler("org.cagnulein.ConnectIQComms-ciq", URLHandler, "handleURL");
#endif

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QScopedPointer<QApplication> app(new QApplication(argc, argv));
#endif

#ifdef CHARTJS
    QtWebView::initialize();
#endif

#ifdef Q_OS_LINUX
#ifndef Q_OS_ANDROID
    if (getuid() && !smokeTest) {

        printf("Runme as root!\n");
        return -1;
    } else
        printf("%s", "OK, you are root.\n");
#endif
#endif

    if (smokeTest) {
        printf("SMOKE_OK\n");
        return 0;
    }

    app->setOrganizationName(QStringLiteral("Roberto Viola"));
    app->setOrganizationDomain(QStringLiteral("robertoviola.cloud"));
    app->setApplicationName(QStringLiteral("qDomyos-Zwift"));

    QSettings settings;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString profileName = "";
#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
    profileName = lockscreen::get_action_profile();
    lockscreen::nslog(QString("quick_action profile " + profileName).toLatin1());
#endif
#else
    QAndroidJniObject javaPath = QAndroidJniObject::fromString(QzPaths::getWritableAppDir());
    QAndroidJniObject r = QAndroidJniObject::callStaticObjectMethod("org/cagnulen/qdomyoszwift/Shortcuts", "getProfileExtras",
                                                "(Landroid/content/Context;)Ljava/lang/String;", QtAndroid::androidContext().object());
    profileName = r.toString();
#endif
    
    QFileInfo pp(profileName);
    profileName = pp.baseName();
    
    if(profileName.count()) {
        if (QFile::exists(QzPaths::getProfileDir() + "/" + profileName + ".qzs")) {
            profileToLoad = QUrl::fromLocalFile(QzPaths::getProfileDir() + "/" + profileName + ".qzs");
        } else {
            qDebug() << QzPaths::getProfileDir() + "/" + profileName << "not found!";
        }
    }
#endif

    if (!profileToLoad.isEmpty()) {
        QzPaths::loadSettings(profileToLoad);
    }

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)

    if (forceQml)
#endif
    {
        bool defaultNoHeartService = !noHeartService;

        // some Android 6 doesn't support wake lock
        if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::Android, 7) &&
            !settings.value(QZSettings::android_wakelock).isValid()) {
            settings.setValue(QZSettings::android_wakelock, false);
        }

        noHeartService = settings.value(QZSettings::bike_heartrate_service, defaultNoHeartService).toBool();
        bikeResistanceOffset = settings.value(QZSettings::bike_resistance_offset, bikeResistanceOffset).toInt();
        bikeResistanceGain = settings.value(QZSettings::bike_resistance_gain_f, bikeResistanceGain).toDouble();
        deviceName = settings.value(QZSettings::filter_device, QZSettings::default_filter_device).toString();
        pollDeviceTime = settings.value(QZSettings::poll_device_time, QZSettings::default_poll_device_time).toInt();
    }
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    else {
        settings.setValue(QZSettings::miles_unit, miles);
        settings.setValue(QZSettings::bluetooth_no_reconnection, bluetooth_no_reconnection);
        settings.setValue(QZSettings::bluetooth_relaxed, bluetooth_relaxed);
        settings.setValue(QZSettings::bike_cadence_sensor, bike_cadence_sensor);
        settings.setValue(QZSettings::bike_power_sensor, bike_power_sensor);
        settings.setValue(QZSettings::battery_service, battery_service);
        settings.setValue(QZSettings::service_changed, service_changed);
        settings.setValue(QZSettings::bike_wheel_revs, bike_wheel_revs);
        settings.setValue(QZSettings::zwift_click, zwift_click);
        settings.setValue(QZSettings::zwift_play, zwift_play);
        settings.setValue(QZSettings::zwift_play_emulator, zwift_play_emulator);
        settings.setValue(QZSettings::virtual_device_bluetooth, virtual_device_bluetooth);
        settings.setValue(QZSettings::power_sensor_name, power_sensor_name);
    }
#endif

#ifdef Q_OS_ANDROID
    if (settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
        qDebug() << "handling volume keys";
        qputenv("QT_ANDROID_VOLUME_KEYS", "1"); // "1" is dummy
    }
#endif
    
    // Register custom meta types used in queued invocations
    qRegisterMetaType<SessionLine>("SessionLine");
    qRegisterMetaType<QList<SessionLine>>("QList<SessionLine>");
    qRegisterMetaType<BLUETOOTH_TYPE>("BLUETOOTH_TYPE");
    qRegisterMetaType<uint32_t>("uint32_t");

    qInstallMessageHandler(myMessageOutput);
    qDebug() << QStringLiteral("version ") << app->applicationVersion();
    // Which commit is this, really? Anything logged before the handler is installed
    // goes nowhere, so this has to sit here. The Qt runtime version earns its place
    // next to it because the exe-only CI artifact drops into an existing install: if
    // these DLLs ever drift from the build, the log is the only way to see it.
    qDebug() << QStringLiteral("QZ fork release") << QStringLiteral(QZ_FORK_VERSION);
    qDebug() << QStringLiteral("QZ build") << QStringLiteral(QZ_GIT_SHA) << QStringLiteral("Qt")
             << qVersion() << QStringLiteral("on") << QSysInfo::prettyProductName();
    foreach (QString s, settings.allKeys()) {
        if (!s.contains(QStringLiteral("password")) && !s.contains("user_email") && !s.contains("username") && !s.contains("token")) {

            qDebug() << s << settings.value(s);
        }
    }

#if 0
    qDebug() << "-";
    qDebug() << "Settings from QZSettings";
    QZSettings::qDebugAllSettings();
    qDebug() << "-";
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    if (!forceQml) {
        if (onlyVirtualBike) {
            virtualbike V(new bike(), noWriteResistance,
                          noHeartService); // FIXED: clang-analyzer-cplusplus.NewDeleteLeaks - potential leak

            Q_UNUSED(V)
            return app->exec();
        }
    }
#endif

    settings.setValue(QZSettings::app_opening,
                      settings.value(QZSettings::app_opening, QZSettings::default_app_opening).toInt() + 1);

#if defined(Q_OS_ANDROID)
    auto result = QtAndroid::checkPermission(QString("android.permission.READ_EXTERNAL_STORAGE"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.READ_EXTERNAL_STORAGE"}));
        if (resultHash["android.permission.READ_EXTERNAL_STORAGE"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "READ_EXTERNAL_STORAGE denied!";
    }

    result = QtAndroid::checkPermission(QString("android.permission.ACCESS_FINE_LOCATION"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.ACCESS_FINE_LOCATION"}));
        if (resultHash["android.permission.ACCESS_FINE_LOCATION"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "ACCESS_FINE_LOCATION denied!";
    }

    result = QtAndroid::checkPermission(QString("android.permission.BLUETOOTH"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.BLUETOOTH"}));
        if (resultHash["android.permission.BLUETOOTH"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "BLUETOOTH denied!";
    }

    result = QtAndroid::checkPermission(QString("android.permission.BLUETOOTH_ADMIN"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.BLUETOOTH_ADMIN"}));
        if (resultHash["android.permission.BLUETOOTH_ADMIN"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "BLUETOOTH_ADMIN denied!";
    }

    result = QtAndroid::checkPermission(QString("android.permission.BLUETOOTH_SCAN"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.BLUETOOTH_SCAN"}));
        if (resultHash["android.permission.BLUETOOTH_SCAN"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "BLUETOOTH_SCAN denied!";
    }

    result = QtAndroid::checkPermission(QString("android.permission.BLUETOOTH_ADVERTISE"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.BLUETOOTH_ADVERTISE"}));
        if (resultHash["android.permission.BLUETOOTH_ADVERTISE"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "BLUETOOTH_ADVERTISE denied!";
    }

    result = QtAndroid::checkPermission(QString("android.permission.BLUETOOTH_CONNECT"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.BLUETOOTH_CONNECT"}));
        if (resultHash["android.permission.BLUETOOTH_CONNECT"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "BLUETOOTH_CONNECT denied!";
    }

    result = QtAndroid::checkPermission(QString("android.permission.POST_NOTIFICATIONS"));
    if (result == QtAndroid::PermissionResult::Denied) {
        QtAndroid::PermissionResultMap resultHash =
            QtAndroid::requestPermissionsSync(QStringList({"android.permission.POST_NOTIFICATIONS"}));
        if (resultHash["android.permission.POST_NOTIFICATIONS"] == QtAndroid::PermissionResult::Denied)
            qDebug() << "POST_NOTIFICATIONS denied!";
    }    
#endif

    /* test virtual echelon
     * settings.setValue(QZSettings::virtual_device_echelon, true);
    virtualbike* V = new virtualbike(new bike(), noWriteResistance, noHeartService);
    Q_UNUSED(V)
    return app->exec();*/
    // Written here rather than in the non-QML settings block below, because the desktop build
    // this fork ships *is* the QML one and that block never runs for it. Only ever written when
    // the flag was actually given: an absent -simulated-bike must not silently turn off a
    // simulated session the user switched on in the settings.
    if (simulatedBike) {
        settings.setValue(QZSettings::simulated_bike, true);
        if (!simulatedBikeRide.isEmpty())
            settings.setValue(QZSettings::simulated_bike_ride, simulatedBikeRide);
    }

    bluetooth bl(logs, deviceName, noWriteResistance, noHeartService, pollDeviceTime, noConsole, testResistance,
                 bikeResistanceOffset,
                 bikeResistanceGain); // FIXED: clang-analyzer-cplusplus.NewDeleteLeaks - potential leak



    // MyWhoosh Link integration
    bool mywhoosh_link_enabled = settings.value(QZSettings::mywhoosh_link_enabled, QZSettings::default_mywhoosh_link_enabled).toBool();
    if(mywhoosh_link_enabled) {
        MyWhooshLink* mywhooshLink = new MyWhooshLink(&bl);
    }

#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
    lockscreen h;
    h.request();
#endif
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    if (forceQml)
#endif
    {
        AndroidStatusBar::registerQmlType();

#ifdef Q_OS_ANDROID
        FontManager fontManager;
        fontManager.initializeEmojiFont();
#endif

        // The whole of what the UI is allowed to know about a ride. Section 9.2 caps
        // this surface at 20 members and TestRideState holds it to the list.
        //
        // Declared before the engine on purpose. Locals are destroyed in reverse, so
        // an engine built first is torn down last - and tearing down a QML tree
        // re-evaluates its bindings, which by then were reading a context property
        // whose object had already gone. That cost 14 TypeErrors at every exit.
        RideState rideState(&bl);

        QQmlApplicationEngine engine;
        const QUrl url(QStringLiteral("qrc:/ui/Main.qml"));
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, qobject_cast<QGuiApplication *>(app.data()),
            [url](QObject *obj, const QUrl &objUrl) {
                if (!obj && url == objUrl)
                    QCoreApplication::exit(-1);
            },
            Qt::QueuedConnection);

#ifdef Q_OS_ANDROID
        engine.rootContext()->setContextProperty("OS_VERSION", QVariant("Android"));
#elif defined(Q_OS_IOS)
        engine.rootContext()->setContextProperty("OS_VERSION", QVariant("iOS"));
#else
        engine.rootContext()->setContextProperty("OS_VERSION", QVariant("Other"));
#endif
#ifdef CHARTJS
        engine.rootContext()->setContextProperty("CHARTJS", QVariant(true));
#else
        engine.rootContext()->setContextProperty("CHARTJS", QVariant(false));
#endif
#ifdef Q_OS_ANDROID
        engine.rootContext()->setContextProperty("fontManager", &fontManager);
#endif
        // Expose FileSearcher for fast recursive file searching
        FileSearcher fileSearcher;
        engine.rootContext()->setContextProperty("fileSearcher", &fileSearcher);

        engine.rootContext()->setContextProperty("rideState", &rideState);

        // The overlay: one producer, and as many sinks as the rider switched on. It composes
        // its lines off rideState and hands them to RTSS and to OsdWindow.qml, which is why
        // it is here and not a member of RideState - see qzosd.h. Parented to rideState for
        // the same reason `language` below is: the engine is destroyed first.
        QzOsd *osd = new QzOsd(&rideState, &rideState);
        engine.rootContext()->setContextProperty("osd", osd);

        // The language, and the only thing that can change it. Built here rather than
        // before the engine because it needs one to retranslate, and before
        // engine.load() below so the first tree is built in the rider's language.
        //
        // Parented to rideState for the reason spelled out above it: a local declared
        // after the engine is destroyed before the engine, and the QML tree reads this
        // object's `current` while it is being torn down. rideState outlives the engine,
        // so anything parented to it does too - which is also why `pad` below is.
        QzLanguage *language = new QzLanguage(&engine, &rideState);
        engine.rootContext()->setContextProperty("language", language);

        // A gamepad is the only shifter that works once the training app owns the
        // screen. It used to reach the bike through homeform's keyboard entry points,
        // which were the same ones the shortcuts and the gear tile used; all three are
        // gone, and RideState carries the same three actions straight to the device.
        //
        // Built on every platform now, not just Windows: the class already reports
        // available() == false where XInput is not there, and the mapping screen needs
        // an object to ask. A screen that cannot say "not supported here" would have to
        // pretend instead, which is the failure section 3.2.1 exists to prevent.
        // Created before engine.load() because the screen binds to it.
        gamepadcontroller *pad = new gamepadcontroller(&rideState);
        QObject::connect(pad, &gamepadcontroller::gearUp, &rideState, &RideState::gearUp);
        QObject::connect(pad, &gamepadcontroller::gearDown, &rideState, &RideState::gearDown);
        QObject::connect(pad, &gamepadcontroller::ergToggle, &rideState, &RideState::toggleErg);
        engine.rootContext()->setContextProperty("gamepad", pad);

        // The volume keys, which on Android are the only input that survives losing focus - the
        // broadcast goes to every registered receiver, not to whoever is in front. It is what a
        // pad in its keyboard mode can reach, and what a pad read as a pad cannot. Inert unless
        // volume_change_gears is on, and parented like the pad because the engine dies first.
        volumekeys *volume = new volumekeys(&rideState);
        QObject::connect(volume, &volumekeys::gearUp, &rideState, &RideState::gearUp);
        QObject::connect(volume, &volumekeys::gearDown, &rideState, &RideState::gearDown);

        // Where the bridge posts "battery at 40%", "restart to apply", "another device
        // has the bike". Drivers emit into QzNotify and ToastArea.qml shows what lands.
        engine.rootContext()->setContextProperty("qzNotify", QzNotify::singleton());

        // The QZWS WebSocket: the only way a PC reads this ride, and what
        // tools/qz-rouvy-rtss and tools/xbox-mywhoosh-gears both talk to (section 6).
        // It was built in homeform's constructor until 7c-2a moved it here, which is
        // the only reason deleting that class did not take the socket with it.
        const QString qzTemplatePath = QzPaths::getWritableAppDir() + QStringLiteral("QZTemplates");
        TemplateInfoSenderBuilder *qzws = TemplateInfoSenderBuilder::getInstance(
            QStringLiteral("user"), QStringList({qzTemplatePath}), app.data());
        QObject::connect(&bl, &bluetooth::bluetoothDeviceConnected, qzws,
                         [qzws](bluetoothdevice *b) { qzws->start(b); });
        QObject::connect(&bl, &bluetooth::bluetoothDeviceDisconnected, qzws,
                         [qzws]() { qzws->stop(); });

        // The control half. Only the commands that still mean something on a bike are
        // routed: a shift, and the auto-resistance toggle whose state the broadcast
        // already reports. Lap, Start/Pause/Stop and the treadmill pair refer to
        // recording and to machines this fork no longer has, so they are parsed and
        // dropped - which for a concept that no longer exists is the correct answer,
        // not the silent failure section 7 group D complained about.
        QObject::connect(qzws, &TemplateInfoSenderBuilder::gears_Plus, &rideState, &RideState::gearUp);
        QObject::connect(qzws, &TemplateInfoSenderBuilder::gears_Minus, &rideState, &RideState::gearDown);
        QObject::connect(qzws, &TemplateInfoSenderBuilder::autoResistance, &rideState,
                         &RideState::toggleAutoResistance);

        engine.load(url);

        // A UI tree exists from here and RideState is connected to the bridge, so
        // discovery may announce to somebody. Until 7c-2b homeform set this too.
        bl.uiLoaded = true;

        // Bring the DIRCON endpoint up now rather than when a bike connects. A client
        // that caches discovery results and does not retry a failed connect will
        // otherwise hold a record for a port nobody is listening on.
        DirconManager::startIdleEndpoint();

        {
#ifdef Q_OS_ANDROID
            KeepAwakeHelper helper;
#elif defined Q_OS_MACOS
            lockScreen();
#elif defined Q_OS_IOS
#ifndef IO_UNDER_QT
            lockscreen yc;
            yc.setTimerDisabled();
#endif
#endif
            // screen and CPU will stay awake during this section
            // lock will be released when helper object goes out of scope
            return app->exec();
        }
#ifdef Q_OS_MACOS
        unlockScreen();
#endif
    }
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    else {
        bl.uiLoaded = true;
    }
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#ifdef Q_OS_LINUX
#ifndef Q_OS_ANDROID
    if(eventGearDevice.length())
        new BluetoothHandler(&bl, eventGearDevice);
#endif
#endif
    return app->exec();
#endif
}
