#ifndef HOMEFORM_H
#define HOMEFORM_H

#include "qtchartscompat.h"
#include "bluetooth.h"
#include "rtssosd.h"
#include "qmdnsengine/browser.h"
#include "qmdnsengine/cache.h"
#include "qmdnsengine/resolver.h"
#include "screencapture.h"
#include "sessionline.h"
#include <QChart>
#include <QColor>
#include <QGraphicsScene>
#include <QMediaPlayer>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QTextToSpeech>
#include <QThread>

#ifdef Q_OS_IOS
#include "ios/lockscreen.h"
#endif
#ifdef Q_OS_ANDROID
#include <QAndroidJniEnvironment>
#include <QtAndroid>
#endif

#if __has_include("secret.h")
#include "secret.h"
#endif

class DataObject : public QObject {

    Q_OBJECT

    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString icon READ icon NOTIFY iconChanged)
    Q_PROPERTY(int gridId READ gridId NOTIFY gridIdChanged WRITE setGridId)
    Q_PROPERTY(QString value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(QString secondLine READ secondLine WRITE setSecondLine NOTIFY secondLineChanged)
    Q_PROPERTY(int valueFontSize READ valueFontSize WRITE setValueFontSize NOTIFY valueFontSizeChanged)
    Q_PROPERTY(QString valueFontColor READ valueFontColor WRITE setValueFontColor NOTIFY valueFontColorChanged)
    Q_PROPERTY(int labelFontSize READ labelFontSize WRITE setLabelFontSize NOTIFY labelFontSizeChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY writableChanged)
    Q_PROPERTY(bool visibleItem READ visibleItem NOTIFY visibleChanged)
    Q_PROPERTY(bool largeButton READ largeButton NOTIFY largeButtonChanged)
    Q_PROPERTY(QString largeButtonColor READ largeButtonColor NOTIFY largeButtonColorChanged)
    Q_PROPERTY(QString largeButtonLabel READ largeButtonLabel NOTIFY largeButtonLabelChanged)
    Q_PROPERTY(QString plusName READ plusName NOTIFY plusNameChanged)
    Q_PROPERTY(QString minusName READ minusName NOTIFY minusNameChanged)
    Q_PROPERTY(QString identificator READ identificator NOTIFY identificatorChanged)

  public:
    DataObject(const QString &name, const QString &icon, const QString &value, bool writable, const QString &id,
               int valueFontSize, int labelFontSize, const QString &valueFontColor = QStringLiteral("white"),
               const QString &secondLine = QLatin1String(""), const int gridId = 0, const bool largeButton = false,
               const QString largeButtonLabel = QLatin1String(""),
               const QString largeButtonColor = QZSettings::default_tile_preset_resistance_1_color);
    void setName(const QString &value);
    void setValue(const QString &value);
    void setSecondLine(const QString &value);
    void setValueFontSize(int value);
    void setValueFontColor(const QString &value);
    void setLabelFontSize(int value);
    void setVisible(bool visible);
    void setGridId(int id);
    void setLargeButtonColor(const QString &color);
    void setLargeButtonLabel(const QString &label);
    QString name() { return m_name; }
    QString icon() { return m_icon; }
    QString value() { return m_value; }
    QString secondLine() { return m_secondLine; }
    int gridId() { return m_gridId; }
    int valueFontSize() { return m_valueFontSize; }
    QString valueFontColor() { return m_valueFontColor; }
    int labelFontSize() { return m_labelFontSize; }
    bool writable() { return m_writable; }
    bool visibleItem() { return m_visible; }
    QString plusName() { return m_id + QStringLiteral("_plus"); }
    QString minusName() { return m_id + QStringLiteral("_minus"); }
    QString identificator() { return m_id; }
    bool largeButton() { return m_largeButton; }
    QString largeButtonLabel() { return m_largeButtonLabel; }
    QString largeButtonColor() { return m_largeButtonColor; }

    QString m_id;
    QString m_name;
    QString m_icon;
    QString m_value;
    QString m_secondLine = QLatin1String("");
    int m_valueFontSize;
    int m_gridId;
    QString m_valueFontColor = QStringLiteral("white");
    int m_labelFontSize;
    bool m_writable;
    bool m_visible = true;
    bool m_largeButton = false;
    QString m_largeButtonLabel = QLatin1String("");
    QString m_largeButtonColor = QZSettings::default_tile_preset_resistance_1_color;

  signals:
    void valueChanged(QString value);
    void secondLineChanged(QString value);
    void valueFontSizeChanged(int value);
    void valueFontColorChanged(QString value);
    void labelFontSizeChanged(int value);
    void gridIdChanged(int value);
    void nameChanged(QString value);
    void iconChanged(QString value);
    void writableChanged(bool value);
    void visibleChanged(bool value);
    void plusNameChanged(QString value);
    void minusNameChanged(QString value);
    void identificatorChanged(QString value);
    void largeButtonChanged(bool value);
    void largeButtonLabelChanged(QString value);
    void largeButtonColorChanged(QString value);
};

class homeform : public QObject {

    Q_OBJECT
    Q_PROPERTY(bool labelHelp READ labelHelp NOTIFY changeLabelHelp)
    Q_PROPERTY(bool device READ getDevice NOTIFY changeOfdevice)
    Q_PROPERTY(bool lap READ getLap NOTIFY changeOflap)
    Q_PROPERTY(int topBarHeight READ topBarHeight NOTIFY topBarHeightChanged)
    Q_PROPERTY(QString info READ info NOTIFY infoChanged)
    Q_PROPERTY(QString signal READ signal NOTIFY signalChanged)
    Q_PROPERTY(QString startText READ startText NOTIFY startTextChanged)
    Q_PROPERTY(QString startIcon READ startIcon NOTIFY startIconChanged)
    Q_PROPERTY(QString startColor READ startColor NOTIFY startColorChanged)
    Q_PROPERTY(QString stopText READ stopText NOTIFY stopTextChanged)
    Q_PROPERTY(QString stopIcon READ stopIcon NOTIFY stopIconChanged)
    Q_PROPERTY(QString stopColor READ stopColor NOTIFY stopColorChanged)
    Q_PROPERTY(QStringList metrics READ metrics)
    Q_PROPERTY(QStringList bluetoothDevices READ bluetoothDevices NOTIFY bluetoothDevicesChanged)
    Q_PROPERTY(QStringList tile_order READ tile_order NOTIFY tile_orderChanged)
    Q_PROPERTY(bool generalPopupVisible READ generalPopupVisible NOTIFY generalPopupVisibleChanged WRITE
                   setGeneralPopupVisible)
    Q_PROPERTY(bool licensePopupVisible READ licensePopupVisible NOTIFY licensePopupVisibleChanged WRITE
                   setLicensePopupVisible)
    Q_PROPERTY(double currentSpeed READ currentSpeed NOTIFY currentSpeedChanged)
    Q_PROPERTY(int zwiftLogin READ zwiftLogin NOTIFY zwiftLoginChanged)
    Q_PROPERTY(QString workoutStartDate READ workoutStartDate)
    Q_PROPERTY(QString workoutName READ workoutName)
    Q_PROPERTY(QString instructorName READ instructorName)
    Q_PROPERTY(double wattMaxChart READ wattMaxChart)
    Q_PROPERTY(bool autoResistance READ autoResistance NOTIFY autoResistanceChanged WRITE setAutoResistance)
    Q_PROPERTY(bool stopRequested READ stopRequested NOTIFY stopRequestedChanged WRITE setStopRequestedChanged)
    Q_PROPERTY(bool startRequested READ startRequested NOTIFY startRequestedChanged WRITE setStartRequestedChanged)
    Q_PROPERTY(QString toastRequested READ toastRequested NOTIFY toastRequestedChanged WRITE setToastRequested)

    // workout preview
    Q_PROPERTY(bool miles_unit READ miles_unit)
    Q_PROPERTY(bool iPadMultiWindowMode READ iPadMultiWindowMode)





  public:
    static homeform *singleton() { return m_singleton; }
    bluetooth *bluetoothManager;
    QQmlApplicationEngine *getEngine() { return engine; }

    Q_INVOKABLE void save_screenshot() {

        QString path = getWritableAppDir();

        QString filenameScreenshot =
            path + QDateTime::currentDateTime().toString().replace(QStringLiteral(":"), QStringLiteral("_")) +
            QStringLiteral(".jpg");
        QObject *rootObject = engine->rootObjects().constFirst();
        QObject *stack = rootObject;
        screenCapture s(reinterpret_cast<QQuickView *>(stack));
        s.capture(filenameScreenshot);
    }

    Q_INVOKABLE void update_chart_power(QQuickItem *item) {
        if (QGraphicsScene *scene = item->findChild<QGraphicsScene *>()) {
            auto items_list = scene->items();
            for (QGraphicsItem *it : qAsConst(items_list)) {
                if (QChart *chart = dynamic_cast<QChart *>(it)) {
                    // Customize chart background
                    QLinearGradient backgroundGradient;
                    double maxWatt = wattMaxChart();
                    QSettings settings;
                    double ftpSetting = settings.value(QZSettings::ftp, QZSettings::default_ftp).toDouble();
                    /*backgroundGradient.setStart(QPointF(0, 0));
                    backgroundGradient.setFinalStop(QPointF(0, 1));
                    backgroundGradient.setColorAt((maxWatt - (ftpSetting * 0.55)) / maxWatt, QColor("white"));
                    backgroundGradient.setColorAt((maxWatt - (ftpSetting * 0.75)) / maxWatt, QColor("limegreen"));
                    backgroundGradient.setColorAt((maxWatt - (ftpSetting * 0.90)) / maxWatt, QColor("gold"));
                    backgroundGradient.setColorAt((maxWatt - (ftpSetting * 1.05)) / maxWatt, QColor("orange"));
                    backgroundGradient.setColorAt((maxWatt - (ftpSetting * 1.20)) / maxWatt, QColor("darkorange"));
                    backgroundGradient.setColorAt((maxWatt - (ftpSetting * 1.5)) / maxWatt, QColor("orangered"));
                    backgroundGradient.setColorAt(0.0, QColor("red"));*/

                    // backgroundGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
                    // chart->setBackgroundBrush(backgroundGradient);
                    // Customize plot area background
                    QLinearGradient plotAreaGradient;
                    plotAreaGradient.setStart(QPointF(0, 0));
                    plotAreaGradient.setFinalStop(QPointF(0, 1));
                    plotAreaGradient.setColorAt((maxWatt - (ftpSetting * 0.55)) / maxWatt, QColor("white"));
                    plotAreaGradient.setColorAt((maxWatt - (ftpSetting * 0.75)) / maxWatt, QColor("limegreen"));
                    plotAreaGradient.setColorAt((maxWatt - (ftpSetting * 0.90)) / maxWatt, QColor("gold"));
                    plotAreaGradient.setColorAt((maxWatt - (ftpSetting * 1.05)) / maxWatt, QColor("orange"));
                    plotAreaGradient.setColorAt((maxWatt - (ftpSetting * 1.20)) / maxWatt, QColor("darkorange"));
                    plotAreaGradient.setColorAt((maxWatt - (ftpSetting * 1.5)) / maxWatt, QColor("orangered"));
                    plotAreaGradient.setColorAt(0.0, QColor("red"));
                    plotAreaGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
                    chart->setPlotAreaBackgroundBrush(plotAreaGradient);
                    chart->setPlotAreaBackgroundVisible(true);
                }
            }
        }
    }

    Q_INVOKABLE void update_axes(QAbstractAxis *axisX, QAbstractAxis *axisY) {
        if (axisX && axisY) {
            // Customize axis colors
            QPen axisPen(QRgb(0xd18952));
            axisPen.setWidth(2);
            axisX->setLinePen(axisPen);
            axisY->setLinePen(axisPen);
            // Customize grid lines and shades
            axisY->setShadesPen(Qt::NoPen);
            axisY->setShadesBrush(QBrush(QColor(0x99, 0xcc, 0xcc, 0x55)));
        }
    }

    Q_INVOKABLE bool firstRun() {
        QSettings settings;
        
        bool android_antbike = settings.value(QZSettings::android_antbike, QZSettings::default_android_antbike).toBool();
        QString proformtdf4ip = settings.value(QZSettings::proformtdf4ip, QZSettings::default_proformtdf4ip).toString();
        QString proformtdf1ip = settings.value(QZSettings::proformtdf1ip, QZSettings::default_proformtdf1ip).toString();
        QString proformtreadmillip = settings.value(QZSettings::proformtreadmillip, QZSettings::default_proformtreadmillip).toString();
        QString freebeatSerialPort =
            settings.value(QZSettings::freebeat_serialport, QZSettings::default_freebeat_serialport).toString();

        QString nordictrack_2950_ip =
            settings.value(QZSettings::nordictrack_2950_ip, QZSettings::default_nordictrack_2950_ip).toString();
        QString tdf_10_ip = settings.value(QZSettings::tdf_10_ip, QZSettings::default_tdf_10_ip).toString();
        QString proform_elliptical_ip = settings.value(QZSettings::proform_elliptical_ip, QZSettings::default_proform_elliptical_ip).toString();
        bool fake_bike =
            settings.value(QZSettings::applewatch_fakedevice, QZSettings::default_applewatch_fakedevice).toBool();
        bool fakedevice_elliptical =
            settings.value(QZSettings::fakedevice_elliptical, QZSettings::default_fakedevice_elliptical).toBool();
        bool fakedevice_rower = settings.value(QZSettings::fakedevice_rower, QZSettings::default_fakedevice_rower).toBool();
        bool waterrower_usb = settings.value(QZSettings::waterrower_usb, QZSettings::default_waterrower_usb).toBool();
        bool fakedevice_treadmill =
            settings.value(QZSettings::fakedevice_treadmill, QZSettings::default_fakedevice_treadmill).toBool();
        bool antbike =
            settings.value(QZSettings::antbike, QZSettings::default_antbike).toBool();
        // A simulated bike is a configured device. Without this the wizard opens over the
        // dashboard on every start: nothing was ever discovered, so bluetooth_lastdevice_name
        // is empty and QZ concludes it has never been set up. This is the same reason the
        // dead applewatch_fakedevice flag is still named in this condition.
        bool simulated_bike =
            settings.value(QZSettings::simulated_bike, QZSettings::default_simulated_bike).toBool();

        return settings.value(QZSettings::bluetooth_lastdevice_name, QZSettings::default_bluetooth_lastdevice_name).toString().isEmpty() &&
                !simulated_bike &&
                nordictrack_2950_ip.isEmpty() && tdf_10_ip.isEmpty() && !fake_bike && !fakedevice_elliptical &&
                !fakedevice_rower && !waterrower_usb && !fakedevice_treadmill && !antbike && !android_antbike && proform_elliptical_ip.isEmpty() &&
                proformtdf4ip.isEmpty() && proformtdf1ip.isEmpty() && proformtreadmillip.isEmpty() &&
                freebeatSerialPort.isEmpty();
    }


    // Auto-inclination was a treadmill's, published by virtualtreadmill, which went with
    // Group E. Nothing this fork can connect to reports a TREADMILL device type, so the
    // answer is now constant - but Home.qml still calls it, and QML resolves invokables at
    // runtime, so removing it would blank the inclination tile rather than fail the build.
    Q_INVOKABLE bool autoInclinationEnabled() { return false; }

    Q_INVOKABLE bool confirmStopEnabled() {
        QSettings settings;
        return settings.value(QZSettings::confirm_stop_workout, QZSettings::default_confirm_stop_workout).toBool();
    }

    Q_INVOKABLE bool locationServices() {
        return m_locationServices;
    }

    Q_INVOKABLE void enableLocationServices() {
#ifdef Q_OS_ANDROID
        QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/LocationHelper", "requestPermissions",
                                                  "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
#endif
    }

    homeform(QQmlApplicationEngine *engine, bluetooth *bl);
    ~homeform();
    int topBarHeight() { return m_topBarHeight; }
    bool stopRequested() { return m_stopRequested; }
    bool startRequested() { return m_startRequested; }
    QString info() { return m_info; }
    QString signal();
    QString startText();
    QString startIcon();
    QString startColor();
    QString stopText();
    QString stopIcon();
    QString stopColor();
    QString workoutStartDate() {
        if (!Session.isEmpty()) {
            return Session.constFirst().time.toString();
        } else {
            return QLatin1String("");
        }
    }
    QString workoutNameBasedOnBluetoothDevice() {
        if (bluetoothManager->device() && bluetoothManager->device()->deviceType() == BIKE) {
            return QStringLiteral("Ride");
        } else if (bluetoothManager->device() && bluetoothManager->device()->deviceType() == ROWING) {
            return QStringLiteral("Row");
        } else {
            return QStringLiteral("Run");
        }
    }
    QString workoutName() {
        if (!stravaPelotonActivityName.isEmpty()) {
            return stravaPelotonActivityName;
        } else if (!stravaWorkoutName.isEmpty()) {
            return stravaWorkoutName;
        } else {
            return workoutNameBasedOnBluetoothDevice();
        }
    }
    QString instructorName() { return stravaPelotonInstructorName; }
    int zwiftLogin() { return m_zwiftLoginState; }
    QString toastRequested() { return m_toastRequested; }
    bool generalPopupVisible();
    bool licensePopupVisible();
    double currentSpeed() {
        if (bluetoothManager && bluetoothManager->device())
            return bluetoothManager->device()->currentSpeed().value();
        else
            return 0;
    }
    bool labelHelp();
    QStringList metrics();
    QStringList bluetoothDevices();
    QStringList tile_order();
    bool autoResistance() { return m_autoresistance; }
    void setAutoResistance(bool value) {
        m_autoresistance = value;
        emit autoResistanceChanged(value);
        if (bluetoothManager->device()) {
            bluetoothManager->device()->setAutoResistance(value);
        }
    }
    void setStopRequestedChanged(bool value) {
        m_stopRequested = value;
        emit stopRequestedChanged(value);
    }
    void setStartRequestedChanged(bool value) {
        m_startRequested = value;
        emit startRequestedChanged(value);
    }
    void setLicensePopupVisible(bool value);
    void setToastRequested(QString value) { m_toastRequested = value; emit toastRequestedChanged(value); }

    Q_INVOKABLE void selectGymModeDevice(const QString &deviceName);
    Q_INVOKABLE bool hasConnectedDevice() const;

private:
    void clearWebViewCache();

public:
    void setGeneralPopupVisible(bool value);

#if defined(Q_OS_ANDROID)
    QString getBluetoothName();
    static QString getAndroidDataAppDir();
#endif
    Q_INVOKABLE static QString getWritableAppDir();
    Q_INVOKABLE static QString getProfileDir();
    Q_INVOKABLE static void clearFiles();
    Q_INVOKABLE void openAndroidDocumentPicker(const QString &kind);
    Q_INVOKABLE bool deleteTrainingProgramFile(const QString &fileUrl);

    double wattMaxChart() {
        QSettings settings;
        if (bluetoothManager && bluetoothManager->device() &&
            bluetoothManager->device()->wattsMetric().max() >
                (settings.value(QZSettings::ftp, QZSettings::default_ftp).toDouble() * 2)) {
            return bluetoothManager->device()->wattsMetric().max();
        } else {
            return settings.value(QZSettings::ftp, QZSettings::default_ftp).toDouble() * 2;
        }
    }
    Q_INVOKABLE void keyboardStartStop() { StartRequested(); }
    Q_INVOKABLE void keyboardStop() { StopRequested(); }
    Q_INVOKABLE void keyboardLap() { Lap(); }
    Q_INVOKABLE void keyboardPlus(const QString &name) { Plus(name); }
    Q_INVOKABLE void keyboardMinus(const QString &name) { Minus(name); }
    Q_INVOKABLE void keyboardLargeButton(const QString &name) { LargeButton(name); }
    Q_INVOKABLE bool handleKeyboardShortcut(const QString &sequence);
    Q_INVOKABLE void setNativeShortcutCaptureSuspended(bool suspended) {
        m_nativeShortcutCaptureSuspended = suspended;
    }

    Q_INVOKABLE void sortTiles();
    Q_INVOKABLE void moveTile(QString name, int newIndex, int oldIndex);
    DataObject *tileFromName(QString name);

    bool miles_unit() {
        QSettings settings;
        return settings.value(QZSettings::miles_unit, QZSettings::default_miles_unit).toBool();
    }

    bool iPadMultiWindowMode() {
#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
        return lockscreen::isInMultiWindowMode();
#else
        return false;
#endif
#else
        return false;
#endif
    }


    void updateGearsValue();
    
    DataObject *speed;
    DataObject *inclination;
    DataObject *negative_inclination;
    DataObject *cadence;
    DataObject *elevation;
    DataObject *calories;
    DataObject *odometer;
    DataObject *pace;
    DataObject *avg_pace;
    DataObject *grade_adjusted_pace;
    DataObject *datetime;
    DataObject *resistance;
    DataObject *watt;
    DataObject *avgWatt;
    DataObject *avgWattLap;
    DataObject *heart;
    DataObject *fan;
    DataObject *jouls;
    DataObject *peloton_offset;
    DataObject *peloton_remaining;
    DataObject *elapsed;
    DataObject *moving_time;
    DataObject *peloton_resistance;
    DataObject *target_resistance;
    DataObject *target_peloton_resistance;
    DataObject *target_cadence;
    DataObject *target_power;
    DataObject *target_zone;
    DataObject *target_speed;
    DataObject *target_pace;
    DataObject *target_incline;
    DataObject *ftp;
    DataObject *lapElapsed;
    DataObject *weightLoss;
    DataObject *strokesLength;
    DataObject *strokesCount;
    DataObject *wattKg;
    DataObject *gears;
    DataObject *biggearsPlus;
    DataObject *biggearsMinus;
    DataObject *remaningTimeTrainingProgramCurrentRow;
    DataObject *nextRows;
    DataObject *mets;
    DataObject *targetMets;
    DataObject *steeringAngle;
    DataObject *pidHR;
    DataObject *extIncline;
    DataObject *instantaneousStrideLengthCM;
    DataObject *groundContactMS;
    DataObject *verticalOscillationMM;
    DataObject *preset_resistance_1;
    DataObject *preset_resistance_2;
    DataObject *preset_resistance_3;
    DataObject *preset_resistance_4;
    DataObject *preset_resistance_5;
    DataObject *preset_speed_1;
    DataObject *preset_speed_2;
    DataObject *preset_speed_3;
    DataObject *preset_speed_4;
    DataObject *preset_speed_5;
    DataObject *preset_inclination_1;
    DataObject *preset_inclination_2;
    DataObject *preset_inclination_3;
    DataObject *preset_inclination_4;
    DataObject *preset_inclination_5;
    DataObject *pace_last500m;
    DataObject *stepCount;
    DataObject *ergMode;
    DataObject *rss;
    DataObject *preset_powerzone_1;
    DataObject *preset_powerzone_2;
    DataObject *preset_powerzone_3;
    DataObject *preset_powerzone_4;
    DataObject *preset_powerzone_5;
    DataObject *preset_powerzone_6;
    DataObject *preset_powerzone_7;
    DataObject *tile_hr_time_in_zone_1;
    DataObject *tile_hr_time_in_zone_2;
    DataObject *tile_hr_time_in_zone_3;
    DataObject *tile_hr_time_in_zone_4;
    DataObject *tile_hr_time_in_zone_5;
    DataObject *tile_heat_time_in_zone_1;
    DataObject *tile_heat_time_in_zone_2;
    DataObject *tile_heat_time_in_zone_3;
    DataObject *tile_heat_time_in_zone_4;
    DataObject *coreTemperature;
    DataObject *autoVirtualShiftingCruise;
    DataObject *autoVirtualShiftingClimb;
    DataObject *autoVirtualShiftingSprint;
    DataObject *powerAvg;
    DataObject *hrv;

  private:
    static homeform *m_singleton;
    TemplateInfoSenderBuilder *userTemplateManager = nullptr;
    QList<QObject *> dataList;
    QList<SessionLine> Session;
    QQmlApplicationEngine *engine;

    int m_topBarHeight = 120;
    QString m_info = QStringLiteral("Connecting...");
    bool m_labelHelp = true;
    bool m_generalPopupVisible = false;
    bool m_LicensePopupVisible = false;


    bool paused = false;
    bool stopped = false;
    bool lapTrigger = false;

    // Automatic Virtual Shifting variables
    QDateTime automaticShiftingGearUpStartTime = QDateTime::currentDateTime();
    QDateTime automaticShiftingGearDownStartTime = QDateTime::currentDateTime();

    // Timer jitter detection variables (same logic as trainprogram::scheduler)
    QDateTime lastUpdateCall = QDateTime::currentDateTime();
    qint64 currentUpdateJitter = 0;

    QString m_toastRequested = "";
    int m_zwiftLoginState = -1;
    QString stravaPelotonActivityName;
    QString stravaPelotonInstructorName;
    QString stravaWorkoutName = "";


    bool m_autoresistance = true;
    bool m_stopRequested = false;
    bool m_startRequested = false;
    bool m_overridePower = false;
    bool m_nativeShortcutCaptureSuspended = false;

    QTimer *timer;
    QTimer *automaticShiftingTimer;

    // HR PID controller state - tracks when training program changes speed to prevent race conditions
    QDateTime lastTrainingProgramSpeedChange = QDateTime::fromMSecsSinceEpoch(0);



    static quint64 cryptoKeySettingsProfiles();

    static QString copyAndroidContentsURI(QUrl file, QString subfolder);
    static QString getFileNameFromContentUri(const QString &uriString);

    int16_t fanOverride = 0;
    const float powerJog = 5.0;

    void update();
    void ten_hz();
    double heartRateMax();
    bool getDevice();
    bool getLap();
    void Start_inner(bool send_event_to_device);

    QTextToSpeech m_speech;
    int tts_summary_count = 0;

// Keep this condition identical to the one in homeform.cpp and to the slot block
// below: these are declarations for definitions that only exist under the same
// guard, and moc emits calls to the slots, so a mismatch is a link error.
#if defined(LICENSE) && (defined(Q_OS_WIN) || (defined(Q_OS_MAC) && !defined(Q_OS_IOS)) || defined(Q_OS_ANDROID))
    QTimer tLicense;
    QNetworkAccessManager *mgr = nullptr;
    void licenseRequest();
#endif


    // Gear/resistance/ERG drawn over a training app that owns the screen. Costs
    // nothing when RivaTuner Statistics Server is not running, and is Windows
    // only - see rtssosd.h.
    RtssOsd rtssOsd;
    void updateRtssOsd();

#ifdef Q_OS_IOS
    lockscreen *h = nullptr;
#endif
    bool m_locationServices = true;

#ifndef Q_OS_IOS
    QMdnsEngine::Browser *iphone_browser = nullptr;
    QMdnsEngine::Resolver *iphone_resolver = nullptr;
    QMdnsEngine::Server iphone_server;
    QMdnsEngine::Cache iphone_cache;
    QTcpSocket *iphone_socket = nullptr;
    QMdnsEngine::Service iphone_service;
    QHostAddress iphone_address;
#endif

  public slots:
    void aboutToQuit();
    void saveSettings(const QUrl &filename);
    static void loadSettings(const QUrl &filename);
    void deleteSettings(const QUrl &filename);
    void restoreSettings();
    void saveProfile(QString profilename);
    void restart();
    void Minus(const QString &);
    void Plus(const QString &);
    void handleAndroidDocumentPicked(int requestCode, const QString &uriString);

  private slots:
    void Start();
    void Stop();
    void StartRequested();
    void StopRequested();
    void Lap();
    void LargeButton(const QString &);
    void volumeDown();
    void volumeUp();
    void keyMediaPrevious();
    void keyMediaNext();
    void deviceFound(const QString &name);
    void deviceConnected(QBluetoothDeviceInfo b);
    void profile_open_clicked(const QUrl &fileName);
    void onTrainingProgramSpeedChanged(double speed);
    void refresh_bluetooth_devices_clicked();
    void zwiftLoginState(bool ok);
    void sortTilesTimeout();
    void gearUp();
    void gearDown();
    void speedPlus();
    void speedMinus();
    void inclinationPlus();
    void inclinationMinus();
    void pelotonOffset_Plus();
    void pelotonOffset_Minus();
    void bluetoothDeviceConnected(bluetoothdevice *b);
    void bluetoothDeviceDisconnected();
    void onToastRequested(QString message);
    void onTrainingProgramIntervalTransition();
    void StartFromDevice();  // Called when physical start button pressed on hardware
    void PauseFromDevice();  // Called when physical pause button pressed on hardware
    void StopFromDevice();   // Called when physical stop button pressed on hardware

#if defined(LICENSE) && (defined(Q_OS_WIN) || (defined(Q_OS_MAC) && !defined(Q_OS_IOS)) || defined(Q_OS_ANDROID))
    void licenseReply(QNetworkReply *reply);
    void licenseTimeout();
#endif

    void toggleAutoResistance() { setAutoResistance(!autoResistance()); }

  signals:

    void changeOfdevice();
    void changeOflap();
    void androidDocumentPicked(QString kind, QUrl localUrl);
    void signalChanged(QString value);
    void startTextChanged(QString value);
    void startIconChanged(QString value);
    void startColorChanged(QString value);
    void stopTextChanged(QString value);
    void stopIconChanged(QString value);
    void stopColorChanged(QString value);
    void infoChanged(QString value);
    void topBarHeightChanged(int value);
    void bluetoothDevicesChanged(QStringList value);
    void tile_orderChanged(QStringList value);
    void changeLabelHelp(bool value);
    void toastRequestedChanged(QString value);
    void generalPopupVisibleChanged(bool value);
    void licensePopupVisibleChanged(bool value);
    void manualCscBikeResistanceAdjusted(resistance_t resistance);
    void currentSpeedChanged(double value);
    void autoResistanceChanged(bool value);
    void zwiftLoginChanged(int ok);
    void userProfileChanged();
    void workoutNameChanged(QString name);
    void workoutStartDateChanged(QString name);
    void instructorNameChanged(QString name);
    void startRequestedChanged(bool value);
    void stopRequestedChanged(bool value);
    void trainingProgramIntervalSoundRequested();




    void workoutEventStateChanged(bluetoothdevice::WORKOUT_EVENT_STATE state);

    void heartRate(uint8_t heart);
};

#endif // HOMEFORM_H
