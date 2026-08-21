#include "homeform.h"
#ifdef Q_OS_IOS
#include "ios/lockscreen.h"
#include "ios/ios_liveactivity.h"
#endif
#include "localipaddress.h"
#include "rtssosd.h"
#ifdef Q_OS_ANDROID
#include "keepawakehelper.h"
#include <jni.h>
#include <QAndroidJniObject>
#endif
#include "material.h"
#include "simplecrypt.h"
#include "templateinfosenderbuilder.h"
#include "zwiftworkout.h"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGeoCoordinate>
#include <QHttpMultiPart>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkInterface>
#include <QProcess>
#include <QQmlContext>
#include <QQmlFile>

#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QFile>
#include <QUuid>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <chrono>

homeform *homeform::m_singleton = 0;
using namespace std::chrono_literals;

namespace {
QString sanitizeClipboardWorkoutName(const QString &input) {
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        trimmed = QStringLiteral("Clipboard_Workout");
    }
    QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_\\- ]"));
    trimmed.replace(invalid, QStringLiteral("_"));
    trimmed.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
    return trimmed;
}

QString uniqueClipboardWorkoutPath(const QString &extension, QString *displayName) {
    const QString trainingDir = homeform::getWritableAppDir() + QStringLiteral("training/");
    QDir dir(trainingDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString baseName = sanitizeClipboardWorkoutName(
        QStringLiteral("Clipboard_Workout_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    QString candidate = baseName;
    int suffix = 2;
    while (QFile::exists(trainingDir + candidate + QStringLiteral(".") + extension)) {
        candidate = QStringLiteral("%1_%2").arg(baseName).arg(suffix++);
    }
    if (displayName) {
        *displayName = candidate + QStringLiteral(".") + extension;
    }
    return trainingDir + candidate + QStringLiteral(".") + extension;
}

QString firstXmlElementName(const QString &xml) {
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            return reader.name().toString();
        }
    }
    return QString();
}

bool writeClipboardWorkoutFile(const QString &path, const QString &content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(content.toUtf8());
    file.close();
    return true;
}

#ifdef Q_OS_ANDROID
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

constexpr int AndroidDocumentPickerProfileRequestCode = 4101;
constexpr int AndroidDocumentPickerTrainingRequestCode = 4102;
constexpr int AndroidDocumentPickerGpxRequestCode = 4103;
constexpr int AndroidDocumentPickerSettingsRequestCode = 4104;
constexpr jint AndroidActivityResultOk = -1;
#endif
double interpolatedHeartZone(double percentHeartRate, double zone1, double zone2, double zone3, double zone4) {
    const double z1 = std::max(1.0, zone1);
    const double z2 = zone2 > z1 ? zone2 : z1 + 10.0;
    const double z3 = zone3 > z2 ? zone3 : z2 + 10.0;
    const double z4 = zone4 > z3 ? zone4 : z3 + 10.0;

    if (percentHeartRate < z1)
        return std::max(0.0, percentHeartRate / z1);
    if (percentHeartRate < z2)
        return 1.0 + ((percentHeartRate - z1) / (z2 - z1));
    if (percentHeartRate < z3)
        return 2.0 + ((percentHeartRate - z2) / (z3 - z2));
    if (percentHeartRate < z4)
        return 3.0 + ((percentHeartRate - z3) / (z4 - z3));

    const double top = z4 < 100.0 ? 100.0 : z4 + 10.0;
    return 4.0 + ((percentHeartRate - z4) / (top - z4));
}

} // namespace

DataObject::DataObject(const QString &name, const QString &icon, const QString &value, bool writable, const QString &id,
                       int valueFontSize, int labelFontSize, const QString &valueFontColor, const QString &secondLine,
                       const int gridId, bool largeButton, QString largeButtonLabel, QString largeButtonColor) {
    m_name = name;
    m_icon = icon;
    m_value = value;
    m_secondLine = secondLine;
    m_writable = writable;
    m_id = id;
    m_valueFontSize = valueFontSize;
    m_valueFontColor = valueFontColor;
    m_labelFontSize = labelFontSize;
    m_gridId = gridId;
    m_largeButton = largeButton;
    m_largeButtonLabel = largeButtonLabel;
    m_largeButtonColor = largeButtonColor;

    emit plusNameChanged(plusName());   // NOTE: clazy-incorrecrt-emit
    emit minusNameChanged(minusName()); // NOTE: clazy-incorrecrt-emit
    emit identificatorChanged(identificator());
    emit largeButtonChanged(this->largeButton());
    emit largeButtonLabelChanged(this->largeButtonLabel());
    emit largeButtonColorChanged(this->largeButtonColor());
}

void DataObject::setName(const QString &v) {
    m_name = v;
    emit nameChanged(m_name);
}
void DataObject::setValue(const QString &v) {
    m_value = v;
    emit valueChanged(m_value);
}
void DataObject::setSecondLine(const QString &value) {
    m_secondLine = value;
    emit secondLineChanged(m_secondLine);
}
void DataObject::setValueFontSize(int value) {
    m_valueFontSize = value;
    emit valueFontSizeChanged(m_valueFontSize);
}
void DataObject::setValueFontColor(const QString &value) {
    m_valueFontColor = value;
    emit valueFontColorChanged(m_valueFontColor);
}
void DataObject::setLargeButtonColor(const QString &color) {
    m_largeButtonColor = color;
    emit largeButtonColorChanged(m_largeButtonColor);
}
void DataObject::setLargeButtonLabel(const QString &label) {
    m_largeButtonLabel = label;
    emit largeButtonLabelChanged(m_largeButtonLabel);
}
void DataObject::setLabelFontSize(int value) {
    m_labelFontSize = value;
    emit labelFontSizeChanged(m_labelFontSize);
}
void DataObject::setGridId(int id) {
    m_gridId = id;
    emit gridIdChanged(m_gridId);
}
void DataObject::setVisible(bool visible) {
    m_visible = visible;
    emit visibleChanged(m_visible);
}

homeform::homeform(QQmlApplicationEngine *engine, bluetooth *bl) {
    m_singleton = this;
    QSettings settings;
    bool miles = settings.value(QZSettings::miles_unit, QZSettings::default_miles_unit).toBool();
    QString unit = QStringLiteral("km");
    QString meters = QStringLiteral("m");
    QString weightLossUnit = QStringLiteral("Kg");
    QString cm = QStringLiteral("cm");
    if (miles) {
        unit = QStringLiteral("mi");
        weightLossUnit = QStringLiteral("Oz");
        meters = QStringLiteral("ft");
        cm = QStringLiteral("in");
    }

#ifdef Q_OS_ANDROID
    m_locationServices = QAndroidJniObject::callStaticMethod<jboolean>("org/cagnulen/qdomyoszwift/LocationHelper", "start",
                                              "(Landroid/content/Context;)Z", QtAndroid::androidContext().object());
    if(m_locationServices) {
        QSettings settings;
        // so if someone pressed the skip message but now he forgot to enable GPS it will prompt out
        settings.setValue(QZSettings::skipLocationServicesDialog, QZSettings::default_skipLocationServicesDialog);
    }
#endif

#ifdef Q_OS_IOS
    const int labelFontSize = 14;
    const int valueElapsedFontSize = 36;
    const int valueTimeFontSize = 26;
#elif defined Q_OS_ANDROID
    const int labelFontSize = 16;
    const int valueElapsedFontSize = 36;
    const int valueTimeFontSize = 26;
#else
    const int labelFontSize = 10;
    const int valueElapsedFontSize = 30;
    const int valueTimeFontSize = 22;
#endif


    QString innerId = QStringLiteral("inner");
    QString sKey = QStringLiteral("template_") + innerId + QStringLiteral("_" TEMPLATE_PRIVATE_WEBSERVER_ID "_");

    QString path = homeform::getWritableAppDir() + QStringLiteral("QZTemplates");
    this->userTemplateManager =
        TemplateInfoSenderBuilder::getInstance(QStringLiteral("user"), QStringList({path}), this);

    settings.setValue(sKey + QStringLiteral("enabled"), true);
    settings.setValue(sKey + QStringLiteral("type"), TEMPLATE_TYPE_WEBSERVER);
    settings.setValue(sKey + QStringLiteral("port"), 0);
    this->innerTemplateManager =
        TemplateInfoSenderBuilder::getInstance(innerId, QStringList({QStringLiteral(":/inner_templates/")}), this);

    speed = new DataObject(tr("Speed (%1/h)").arg(unit),
                           QStringLiteral("icons/icons/speed.png"), QStringLiteral("0.0"), true,
                           QStringLiteral("speed"), 48, labelFontSize);
    inclination = new DataObject(tr("Inclination (%)"), QStringLiteral("icons/icons/inclination.png"),
                                 QStringLiteral("0.0"), true, QStringLiteral("inclination"), 48, labelFontSize);
    negative_inclination = new DataObject(tr("Descent (%1)").arg(meters), QStringLiteral("icons/icons/inclination.png"),
                                 QStringLiteral("0.0"), false, QStringLiteral("negative_inclination"), 48, labelFontSize);
    cadence = new DataObject(tr("Cadence (rpm)"), QStringLiteral("icons/icons/cadence.png"),
                             QStringLiteral("0"), false, QStringLiteral("cadence"), 48, labelFontSize);
    elevation = new DataObject(tr("Elev. Gain (%1)").arg(meters),
                               QStringLiteral("icons/icons/elevationgain.png"), QStringLiteral("0"), false,
                               QStringLiteral("elevation"), 48, labelFontSize);
    calories = new DataObject(tr("Calories (KCal)"), QStringLiteral("icons/icons/kcal.png"),
                              QStringLiteral("0"), false, QStringLiteral("calories"), 48, labelFontSize);
    odometer = new DataObject(tr("Odometer (%1)").arg(unit),
                              QStringLiteral("icons/icons/odometer.png"), QStringLiteral("0.0"), false,
                              QStringLiteral("odometer"), 48, labelFontSize);
    pace =
        new DataObject(tr("Pace (m/%1)").arg(unit), QStringLiteral("icons/icons/pace.png"),
                       QStringLiteral("0:00"), false, QStringLiteral("pace"), 48, labelFontSize);

    avg_pace =
        new DataObject(tr("Avg Pace (m/%1)").arg(unit), QStringLiteral("icons/icons/pace.png"),
                       QStringLiteral("0:00"), false, QStringLiteral("avg_pace"), 48, labelFontSize);

    grade_adjusted_pace =
        new DataObject(tr("GAP (m/%1)").arg(unit),
                       QStringLiteral("icons/icons/pace.png"), QStringLiteral("0:00"), false,
                       QStringLiteral("grade_adjusted_pace"), 48, labelFontSize);

    target_pace =
        new DataObject(tr("T.Pace(m/%1)").arg(unit), QStringLiteral("icons/icons/pace.png"),
                       QStringLiteral("0:00"), false, QStringLiteral("pace"), 48, labelFontSize);

    pace_last500m = new DataObject(tr("Pace 500m (m/%1)").arg(unit),
                                   QStringLiteral("icons/icons/pace.png"), QStringLiteral("0:00"), false,
                                   QStringLiteral("pace"), 48, labelFontSize);

    resistance = new DataObject(tr("Resistance"), QStringLiteral("icons/icons/resistance.png"),
                                QStringLiteral("0"), true, QStringLiteral("resistance"), 48, labelFontSize);
    peloton_resistance =
        new DataObject(tr("Peloton R(%)"), QStringLiteral("icons/icons/resistance.png"),
                       QStringLiteral("0"), true, QStringLiteral("peloton_resistance"), 48, labelFontSize);
    target_resistance =
        new DataObject(tr("Target R."), QStringLiteral("icons/icons/resistance.png"), QStringLiteral("0"),
                       true, QStringLiteral("target_resistance"), 48, labelFontSize);
    target_peloton_resistance =
        new DataObject(tr("T.Peloton R(%)"), QStringLiteral("icons/icons/resistance.png"),
                       QStringLiteral("0"), false, QStringLiteral("target_peloton_resistance"), 48, labelFontSize);
    target_cadence = new DataObject(tr("T.Cadence(rpm)"), QStringLiteral("icons/icons/cadence.png"),
                                    QStringLiteral("0"), false, QStringLiteral("target_cadence"), 48, labelFontSize);
    target_power = new DataObject(tr("T.Power(W)"), QStringLiteral("icons/icons/watt.png"),
                                  QStringLiteral("0"), true, QStringLiteral("target_power"), 48, labelFontSize);
    target_zone = new DataObject(tr("T.Zone"), QStringLiteral("icons/icons/watt.png"), QStringLiteral("1"),
                                 true, QStringLiteral("target_zone"), 48, labelFontSize);
    target_speed = new DataObject(tr("T.Speed (%1/h)").arg(unit),
                                  QStringLiteral("icons/icons/speed.png"), QStringLiteral("0.0"), true,
                                  QStringLiteral("target_speed"), 48, labelFontSize);
    target_incline =
        new DataObject(tr("T.Incline (%)"), QStringLiteral("icons/icons/inclination.png"),
                       QStringLiteral("0.0"), true, QStringLiteral("target_inclination"), 48, labelFontSize);

    watt = new DataObject(tr("Watts"), QStringLiteral("icons/icons/watt.png"), QStringLiteral("0"), false,
                          QStringLiteral("watt"), 48, labelFontSize);
    weightLoss = new DataObject(tr("Weight Loss(%1)").arg(weightLossUnit),
                                QStringLiteral("icons/icons/kcal.png"), QStringLiteral("0"), false,
                                QStringLiteral("weight_loss"), 48, labelFontSize);
    avgWatt = new DataObject(tr("AVG Watts"), QStringLiteral("icons/icons/watt.png"), QStringLiteral("0"),
                             false, QStringLiteral("avgWatt"), 48, labelFontSize);
    avgWattLap = new DataObject(tr("AVG Watt Lap"), QStringLiteral("icons/icons/watt.png"),
                                QStringLiteral("0"), false, QStringLiteral("avgWattLap"), 48, labelFontSize);
    wattKg = new DataObject(tr("Watt/Kg"), QStringLiteral("icons/icons/watt.png"), QStringLiteral("0"),
                            false, QStringLiteral("watt_kg"), 48, labelFontSize);
    ftp = new DataObject(tr("FTP Zone"), QStringLiteral("icons/icons/watt.png"), QStringLiteral("0"), false,
                         QStringLiteral("ftp"), 48, labelFontSize);
    heart = new DataObject(tr("Heart (bpm)"), QStringLiteral("icons/icons/heart_red.png"),
                           QStringLiteral("0"), false, QStringLiteral("heart"), 48, labelFontSize);
    fan = new DataObject(tr("Fan Speed"), QStringLiteral("icons/icons/fan.png"), QStringLiteral("0"), true,
                         QStringLiteral("fan"), 48, labelFontSize);
    jouls = new DataObject(tr("KJouls"), QStringLiteral("icons/icons/joul.png"), QStringLiteral("0"), false,
                           QStringLiteral("joul"), 48, labelFontSize);
    elapsed =
        new DataObject(tr("Elapsed"), QStringLiteral("icons/icons/clock.png"), QStringLiteral("0:00:00"),
                       false, QStringLiteral("elapsed"), valueElapsedFontSize, labelFontSize);
    moving_time =
        new DataObject(tr("Moving T."), QStringLiteral("icons/icons/clock.png"), QStringLiteral("0:00:00"),
                       false, QStringLiteral("moving_time"), valueElapsedFontSize, labelFontSize);
    datetime = new DataObject(tr("Clock"), QStringLiteral("icons/icons/clock.png"),
                              QTime::currentTime().toString(QStringLiteral("hh:mm:ss")), false,
                              QStringLiteral("datetime"), valueTimeFontSize, labelFontSize);
    lapElapsed = new DataObject(tr("Lap Elapsed"), QStringLiteral("icons/icons/clock.png"),
                                QStringLiteral("0:00:00"), false, QStringLiteral("lapElapsed"), valueElapsedFontSize,
                                labelFontSize);
    remaningTimeTrainingProgramCurrentRow = new DataObject(
        tr("Time to Next"), QStringLiteral("icons/icons/clock.png"), QStringLiteral("0:00:00"), true,
        QStringLiteral("remainingtimetrainprogramrow"), valueElapsedFontSize, labelFontSize);

    nextRows =
        new DataObject(tr("Next Rows"), QStringLiteral("icons/icons/clock.png"), QStringLiteral("N/A"),
                       false, QStringLiteral("nextrows"), valueElapsedFontSize, labelFontSize);

    mets = new DataObject(tr("METS"), QStringLiteral("icons/icons/watt.png"), QStringLiteral("0"), false,
                          QStringLiteral("mets"), 48, labelFontSize);
    targetMets = new DataObject(tr("Target METS"), QStringLiteral("icons/icons/watt.png"),
                                QStringLiteral("0"), false, QStringLiteral("targetmets"), 48, labelFontSize);
    rss = new DataObject(tr("RSS"), QStringLiteral("icons/icons/watt.png"),
                                QStringLiteral("0"), false, QStringLiteral("rss"), 48, labelFontSize);
    steeringAngle = new DataObject(tr("Steering"), QStringLiteral("icons/icons/cadence.png"),
                                   QStringLiteral("0"), false, QStringLiteral("steeringangle"), 48, labelFontSize);
    peloton_offset =
        new DataObject(tr("Peloton Offset"), QStringLiteral("icons/icons/clock.png"), QStringLiteral("0"),
                       true, QStringLiteral("peloton_offset"), valueElapsedFontSize, labelFontSize);
    peloton_remaining =
        new DataObject(tr("Peloton Rem."), QStringLiteral("icons/icons/clock.png"), QStringLiteral("0"),
                       true, QStringLiteral("peloton_remaining"), valueElapsedFontSize, labelFontSize);
    strokesCount = new DataObject(tr("Strokes Count"), QStringLiteral("icons/icons/cadence.png"),
                                  QStringLiteral("0"), false, QStringLiteral("strokes_count"), 48, labelFontSize);
    strokesLength = new DataObject(tr("Stroke Length"), QStringLiteral("icons/icons/cadence.png"),
                                   QStringLiteral("0"), false, QStringLiteral("strokes_length"), 48, labelFontSize);
    gears = new DataObject(tr("Gears"), QStringLiteral("icons/icons/elevationgain.png"),
                           QStringLiteral("0"), true, QStringLiteral("gears"), 48, labelFontSize);
    biggearsPlus = new DataObject(tr("GearsPlus"), QStringLiteral("icons/icons/elevationgain.png"),
                                  QStringLiteral("0"), true, QStringLiteral("biggearsplus"), 48, labelFontSize, QStringLiteral("white"), QLatin1String(""), 0, true, "Gear +", QStringLiteral("red"));
    biggearsMinus = new DataObject(tr("GearsMinus"), QStringLiteral("icons/icons/elevationgain.png"),
                                  QStringLiteral("0"), true, QStringLiteral("biggearsminus"), 48, labelFontSize, QStringLiteral("white"), QLatin1String(""), 0, true, "Gear -", QStringLiteral("green"));
    autoVirtualShiftingCruise = new DataObject(tr("Cruise"), QStringLiteral("icons/icons/speed.png"),
                                              QStringLiteral("0"), true, QStringLiteral("autoVirtualShiftingCruise"), 48, labelFontSize, QStringLiteral("white"), QLatin1String(""), 0, true, "Cruise", QStringLiteral("red"));
    autoVirtualShiftingClimb = new DataObject(tr("Climb"), QStringLiteral("icons/icons/inclination.png"),
                                             QStringLiteral("0"), true, QStringLiteral("autoVirtualShiftingClimb"), 48, labelFontSize, QStringLiteral("white"), QLatin1String(""), 0, true, "Climb", QStringLiteral("red"));
    autoVirtualShiftingSprint = new DataObject(QStringLiteral("Sprint"), QStringLiteral("icons/icons/watt.png"),
                                              QStringLiteral("0"), true, QStringLiteral("autoVirtualShiftingSprint"), 48, labelFontSize, QStringLiteral("white"), QLatin1String(""), 0, true, "Sprint", QStringLiteral("red"));
    powerAvg = new DataObject(tr("Power Avg"), QStringLiteral("icons/icons/watt.png"),
                             QStringLiteral("0"), true, QStringLiteral("powerAvg"), 48, labelFontSize, QStringLiteral("white"), QLatin1String(""), 0, true, "Off", QStringLiteral("grey"));
    hrv = new DataObject(QStringLiteral("HRV (ms)"), QStringLiteral("icons/icons/heart_red.png"),
                         QStringLiteral("0"), false, QStringLiteral("hrv"), 48, labelFontSize);
    pidHR = new DataObject(tr("PID Heart"), QStringLiteral("icons/icons/heart_red.png"),
                           QStringLiteral("0"), true, QStringLiteral("pid_hr"), 48, labelFontSize);
    extIncline = new DataObject(tr("Ext.Inclin.(%)"), QStringLiteral("icons/icons/inclination.png"),
                                QStringLiteral("0.0"), true, QStringLiteral("external_inclination"), 48, labelFontSize);
    instantaneousStrideLengthCM =
        new DataObject(tr("Stride L.(%1)").arg(cm), QStringLiteral("icons/icons/inclination.png"),
                       QStringLiteral("0"), false, QStringLiteral("stride_length"), 48, labelFontSize);
    groundContactMS = new DataObject(tr("Ground C.(ms)"), QStringLiteral("icons/icons/inclination.png"),
                                     QStringLiteral("0"), false, QStringLiteral("ground_contact"), 48, labelFontSize);
    verticalOscillationMM =
        new DataObject(tr("Vert.Osc.(mm)"), QStringLiteral("icons/icons/inclination.png"),
                       QStringLiteral("0"), false, QStringLiteral("vertical_oscillation"), 48, labelFontSize);

    stepCount =
        new DataObject(tr("Step Count"), QStringLiteral("icons/icons/pace.png"),
                       QStringLiteral("0"), false, QStringLiteral("step_count"), 48, labelFontSize);

    ergMode = new DataObject(
        "", "", "", false, QStringLiteral("erg_mode"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true, QStringLiteral("ERG MODE"), "#696969");

    preset_resistance_1 = new DataObject(
        "", "", "", false, QStringLiteral("preset_resistance_1"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_resistance_1_label, QZSettings::default_tile_preset_resistance_1_label)
            .toString(),
        settings.value(QZSettings::tile_preset_resistance_1_color, QZSettings::default_tile_preset_resistance_1_color)
            .toString());
    preset_resistance_2 = new DataObject(
        "", "", "", false, QStringLiteral("preset_resistance_2"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_resistance_2_label, QZSettings::default_tile_preset_resistance_2_label)
            .toString(),
        settings.value(QZSettings::tile_preset_resistance_2_color, QZSettings::default_tile_preset_resistance_2_color)
            .toString());
    preset_resistance_3 = new DataObject(
        "", "", "", false, QStringLiteral("preset_resistance_3"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_resistance_3_label, QZSettings::default_tile_preset_resistance_3_label)
            .toString(),
        settings.value(QZSettings::tile_preset_resistance_3_color, QZSettings::default_tile_preset_resistance_3_color)
            .toString());
    preset_resistance_4 = new DataObject(
        "", "", "", false, QStringLiteral("preset_resistance_4"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_resistance_4_label, QZSettings::default_tile_preset_resistance_4_label)
            .toString(),
        settings.value(QZSettings::tile_preset_resistance_4_color, QZSettings::default_tile_preset_resistance_4_color)
            .toString());
    preset_resistance_5 = new DataObject(
        "", "", "", false, QStringLiteral("preset_resistance_5"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_resistance_5_label, QZSettings::default_tile_preset_resistance_5_label)
            .toString(),
        settings.value(QZSettings::tile_preset_resistance_5_color, QZSettings::default_tile_preset_resistance_5_color)
            .toString());
    preset_speed_1 = new DataObject(
        "", "", "", false, QStringLiteral("preset_speed_1"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_speed_1_label, QZSettings::default_tile_preset_speed_1_label).toString(),
        settings.value(QZSettings::tile_preset_speed_1_color, QZSettings::default_tile_preset_speed_1_color)
            .toString());
    preset_speed_2 = new DataObject(
        "", "", "", false, QStringLiteral("preset_speed_2"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_speed_2_label, QZSettings::default_tile_preset_speed_2_label).toString(),
        settings.value(QZSettings::tile_preset_speed_2_color, QZSettings::default_tile_preset_speed_2_color)
            .toString());
    preset_speed_3 = new DataObject(
        "", "", "", false, QStringLiteral("preset_speed_3"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_speed_3_label, QZSettings::default_tile_preset_speed_3_label).toString(),
        settings.value(QZSettings::tile_preset_speed_3_color, QZSettings::default_tile_preset_speed_3_color)
            .toString());
    preset_speed_4 = new DataObject(
        "", "", "", false, QStringLiteral("preset_speed_4"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_speed_4_label, QZSettings::default_tile_preset_speed_4_label).toString(),
        settings.value(QZSettings::tile_preset_speed_4_color, QZSettings::default_tile_preset_speed_4_color)
            .toString());
    preset_speed_5 = new DataObject(
        "", "", "", false, QStringLiteral("preset_speed_5"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_speed_5_label, QZSettings::default_tile_preset_speed_5_label).toString(),
        settings.value(QZSettings::tile_preset_speed_5_color, QZSettings::default_tile_preset_speed_5_color)
            .toString());
    preset_inclination_1 = new DataObject(
        "", "", "", false, QStringLiteral("preset_inclination_1"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_inclination_1_label, QZSettings::default_tile_preset_inclination_1_label)
            .toString(),
        settings.value(QZSettings::tile_preset_inclination_1_color, QZSettings::default_tile_preset_inclination_1_color)
            .toString());
    preset_inclination_2 = new DataObject(
        "", "", "", false, QStringLiteral("preset_inclination_2"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_inclination_2_label, QZSettings::default_tile_preset_inclination_2_label)
            .toString(),
        settings.value(QZSettings::tile_preset_inclination_2_color, QZSettings::default_tile_preset_inclination_2_color)
            .toString());
    preset_inclination_3 = new DataObject(
        "", "", "", false, QStringLiteral("preset_inclination_3"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_inclination_3_label, QZSettings::default_tile_preset_inclination_3_label)
            .toString(),
        settings.value(QZSettings::tile_preset_inclination_3_color, QZSettings::default_tile_preset_inclination_3_color)
            .toString());
    preset_inclination_4 = new DataObject(
        "", "", "", false, QStringLiteral("preset_inclination_4"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_inclination_4_label, QZSettings::default_tile_preset_inclination_4_label)
            .toString(),
        settings.value(QZSettings::tile_preset_inclination_4_color, QZSettings::default_tile_preset_inclination_4_color)
            .toString());
    preset_inclination_5 = new DataObject(
        "", "", "", false, QStringLiteral("preset_inclination_5"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_inclination_5_label, QZSettings::default_tile_preset_inclination_5_label)
            .toString(),
        settings.value(QZSettings::tile_preset_inclination_5_color, QZSettings::default_tile_preset_inclination_5_color)
            .toString());
    preset_powerzone_1 = new DataObject(
        "", "", "", false, QStringLiteral("preset_powerzone_1"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_powerzone_1_label, QZSettings::default_tile_preset_powerzone_1_label).toString(),
        settings.value(QZSettings::tile_preset_powerzone_1_color, QZSettings::default_tile_preset_powerzone_1_color).toString());

    preset_powerzone_2 = new DataObject(
        "", "", "", false, QStringLiteral("preset_powerzone_2"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_powerzone_2_label, QZSettings::default_tile_preset_powerzone_2_label).toString(),
        settings.value(QZSettings::tile_preset_powerzone_2_color, QZSettings::default_tile_preset_powerzone_2_color).toString());

    preset_powerzone_3 = new DataObject(
        "", "", "", false, QStringLiteral("preset_powerzone_3"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_powerzone_3_label, QZSettings::default_tile_preset_powerzone_3_label).toString(),
        settings.value(QZSettings::tile_preset_powerzone_3_color, QZSettings::default_tile_preset_powerzone_3_color).toString());

    preset_powerzone_4 = new DataObject(
        "", "", "", false, QStringLiteral("preset_powerzone_4"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_powerzone_4_label, QZSettings::default_tile_preset_powerzone_4_label).toString(),
        settings.value(QZSettings::tile_preset_powerzone_4_color, QZSettings::default_tile_preset_powerzone_4_color).toString());

    preset_powerzone_5 = new DataObject(
        "", "", "", false, QStringLiteral("preset_powerzone_5"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_powerzone_5_label, QZSettings::default_tile_preset_powerzone_5_label).toString(),
        settings.value(QZSettings::tile_preset_powerzone_5_color, QZSettings::default_tile_preset_powerzone_5_color).toString());

    preset_powerzone_6 = new DataObject(
        "", "", "", false, QStringLiteral("preset_powerzone_6"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_powerzone_6_label, QZSettings::default_tile_preset_powerzone_6_label).toString(),
        settings.value(QZSettings::tile_preset_powerzone_6_color, QZSettings::default_tile_preset_powerzone_6_color).toString());

    preset_powerzone_7 = new DataObject(
        "", "", "", false, QStringLiteral("preset_powerzone_7"), 48, labelFontSize, QStringLiteral("white"),
        QLatin1String(""), 0, true,
        settings.value(QZSettings::tile_preset_powerzone_7_label, QZSettings::default_tile_preset_powerzone_7_label).toString(),
        settings.value(QZSettings::tile_preset_powerzone_7_color, QZSettings::default_tile_preset_powerzone_7_color).toString());

    tile_hr_time_in_zone_1 = new DataObject(QStringLiteral("HR Zone 1+"), QStringLiteral("icons/icons/heart_red.png"),
                                            QStringLiteral("0:00:00"), false, QStringLiteral("tile_hr_time_in_zone_1"), valueElapsedFontSize, labelFontSize);

    tile_hr_time_in_zone_2 = new DataObject(QStringLiteral("HR Zone 2+"), QStringLiteral("icons/icons/heart_red.png"),
                                            QStringLiteral("0:00:00"), false, QStringLiteral("tile_hr_time_in_zone_2"), valueElapsedFontSize, labelFontSize);

    tile_hr_time_in_zone_3 = new DataObject(QStringLiteral("HR Zone 3+"), QStringLiteral("icons/icons/heart_red.png"),
                                            QStringLiteral("0:00:00"), false, QStringLiteral("tile_hr_time_in_zone_3"), valueElapsedFontSize, labelFontSize);

    tile_hr_time_in_zone_4 = new DataObject(QStringLiteral("HR Zone 4+"), QStringLiteral("icons/icons/heart_red.png"),
                                            QStringLiteral("0:00:00"), false, QStringLiteral("tile_hr_time_in_zone_4"), valueElapsedFontSize, labelFontSize);

    tile_hr_time_in_zone_5 = new DataObject(QStringLiteral("HR Zone 5"), QStringLiteral("icons/icons/heart_red.png"),
                                            QStringLiteral("0:00:00"), false, QStringLiteral("tile_hr_time_in_zone_5"), valueElapsedFontSize, labelFontSize);

    tile_heat_time_in_zone_1 = new DataObject(QStringLiteral("Heat Zone 1"), QStringLiteral("icons/icons/fan.png"),
                                              QStringLiteral("0:00:00"), false, QStringLiteral("tile_heat_time_in_zone_1"), valueElapsedFontSize, labelFontSize);

    tile_heat_time_in_zone_2 = new DataObject(QStringLiteral("Heat Zone 2"), QStringLiteral("icons/icons/fan.png"),
                                              QStringLiteral("0:00:00"), false, QStringLiteral("tile_heat_time_in_zone_2"), valueElapsedFontSize, labelFontSize);

    tile_heat_time_in_zone_3 = new DataObject(QStringLiteral("Heat Zone 3"), QStringLiteral("icons/icons/fan.png"),
                                              QStringLiteral("0:00:00"), false, QStringLiteral("tile_heat_time_in_zone_3"), valueElapsedFontSize, labelFontSize);

    tile_heat_time_in_zone_4 = new DataObject(QStringLiteral("Heat Zone 4"), QStringLiteral("icons/icons/fan.png"),
                                              QStringLiteral("0:00:00"), false, QStringLiteral("tile_heat_time_in_zone_4"), valueElapsedFontSize, labelFontSize);

    coreTemperature = new DataObject(QStringLiteral("Core Temp"), QStringLiteral("icons/icons/heart_red.png"),
                                  QStringLiteral("0"), false, QStringLiteral("coretemperature"), 48, labelFontSize, QStringLiteral("white"), QLatin1String(""));


    if (!settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {

        m_topBarHeight = 0;
        emit topBarHeightChanged(m_topBarHeight); // NOTE: clazy-incorrecrt-emit
        m_info = QLatin1String("");
        emit infoChanged(m_info); // NOTE: clazy-incorrecrt-emit
    }

    stravaPelotonActivityName = QLatin1String("");
    stravaPelotonInstructorName = QLatin1String("");
    activityDescription = QLatin1String("");
    stravaWorkoutName = QLatin1String("");
    movieFileName = QUrl("");

#if defined(LICENSE) && (defined(Q_OS_WIN) || (defined(Q_OS_MAC) && !defined(Q_OS_IOS)) || defined(Q_OS_ANDROID))
#ifndef STEAM_STORE
    connect(engine, &QQmlApplicationEngine::quit, &QGuiApplication::quit);
    connect(&tLicense, &QTimer::timeout, this, &homeform::licenseTimeout);
    tLicense.start(600000);
    licenseRequest();
#endif
#endif

    this->bluetoothManager = bl;
    this->engine = engine;
    connect(bluetoothManager, &bluetooth::bluetoothDeviceConnected, this, &homeform::bluetoothDeviceConnected);
    connect(bluetoothManager, &bluetooth::bluetoothDeviceDisconnected, this, &homeform::bluetoothDeviceDisconnected);
    connect(bluetoothManager, &bluetooth::deviceFound, this, &homeform::deviceFound);
    connect(bluetoothManager, &bluetooth::deviceConnected, this, &homeform::deviceConnected);
    connect(bluetoothManager, &bluetooth::deviceConnected, this, &homeform::trainProgramSignals);
    // Both endpoints speak the same vocabulary, and both are driven from outside the app:
    // inner_QZWS backs the pages QZ serves itself, and user_QZWS is what tools/qz-rouvy-rtss
    // and tools/xbox-mywhoosh-gears connect to from a PC. The control half used to be wired
    // from the inner manager alone, so a gears_plus arriving on the documented port 6666 was
    // parsed, dispatched, emitted and then dropped on the floor. See STRIP-SPEC.md, 7 Group D.
    for (TemplateInfoSenderBuilder *tm : {this->userTemplateManager, this->innerTemplateManager}) {
        connect(this, &homeform::workoutNameChanged, tm, &TemplateInfoSenderBuilder::onWorkoutNameChanged);
        connect(this, &homeform::workoutStartDateChanged, tm, &TemplateInfoSenderBuilder::onWorkoutStartDate);
        connect(this, &homeform::instructorNameChanged, tm, &TemplateInfoSenderBuilder::onInstructorName);
        connect(this, &homeform::workoutEventStateChanged, tm, &TemplateInfoSenderBuilder::workoutEventStateChanged);
        connect(tm, &TemplateInfoSenderBuilder::activityDescriptionChanged, this, &homeform::setActivityDescription);
        connect(tm, &TemplateInfoSenderBuilder::lap, this, &homeform::Lap);
        connect(tm, &TemplateInfoSenderBuilder::autoResistance, this, &homeform::toggleAutoResistance);
        connect(tm, &TemplateInfoSenderBuilder::pelotonOffset_Plus, this, &homeform::pelotonOffset_Plus);
        connect(tm, &TemplateInfoSenderBuilder::pelotonOffset_Minus, this, &homeform::pelotonOffset_Minus);
        connect(tm, &TemplateInfoSenderBuilder::pelotonOffset, this, &homeform::pelotonOffset);
        connect(tm, &TemplateInfoSenderBuilder::gears_Plus, this, &homeform::gearUp);
        connect(tm, &TemplateInfoSenderBuilder::gears_Minus, this, &homeform::gearDown);
        connect(tm, &TemplateInfoSenderBuilder::speed_Plus, this, &homeform::speedPlus);
        connect(tm, &TemplateInfoSenderBuilder::speed_Minus, this, &homeform::speedMinus);
        connect(tm, &TemplateInfoSenderBuilder::inclination_Plus, this, &homeform::inclinationPlus);
        connect(tm, &TemplateInfoSenderBuilder::inclination_Minus, this, &homeform::inclinationMinus);
        connect(tm, &TemplateInfoSenderBuilder::resistance_Plus, this, [this]() { Plus(QStringLiteral("resistance")); });
        connect(tm, &TemplateInfoSenderBuilder::resistance_Minus, this, [this]() { Minus(QStringLiteral("resistance")); });
        connect(tm, &TemplateInfoSenderBuilder::Start, this, &homeform::StartRequested);
        connect(tm, &TemplateInfoSenderBuilder::Pause, this, &homeform::Start);
        connect(tm, &TemplateInfoSenderBuilder::Stop, this, &homeform::StopRequested);
    }
    engine->rootContext()->setContextProperty(QStringLiteral("rootItem"), (QObject *)this);

    this->trainProgram = new trainprogram(QList<trainrow>(), bl);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &homeform::update);
    timer->start(1s);

    automaticShiftingTimer = new QTimer(this);
    connect(automaticShiftingTimer, &QTimer::timeout, this, &homeform::ten_hz);
    
    if (settings.value(QZSettings::automatic_virtual_shifting_enabled, QZSettings::default_automatic_virtual_shifting_enabled).toBool()) {
        automaticShiftingTimer->start(100); // 100ms = 10Hz
    }

    if (settings.value(QZSettings::trainprogram_clipboard_workout_enabled,
                       QZSettings::default_trainprogram_clipboard_workout_enabled)
            .toBool()) {
        clipboardWorkoutTimer = new QTimer(this);
        connect(clipboardWorkoutTimer, &QTimer::timeout, this, &homeform::checkClipboardForWorkout);
        clipboardWorkoutTimer->start(5s);
    }

    QObject *rootObject = engine->rootObjects().constFirst();
    QObject *home = rootObject->findChild<QObject *>(QStringLiteral("home"));
    QObject *stack = rootObject;
    engine->rootContext()->setContextProperty("pathController", &pathController);
    QObject::connect(home, SIGNAL(start_clicked()), this, SLOT(Start()));
    QObject::connect(home, SIGNAL(stop_clicked()), this, SLOT(Stop()));
    QObject::connect(stack, SIGNAL(trainprogram_open_clicked(QUrl)), this, SLOT(trainprogram_open_clicked(QUrl)));
    QObject::connect(stack, SIGNAL(trainprogram_open_other_folder(QUrl)), this, SLOT(trainprogram_open_other_folder(QUrl)));
    QObject::connect(stack, SIGNAL(gpx_open_other_folder(QUrl)), this, SLOT(gpx_open_other_folder(QUrl)));
    QObject::connect(stack, SIGNAL(profile_open_clicked(QUrl)), this, SLOT(profile_open_clicked(QUrl)));
    QObject::connect(stack, SIGNAL(trainprogram_preview(QUrl)), this, SLOT(trainprogram_preview(QUrl)));
    QObject::connect(stack, SIGNAL(gpxpreview_open_clicked(QUrl)), this, SLOT(gpxpreview_open_clicked(QUrl)));
    QObject::connect(stack, SIGNAL(fitfile_preview_clicked(QUrl)), this, SLOT(fitfile_preview_clicked(QUrl)));
    QObject::connect(stack, SIGNAL(trainprogram_zwo_loaded(QString)), this, SLOT(trainprogram_zwo_loaded(QString)));
    QObject::connect(stack, SIGNAL(trainprogram_autostart_requested()), this, SLOT(trainprogram_autostart_requested()));
    QObject::connect(stack, SIGNAL(gpx_open_clicked(QUrl)), this, SLOT(gpx_open_clicked(QUrl)));
    QObject::connect(stack, SIGNAL(gpx_save_clicked()), this, SLOT(gpx_save_clicked()));
    QObject::connect(stack, SIGNAL(fit_save_clicked()), this, SLOT(fit_save_clicked()));
    QObject::connect(stack, SIGNAL(refresh_bluetooth_devices_clicked()), this,
                     SLOT(refresh_bluetooth_devices_clicked()));
    QObject::connect(home, SIGNAL(lap_clicked()), this, SLOT(Lap()));
    QObject::connect(stack, SIGNAL(loadSettings(QUrl)), this, SLOT(loadSettings(QUrl)));
    QObject::connect(stack, SIGNAL(saveSettings(QUrl)), this, SLOT(saveSettings(QUrl)));
    QObject::connect(stack, SIGNAL(deleteSettings(QUrl)), this, SLOT(deleteSettings(QUrl)));
    QObject::connect(stack, SIGNAL(restoreSettings()), this, SLOT(restoreSettings()));
    QObject::connect(stack, SIGNAL(saveProfile(QString)), this, SLOT(saveProfile(QString)));
    QObject::connect(stack, SIGNAL(restart()), this, SLOT(restart()));

    QObject::connect(stack, SIGNAL(volumeUp()), this, SLOT(volumeUp()));
    QObject::connect(stack, SIGNAL(volumeDown()), this, SLOT(volumeDown()));
    QObject::connect(stack, SIGNAL(keyMediaPrevious()), this, SLOT(keyMediaPrevious()));
    QObject::connect(stack, SIGNAL(keyMediaNext()), this, SLOT(keyMediaNext()));

    qDebug() << "homeform constructor events linked";

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QObject::connect(engine, &QQmlApplicationEngine::quit, &QGuiApplication::quit);
#endif

    if (settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {
        emit stopIconChanged(stopIcon());     // NOTE: clazy-incorrecrt-emit
        emit stopTextChanged(stopText());     // NOTE: clazy-incorrecrt-emit
        emit startIconChanged(startIcon());   // NOTE: clazy-incorrecrt-emit
        emit startTextChanged(startText());   // NOTE: clazy-incorrecrt-emit
        emit startColorChanged(startColor()); // NOTE: clazy-incorrecrt-emit
        emit stopColorChanged(stopColor());   // NOTE: clazy-incorrecrt-emit
    }

    emit tile_orderChanged(tile_order()); // NOTE: clazy-incorrecrt-emit

    // copying bundles zwo files in the right path if necessary
    QDirIterator itZwo(":/zwo/");
    QDir().mkdir(getWritableAppDir() + "training/");
    while (itZwo.hasNext()) {
        qDebug() << itZwo.next() << itZwo.fileName();
        QString targetPath = getWritableAppDir() + "training/" + itZwo.fileName();
        QString markerPath = getWritableAppDir() + "training/.deleted_" + itZwo.fileName();
        // Only copy if file doesn't exist AND no deletion marker exists
        if (!QFile(targetPath).exists() && !QFile(markerPath).exists()) {
            QFile::copy(":/zwo/" + itZwo.fileName(), targetPath);
        }
    }

    QDirIterator itGpx(":/gpx/");
    QDir().mkdir(getWritableAppDir() + "gpx/");
    while (itGpx.hasNext()) {
        qDebug() << itGpx.next() << itGpx.fileName();
        QString targetPath = getWritableAppDir() + "gpx/" + itGpx.fileName();
        QString markerPath = getWritableAppDir() + "gpx/.deleted_" + itGpx.fileName();
        // Only copy if file doesn't exist AND no deletion marker exists
        if (!QFile(targetPath).exists() && !QFile(markerPath).exists()) {
            QFile::copy(":/gpx/" + itGpx.fileName(), targetPath);
        }
    }

    QDirIterator itFit(getWritableAppDir(), QStringList() << "*.fit", QDir::Files);
    qDebug() << itFit.path();
    QDir().mkdir(getWritableAppDir() + "fit");
    while (itFit.hasNext()) {
        qDebug() << itFit.filePath() << itFit.fileName() << itFit.filePath().replace(itFit.path(), "");
        if (!QFile(getWritableAppDir() + "fit/" + itFit.next().replace(itFit.path(), "")).exists() && !itFit.fileName().contains("backup")) {
            if(QFile::copy(itFit.filePath(), getWritableAppDir() + "fit/" + itFit.filePath().replace(itFit.path(), "")))
                QFile::remove(itFit.filePath());
        }
    }


#ifdef Q_OS_ANDROID

    QString bluetoothName = getBluetoothName();
    qDebug() << "getBluetoothName()" << bluetoothName;

    QRegularExpression regex("^[A-Za-z0-9 ]+$");
    if(bluetoothName.length() > 9 || !regex.match(bluetoothName).hasMatch()) {
        setToastRequested("Bluetooth name too long, change it to a 4 letters one in the android settings and use only A-Z or 0-9 characters");
    }
    
    // Android 14 restrics access to /Android/data folder
    bool android_documents_folder = settings.value(QZSettings::android_documents_folder, QZSettings::default_android_documents_folder).toBool();
    if (android_documents_folder || QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Android, 14)) {
        QDirIterator itAndroid(getAndroidDataAppDir(), QDirIterator::Subdirectories);
        QDir().mkdir(getWritableAppDir());
        QDir().mkdir(getProfileDir());
        while (itAndroid.hasNext()) {
            qDebug() << itAndroid.filePath() << itAndroid.fileName() << itAndroid.filePath().replace(itAndroid.path(), "");
            if (!QFile(getWritableAppDir() + itAndroid.next().replace(itAndroid.path(), "")).exists()) {
                if(QFile::copy(itAndroid.filePath(), getWritableAppDir() + itAndroid.filePath().replace(itAndroid.path(), "")))
                       QFile::remove(itAndroid.filePath());
            }
        }
    }
#endif

    m_speech.setLocale(QLocale::English);

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    QBluetoothDeviceInfo b;
    deviceConnected(b);
#endif

    if (settings.value(QZSettings::peloton_bike_ocr, QZSettings::default_peloton_bike_ocr).toBool()) {
        QBluetoothDeviceInfo b;
        deviceConnected(b);
    }

#ifndef Q_OS_IOS
    iphone_browser = new QMdnsEngine::Browser(&iphone_server, "_qz_iphone._tcp.local.", &iphone_cache);

    QObject::connect(iphone_browser, &QMdnsEngine::Browser::serviceAdded, [](const QMdnsEngine::Service &service) {
        homeform::singleton()->iphone_service = service;
        qDebug() << service.name() << service.hostname() << service.port() << "discovered!";

        if (homeform::singleton()->iphone_resolver)
            delete homeform::singleton()->iphone_resolver;
        homeform::singleton()->iphone_resolver = new QMdnsEngine::Resolver(
            &homeform::singleton()->iphone_server, service.hostname(), &homeform::singleton()->iphone_cache);
        QObject::connect(homeform::singleton()->iphone_resolver, &QMdnsEngine::Resolver::resolved,
                         [](const QHostAddress &address) {
                             qDebug() << "resolved to" << address;
                             if (address.protocol() == QAbstractSocket::IPv4Protocol &&
                                 (homeform::singleton()->iphone_socket == nullptr ||
                                  !homeform::singleton()->iphone_address.isEqual(address))) {
                                 if (homeform::singleton()->iphone_socket)
                                     delete homeform::singleton()->iphone_socket;
                                 homeform::singleton()->iphone_socket = new QTcpSocket();
                                 QObject::connect(homeform::singleton()->iphone_socket, &QTcpSocket::connected,
                                                  []() { qDebug() << "iphone_socket connected!"; });
                                 QObject::connect(homeform::singleton()->iphone_socket, &QTcpSocket::readyRead, []() {
                                     QString rec = homeform::singleton()->iphone_socket->readAll();
                                     qDebug() << "iphone_socket received << " << rec;
                                     QStringList fields = rec.split("#");
                                     foreach (QString f, fields) {
                                         if (f.contains("HR")) {
                                             QStringList values = f.split("=");
                                             if (values.length() > 1) {
                                                 double hr = values[1].toDouble();
                                                 emit homeform::singleton()->heartRate(hr);
#ifndef IO_UNDER_QT
                                                 lockscreen ls;
                                                 ls.setHeartRate((unsigned char)hr);
#endif
                                             }
                                         }
                                     }
                                 });

                                 homeform::singleton()->iphone_address = address;
                                 homeform::singleton()->iphone_socket->connectToHost(
                                     address, homeform::singleton()->iphone_service.port());
                             }
                         });
    });

    QObject::connect(iphone_browser, &QMdnsEngine::Browser::serviceUpdated, [](const QMdnsEngine::Service &service) {
        homeform::singleton()->iphone_service = service;
        qDebug() << service.name() << service.hostname() << service.port() << "updated!";

        if (homeform::singleton()->iphone_resolver)
            delete homeform::singleton()->iphone_resolver;
        homeform::singleton()->iphone_resolver = new QMdnsEngine::Resolver(
            &homeform::singleton()->iphone_server, service.hostname(), &homeform::singleton()->iphone_cache);
        QObject::connect(homeform::singleton()->iphone_resolver, &QMdnsEngine::Resolver::resolved,
                         [](const QHostAddress &address) {
                             if (address.protocol() == QAbstractSocket::IPv4Protocol &&
                                 (homeform::singleton()->iphone_socket == nullptr ||
                                  !homeform::singleton()->iphone_address.isEqual(address))) {
                                 if (homeform::singleton()->iphone_socket)
                                     delete homeform::singleton()->iphone_socket;
                                 qDebug() << "resolved to" << address;
                                 homeform::singleton()->iphone_socket = new QTcpSocket();
                                 QObject::connect(homeform::singleton()->iphone_socket, &QTcpSocket::connected,
                                                  []() { qDebug() << "iphone_socket connected!"; });
                                 QObject::connect(homeform::singleton()->iphone_socket, &QTcpSocket::readyRead, []() {
                                     QString rec = homeform::singleton()->iphone_socket->readAll();
                                     qDebug() << "iphone_socket received << " << rec;
                                     QStringList fields = rec.split("#");
                                     foreach (QString f, fields) {
                                         if (f.contains("HR")) {
                                             QStringList values = f.split("=");
                                             if (values.length() > 1) {
                                                 double hr = values[1].toDouble();
                                                 emit homeform::singleton()->heartRate(hr);
#ifndef IO_UNDER_QT
                                                 lockscreen ls;
                                                 ls.setHeartRate((unsigned char)hr);
#endif
                                             }
                                         }
                                     }
                                 });
                                 homeform::singleton()->iphone_address = address;
                                 homeform::singleton()->iphone_socket->connectToHost(
                                     address, homeform::singleton()->iphone_service.port());
                             }
                         });
    });
#else
#ifndef IO_UNDER_QT
    h = new lockscreen();
    h->appleWatchAppInstalled();
#endif
#endif

    if (QSslSocket::supportsSsl()) {
        qDebug() << "SSL supported";
    } else {
        qDebug() << "SSL non supported";
    }
    
#ifdef Q_OS_ANDROID
    QAndroidJniObject javaPath = QAndroidJniObject::fromString(getWritableAppDir());
    QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/Shortcuts", "createShortcutsForFiles",
                                                "(Ljava/lang/String;Landroid/content/Context;)V", javaPath.object<jstring>(), QtAndroid::androidContext().object());

    // Only register MediaButtonReceiver if volume_change_gears is enabled
    if (settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
        qDebug() << "Registering MediaButtonReceiver for volume gear control";
        QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/MediaButtonReceiver",
                                                  "registerReceiver",
                                                  "(Landroid/content/Context;)V",
                                                  QtAndroid::androidContext().object());
    }
#endif

    bluetoothManager->homeformLoaded = true;
}

#ifdef Q_OS_ANDROID
extern "C" {
JNIEXPORT void JNICALL
Java_org_cagnulen_qdomyoszwift_CustomQtActivity_nativeOnDocumentPicked(JNIEnv *env, jclass clazz, jint requestCode,
                                                                       jint resultCode, jstring localPathString) {
    Q_UNUSED(clazz)
    if (resultCode != AndroidActivityResultOk || !homeform::singleton()) {
        return;
    }

    QString localPath;
    if (localPathString) {
        const char *pathChars = env->GetStringUTFChars(localPathString, nullptr);
        localPath = QString::fromUtf8(pathChars ? pathChars : "");
        if (pathChars) {
            env->ReleaseStringUTFChars(localPathString, pathChars);
        }
    }

    if (localPath.isEmpty()) {
        return;
    }

    QMetaObject::invokeMethod(homeform::singleton(), "handleAndroidDocumentPicked", Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(requestCode)), Q_ARG(QString, localPath));
}

JNIEXPORT void JNICALL
  Java_org_cagnulen_qdomyoszwift_MediaButtonReceiver_nativeOnMediaButtonEvent(JNIEnv *env, jobject obj, jint prev, jint current, jint max) {
    qDebug() << "Media button event: current =" << current << "max =" << max << "prev =" << prev;
    static QDateTime volumeLastChange = QDateTime::currentDateTime();
    QSettings settings;
    bool gears_volume_debouncing = settings.value(QZSettings::gears_volume_debouncing, QZSettings::default_gears_volume_debouncing).toBool();

    if (!settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
      qDebug() << "volume_change_gears disabled!"; 
      return;
    }
  
    if(gears_volume_debouncing && volumeLastChange.msecsTo(QDateTime::currentDateTime()) < 500) {
      qDebug() << "volume debouncing"; 
      return;
    }
  
    if(prev > current)
      homeform::singleton()->Minus(QStringLiteral("gears"));
    else
      homeform::singleton()->Plus(QStringLiteral("gears"));      

    volumeLastChange = QDateTime::currentDateTime();
  }
}
#endif

void homeform::setActivityDescription(QString desc) { activityDescription = desc; }

void homeform::keyMediaPrevious() {
    qDebug() << QStringLiteral("keyMediaPrevious");
    QSettings settings;
    if (settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
        Minus(QStringLiteral("gears"));
        Minus(QStringLiteral("gears"));
        Minus(QStringLiteral("gears"));
        Minus(QStringLiteral("gears"));
        Minus(QStringLiteral("gears"));
    }
}

void homeform::keyMediaNext() {
    qDebug() << QStringLiteral("keyMediaNext");
    QSettings settings;
    if (settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
        Plus(QStringLiteral("gears"));
        Plus(QStringLiteral("gears"));
        Plus(QStringLiteral("gears"));
        Plus(QStringLiteral("gears"));
        Plus(QStringLiteral("gears"));
    }
}

void homeform::volumeUp() {
    qDebug() << QStringLiteral("volumeUp");
    QSettings settings;
    if (bluetoothManager->device() && bluetoothManager->device()->deviceType() == TREADMILL) {
        Plus(QStringLiteral("speed"));
    } else if (settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
        Plus(QStringLiteral("gears"));
    }
}

void homeform::volumeDown() {
    qDebug() << QStringLiteral("volumeDown");
    QSettings settings;
    if (bluetoothManager->device() && bluetoothManager->device()->deviceType() == TREADMILL) {
        Minus(QStringLiteral("speed"));
    } else if (settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
        Minus(QStringLiteral("gears"));
    }
}

void homeform::zwiftLoginState(bool ok) {

    m_zwiftLoginState = (ok ? 1 : 0);
    emit zwiftLoginChanged(m_zwiftLoginState);
    if (!ok) {
        setToastRequested("Zwift Login Error!");
    }
}


QString homeform::getWritableAppDir() {
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

void homeform::ten_hz() {
    // Automatic Virtual Shifting logic - only for bikes and when device is connected
    if (!bluetoothManager->device() || bluetoothManager->device()->deviceType() != BIKE) {
        return;
    }
    
    QSettings settings;
    if (!settings.value(QZSettings::automatic_virtual_shifting_enabled, QZSettings::default_automatic_virtual_shifting_enabled).toBool()) {
        return;
    }
    
    uint8_t cadence = bluetoothManager->device()->currentCadence().value();
    if (cadence == 0) {
        return; // No cadence data available
    }
    
    // Get selected profile (0=cruise, 1=climb, 2=sprint)
    int profile = settings.value(QZSettings::automatic_virtual_shifting_profile, QZSettings::default_automatic_virtual_shifting_profile).toInt();
    
    int gearUpCadenceThreshold, gearDownCadenceThreshold;
    float gearUpTimeThreshold, gearDownTimeThreshold;
    
    // Load settings based on selected profile
    switch (profile) {
        case 1: // Climb profile
            gearUpCadenceThreshold = settings.value(QZSettings::automatic_virtual_shifting_climb_gear_up_cadence, QZSettings::default_automatic_virtual_shifting_climb_gear_up_cadence).toInt();
            gearUpTimeThreshold = settings.value(QZSettings::automatic_virtual_shifting_climb_gear_up_time, QZSettings::default_automatic_virtual_shifting_climb_gear_up_time).toFloat();
            gearDownCadenceThreshold = settings.value(QZSettings::automatic_virtual_shifting_climb_gear_down_cadence, QZSettings::default_automatic_virtual_shifting_climb_gear_down_cadence).toInt();
            gearDownTimeThreshold = settings.value(QZSettings::automatic_virtual_shifting_climb_gear_down_time, QZSettings::default_automatic_virtual_shifting_climb_gear_down_time).toFloat();
            break;
        case 2: // Sprint profile
            gearUpCadenceThreshold = settings.value(QZSettings::automatic_virtual_shifting_sprint_gear_up_cadence, QZSettings::default_automatic_virtual_shifting_sprint_gear_up_cadence).toInt();
            gearUpTimeThreshold = settings.value(QZSettings::automatic_virtual_shifting_sprint_gear_up_time, QZSettings::default_automatic_virtual_shifting_sprint_gear_up_time).toFloat();
            gearDownCadenceThreshold = settings.value(QZSettings::automatic_virtual_shifting_sprint_gear_down_cadence, QZSettings::default_automatic_virtual_shifting_sprint_gear_down_cadence).toInt();
            gearDownTimeThreshold = settings.value(QZSettings::automatic_virtual_shifting_sprint_gear_down_time, QZSettings::default_automatic_virtual_shifting_sprint_gear_down_time).toFloat();
            break;
        default: // Cruise profile (0)
            gearUpCadenceThreshold = settings.value(QZSettings::automatic_virtual_shifting_gear_up_cadence, QZSettings::default_automatic_virtual_shifting_gear_up_cadence).toInt();
            gearUpTimeThreshold = settings.value(QZSettings::automatic_virtual_shifting_gear_up_time, QZSettings::default_automatic_virtual_shifting_gear_up_time).toFloat();
            gearDownCadenceThreshold = settings.value(QZSettings::automatic_virtual_shifting_gear_down_cadence, QZSettings::default_automatic_virtual_shifting_gear_down_cadence).toInt();
            gearDownTimeThreshold = settings.value(QZSettings::automatic_virtual_shifting_gear_down_time, QZSettings::default_automatic_virtual_shifting_gear_down_time).toFloat();
            break;
    }
    
    QDateTime now = QDateTime::currentDateTime();
    
    // Check for gear up condition
    if (cadence >= gearUpCadenceThreshold) {
        // Start or continue timing for gear up
        if (automaticShiftingGearUpStartTime.isNull() || automaticShiftingGearUpStartTime.msecsTo(now) < 0) {
            automaticShiftingGearUpStartTime = now;
        }
        // Reset gear down timer since we're above gear up threshold
        automaticShiftingGearDownStartTime = now;
        
        // Check if enough time has passed for gear up
        if (automaticShiftingGearUpStartTime.msecsTo(now) >= (gearUpTimeThreshold * 1000)) {
            qDebug() << "Automatic gear up triggered: cadence" << cadence << "threshold" << gearUpCadenceThreshold << "time" << automaticShiftingGearUpStartTime.msecsTo(now) / 1000.0;
            ((bike *)bluetoothManager->device())->gearUp();
            // Reset both timers after shifting
            automaticShiftingGearUpStartTime = now;
            automaticShiftingGearDownStartTime = now;
        }
    }
    // Check for gear down condition
    else if (cadence <= gearDownCadenceThreshold) {
        // Start or continue timing for gear down
        if (automaticShiftingGearDownStartTime.isNull() || automaticShiftingGearDownStartTime.msecsTo(now) < 0) {
            automaticShiftingGearDownStartTime = now;
        }
        // Reset gear up timer since we're below gear down threshold
        automaticShiftingGearUpStartTime = now;
        
        // Check if enough time has passed for gear down
        if (automaticShiftingGearDownStartTime.msecsTo(now) >= (gearDownTimeThreshold * 1000)) {
            qDebug() << "Automatic gear down triggered: cadence" << cadence << "threshold" << gearDownCadenceThreshold << "time" << automaticShiftingGearDownStartTime.msecsTo(now) / 1000.0;
            ((bike *)bluetoothManager->device())->gearDown();
            // Reset both timers after shifting
            automaticShiftingGearUpStartTime = now;
            automaticShiftingGearDownStartTime = now;
        }
    }
    // Cadence is between thresholds - reset both timers
    else {
        automaticShiftingGearUpStartTime = now;
        automaticShiftingGearDownStartTime = now;
    }
}

QString homeform::stopColor() { return QStringLiteral("#00000000"); }

QString homeform::startColor() {
    static uint8_t startColorToggle = 0;
    if (paused || stopped) {
        if (startColorToggle) {

            startColorToggle = 0;
            return QStringLiteral("red");
        } else {

            startColorToggle = 1;
            return QStringLiteral("#00000000");
        }
    }
    return QStringLiteral("#00000000");
}

void homeform::refresh_bluetooth_devices_clicked() {

    bluetoothManager->onlyDiscover = true;
    bluetoothManager->restart();
}

void homeform::selectGymModeDevice(const QString &deviceName) {
    if (!bluetoothManager)
        return;

    bluetoothManager->selectGymModeDevice(deviceName);
}

bool homeform::hasConnectedDevice() const {
    return bluetoothManager && bluetoothManager->device();
}

homeform::~homeform() { gpx_save_clicked(); }

void homeform::aboutToQuit() {
    qDebug() << "homeform::aboutToQuit()";

#ifdef Q_OS_ANDROID
    QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/NotificationClient", "hide", "()V");
#endif

#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
    // End iOS Live Activity
    ios_liveactivity::endLiveActivity();
#endif
#endif

    // Before the device goes: RTSS keeps drawing whatever was left behind, so a
    // stale gear number would stay frozen over the training app.
    rtssOsd.release();

    if (bluetoothManager->device())
        bluetoothManager->device()->disconnectBluetooth();
}

void homeform::trainProgramSignals() {
    if (bluetoothManager->device()) {
        disconnect(trainProgram, &trainprogram::start, bluetoothManager->device(), &bluetoothdevice::start);
        disconnect(trainProgram, &trainprogram::stop, bluetoothManager->device(), &bluetoothdevice::stop);
        disconnect(trainProgram, &trainprogram::stop, this, &homeform::StopFromTrainProgram);
        disconnect(trainProgram, &trainprogram::lap, this, &homeform::Lap);
        disconnect(trainProgram, &trainprogram::changeSpeed, ((treadmill *)bluetoothManager->device()),
                   &treadmill::changeSpeed);
        disconnect(trainProgram, &trainprogram::changeSpeed, this,
                   &homeform::onTrainingProgramSpeedChanged);
        disconnect(trainProgram, &trainprogram::changeInclination, ((treadmill *)bluetoothManager->device()),
                   &treadmill::changeInclination);
        disconnect(trainProgram, &trainprogram::changeNextInclination300Meters, bluetoothManager->device(),
                   &bluetoothdevice::changeNextInclination300Meters);
        disconnect(trainProgram, &trainprogram::changeInclination, ((bike *)bluetoothManager->device()),
                   &bike::changeInclination);
        disconnect(trainProgram, &trainprogram::changeFanSpeed, ((treadmill *)bluetoothManager->device()),
                   &treadmill::changeFanSpeed);
        disconnect(trainProgram, &trainprogram::changeSpeedAndInclination, ((treadmill *)bluetoothManager->device()),
                   &treadmill::changeSpeedAndInclination);
        disconnect(trainProgram, &trainprogram::changeResistance, ((bike *)bluetoothManager->device()),
                   &bike::changeResistance);
        disconnect(trainProgram, &trainprogram::changeRequestedPelotonResistance, ((bike *)bluetoothManager->device()),
                   &bike::changeRequestedPelotonResistance);
        disconnect(trainProgram, &trainprogram::changeCadence, ((bike *)bluetoothManager->device()),
                   &bike::changeCadence);
        disconnect(trainProgram, &trainprogram::changePower, ((treadmill *)bluetoothManager->device()), &treadmill::changePower);
        disconnect(trainProgram, &trainprogram::intervalTransitionApplied, ((treadmill *)bluetoothManager->device()),
                   &treadmill::onTrainingProgramTransition);
        disconnect(trainProgram, &trainprogram::changePower, ((bike *)bluetoothManager->device()), &bike::changePower);
        disconnect(trainProgram, &trainprogram::changePower, ((rower *)bluetoothManager->device()),
                   &rower::changePower);
        disconnect(trainProgram, &trainprogram::changeSpeed, ((rower *)bluetoothManager->device()),
                   &rower::changeSpeed);
        disconnect(trainProgram, &trainprogram::changeCadence, ((elliptical *)bluetoothManager->device()),
                   &elliptical::changeCadence);
        disconnect(trainProgram, &trainprogram::changePower, ((elliptical *)bluetoothManager->device()),
                   &elliptical::changePower);
        disconnect(trainProgram, &trainprogram::changeInclination, ((elliptical *)bluetoothManager->device()),
                   &elliptical::changeInclination);
        disconnect(trainProgram, &trainprogram::changeResistance, ((elliptical *)bluetoothManager->device()),
                   &elliptical::changeResistance);
        disconnect(trainProgram, &trainprogram::changeRequestedPelotonResistance,
                   ((elliptical *)bluetoothManager->device()), &elliptical::changeRequestedPelotonResistance);
        disconnect(((treadmill *)bluetoothManager->device()), &treadmill::tapeStarted, trainProgram,
                   &trainprogram::onTapeStarted);
        disconnect(((treadmill *)bluetoothManager->device()), &treadmill::buttonHWStart, this,
                   &homeform::StartFromDevice);
        disconnect(((treadmill *)bluetoothManager->device()), &treadmill::buttonHWPause, this,
                   &homeform::PauseFromDevice);
        disconnect(((treadmill *)bluetoothManager->device()), &treadmill::buttonHWStop, this,
                   &homeform::StopFromDevice);
        disconnect(((bike *)bluetoothManager->device()), &bike::bikeStarted, trainProgram,
                   &trainprogram::onTapeStarted);
        disconnect(trainProgram, &trainprogram::changeGeoPosition, bluetoothManager->device(),
                   &bluetoothdevice::changeGeoPosition);
        disconnect(this, &homeform::workoutEventStateChanged, bluetoothManager->device(),
                   &bluetoothdevice::workoutEventStateChanged);
        disconnect(trainProgram, &trainprogram::changeTimestamp, this, &homeform::changeTimestamp);
        disconnect(trainProgram, &trainprogram::toastRequest, this, &homeform::onToastRequested);
        disconnect(trainProgram, &trainprogram::intervalTransitionApplied, this,
                   &homeform::onTrainingProgramIntervalTransition);
        disconnect(trainProgram, &trainprogram::zwiftLoginState, this, &homeform::zwiftLoginState);

        connect(trainProgram, &trainprogram::start, bluetoothManager->device(), &bluetoothdevice::start);
        connect(trainProgram, &trainprogram::stop, bluetoothManager->device(), &bluetoothdevice::stop);
        connect(trainProgram, &trainprogram::stop, this, &homeform::StopFromTrainProgram);
        connect(trainProgram, &trainprogram::lap, this, &homeform::Lap);
        connect(trainProgram, &trainprogram::toastRequest, this, &homeform::onToastRequested);
        connect(trainProgram, &trainprogram::intervalTransitionApplied, this,
                &homeform::onTrainingProgramIntervalTransition);
        // Connect training program speed changes to reset HR PID timer
        connect(trainProgram, &trainprogram::changeSpeed, this,
                &homeform::onTrainingProgramSpeedChanged);
        if (bluetoothManager->device()->deviceType() == TREADMILL) {
            connect(trainProgram, &trainprogram::changeSpeed, ((treadmill *)bluetoothManager->device()),
                    &treadmill::changeSpeed);
            connect(trainProgram, &trainprogram::changeFanSpeed, ((treadmill *)bluetoothManager->device()),
                    &treadmill::changeFanSpeed);
            connect(trainProgram, &trainprogram::changeInclination, ((treadmill *)bluetoothManager->device()),
                    &treadmill::changeInclination);
            connect(trainProgram, &trainprogram::changeSpeedAndInclination, ((treadmill *)bluetoothManager->device()),
                    &treadmill::changeSpeedAndInclination);
            connect(trainProgram, &trainprogram::intervalTransitionApplied, ((treadmill *)bluetoothManager->device()),
                    &treadmill::onTrainingProgramTransition);
            connect(((treadmill *)bluetoothManager->device()), &treadmill::tapeStarted, trainProgram,
                    &trainprogram::onTapeStarted);
            connect(((treadmill *)bluetoothManager->device()), &treadmill::buttonHWStart, this,
                    &homeform::StartFromDevice);
            connect(((treadmill *)bluetoothManager->device()), &treadmill::buttonHWPause, this,
                    &homeform::PauseFromDevice);
            connect(((treadmill *)bluetoothManager->device()), &treadmill::buttonHWStop, this,
                    &homeform::StopFromDevice);
            connect(trainProgram, &trainprogram::changePower, ((treadmill *)bluetoothManager->device()), &treadmill::changePower);
        } else if (bluetoothManager->device()->deviceType() == BIKE) {
            connect(trainProgram, &trainprogram::changeCadence, ((bike *)bluetoothManager->device()),
                    &bike::changeCadence);
            connect(trainProgram, &trainprogram::changePower, ((bike *)bluetoothManager->device()), &bike::changePower);
            connect(trainProgram, &trainprogram::changeInclination, ((bike *)bluetoothManager->device()),
                    &bike::changeInclination);
            connect(trainProgram, &trainprogram::changeResistance, ((bike *)bluetoothManager->device()),
                    &bike::changeResistance);
            connect(trainProgram, &trainprogram::changeRequestedPelotonResistance, ((bike *)bluetoothManager->device()),
                    &bike::changeRequestedPelotonResistance);
            connect(((bike *)bluetoothManager->device()), &bike::bikeStarted, trainProgram,
                    &trainprogram::onTapeStarted);
        } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
            connect(trainProgram, &trainprogram::changeCadence, ((elliptical *)bluetoothManager->device()),
                    &elliptical::changeCadence);
            connect(trainProgram, &trainprogram::changePower, ((elliptical *)bluetoothManager->device()),
                    &elliptical::changePower);
            connect(trainProgram, &trainprogram::changeInclination, ((elliptical *)bluetoothManager->device()),
                    &elliptical::changeInclination);
            connect(trainProgram, &trainprogram::changeResistance, ((elliptical *)bluetoothManager->device()),
                    &elliptical::changeResistance);
            connect(trainProgram, &trainprogram::changeRequestedPelotonResistance,
                    ((elliptical *)bluetoothManager->device()), &elliptical::changeRequestedPelotonResistance);
        } else if (bluetoothManager->device()->deviceType() == ROWING) {
            connect(trainProgram, &trainprogram::changePower, ((rower *)bluetoothManager->device()),
                    &rower::changePower);
            connect(trainProgram, &trainprogram::changeResistance, ((rower *)bluetoothManager->device()),
                    &rower::changeResistance);
            connect(trainProgram, &trainprogram::changeCadence, ((rower *)bluetoothManager->device()),
                    &rower::changeCadence);
            connect(trainProgram, &trainprogram::changeSpeed, ((rower *)bluetoothManager->device()),
                    &rower::changeSpeed);
        }
        connect(trainProgram, &trainprogram::changeNextInclination300Meters, bluetoothManager->device(),
                &bluetoothdevice::changeNextInclination300Meters);
        connect(trainProgram, &trainprogram::changeGeoPosition, bluetoothManager->device(),
                &bluetoothdevice::changeGeoPosition);
        connect(trainProgram, &trainprogram::changeTimestamp, this, &homeform::changeTimestamp);
        connect(this, &homeform::workoutEventStateChanged, bluetoothManager->device(),
                &bluetoothdevice::workoutEventStateChanged);
        connect(trainProgram, &trainprogram::zwiftLoginState, this, &homeform::zwiftLoginState);

        qDebug() << QStringLiteral("trainProgram associated to a device");
    } else {
        qDebug() << QStringLiteral("trainProgram NOT associated to a device");
    }
}

void homeform::onToastRequested(QString message) {
    QSettings settings;
    setToastRequested(message);

    // Use TTS if enabled
    if (settings.value(QZSettings::tts_enabled, QZSettings::default_tts_enabled).toBool()) {
        m_speech.say(message);
    }
}

void homeform::onTrainingProgramIntervalTransition() {
    QSettings settings;
    if (settings.value(QZSettings::trainprogram_sound_on_segment,
                       QZSettings::default_trainprogram_sound_on_segment)
            .toBool()) {
        emit trainingProgramIntervalSoundRequested();
    }
}

void homeform::onTrainingProgramSpeedChanged(double speed) {
    // Record the timestamp when the training program changed speed
    // This is used by the HR PID controller to avoid race conditions
    lastTrainingProgramSpeedChange = QDateTime::currentDateTime();
}


QStringList homeform::tile_order() {

    QStringList r;
    r.reserve(100);
    for (int i = 0; i < 100; i++) {
        r.append(QString::number(i));
    }
    return r;
}

// these events come from the shifters, so when the auto resistance is off they should not be processed
void homeform::gearUp() {
    if (autoResistance()) {
        Plus(QStringLiteral("gears"));
        // Reset automatic shifting timers when user manually changes gears
        automaticShiftingGearUpStartTime = QDateTime::currentDateTime();
        automaticShiftingGearDownStartTime = QDateTime::currentDateTime();
    }
}

void homeform::gearDown() {
    if (autoResistance()) {
        Minus(QStringLiteral("gears"));
        // Reset automatic shifting timers when user manually changes gears
        automaticShiftingGearUpStartTime = QDateTime::currentDateTime();
        automaticShiftingGearDownStartTime = QDateTime::currentDateTime();
    }
}

void homeform::speedPlus() {
    Plus(QStringLiteral("speed"));
}

void homeform::speedMinus() {
    Minus(QStringLiteral("speed"));
}

void homeform::inclinationPlus() {
    Plus(QStringLiteral("inclination"));
}

void homeform::inclinationMinus() {
    Minus(QStringLiteral("inclination"));
}

void homeform::sortTiles() {

    QSettings settings;
    bool pelotoncadence =
        settings.value(QZSettings::bike_cadence_sensor, QZSettings::default_bike_cadence_sensor).toBool();

    if (!bluetoothManager || !bluetoothManager->device())
        return;

    dataList.clear();

    if (bluetoothManager->device()->deviceType() == TREADMILL) {
        for (int i = 0; i < 100; i++) {
            if (settings.value(QZSettings::tile_speed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_speed_order, 0).toInt() == i) {

                speed->setGridId(i);
                dataList.append(speed);
            }

            if (settings.value(QZSettings::tile_inclination_enabled, true).toBool() &&
                settings.value(QZSettings::tile_inclination_order, 0).toInt() == i) {
                inclination->setGridId(i);
                dataList.append(inclination);
            }

            if (settings.value(QZSettings::tile_elevation_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elevation_order, 0).toInt() == i) {
                elevation->setGridId(i);
                dataList.append(elevation);
            }

            if (settings.value(QZSettings::tile_elapsed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elapsed_order, 0).toInt() == i) {
                elapsed->setGridId(i);
                dataList.append(elapsed);
            }

            if (settings.value(QZSettings::tile_moving_time_enabled, false).toBool() &&
                settings.value(QZSettings::tile_moving_time_order, 19).toInt() == i) {
                moving_time->setGridId(i);
                dataList.append(moving_time);
            }

            if (settings.value(QZSettings::tile_peloton_offset_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_offset_order, 20).toInt() == i) {
                peloton_offset->setGridId(i);
                dataList.append(peloton_offset);
            }

            if (settings.value(QZSettings::tile_peloton_remaining_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_remaining_order, 20).toInt() == i) {
                peloton_remaining->setGridId(i);
                dataList.append(peloton_remaining);
            }

            if (settings.value(QZSettings::tile_calories_enabled, true).toBool() &&
                settings.value(QZSettings::tile_calories_order, 0).toInt() == i) {
                calories->setGridId(i);
                dataList.append(calories);
            }

            if (settings.value(QZSettings::tile_odometer_enabled, true).toBool() &&
                settings.value(QZSettings::tile_odometer_order, 0).toInt() == i) {
                odometer->setGridId(i);
                dataList.append(odometer);
            }

            if (settings.value(QZSettings::tile_pace_enabled, true).toBool() &&
                settings.value(QZSettings::tile_pace_order, 0).toInt() == i) {
                pace->setGridId(i);
                dataList.append(pace);
            }

            if (settings.value(QZSettings::tile_avg_pace_enabled, QZSettings::default_tile_avg_pace_enabled).toBool() &&
                settings.value(QZSettings::tile_avg_pace_order, QZSettings::default_tile_avg_pace_order).toInt() == i) {
                avg_pace->setGridId(i);
                dataList.append(avg_pace);
            }

            if (settings.value(QZSettings::tile_grade_adjusted_pace_enabled,
                               QZSettings::default_tile_grade_adjusted_pace_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_grade_adjusted_pace_order,
                               QZSettings::default_tile_grade_adjusted_pace_order)
                        .toInt() == i) {
                grade_adjusted_pace->setGridId(i);
                dataList.append(grade_adjusted_pace);
            }

            if (settings.value(QZSettings::tile_watt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_watt_order, 0).toInt() == i) {
                watt->setGridId(i);
                dataList.append(watt);
            }

            if (settings.value(QZSettings::tile_weight_loss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_weight_loss_order, 24).toInt() == i) {
                weightLoss->setGridId(i);
                dataList.append(weightLoss);
            }

            if (settings.value(QZSettings::tile_avgwatt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avgwatt_order, 0).toInt() == i) {
                avgWatt->setGridId(i);
                dataList.append(avgWatt);
            }

            if (settings.value(QZSettings::tile_avg_watt_lap_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avg_watt_lap_order, 0).toInt() == i) {
                avgWattLap->setGridId(i);
                dataList.append(avgWattLap);
            }

            if (settings.value(QZSettings::tile_ftp_enabled, true).toBool() &&
                settings.value(QZSettings::tile_ftp_order, 0).toInt() == i) {
                ftp->setGridId(i);
                dataList.append(ftp);
            }

            if (settings.value(QZSettings::tile_jouls_enabled, true).toBool() &&
                settings.value(QZSettings::tile_jouls_order, 0).toInt() == i) {
                jouls->setGridId(i);
                dataList.append(jouls);
            }

            if (settings.value(QZSettings::tile_heart_enabled, true).toBool() &&
                settings.value(QZSettings::tile_heart_order, 0).toInt() == i) {
                heart->setGridId(i);
                dataList.append(heart);
            }

            if (settings.value(QZSettings::tile_hrv_enabled, QZSettings::default_tile_hrv_enabled).toBool() &&
                settings.value(QZSettings::tile_hrv_order, QZSettings::default_tile_hrv_order).toInt() == i) {
                hrv->setGridId(i);
                dataList.append(hrv);
            }

            if (settings.value(QZSettings::tile_fan_enabled, true).toBool() &&
                settings.value(QZSettings::tile_fan_order, 0).toInt() == i) {
                fan->setGridId(i);
                dataList.append(fan);
            }

            if (settings.value(QZSettings::tile_datetime_enabled, true).toBool() &&
                settings.value(QZSettings::tile_datetime_order, 0).toInt() == i) {
                datetime->setGridId(i);
                dataList.append(datetime);
            }

            if (settings.value(QZSettings::tile_lapelapsed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_lapelapsed_order, 18).toInt() == i) {
                lapElapsed->setGridId(i);
                dataList.append(lapElapsed);
            }

            if (settings.value(QZSettings::tile_watt_kg_enabled, false).toBool() &&
                settings.value(QZSettings::tile_watt_kg_order, 24).toInt() == i) {
                wattKg->setGridId(i);
                dataList.append(wattKg);
            }

            if (settings.value(QZSettings::tile_remainingtimetrainprogramrow_enabled, false).toBool() &&
                settings.value(QZSettings::tile_remainingtimetrainprogramrow_order, 27).toInt() == i) {

                remaningTimeTrainingProgramCurrentRow->setGridId(i);
                dataList.append(remaningTimeTrainingProgramCurrentRow);
            }

            if (settings.value(QZSettings::tile_nextrowstrainprogram_enabled, false).toBool() &&
                settings.value(QZSettings::tile_nextrowstrainprogram_order, 31).toInt() == i) {

                nextRows->setGridId(i);
                dataList.append(nextRows);
            }

            if (settings.value(QZSettings::tile_mets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_mets_order, 28).toInt() == i) {

                mets->setGridId(i);
                dataList.append(mets);
            }
            if (settings.value(QZSettings::tile_targetmets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_targetmets_order, 29).toInt() == i) {

                targetMets->setGridId(i);
                dataList.append(targetMets);
            }

            if (settings.value(QZSettings::tile_target_speed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_speed_order, 28).toInt() == i) {
                target_speed->setGridId(i);
                dataList.append(target_speed);
            }

            if (settings.value(QZSettings::tile_target_incline_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_incline_order, 29).toInt() == i) {
                target_incline->setGridId(i);
                dataList.append(target_incline);
            }

            if (settings.value(QZSettings::tile_cadence_enabled, false).toBool() &&
                settings.value(QZSettings::tile_cadence_order, 30).toInt() == i) {
                cadence->setGridId(i);
                dataList.append(cadence);
            }

            if (settings.value(QZSettings::tile_pid_hr_enabled, false).toBool() &&
                settings.value(QZSettings::tile_pid_hr_order, 31).toInt() == i) {
                pidHR->setGridId(i);
                dataList.append(pidHR);
            }

            if (settings.value(QZSettings::tile_instantaneous_stride_length_enabled, false).toBool() &&
                settings.value(QZSettings::tile_instantaneous_stride_length_order, 32).toInt() == i) {
                instantaneousStrideLengthCM->setGridId(i);
                dataList.append(instantaneousStrideLengthCM);
            }

            if (settings.value(QZSettings::tile_ground_contact_enabled, false).toBool() &&
                settings.value(QZSettings::tile_ground_contact_order, 33).toInt() == i) {
                groundContactMS->setGridId(i);
                dataList.append(groundContactMS);
            }

            if (settings.value(QZSettings::tile_vertical_oscillation_enabled, false).toBool() &&
                settings.value(QZSettings::tile_vertical_oscillation_order, 34).toInt() == i) {
                verticalOscillationMM->setGridId(i);
                dataList.append(verticalOscillationMM);
            }

            if (settings.value(QZSettings::tile_preset_speed_1_enabled, QZSettings::default_tile_preset_speed_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_1_order, QZSettings::default_tile_preset_speed_1_order)
                        .toInt() == i) {
                preset_speed_1->setGridId(i);
                dataList.append(preset_speed_1);
            }
            if (settings.value(QZSettings::tile_preset_speed_2_enabled, QZSettings::default_tile_preset_speed_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_2_order, QZSettings::default_tile_preset_speed_2_order)
                        .toInt() == i) {
                preset_speed_2->setGridId(i);
                dataList.append(preset_speed_2);
            }
            if (settings.value(QZSettings::tile_preset_speed_3_enabled, QZSettings::default_tile_preset_speed_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_3_order, QZSettings::default_tile_preset_speed_3_order)
                        .toInt() == i) {
                preset_speed_3->setGridId(i);
                dataList.append(preset_speed_3);
            }
            if (settings.value(QZSettings::tile_preset_speed_4_enabled, QZSettings::default_tile_preset_speed_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_4_order, QZSettings::default_tile_preset_speed_4_order)
                        .toInt() == i) {
                preset_speed_4->setGridId(i);
                dataList.append(preset_speed_4);
            }
            if (settings.value(QZSettings::tile_preset_speed_5_enabled, QZSettings::default_tile_preset_speed_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_5_order, QZSettings::default_tile_preset_speed_5_order)
                        .toInt() == i) {
                preset_speed_5->setGridId(i);
                dataList.append(preset_speed_5);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_1_enabled,
                           QZSettings::default_tile_preset_inclination_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_1_order,
                               QZSettings::default_tile_preset_inclination_1_order)
                        .toInt() == i) {
                preset_inclination_1->setGridId(i);
                dataList.append(preset_inclination_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_2_enabled,
                           QZSettings::default_tile_preset_inclination_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_2_order,
                               QZSettings::default_tile_preset_inclination_2_order)
                        .toInt() == i) {
                preset_inclination_2->setGridId(i);
                dataList.append(preset_inclination_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_3_enabled,
                           QZSettings::default_tile_preset_inclination_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_3_order,
                               QZSettings::default_tile_preset_inclination_3_order)
                        .toInt() == i) {
                preset_inclination_3->setGridId(i);
                dataList.append(preset_inclination_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_4_enabled,
                           QZSettings::default_tile_preset_inclination_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_4_order,
                               QZSettings::default_tile_preset_inclination_4_order)
                        .toInt() == i) {
                preset_inclination_4->setGridId(i);
                dataList.append(preset_inclination_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_5_enabled,
                           QZSettings::default_tile_preset_inclination_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_5_order,
                               QZSettings::default_tile_preset_inclination_5_order)
                        .toInt() == i) {
                preset_inclination_5->setGridId(i);
                dataList.append(preset_inclination_5);
            }

            if (settings.value(QZSettings::tile_target_pace_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_pace_order, 50).toInt() == i) {
                target_pace->setGridId(i);
                dataList.append(target_pace);
            }

            if (settings.value(QZSettings::tile_step_count_enabled, QZSettings::default_tile_step_count_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_step_count_order, QZSettings::default_tile_step_count_order)
                        .toInt() == i) {

                stepCount->setGridId(i);
                dataList.append(stepCount);
            }            

            if (settings.value(QZSettings::tile_rss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_rss_order, 53).toInt() == i) {
                rss->setGridId(i);
                dataList.append(rss);
            }

            if (settings.value(QZSettings::tile_target_power_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_power_order, 20).toInt() == i) {
                target_power->setGridId(i);
                dataList.append(target_power);
            }

            if (settings.value(QZSettings::tile_target_zone_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_zone_order, 24).toInt() == i) {
                target_zone->setGridId(i);
                dataList.append(target_zone);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_1_order, 0).toInt() == i) {
                tile_hr_time_in_zone_1->setGridId(i);
                dataList.append(tile_hr_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_2_order, 0).toInt() == i) {
                tile_hr_time_in_zone_2->setGridId(i);
                dataList.append(tile_hr_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_3_order, 0).toInt() == i) {
                tile_hr_time_in_zone_3->setGridId(i);
                dataList.append(tile_hr_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_4_order, 0).toInt() == i) {
                tile_hr_time_in_zone_4->setGridId(i);
                dataList.append(tile_hr_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_5_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_5_order, 0).toInt() == i) {
                tile_hr_time_in_zone_5->setGridId(i);
                dataList.append(tile_hr_time_in_zone_5);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_1_order, 0).toInt() == i) {
                tile_heat_time_in_zone_1->setGridId(i);
                dataList.append(tile_heat_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_2_order, 0).toInt() == i) {
                tile_heat_time_in_zone_2->setGridId(i);
                dataList.append(tile_heat_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_3_order, 0).toInt() == i) {
                tile_heat_time_in_zone_3->setGridId(i);
                dataList.append(tile_heat_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_4_order, 0).toInt() == i) {
                tile_heat_time_in_zone_4->setGridId(i);
                dataList.append(tile_heat_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_coretemperature_enabled, QZSettings::default_tile_coretemperature_enabled).toBool() &&
                settings.value(QZSettings::tile_coretemperature_order, QZSettings::default_tile_coretemperature_order).toInt() == i) {
                coreTemperature->setGridId(i);
                dataList.append(coreTemperature);
            }

            if (settings.value(QZSettings::tile_negative_inclination_enabled, QZSettings::default_tile_negative_inclination_enabled).toBool() &&
                settings.value(QZSettings::tile_negative_inclination_order, QZSettings::default_tile_negative_inclination_order).toInt() == i) {
                negative_inclination->setGridId(i);
                dataList.append(negative_inclination);
            }
        }
    } else     if (bluetoothManager->device()->deviceType() == STAIRCLIMBER) {
        for (int i = 0; i < 100; i++) {
            if (settings.value(QZSettings::tile_speed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_speed_order, 0).toInt() == i) {

                speed->setGridId(i);
                dataList.append(speed);
            }

            if (settings.value(QZSettings::tile_inclination_enabled, true).toBool() &&
                settings.value(QZSettings::tile_inclination_order, 0).toInt() == i) {
                inclination->setGridId(i);
                dataList.append(inclination);
            }

            if (settings.value(QZSettings::tile_elevation_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elevation_order, 0).toInt() == i) {
                elevation->setGridId(i);
                dataList.append(elevation);
            }

            if (settings.value(QZSettings::tile_elapsed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elapsed_order, 0).toInt() == i) {
                elapsed->setGridId(i);
                dataList.append(elapsed);
            }

            if (settings.value(QZSettings::tile_moving_time_enabled, false).toBool() &&
                settings.value(QZSettings::tile_moving_time_order, 19).toInt() == i) {
                moving_time->setGridId(i);
                dataList.append(moving_time);
            }

            if (settings.value(QZSettings::tile_peloton_offset_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_offset_order, 20).toInt() == i) {
                peloton_offset->setGridId(i);
                dataList.append(peloton_offset);
            }

            if (settings.value(QZSettings::tile_peloton_remaining_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_remaining_order, 20).toInt() == i) {
                peloton_remaining->setGridId(i);
                dataList.append(peloton_remaining);
            }

            if (settings.value(QZSettings::tile_calories_enabled, true).toBool() &&
                settings.value(QZSettings::tile_calories_order, 0).toInt() == i) {
                calories->setGridId(i);
                dataList.append(calories);
            }

            if (settings.value(QZSettings::tile_odometer_enabled, true).toBool() &&
                settings.value(QZSettings::tile_odometer_order, 0).toInt() == i) {
                odometer->setGridId(i);
                dataList.append(odometer);
            }

            if (settings.value(QZSettings::tile_pace_enabled, true).toBool() &&
                settings.value(QZSettings::tile_pace_order, 0).toInt() == i) {
                pace->setGridId(i);
                dataList.append(pace);
            }

            if (settings.value(QZSettings::tile_avg_pace_enabled, QZSettings::default_tile_avg_pace_enabled).toBool() &&
                settings.value(QZSettings::tile_avg_pace_order, QZSettings::default_tile_avg_pace_order).toInt() == i) {
                avg_pace->setGridId(i);
                dataList.append(avg_pace);
            }

            if (settings.value(QZSettings::tile_watt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_watt_order, 0).toInt() == i) {
                watt->setGridId(i);
                dataList.append(watt);
            }

            if (settings.value(QZSettings::tile_weight_loss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_weight_loss_order, 24).toInt() == i) {
                weightLoss->setGridId(i);
                dataList.append(weightLoss);
            }

            if (settings.value(QZSettings::tile_avgwatt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avgwatt_order, 0).toInt() == i) {
                avgWatt->setGridId(i);
                dataList.append(avgWatt);
            }

            if (settings.value(QZSettings::tile_avg_watt_lap_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avg_watt_lap_order, 0).toInt() == i) {
                avgWattLap->setGridId(i);
                dataList.append(avgWattLap);
            }

            if (settings.value(QZSettings::tile_ftp_enabled, true).toBool() &&
                settings.value(QZSettings::tile_ftp_order, 0).toInt() == i) {
                ftp->setGridId(i);
                dataList.append(ftp);
            }

            if (settings.value(QZSettings::tile_jouls_enabled, true).toBool() &&
                settings.value(QZSettings::tile_jouls_order, 0).toInt() == i) {
                jouls->setGridId(i);
                dataList.append(jouls);
            }

            if (settings.value(QZSettings::tile_heart_enabled, true).toBool() &&
                settings.value(QZSettings::tile_heart_order, 0).toInt() == i) {
                heart->setGridId(i);
                dataList.append(heart);
            }

            if (settings.value(QZSettings::tile_hrv_enabled, QZSettings::default_tile_hrv_enabled).toBool() &&
                settings.value(QZSettings::tile_hrv_order, QZSettings::default_tile_hrv_order).toInt() == i) {
                hrv->setGridId(i);
                dataList.append(hrv);
            }

            if (settings.value(QZSettings::tile_fan_enabled, true).toBool() &&
                settings.value(QZSettings::tile_fan_order, 0).toInt() == i) {
                fan->setGridId(i);
                dataList.append(fan);
            }

            if (settings.value(QZSettings::tile_datetime_enabled, true).toBool() &&
                settings.value(QZSettings::tile_datetime_order, 0).toInt() == i) {
                datetime->setGridId(i);
                dataList.append(datetime);
            }

            if (settings.value(QZSettings::tile_lapelapsed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_lapelapsed_order, 18).toInt() == i) {
                lapElapsed->setGridId(i);
                dataList.append(lapElapsed);
            }

            if (settings.value(QZSettings::tile_watt_kg_enabled, false).toBool() &&
                settings.value(QZSettings::tile_watt_kg_order, 24).toInt() == i) {
                wattKg->setGridId(i);
                dataList.append(wattKg);
            }

            if (settings.value(QZSettings::tile_remainingtimetrainprogramrow_enabled, false).toBool() &&
                settings.value(QZSettings::tile_remainingtimetrainprogramrow_order, 27).toInt() == i) {

                remaningTimeTrainingProgramCurrentRow->setGridId(i);
                dataList.append(remaningTimeTrainingProgramCurrentRow);
            }

            if (settings.value(QZSettings::tile_nextrowstrainprogram_enabled, false).toBool() &&
                settings.value(QZSettings::tile_nextrowstrainprogram_order, 31).toInt() == i) {

                nextRows->setGridId(i);
                dataList.append(nextRows);
            }

            if (settings.value(QZSettings::tile_mets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_mets_order, 28).toInt() == i) {

                mets->setGridId(i);
                dataList.append(mets);
            }
            if (settings.value(QZSettings::tile_targetmets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_targetmets_order, 29).toInt() == i) {

                targetMets->setGridId(i);
                dataList.append(targetMets);
            }

            if (settings.value(QZSettings::tile_target_speed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_speed_order, 28).toInt() == i) {
                target_speed->setGridId(i);
                dataList.append(target_speed);
            }

            if (settings.value(QZSettings::tile_target_incline_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_incline_order, 29).toInt() == i) {
                target_incline->setGridId(i);
                dataList.append(target_incline);
            }

            if (settings.value(QZSettings::tile_cadence_enabled, false).toBool() &&
                settings.value(QZSettings::tile_cadence_order, 30).toInt() == i) {
                cadence->setGridId(i);
                dataList.append(cadence);
            }

            if (settings.value(QZSettings::tile_pid_hr_enabled, false).toBool() &&
                settings.value(QZSettings::tile_pid_hr_order, 31).toInt() == i) {
                pidHR->setGridId(i);
                dataList.append(pidHR);
            }

            if (settings.value(QZSettings::tile_instantaneous_stride_length_enabled, false).toBool() &&
                settings.value(QZSettings::tile_instantaneous_stride_length_order, 32).toInt() == i) {
                instantaneousStrideLengthCM->setGridId(i);
                dataList.append(instantaneousStrideLengthCM);
            }

            if (settings.value(QZSettings::tile_ground_contact_enabled, false).toBool() &&
                settings.value(QZSettings::tile_ground_contact_order, 33).toInt() == i) {
                groundContactMS->setGridId(i);
                dataList.append(groundContactMS);
            }

            if (settings.value(QZSettings::tile_vertical_oscillation_enabled, false).toBool() &&
                settings.value(QZSettings::tile_vertical_oscillation_order, 34).toInt() == i) {
                verticalOscillationMM->setGridId(i);
                dataList.append(verticalOscillationMM);
            }

            if (settings.value(QZSettings::tile_preset_speed_1_enabled, QZSettings::default_tile_preset_speed_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_1_order, QZSettings::default_tile_preset_speed_1_order)
                        .toInt() == i) {
                preset_speed_1->setGridId(i);
                dataList.append(preset_speed_1);
            }
            if (settings.value(QZSettings::tile_preset_speed_2_enabled, QZSettings::default_tile_preset_speed_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_2_order, QZSettings::default_tile_preset_speed_2_order)
                        .toInt() == i) {
                preset_speed_2->setGridId(i);
                dataList.append(preset_speed_2);
            }
            if (settings.value(QZSettings::tile_preset_speed_3_enabled, QZSettings::default_tile_preset_speed_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_3_order, QZSettings::default_tile_preset_speed_3_order)
                        .toInt() == i) {
                preset_speed_3->setGridId(i);
                dataList.append(preset_speed_3);
            }
            if (settings.value(QZSettings::tile_preset_speed_4_enabled, QZSettings::default_tile_preset_speed_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_4_order, QZSettings::default_tile_preset_speed_4_order)
                        .toInt() == i) {
                preset_speed_4->setGridId(i);
                dataList.append(preset_speed_4);
            }
            if (settings.value(QZSettings::tile_preset_speed_5_enabled, QZSettings::default_tile_preset_speed_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_speed_5_order, QZSettings::default_tile_preset_speed_5_order)
                        .toInt() == i) {
                preset_speed_5->setGridId(i);
                dataList.append(preset_speed_5);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_1_enabled,
                           QZSettings::default_tile_preset_inclination_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_1_order,
                               QZSettings::default_tile_preset_inclination_1_order)
                        .toInt() == i) {
                preset_inclination_1->setGridId(i);
                dataList.append(preset_inclination_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_2_enabled,
                           QZSettings::default_tile_preset_inclination_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_2_order,
                               QZSettings::default_tile_preset_inclination_2_order)
                        .toInt() == i) {
                preset_inclination_2->setGridId(i);
                dataList.append(preset_inclination_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_3_enabled,
                           QZSettings::default_tile_preset_inclination_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_3_order,
                               QZSettings::default_tile_preset_inclination_3_order)
                        .toInt() == i) {
                preset_inclination_3->setGridId(i);
                dataList.append(preset_inclination_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_4_enabled,
                           QZSettings::default_tile_preset_inclination_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_4_order,
                               QZSettings::default_tile_preset_inclination_4_order)
                        .toInt() == i) {
                preset_inclination_4->setGridId(i);
                dataList.append(preset_inclination_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_5_enabled,
                           QZSettings::default_tile_preset_inclination_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_5_order,
                               QZSettings::default_tile_preset_inclination_5_order)
                        .toInt() == i) {
                preset_inclination_5->setGridId(i);
                dataList.append(preset_inclination_5);
            }

            if (settings.value(QZSettings::tile_target_pace_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_pace_order, 50).toInt() == i) {
                target_pace->setGridId(i);
                dataList.append(target_pace);
            }

            if (settings.value(QZSettings::tile_step_count_enabled, QZSettings::default_tile_step_count_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_step_count_order, QZSettings::default_tile_step_count_order)
                        .toInt() == i) {

                stepCount->setGridId(i);
                dataList.append(stepCount);
            }

            if (settings.value(QZSettings::tile_rss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_rss_order, 53).toInt() == i) {
                rss->setGridId(i);
                dataList.append(rss);
            }

            if (settings.value(QZSettings::tile_target_power_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_power_order, 20).toInt() == i) {
                target_power->setGridId(i);
                dataList.append(target_power);
            }

            if (settings.value(QZSettings::tile_target_zone_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_zone_order, 24).toInt() == i) {
                target_zone->setGridId(i);
                dataList.append(target_zone);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_1_order, 0).toInt() == i) {
                tile_hr_time_in_zone_1->setGridId(i);
                dataList.append(tile_hr_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_2_order, 0).toInt() == i) {
                tile_hr_time_in_zone_2->setGridId(i);
                dataList.append(tile_hr_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_3_order, 0).toInt() == i) {
                tile_hr_time_in_zone_3->setGridId(i);
                dataList.append(tile_hr_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_4_order, 0).toInt() == i) {
                tile_hr_time_in_zone_4->setGridId(i);
                dataList.append(tile_hr_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_5_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_5_order, 0).toInt() == i) {
                tile_hr_time_in_zone_5->setGridId(i);
                dataList.append(tile_hr_time_in_zone_5);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_1_order, 0).toInt() == i) {
                tile_heat_time_in_zone_1->setGridId(i);
                dataList.append(tile_heat_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_2_order, 0).toInt() == i) {
                tile_heat_time_in_zone_2->setGridId(i);
                dataList.append(tile_heat_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_3_order, 0).toInt() == i) {
                tile_heat_time_in_zone_3->setGridId(i);
                dataList.append(tile_heat_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_4_order, 0).toInt() == i) {
                tile_heat_time_in_zone_4->setGridId(i);
                dataList.append(tile_heat_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_coretemperature_enabled, QZSettings::default_tile_coretemperature_enabled).toBool() &&
                settings.value(QZSettings::tile_coretemperature_order, QZSettings::default_tile_coretemperature_order).toInt() == i) {
                coreTemperature->setGridId(i);
                dataList.append(coreTemperature);
            }

            if (settings.value(QZSettings::tile_negative_inclination_enabled, QZSettings::default_tile_negative_inclination_enabled).toBool() &&
                settings.value(QZSettings::tile_negative_inclination_order, QZSettings::default_tile_negative_inclination_order).toInt() == i) {
                negative_inclination->setGridId(i);
                dataList.append(negative_inclination);
            }
        }
    } else if (bluetoothManager->device()->deviceType() == BIKE) {
        for (int i = 0; i < 100; i++) {
            if (settings.value(QZSettings::tile_speed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_speed_order, 0).toInt() == i) {

                speed->setGridId(i);
                dataList.append(speed);
            }

            if (settings.value(QZSettings::tile_cadence_enabled, true).toBool() &&
                settings.value(QZSettings::tile_cadence_order, 0).toInt() == i) {
                cadence->setGridId(i);
                dataList.append(cadence);
            }

            if (settings.value(QZSettings::tile_elevation_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elevation_order, 0).toInt() == i) {
                elevation->setGridId(i);
                dataList.append(elevation);
            }

            if (settings.value(QZSettings::tile_elapsed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elapsed_order, 0).toInt() == i) {
                elapsed->setGridId(i);
                dataList.append(elapsed);
            }

            if (settings.value(QZSettings::tile_moving_time_enabled, false).toBool() &&
                settings.value(QZSettings::tile_moving_time_order, 19).toInt() == i) {
                moving_time->setGridId(i);
                dataList.append(moving_time);
            }

            if (settings.value(QZSettings::tile_peloton_offset_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_offset_order, 20).toInt() == i) {
                peloton_offset->setGridId(i);
                dataList.append(peloton_offset);
            }

            if (settings.value(QZSettings::tile_peloton_remaining_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_remaining_order, 20).toInt() == i) {
                peloton_remaining->setGridId(i);
                dataList.append(peloton_remaining);
            }

            if (settings.value(QZSettings::tile_calories_enabled, true).toBool() &&
                settings.value(QZSettings::tile_calories_order, 0).toInt() == i) {
                calories->setGridId(i);
                dataList.append(calories);
            }

            if (settings.value(QZSettings::tile_odometer_enabled, true).toBool() &&
                settings.value(QZSettings::tile_odometer_order, 0).toInt() == i) {
                odometer->setGridId(i);
                dataList.append(odometer);
            }

            if (settings.value(QZSettings::tile_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_resistance_order, 0).toInt() == i) {
                resistance->setGridId(i);
                dataList.append(resistance);
            }

            if (settings.value(QZSettings::tile_peloton_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_peloton_resistance_order, 0).toInt() == i) {
                peloton_resistance->setGridId(i);
                dataList.append(peloton_resistance);
            }

            if (settings.value(QZSettings::tile_watt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_watt_order, 0).toInt() == i) {
                watt->setGridId(i);
                dataList.append(watt);
            }

            if (settings.value(QZSettings::tile_weight_loss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_weight_loss_order, 24).toInt() == i) {
                weightLoss->setGridId(i);
                dataList.append(weightLoss);
            }

            if (settings.value(QZSettings::tile_avgwatt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avgwatt_order, 0).toInt() == i) {
                avgWatt->setGridId(i);
                dataList.append(avgWatt);
            }

            if (settings.value(QZSettings::tile_avg_watt_lap_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avg_watt_lap_order, 0).toInt() == i) {
                avgWattLap->setGridId(i);
                dataList.append(avgWattLap);
            }

            if (settings.value(QZSettings::tile_ftp_enabled, true).toBool() &&
                settings.value(QZSettings::tile_ftp_order, 0).toInt() == i) {
                ftp->setGridId(i);
                dataList.append(ftp);
            }

            if (settings.value(QZSettings::tile_jouls_enabled, true).toBool() &&
                settings.value(QZSettings::tile_jouls_order, 0).toInt() == i) {
                jouls->setGridId(i);
                dataList.append(jouls);
            }

            if (settings.value(QZSettings::tile_heart_enabled, true).toBool() &&
                settings.value(QZSettings::tile_heart_order, 0).toInt() == i) {
                heart->setGridId(i);
                dataList.append(heart);
            }

            if (settings.value(QZSettings::tile_hrv_enabled, QZSettings::default_tile_hrv_enabled).toBool() &&
                settings.value(QZSettings::tile_hrv_order, QZSettings::default_tile_hrv_order).toInt() == i) {
                hrv->setGridId(i);
                dataList.append(hrv);
            }

            if (settings.value(QZSettings::tile_fan_enabled, true).toBool() &&
                settings.value(QZSettings::tile_fan_order, 0).toInt() == i) {
                fan->setGridId(i);
                dataList.append(fan);
            }

            if (settings.value(QZSettings::tile_datetime_enabled, true).toBool() &&
                settings.value(QZSettings::tile_datetime_order, 0).toInt() == i) {
                datetime->setGridId(i);
                dataList.append(datetime);
            }

            if (settings.value(QZSettings::tile_target_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_target_resistance_order, 0).toInt() == i) {
                target_resistance->setGridId(i);
                dataList.append(target_resistance);
            }

            if (settings.value(QZSettings::tile_target_peloton_resistance_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_peloton_resistance_order, 21).toInt() == i) {
                target_peloton_resistance->setGridId(i);
                dataList.append(target_peloton_resistance);
            }

            if (settings.value(QZSettings::tile_target_cadence_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_cadence_order, 19).toInt() == i) {
                target_cadence->setGridId(i);
                dataList.append(target_cadence);
            }

            if (settings.value(QZSettings::tile_target_power_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_power_order, 20).toInt() == i) {
                target_power->setGridId(i);
                dataList.append(target_power);
            }

            if (settings.value(QZSettings::tile_target_zone_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_zone_order, 24).toInt() == i) {
                target_zone->setGridId(i);
                dataList.append(target_zone);
            }

            if (settings.value(QZSettings::tile_lapelapsed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_lapelapsed_order, 18).toInt() == i) {
                lapElapsed->setGridId(i);
                dataList.append(lapElapsed);
            }

            if (settings.value(QZSettings::tile_watt_kg_enabled, false).toBool() &&
                settings.value(QZSettings::tile_watt_kg_order, 24).toInt() == i) {
                wattKg->setGridId(i);
                dataList.append(wattKg);
            }
            if (settings.value(QZSettings::tile_gears_enabled, false).toBool() &&
                settings.value(QZSettings::tile_gears_order, 25).toInt() == i) {
                gears->setGridId(i);
                dataList.append(gears);
            }

            if (settings.value(QZSettings::tile_remainingtimetrainprogramrow_enabled, false).toBool() &&
                settings.value(QZSettings::tile_remainingtimetrainprogramrow_order, 27).toInt() == i) {

                remaningTimeTrainingProgramCurrentRow->setGridId(i);
                dataList.append(remaningTimeTrainingProgramCurrentRow);
            }

            if (settings.value(QZSettings::tile_nextrowstrainprogram_enabled, false).toBool() &&
                settings.value(QZSettings::tile_nextrowstrainprogram_order, 31).toInt() == i) {

                nextRows->setGridId(i);
                dataList.append(nextRows);
            }

            if (settings.value(QZSettings::tile_mets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_mets_order, 28).toInt() == i) {
                mets->setGridId(i);
                dataList.append(mets);
            }
            if (settings.value(QZSettings::tile_targetmets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_targetmets_order, 29).toInt() == i) {
                targetMets->setGridId(i);
                dataList.append(targetMets);
            }
            // the proform studio is the only bike managed with an inclination properties.
            // In order to don't break the tiles layout to all the bikes users, i enable this
            // only if this bike is selected
            // since i'm adding the inclination from zwift in this tile, in order to preserve the
            // layour for legacy users, i'm not showing this one if the peloton cadence sensor setting
            // is enabled (assuming that if someone has it, he doesn't want an inclination tile)
            if (!pelotoncadence) {
                if (settings.value(QZSettings::tile_inclination_enabled, true).toBool() &&
                    settings.value(QZSettings::tile_inclination_order, 29).toInt() == i) {
                    inclination->setGridId(i);
                    dataList.append(inclination);
                }
            }
            if (settings.value(QZSettings::tile_steering_angle_enabled, false).toBool() &&
                settings.value(QZSettings::tile_steering_angle_order, 30).toInt() == i) {

                steeringAngle->setGridId(i);
                dataList.append(steeringAngle);
            }

            if (settings.value(QZSettings::tile_pid_hr_enabled, false).toBool() &&
                settings.value(QZSettings::tile_pid_hr_order, 31).toInt() == i) {
                pidHR->setGridId(i);
                dataList.append(pidHR);
            }

            if (settings.value(QZSettings::tile_ext_incline_enabled, false).toBool() &&
                settings.value(QZSettings::tile_ext_incline_order, 32).toInt() == i) {
                extIncline->setGridId(i);
                dataList.append(extIncline);
            }

            if (settings
                    .value(QZSettings::tile_preset_inclination_1_enabled,
                           QZSettings::default_tile_preset_inclination_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_1_order,
                               QZSettings::default_tile_preset_inclination_1_order)
                        .toInt() == i) {
                preset_inclination_1->setGridId(i);
                dataList.append(preset_inclination_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_2_enabled,
                           QZSettings::default_tile_preset_inclination_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_2_order,
                               QZSettings::default_tile_preset_inclination_2_order)
                        .toInt() == i) {
                preset_inclination_2->setGridId(i);
                dataList.append(preset_inclination_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_3_enabled,
                           QZSettings::default_tile_preset_inclination_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_3_order,
                               QZSettings::default_tile_preset_inclination_3_order)
                        .toInt() == i) {
                preset_inclination_3->setGridId(i);
                dataList.append(preset_inclination_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_4_enabled,
                           QZSettings::default_tile_preset_inclination_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_4_order,
                               QZSettings::default_tile_preset_inclination_4_order)
                        .toInt() == i) {
                preset_inclination_4->setGridId(i);
                dataList.append(preset_inclination_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_5_enabled,
                           QZSettings::default_tile_preset_inclination_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_5_order,
                               QZSettings::default_tile_preset_inclination_5_order)
                        .toInt() == i) {
                preset_inclination_5->setGridId(i);
                dataList.append(preset_inclination_5);
            }

            if (settings
                    .value(QZSettings::tile_preset_resistance_1_enabled,
                           QZSettings::default_tile_preset_resistance_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_1_order,
                               QZSettings::default_tile_preset_resistance_1_order)
                        .toInt() == i) {
                preset_resistance_1->setGridId(i);
                dataList.append(preset_resistance_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_2_enabled,
                           QZSettings::default_tile_preset_resistance_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_2_order,
                               QZSettings::default_tile_preset_resistance_2_order)
                        .toInt() == i) {
                preset_resistance_2->setGridId(i);
                dataList.append(preset_resistance_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_3_enabled,
                           QZSettings::default_tile_preset_resistance_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_3_order,
                               QZSettings::default_tile_preset_resistance_3_order)
                        .toInt() == i) {
                preset_resistance_3->setGridId(i);
                dataList.append(preset_resistance_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_4_enabled,
                           QZSettings::default_tile_preset_resistance_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_4_order,
                               QZSettings::default_tile_preset_resistance_4_order)
                        .toInt() == i) {
                preset_resistance_4->setGridId(i);
                dataList.append(preset_resistance_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_5_enabled,
                           QZSettings::default_tile_preset_resistance_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_5_order,
                               QZSettings::default_tile_preset_resistance_5_order)
                        .toInt() == i) {
                preset_resistance_5->setGridId(i);
                dataList.append(preset_resistance_5);
            }
            if (settings.value(QZSettings::tile_erg_mode_enabled, QZSettings::default_tile_erg_mode_enabled).toBool() &&
                settings.value(QZSettings::tile_erg_mode_order, QZSettings::default_tile_erg_mode_order).toInt() == i) {
                ergMode->setGridId(i);
                dataList.append(ergMode);
            }

            if (settings.value(QZSettings::tile_biggears_enabled, false).toBool() &&
                settings.value(QZSettings::tile_biggears_order, 54).toInt() == i + (settings.value(QZSettings::tile_biggears_swap, QZSettings::default_tile_biggears_swap).toBool() ? 1 : 0)) {
                biggearsPlus->setGridId(i);
                dataList.append(biggearsPlus);
            }

            if (settings.value(QZSettings::tile_biggears_enabled, false).toBool() &&
                settings.value(QZSettings::tile_biggears_order, 54).toInt() == i + (settings.value(QZSettings::tile_biggears_swap, QZSettings::default_tile_biggears_swap).toBool() ? 0 : 1)) {
                biggearsMinus->setGridId(i);
                dataList.append(biggearsMinus);
            }

            // Automatic Virtual Shifting tiles
            if (settings.value(QZSettings::tile_auto_virtual_shifting_cruise_enabled, QZSettings::default_tile_auto_virtual_shifting_cruise_enabled).toBool() &&
                settings.value(QZSettings::tile_auto_virtual_shifting_cruise_order, QZSettings::default_tile_auto_virtual_shifting_cruise_order).toInt() == i) {
                autoVirtualShiftingCruise->setGridId(i);
                dataList.append(autoVirtualShiftingCruise);
            }

            if (settings.value(QZSettings::tile_auto_virtual_shifting_climb_enabled, QZSettings::default_tile_auto_virtual_shifting_climb_enabled).toBool() &&
                settings.value(QZSettings::tile_auto_virtual_shifting_climb_order, QZSettings::default_tile_auto_virtual_shifting_climb_order).toInt() == i) {
                autoVirtualShiftingClimb->setGridId(i);
                dataList.append(autoVirtualShiftingClimb);
            }

            if (settings.value(QZSettings::tile_auto_virtual_shifting_sprint_enabled, QZSettings::default_tile_auto_virtual_shifting_sprint_enabled).toBool() &&
                settings.value(QZSettings::tile_auto_virtual_shifting_sprint_order, QZSettings::default_tile_auto_virtual_shifting_sprint_order).toInt() == i) {
                autoVirtualShiftingSprint->setGridId(i);
                dataList.append(autoVirtualShiftingSprint);
            }

            if (settings.value(QZSettings::tile_power_avg_enabled, QZSettings::default_tile_power_avg_enabled).toBool() &&
                settings.value(QZSettings::tile_power_avg_order, QZSettings::default_tile_power_avg_order).toInt() == i) {
                powerAvg->setGridId(i);
                dataList.append(powerAvg);
            }

            if (settings.value(QZSettings::tile_preset_powerzone_1_enabled, QZSettings::default_tile_preset_powerzone_1_enabled).toBool() &&
                settings.value(QZSettings::tile_preset_powerzone_1_order, QZSettings::default_tile_preset_powerzone_1_order).toInt() == i) {
                preset_powerzone_1->setGridId(i);
                dataList.append(preset_powerzone_1);
            }

            if (settings.value(QZSettings::tile_preset_powerzone_2_enabled, QZSettings::default_tile_preset_powerzone_2_enabled).toBool() &&
                settings.value(QZSettings::tile_preset_powerzone_2_order, QZSettings::default_tile_preset_powerzone_2_order).toInt() == i) {
                preset_powerzone_2->setGridId(i);
                dataList.append(preset_powerzone_2);
            }

            if (settings.value(QZSettings::tile_preset_powerzone_3_enabled, QZSettings::default_tile_preset_powerzone_3_enabled).toBool() &&
                settings.value(QZSettings::tile_preset_powerzone_3_order, QZSettings::default_tile_preset_powerzone_3_order).toInt() == i) {
                preset_powerzone_3->setGridId(i);
                dataList.append(preset_powerzone_3);
            }

            if (settings.value(QZSettings::tile_preset_powerzone_4_enabled, QZSettings::default_tile_preset_powerzone_4_enabled).toBool() &&
                settings.value(QZSettings::tile_preset_powerzone_4_order, QZSettings::default_tile_preset_powerzone_4_order).toInt() == i) {
                preset_powerzone_4->setGridId(i);
                dataList.append(preset_powerzone_4);
            }

            if (settings.value(QZSettings::tile_preset_powerzone_5_enabled, QZSettings::default_tile_preset_powerzone_5_enabled).toBool() &&
                settings.value(QZSettings::tile_preset_powerzone_5_order, QZSettings::default_tile_preset_powerzone_5_order).toInt() == i) {
                preset_powerzone_5->setGridId(i);
                dataList.append(preset_powerzone_5);
            }

            if (settings.value(QZSettings::tile_preset_powerzone_6_enabled, QZSettings::default_tile_preset_powerzone_6_enabled).toBool() &&
                settings.value(QZSettings::tile_preset_powerzone_6_order, QZSettings::default_tile_preset_powerzone_6_order).toInt() == i) {
                preset_powerzone_6->setGridId(i);
                dataList.append(preset_powerzone_6);
            }

            if (settings.value(QZSettings::tile_preset_powerzone_7_enabled, QZSettings::default_tile_preset_powerzone_7_enabled).toBool() &&
                settings.value(QZSettings::tile_preset_powerzone_7_order, QZSettings::default_tile_preset_powerzone_7_order).toInt() == i) {
                preset_powerzone_7->setGridId(i);
                dataList.append(preset_powerzone_7);
            }


            if (settings.value(QZSettings::tile_hr_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_1_order, 0).toInt() == i) {
                tile_hr_time_in_zone_1->setGridId(i);
                dataList.append(tile_hr_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_2_order, 0).toInt() == i) {
                tile_hr_time_in_zone_2->setGridId(i);
                dataList.append(tile_hr_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_3_order, 0).toInt() == i) {
                tile_hr_time_in_zone_3->setGridId(i);
                dataList.append(tile_hr_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_4_order, 0).toInt() == i) {
                tile_hr_time_in_zone_4->setGridId(i);
                dataList.append(tile_hr_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_5_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_5_order, 0).toInt() == i) {
                tile_hr_time_in_zone_5->setGridId(i);
                dataList.append(tile_hr_time_in_zone_5);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_1_order, 0).toInt() == i) {
                tile_heat_time_in_zone_1->setGridId(i);
                dataList.append(tile_heat_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_2_order, 0).toInt() == i) {
                tile_heat_time_in_zone_2->setGridId(i);
                dataList.append(tile_heat_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_3_order, 0).toInt() == i) {
                tile_heat_time_in_zone_3->setGridId(i);
                dataList.append(tile_heat_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_4_order, 0).toInt() == i) {
                tile_heat_time_in_zone_4->setGridId(i);
                dataList.append(tile_heat_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_coretemperature_enabled, QZSettings::default_tile_coretemperature_enabled).toBool() &&
                settings.value(QZSettings::tile_coretemperature_order, QZSettings::default_tile_coretemperature_order).toInt() == i) {
                coreTemperature->setGridId(i);
                dataList.append(coreTemperature);
            }
        }
    } else if (bluetoothManager->device()->deviceType() == ROWING) {
        for (int i = 0; i < 100; i++) {
            if (settings.value(QZSettings::tile_speed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_speed_order, 0).toInt() == i) {
                speed->setGridId(i);
                dataList.append(speed);
            }

            if (settings.value(QZSettings::tile_cadence_enabled, true).toBool() &&
                settings.value(QZSettings::tile_cadence_order, 0).toInt() == i) {
                cadence->setGridId(i);
                cadence->setName("Stroke Rate");
                dataList.append(cadence);
            }

            if (settings.value(QZSettings::tile_elevation_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elevation_order, 0).toInt() == i) {
                elevation->setGridId(i);
                dataList.append(elevation);
            }

            if (settings.value(QZSettings::tile_elapsed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elapsed_order, 0).toInt() == i) {
                elapsed->setGridId(i);
                dataList.append(elapsed);
            }

            if (settings.value(QZSettings::tile_moving_time_enabled, false).toBool() &&
                settings.value(QZSettings::tile_moving_time_order, 19).toInt() == i) {
                moving_time->setGridId(i);
                dataList.append(moving_time);
            }

            if (settings.value(QZSettings::tile_peloton_offset_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_offset_order, 20).toInt() == i) {
                peloton_offset->setGridId(i);
                dataList.append(peloton_offset);
            }

            if (settings.value(QZSettings::tile_peloton_remaining_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_remaining_order, 20).toInt() == i) {
                peloton_remaining->setGridId(i);
                dataList.append(peloton_remaining);
            }

            if (settings.value(QZSettings::tile_calories_enabled, true).toBool() &&
                settings.value(QZSettings::tile_calories_order, 0).toInt() == i) {
                calories->setGridId(i);
                dataList.append(calories);
            }

            if (settings.value(QZSettings::tile_odometer_enabled, true).toBool() &&
                settings.value(QZSettings::tile_odometer_order, 0).toInt() == i) {
                odometer->setGridId(i);
                odometer->setName("Odometer (m)");
                dataList.append(odometer);
            }

            if (settings.value(QZSettings::tile_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_resistance_order, 0).toInt() == i) {
                resistance->setGridId(i);
                dataList.append(resistance);
            }

            if (settings.value(QZSettings::tile_peloton_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_peloton_resistance_order, 0).toInt() == i) {
                peloton_resistance->setGridId(i);
                dataList.append(peloton_resistance);
            }

            if (settings.value(QZSettings::tile_watt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_watt_order, 0).toInt() == i) {
                watt->setGridId(i);
                dataList.append(watt);
            }

            if (settings.value(QZSettings::tile_weight_loss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_weight_loss_order, 24).toInt() == i) {
                weightLoss->setGridId(i);
                dataList.append(weightLoss);
            }

            if (settings.value(QZSettings::tile_avgwatt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avgwatt_order, 0).toInt() == i) {
                avgWatt->setGridId(i);
                dataList.append(avgWatt);
            }

            if (settings.value(QZSettings::tile_avg_watt_lap_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avg_watt_lap_order, 0).toInt() == i) {
                avgWattLap->setGridId(i);
                dataList.append(avgWattLap);
            }

            if (settings.value(QZSettings::tile_ftp_enabled, true).toBool() &&
                settings.value(QZSettings::tile_ftp_order, 0).toInt() == i) {
                ftp->setGridId(i);
                dataList.append(ftp);
            }

            if (settings.value(QZSettings::tile_jouls_enabled, true).toBool() &&
                settings.value(QZSettings::tile_jouls_order, 0).toInt() == i) {
                jouls->setGridId(i);
                dataList.append(jouls);
            }

            if (settings.value(QZSettings::tile_heart_enabled, true).toBool() &&
                settings.value(QZSettings::tile_heart_order, 0).toInt() == i) {
                heart->setGridId(i);
                dataList.append(heart);
            }

            if (settings.value(QZSettings::tile_hrv_enabled, QZSettings::default_tile_hrv_enabled).toBool() &&
                settings.value(QZSettings::tile_hrv_order, QZSettings::default_tile_hrv_order).toInt() == i) {
                hrv->setGridId(i);
                dataList.append(hrv);
            }

            if (settings.value(QZSettings::tile_fan_enabled, true).toBool() &&
                settings.value(QZSettings::tile_fan_order, 0).toInt() == i) {
                fan->setGridId(i);
                dataList.append(fan);
            }

            if (settings.value(QZSettings::tile_datetime_enabled, true).toBool() &&
                settings.value(QZSettings::tile_datetime_order, 0).toInt() == i) {
                datetime->setGridId(i);
                dataList.append(datetime);
            }

            if (settings.value(QZSettings::tile_target_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_target_resistance_order, 0).toInt() == i) {
                target_resistance->setGridId(i);
                dataList.append(target_resistance);
            }

            if (settings.value(QZSettings::tile_target_peloton_resistance_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_peloton_resistance_order, 21).toInt() == i) {
                target_peloton_resistance->setGridId(i);
                dataList.append(target_peloton_resistance);
            }

            if (settings.value(QZSettings::tile_target_cadence_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_cadence_order, 19).toInt() == i) {
                target_cadence->setGridId(i);
                dataList.append(target_cadence);
            }

            if (settings.value(QZSettings::tile_target_power_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_power_order, 20).toInt() == i) {
                target_power->setGridId(i);
                dataList.append(target_power);
            }

            if (settings.value(QZSettings::tile_lapelapsed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_lapelapsed_order, 18).toInt() == i) {
                lapElapsed->setGridId(i);
                dataList.append(lapElapsed);
            }

            if (settings.value(QZSettings::tile_strokes_length_enabled, false).toBool() &&
                settings.value(QZSettings::tile_strokes_length_order, 21).toInt() == i) {
                strokesLength->setGridId(i);
                dataList.append(strokesLength);
            }

            if (settings.value(QZSettings::tile_strokes_count_enabled, false).toBool() &&
                settings.value(QZSettings::tile_strokes_count_order, 22).toInt() == i) {
                strokesCount->setGridId(i);
                dataList.append(strokesCount);
            }

            if (settings.value(QZSettings::tile_pace_enabled, true).toBool() &&
                settings.value(QZSettings::tile_pace_order, 0).toInt() == i) {
                pace->setGridId(i);
                pace->setName("Pace (m/500m)");
                dataList.append(pace);
            }

            if (settings.value(QZSettings::tile_avg_pace_enabled, QZSettings::default_tile_avg_pace_enabled).toBool() &&
                settings.value(QZSettings::tile_avg_pace_order, QZSettings::default_tile_avg_pace_order).toInt() == i) {
                avg_pace->setGridId(i);
                avg_pace->setName("Avg Pace (m/500m)");
                dataList.append(avg_pace);
            }

            if (settings.value(QZSettings::tile_watt_kg_enabled, false).toBool() &&
                settings.value(QZSettings::tile_watt_kg_order, 24).toInt() == i) {
                wattKg->setGridId(i);
                dataList.append(wattKg);
            }

            if (settings.value(QZSettings::tile_remainingtimetrainprogramrow_enabled, false).toBool() &&
                settings.value(QZSettings::tile_remainingtimetrainprogramrow_order, 27).toInt() == i) {
                remaningTimeTrainingProgramCurrentRow->setGridId(i);
                dataList.append(remaningTimeTrainingProgramCurrentRow);
            }

            if (settings.value(QZSettings::tile_nextrowstrainprogram_enabled, false).toBool() &&
                settings.value(QZSettings::tile_nextrowstrainprogram_order, 31).toInt() == i) {

                nextRows->setGridId(i);
                dataList.append(nextRows);
            }

            if (settings.value(QZSettings::tile_mets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_mets_order, 28).toInt() == i) {
                mets->setGridId(i);
                dataList.append(mets);
            }
            if (settings.value(QZSettings::tile_targetmets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_targetmets_order, 29).toInt() == i) {
                targetMets->setGridId(i);
                dataList.append(targetMets);
            }

            if (settings.value(QZSettings::tile_pid_hr_enabled, false).toBool() &&
                settings.value(QZSettings::tile_pid_hr_order, 31).toInt() == i) {
                pidHR->setGridId(i);
                dataList.append(pidHR);
            }

            if (settings.value(QZSettings::tile_target_zone_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_zone_order, 24).toInt() == i) {
                target_zone->setGridId(i);
                dataList.append(target_zone);
            }

            if (settings.value(QZSettings::tile_pace_last500m_enabled, QZSettings::default_tile_pace_last500m_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_pace_last500m_order, QZSettings::default_tile_pace_last500m_order)
                        .toInt() == i) {

                pace_last500m->setGridId(i);
                dataList.append(pace_last500m);
            }

            if (settings.value(QZSettings::tile_target_speed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_speed_order, 28).toInt() == i) {
                target_speed->setGridId(i);
                dataList.append(target_speed);
            }

            if (settings.value(QZSettings::tile_target_pace_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_pace_order, 50).toInt() == i) {
                target_pace->setGridId(i);
                target_pace->setName("T.Pace(m/500m)");
                dataList.append(target_pace);
            }

            if (settings
                    .value(QZSettings::tile_preset_resistance_1_enabled,
                           QZSettings::default_tile_preset_resistance_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_1_order,
                               QZSettings::default_tile_preset_resistance_1_order)
                        .toInt() == i) {
                preset_resistance_1->setGridId(i);
                dataList.append(preset_resistance_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_2_enabled,
                           QZSettings::default_tile_preset_resistance_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_2_order,
                               QZSettings::default_tile_preset_resistance_2_order)
                        .toInt() == i) {
                preset_resistance_2->setGridId(i);
                dataList.append(preset_resistance_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_3_enabled,
                           QZSettings::default_tile_preset_resistance_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_3_order,
                               QZSettings::default_tile_preset_resistance_3_order)
                        .toInt() == i) {
                preset_resistance_3->setGridId(i);
                dataList.append(preset_resistance_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_4_enabled,
                           QZSettings::default_tile_preset_resistance_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_4_order,
                               QZSettings::default_tile_preset_resistance_4_order)
                        .toInt() == i) {
                preset_resistance_4->setGridId(i);
                dataList.append(preset_resistance_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_5_enabled,
                           QZSettings::default_tile_preset_resistance_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_5_order,
                               QZSettings::default_tile_preset_resistance_5_order)
                        .toInt() == i) {
                preset_resistance_5->setGridId(i);
                dataList.append(preset_resistance_5);
            }
            if (settings.value(QZSettings::tile_gears_enabled, false).toBool() &&
                settings.value(QZSettings::tile_gears_order, 51).toInt() == i) {
                gears->setGridId(i);
                dataList.append(gears);
            }


            if (settings.value(QZSettings::tile_hr_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_1_order, 0).toInt() == i) {
                tile_hr_time_in_zone_1->setGridId(i);
                dataList.append(tile_hr_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_2_order, 0).toInt() == i) {
                tile_hr_time_in_zone_2->setGridId(i);
                dataList.append(tile_hr_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_3_order, 0).toInt() == i) {
                tile_hr_time_in_zone_3->setGridId(i);
                dataList.append(tile_hr_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_4_order, 0).toInt() == i) {
                tile_hr_time_in_zone_4->setGridId(i);
                dataList.append(tile_hr_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_5_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_5_order, 0).toInt() == i) {
                tile_hr_time_in_zone_5->setGridId(i);
                dataList.append(tile_hr_time_in_zone_5);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_1_order, 0).toInt() == i) {
                tile_heat_time_in_zone_1->setGridId(i);
                dataList.append(tile_heat_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_2_order, 0).toInt() == i) {
                tile_heat_time_in_zone_2->setGridId(i);
                dataList.append(tile_heat_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_3_order, 0).toInt() == i) {
                tile_heat_time_in_zone_3->setGridId(i);
                dataList.append(tile_heat_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_4_order, 0).toInt() == i) {
                tile_heat_time_in_zone_4->setGridId(i);
                dataList.append(tile_heat_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_coretemperature_enabled, QZSettings::default_tile_coretemperature_enabled).toBool() &&
                settings.value(QZSettings::tile_coretemperature_order, QZSettings::default_tile_coretemperature_order).toInt() == i) {
                coreTemperature->setGridId(i);
                dataList.append(coreTemperature);
            }
        }
    } else if (bluetoothManager->device()->deviceType() == JUMPROPE) {
        for (int i = 0; i < 100; i++) {
            if (settings.value(QZSettings::tile_speed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_speed_order, 0).toInt() == i) {
                speed->setGridId(i);
                dataList.append(speed);
            }

            if (settings.value(QZSettings::tile_cadence_enabled, true).toBool() &&
                settings.value(QZSettings::tile_cadence_order, 0).toInt() == i) {
                cadence->setGridId(i);
                dataList.append(cadence);
            }

            if (settings.value(QZSettings::tile_elevation_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elevation_order, 0).toInt() == i) {
                elevation->setGridId(i);
                dataList.append(elevation);
            }

            if (settings.value(QZSettings::tile_elapsed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elapsed_order, 0).toInt() == i) {
                elapsed->setGridId(i);
                dataList.append(elapsed);
            }

            if (settings.value(QZSettings::tile_moving_time_enabled, false).toBool() &&
                settings.value(QZSettings::tile_moving_time_order, 19).toInt() == i) {
                moving_time->setGridId(i);
                dataList.append(moving_time);
            }

            if (settings.value(QZSettings::tile_peloton_offset_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_offset_order, 20).toInt() == i) {
                peloton_offset->setGridId(i);
                dataList.append(peloton_offset);
            }

            if (settings.value(QZSettings::tile_peloton_remaining_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_remaining_order, 20).toInt() == i) {
                peloton_remaining->setGridId(i);
                dataList.append(peloton_remaining);
            }

            if (settings.value(QZSettings::tile_inclination_enabled, true).toBool() &&
                settings.value(QZSettings::tile_inclination_order, 29).toInt() == i) {
                inclination->setGridId(i);
                inclination->setName("Sequence");
                dataList.append(inclination);
            }

            if (settings.value(QZSettings::tile_calories_enabled, true).toBool() &&
                settings.value(QZSettings::tile_calories_order, 0).toInt() == i) {
                calories->setGridId(i);
                dataList.append(calories);
            }

            if (settings.value(QZSettings::tile_odometer_enabled, true).toBool() &&
                settings.value(QZSettings::tile_odometer_order, 0).toInt() == i) {
                odometer->setGridId(i);
                dataList.append(odometer);
            }

            if (settings.value(QZSettings::tile_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_resistance_order, 0).toInt() == i) {
                resistance->setGridId(i);
                dataList.append(resistance);
            }

            if (settings.value(QZSettings::tile_peloton_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_peloton_resistance_order, 0).toInt() == i) {
                peloton_resistance->setGridId(i);
                dataList.append(peloton_resistance);
            }

            if (settings.value(QZSettings::tile_watt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_watt_order, 0).toInt() == i) {
                watt->setGridId(i);
                dataList.append(watt);
            }

            if (settings.value(QZSettings::tile_weight_loss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_weight_loss_order, 24).toInt() == i) {
                weightLoss->setGridId(i);
                dataList.append(weightLoss);
            }

            if (settings.value(QZSettings::tile_avgwatt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avgwatt_order, 0).toInt() == i) {
                avgWatt->setGridId(i);
                dataList.append(avgWatt);
            }

            if (settings.value(QZSettings::tile_avg_watt_lap_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avg_watt_lap_order, 0).toInt() == i) {
                avgWattLap->setGridId(i);
                dataList.append(avgWattLap);
            }

            if (settings.value(QZSettings::tile_ftp_enabled, true).toBool() &&
                settings.value(QZSettings::tile_ftp_order, 0).toInt() == i) {
                ftp->setGridId(i);
                dataList.append(ftp);
            }

            if (settings.value(QZSettings::tile_jouls_enabled, true).toBool() &&
                settings.value(QZSettings::tile_jouls_order, 0).toInt() == i) {
                jouls->setGridId(i);
                dataList.append(jouls);
            }

            if (settings.value(QZSettings::tile_heart_enabled, true).toBool() &&
                settings.value(QZSettings::tile_heart_order, 0).toInt() == i) {
                heart->setGridId(i);
                dataList.append(heart);
            }

            if (settings.value(QZSettings::tile_hrv_enabled, QZSettings::default_tile_hrv_enabled).toBool() &&
                settings.value(QZSettings::tile_hrv_order, QZSettings::default_tile_hrv_order).toInt() == i) {
                hrv->setGridId(i);
                dataList.append(hrv);
            }

            if (settings.value(QZSettings::tile_fan_enabled, true).toBool() &&
                settings.value(QZSettings::tile_fan_order, 0).toInt() == i) {
                fan->setGridId(i);
                dataList.append(fan);
            }

            if (settings.value(QZSettings::tile_datetime_enabled, true).toBool() &&
                settings.value(QZSettings::tile_datetime_order, 0).toInt() == i) {
                datetime->setGridId(i);
                dataList.append(datetime);
            }

            if (settings.value(QZSettings::tile_target_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_target_resistance_order, 0).toInt() == i) {
                target_resistance->setGridId(i);
                dataList.append(target_resistance);
            }

            if (settings.value(QZSettings::tile_target_peloton_resistance_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_peloton_resistance_order, 21).toInt() == i) {
                target_peloton_resistance->setGridId(i);
                dataList.append(target_peloton_resistance);
            }

            if (settings.value(QZSettings::tile_target_cadence_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_cadence_order, 19).toInt() == i) {
                target_cadence->setGridId(i);
                dataList.append(target_cadence);
            }

            if (settings.value(QZSettings::tile_target_power_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_power_order, 20).toInt() == i) {
                target_power->setGridId(i);
                dataList.append(target_power);
            }

            if (settings.value(QZSettings::tile_lapelapsed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_lapelapsed_order, 18).toInt() == i) {
                lapElapsed->setGridId(i);
                dataList.append(lapElapsed);
            }

            if (settings.value(QZSettings::tile_strokes_length_enabled, false).toBool() &&
                settings.value(QZSettings::tile_strokes_length_order, 21).toInt() == i) {
                strokesLength->setGridId(i);
                dataList.append(strokesLength);
            }

            if (settings.value(QZSettings::tile_strokes_count_enabled, false).toBool() &&
                settings.value(QZSettings::tile_strokes_count_order, 22).toInt() == i) {
                strokesCount->setGridId(i);
                dataList.append(strokesCount);
            }

            if (settings.value(QZSettings::tile_pace_enabled, true).toBool() &&
                settings.value(QZSettings::tile_pace_order, 0).toInt() == i) {
                pace->setGridId(i);
                dataList.append(pace);
            }

            if (settings.value(QZSettings::tile_avg_pace_enabled, QZSettings::default_tile_avg_pace_enabled).toBool() &&
                settings.value(QZSettings::tile_avg_pace_order, QZSettings::default_tile_avg_pace_order).toInt() == i) {
                avg_pace->setGridId(i);
                dataList.append(avg_pace);
            }

            if (settings.value(QZSettings::tile_watt_kg_enabled, false).toBool() &&
                settings.value(QZSettings::tile_watt_kg_order, 24).toInt() == i) {
                wattKg->setGridId(i);
                dataList.append(wattKg);
            }

            if (settings.value(QZSettings::tile_step_count_enabled, QZSettings::default_tile_step_count_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_step_count_order, QZSettings::default_tile_step_count_order)
                        .toInt() == i) {

                stepCount->setGridId(i);
                stepCount->setName("Jumps Count");
                dataList.append(stepCount);
            }

            if (settings.value(QZSettings::tile_remainingtimetrainprogramrow_enabled, false).toBool() &&
                settings.value(QZSettings::tile_remainingtimetrainprogramrow_order, 27).toInt() == i) {
                remaningTimeTrainingProgramCurrentRow->setGridId(i);
                dataList.append(remaningTimeTrainingProgramCurrentRow);
            }

            if (settings.value(QZSettings::tile_nextrowstrainprogram_enabled, false).toBool() &&
                settings.value(QZSettings::tile_nextrowstrainprogram_order, 31).toInt() == i) {

                nextRows->setGridId(i);
                dataList.append(nextRows);
            }

            if (settings.value(QZSettings::tile_mets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_mets_order, 28).toInt() == i) {
                mets->setGridId(i);
                dataList.append(mets);
            }
            if (settings.value(QZSettings::tile_targetmets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_targetmets_order, 29).toInt() == i) {
                targetMets->setGridId(i);
                dataList.append(targetMets);
            }

            if (settings.value(QZSettings::tile_pid_hr_enabled, false).toBool() &&
                settings.value(QZSettings::tile_pid_hr_order, 31).toInt() == i) {
                pidHR->setGridId(i);
                dataList.append(pidHR);
            }

            if (settings.value(QZSettings::tile_target_zone_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_zone_order, 24).toInt() == i) {
                target_zone->setGridId(i);
                dataList.append(target_zone);
            }

            if (settings.value(QZSettings::tile_target_speed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_speed_order, 28).toInt() == i) {
                target_speed->setGridId(i);
                dataList.append(target_speed);
            }

            if (settings.value(QZSettings::tile_target_pace_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_pace_order, 50).toInt() == i) {
                target_pace->setGridId(i);
                dataList.append(target_pace);
            }

            if (settings
                    .value(QZSettings::tile_preset_resistance_1_enabled,
                           QZSettings::default_tile_preset_resistance_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_1_order,
                               QZSettings::default_tile_preset_resistance_1_order)
                        .toInt() == i) {
                preset_resistance_1->setGridId(i);
                dataList.append(preset_resistance_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_2_enabled,
                           QZSettings::default_tile_preset_resistance_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_2_order,
                               QZSettings::default_tile_preset_resistance_2_order)
                        .toInt() == i) {
                preset_resistance_2->setGridId(i);
                dataList.append(preset_resistance_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_3_enabled,
                           QZSettings::default_tile_preset_resistance_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_3_order,
                               QZSettings::default_tile_preset_resistance_3_order)
                        .toInt() == i) {
                preset_resistance_3->setGridId(i);
                dataList.append(preset_resistance_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_4_enabled,
                           QZSettings::default_tile_preset_resistance_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_4_order,
                               QZSettings::default_tile_preset_resistance_4_order)
                        .toInt() == i) {
                preset_resistance_4->setGridId(i);
                dataList.append(preset_resistance_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_5_enabled,
                           QZSettings::default_tile_preset_resistance_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_5_order,
                               QZSettings::default_tile_preset_resistance_5_order)
                        .toInt() == i) {
                preset_resistance_5->setGridId(i);
                dataList.append(preset_resistance_5);
            }
            if (settings.value(QZSettings::tile_gears_enabled, false).toBool() &&
                settings.value(QZSettings::tile_gears_order, 51).toInt() == i) {
                gears->setGridId(i);
                dataList.append(gears);
            }


            if (settings.value(QZSettings::tile_hr_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_1_order, 0).toInt() == i) {
                tile_hr_time_in_zone_1->setGridId(i);
                dataList.append(tile_hr_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_2_order, 0).toInt() == i) {
                tile_hr_time_in_zone_2->setGridId(i);
                dataList.append(tile_hr_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_3_order, 0).toInt() == i) {
                tile_hr_time_in_zone_3->setGridId(i);
                dataList.append(tile_hr_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_4_order, 0).toInt() == i) {
                tile_hr_time_in_zone_4->setGridId(i);
                dataList.append(tile_hr_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_5_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_5_order, 0).toInt() == i) {
                tile_hr_time_in_zone_5->setGridId(i);
                dataList.append(tile_hr_time_in_zone_5);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_1_order, 0).toInt() == i) {
                tile_heat_time_in_zone_1->setGridId(i);
                dataList.append(tile_heat_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_2_order, 0).toInt() == i) {
                tile_heat_time_in_zone_2->setGridId(i);
                dataList.append(tile_heat_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_3_order, 0).toInt() == i) {
                tile_heat_time_in_zone_3->setGridId(i);
                dataList.append(tile_heat_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_4_order, 0).toInt() == i) {
                tile_heat_time_in_zone_4->setGridId(i);
                dataList.append(tile_heat_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_coretemperature_enabled, QZSettings::default_tile_coretemperature_enabled).toBool() &&
                settings.value(QZSettings::tile_coretemperature_order, QZSettings::default_tile_coretemperature_order).toInt() == i) {
                coreTemperature->setGridId(i);
                dataList.append(coreTemperature);
            }
        }
    } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
        for (int i = 0; i < 100; i++) {
            if (settings.value(QZSettings::tile_speed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_speed_order, 0).toInt() == i) {
                speed->setGridId(i);
                dataList.append(speed);
            }

            if (settings.value(QZSettings::tile_cadence_enabled, true).toBool() &&
                settings.value(QZSettings::tile_cadence_order, 0).toInt() == i) {
                cadence->setGridId(i);
                dataList.append(cadence);
            }

            if (settings.value(QZSettings::tile_inclination_enabled, true).toBool() &&
                settings.value(QZSettings::tile_inclination_order, 0).toInt() == i) {
                inclination->setGridId(i);
                dataList.append(inclination);
            }

            if (settings.value(QZSettings::tile_elevation_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elevation_order, 0).toInt() == i) {
                elevation->setGridId(i);
                dataList.append(elevation);
            }

            if (settings.value(QZSettings::tile_elapsed_enabled, true).toBool() &&
                settings.value(QZSettings::tile_elapsed_order, 0).toInt() == i) {
                elapsed->setGridId(i);
                dataList.append(elapsed);
            }

            if (settings.value(QZSettings::tile_moving_time_enabled, false).toBool() &&
                settings.value(QZSettings::tile_moving_time_order, 19).toInt() == i) {
                moving_time->setGridId(i);
                dataList.append(moving_time);
            }

            if (settings.value(QZSettings::tile_peloton_offset_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_offset_order, 20).toInt() == i) {
                peloton_offset->setGridId(i);
                dataList.append(peloton_offset);
            }

            if (settings.value(QZSettings::tile_peloton_remaining_enabled, false).toBool() &&
                settings.value(QZSettings::tile_peloton_remaining_order, 20).toInt() == i) {
                peloton_remaining->setGridId(i);
                dataList.append(peloton_remaining);
            }

            if (settings.value(QZSettings::tile_calories_enabled, true).toBool() &&
                settings.value(QZSettings::tile_calories_order, 0).toInt() == i) {
                calories->setGridId(i);
                dataList.append(calories);
            }

            if (settings.value(QZSettings::tile_odometer_enabled, true).toBool() &&
                settings.value(QZSettings::tile_odometer_order, 0).toInt() == i) {
                odometer->setGridId(i);
                dataList.append(odometer);
            }

            if (settings.value(QZSettings::tile_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_resistance_order, 0).toInt() == i) {
                resistance->setGridId(i);
                dataList.append(resistance);
            }

            if (settings.value(QZSettings::tile_peloton_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_peloton_resistance_order, 0).toInt() == i) {
                peloton_resistance->setGridId(i);
                dataList.append(peloton_resistance);
            }

            if (settings.value(QZSettings::tile_watt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_watt_order, 0).toInt() == i) {
                watt->setGridId(i);
                dataList.append(watt);
            }

            if (settings.value(QZSettings::tile_weight_loss_enabled, false).toBool() &&
                settings.value(QZSettings::tile_weight_loss_order, 24).toInt() == i) {
                weightLoss->setGridId(i);
                dataList.append(weightLoss);
            }

            if (settings.value(QZSettings::tile_avgwatt_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avgwatt_order, 0).toInt() == i) {
                avgWatt->setGridId(i);
                dataList.append(avgWatt);
            }

            if (settings.value(QZSettings::tile_avg_watt_lap_enabled, true).toBool() &&
                settings.value(QZSettings::tile_avg_watt_lap_order, 0).toInt() == i) {
                avgWattLap->setGridId(i);
                dataList.append(avgWattLap);
            }

            if (settings.value(QZSettings::tile_ftp_enabled, true).toBool() &&
                settings.value(QZSettings::tile_ftp_order, 0).toInt() == i) {
                ftp->setGridId(i);
                dataList.append(ftp);
            }

            if (settings.value(QZSettings::tile_jouls_enabled, true).toBool() &&
                settings.value(QZSettings::tile_jouls_order, 0).toInt() == i) {
                jouls->setGridId(i);
                dataList.append(jouls);
            }

            if (settings.value(QZSettings::tile_heart_enabled, true).toBool() &&
                settings.value(QZSettings::tile_heart_order, 0).toInt() == i) {
                heart->setGridId(i);
                dataList.append(heart);
            }

            if (settings.value(QZSettings::tile_hrv_enabled, QZSettings::default_tile_hrv_enabled).toBool() &&
                settings.value(QZSettings::tile_hrv_order, QZSettings::default_tile_hrv_order).toInt() == i) {
                hrv->setGridId(i);
                dataList.append(hrv);
            }

            if (settings.value(QZSettings::tile_fan_enabled, true).toBool() &&
                settings.value(QZSettings::tile_fan_order, 0).toInt() == i) {
                fan->setGridId(i);
                dataList.append(fan);
            }

            if (settings.value(QZSettings::tile_datetime_enabled, true).toBool() &&
                settings.value(QZSettings::tile_datetime_order, 0).toInt() == i) {
                datetime->setGridId(i);
                dataList.append(datetime);
            }

            if (settings.value(QZSettings::tile_target_resistance_enabled, true).toBool() &&
                settings.value(QZSettings::tile_target_resistance_order, 0).toInt() == i) {
                target_resistance->setGridId(i);
                dataList.append(target_resistance);
            }

            if (settings.value(QZSettings::tile_lapelapsed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_lapelapsed_order, 18).toInt() == i) {
                lapElapsed->setGridId(i);
                dataList.append(lapElapsed);
            }

            if (settings.value(QZSettings::tile_watt_kg_enabled, false).toBool() &&
                settings.value(QZSettings::tile_watt_kg_order, 24).toInt() == i) {
                wattKg->setGridId(i);
                dataList.append(wattKg);
            }

            if (settings.value(QZSettings::tile_remainingtimetrainprogramrow_enabled, false).toBool() &&
                settings.value(QZSettings::tile_remainingtimetrainprogramrow_order, 27).toInt() == i) {
                remaningTimeTrainingProgramCurrentRow->setGridId(i);
                dataList.append(remaningTimeTrainingProgramCurrentRow);
            }

            if (settings.value(QZSettings::tile_nextrowstrainprogram_enabled, false).toBool() &&
                settings.value(QZSettings::tile_nextrowstrainprogram_order, 31).toInt() == i) {

                nextRows->setGridId(i);
                dataList.append(nextRows);
            }

            if (settings.value(QZSettings::tile_mets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_mets_order, 28).toInt() == i) {
                mets->setGridId(i);
                dataList.append(mets);
            }
            if (settings.value(QZSettings::tile_targetmets_enabled, false).toBool() &&
                settings.value(QZSettings::tile_targetmets_order, 29).toInt() == i) {
                targetMets->setGridId(i);
                dataList.append(targetMets);
            }

            if (settings.value(QZSettings::tile_pid_hr_enabled, false).toBool() &&
                settings.value(QZSettings::tile_pid_hr_order, 31).toInt() == i) {
                pidHR->setGridId(i);
                dataList.append(pidHR);
            }

            if (settings.value(QZSettings::tile_target_cadence_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_cadence_order, 19).toInt() == i) {
                target_cadence->setGridId(i);
                dataList.append(target_cadence);
            }

            if (settings.value(QZSettings::tile_target_speed_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_speed_order, 28).toInt() == i) {
                target_speed->setGridId(i);
                dataList.append(target_speed);
            }

            if (settings
                    .value(QZSettings::tile_preset_inclination_1_enabled,
                           QZSettings::default_tile_preset_inclination_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_1_order,
                               QZSettings::default_tile_preset_inclination_1_order)
                        .toInt() == i) {
                preset_inclination_1->setGridId(i);
                dataList.append(preset_inclination_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_2_enabled,
                           QZSettings::default_tile_preset_inclination_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_2_order,
                               QZSettings::default_tile_preset_inclination_2_order)
                        .toInt() == i) {
                preset_inclination_2->setGridId(i);
                dataList.append(preset_inclination_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_3_enabled,
                           QZSettings::default_tile_preset_inclination_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_3_order,
                               QZSettings::default_tile_preset_inclination_3_order)
                        .toInt() == i) {
                preset_inclination_3->setGridId(i);
                dataList.append(preset_inclination_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_4_enabled,
                           QZSettings::default_tile_preset_inclination_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_4_order,
                               QZSettings::default_tile_preset_inclination_4_order)
                        .toInt() == i) {
                preset_inclination_4->setGridId(i);
                dataList.append(preset_inclination_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_inclination_5_enabled,
                           QZSettings::default_tile_preset_inclination_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_inclination_5_order,
                               QZSettings::default_tile_preset_inclination_5_order)
                        .toInt() == i) {
                preset_inclination_5->setGridId(i);
                dataList.append(preset_inclination_5);
            }

            if (settings
                    .value(QZSettings::tile_preset_resistance_1_enabled,
                           QZSettings::default_tile_preset_resistance_1_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_1_order,
                               QZSettings::default_tile_preset_resistance_1_order)
                        .toInt() == i) {
                preset_resistance_1->setGridId(i);
                dataList.append(preset_resistance_1);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_2_enabled,
                           QZSettings::default_tile_preset_resistance_2_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_2_order,
                               QZSettings::default_tile_preset_resistance_2_order)
                        .toInt() == i) {
                preset_resistance_2->setGridId(i);
                dataList.append(preset_resistance_2);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_3_enabled,
                           QZSettings::default_tile_preset_resistance_3_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_3_order,
                               QZSettings::default_tile_preset_resistance_3_order)
                        .toInt() == i) {
                preset_resistance_3->setGridId(i);
                dataList.append(preset_resistance_3);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_4_enabled,
                           QZSettings::default_tile_preset_resistance_4_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_4_order,
                               QZSettings::default_tile_preset_resistance_4_order)
                        .toInt() == i) {
                preset_resistance_4->setGridId(i);
                dataList.append(preset_resistance_4);
            }
            if (settings
                    .value(QZSettings::tile_preset_resistance_5_enabled,
                           QZSettings::default_tile_preset_resistance_5_enabled)
                    .toBool() &&
                settings.value(QZSettings::tile_preset_resistance_5_order,
                               QZSettings::default_tile_preset_resistance_5_order)
                        .toInt() == i) {
                preset_resistance_5->setGridId(i);
                dataList.append(preset_resistance_5);
            }
            if (settings.value(QZSettings::tile_gears_enabled, false).toBool() &&
                settings.value(QZSettings::tile_gears_order, 25).toInt() == i) {
                gears->setGridId(i);
                dataList.append(gears);
            }

            if (settings.value(QZSettings::tile_target_pace_enabled, false).toBool() &&
                settings.value(QZSettings::tile_target_pace_order, 50).toInt() == i) {
                target_pace->setGridId(i);
                dataList.append(target_pace);
            }

            if (settings.value(QZSettings::tile_pace_enabled, true).toBool() &&
                settings.value(QZSettings::tile_pace_order, 51).toInt() == i) {
                pace->setGridId(i);
                dataList.append(pace);
            }


            if (settings.value(QZSettings::tile_hr_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_1_order, 0).toInt() == i) {
                tile_hr_time_in_zone_1->setGridId(i);
                dataList.append(tile_hr_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_2_order, 0).toInt() == i) {
                tile_hr_time_in_zone_2->setGridId(i);
                dataList.append(tile_hr_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_3_order, 0).toInt() == i) {
                tile_hr_time_in_zone_3->setGridId(i);
                dataList.append(tile_hr_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_4_order, 0).toInt() == i) {
                tile_hr_time_in_zone_4->setGridId(i);
                dataList.append(tile_hr_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_hr_time_in_zone_5_enabled, false).toBool() &&
                settings.value(QZSettings::tile_hr_time_in_zone_5_order, 0).toInt() == i) {
                tile_hr_time_in_zone_5->setGridId(i);
                dataList.append(tile_hr_time_in_zone_5);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_1_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_1_order, 0).toInt() == i) {
                tile_heat_time_in_zone_1->setGridId(i);
                dataList.append(tile_heat_time_in_zone_1);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_2_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_2_order, 0).toInt() == i) {
                tile_heat_time_in_zone_2->setGridId(i);
                dataList.append(tile_heat_time_in_zone_2);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_3_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_3_order, 0).toInt() == i) {
                tile_heat_time_in_zone_3->setGridId(i);
                dataList.append(tile_heat_time_in_zone_3);
            }

            if (settings.value(QZSettings::tile_heat_time_in_zone_4_enabled, false).toBool() &&
                settings.value(QZSettings::tile_heat_time_in_zone_4_order, 0).toInt() == i) {
                tile_heat_time_in_zone_4->setGridId(i);
                dataList.append(tile_heat_time_in_zone_4);
            }

            if (settings.value(QZSettings::tile_coretemperature_enabled, QZSettings::default_tile_coretemperature_enabled).toBool() &&
                settings.value(QZSettings::tile_coretemperature_order, QZSettings::default_tile_coretemperature_order).toInt() == i) {
                coreTemperature->setGridId(i);
                dataList.append(coreTemperature);
            }

            if (settings.value(QZSettings::tile_negative_inclination_enabled, QZSettings::default_tile_negative_inclination_enabled).toBool() &&
                settings.value(QZSettings::tile_negative_inclination_order, QZSettings::default_tile_negative_inclination_order).toInt() == i) {
                negative_inclination->setGridId(i);
                dataList.append(negative_inclination);
            }
        }
    }

    engine->rootContext()->setContextProperty(QStringLiteral("appModel"), QVariant::fromValue(dataList));
}

DataObject *homeform::tileFromName(QString name) {
    foreach (QObject *d, dataList) {
        if (!((DataObject *)d)->name().compare(name)) {
            return (DataObject *)d;
        }
    }
    return nullptr;
}

void homeform::moveTile(QString name, int newIndex, int oldIndex) {
    QSettings settings;
    DataObject *current = tileFromName(name);
    if (current) {
        qDebug() << "moveTile" << name << newIndex << oldIndex;

        foreach (QString s, settings.allKeys()) {
            if (s.contains(QStringLiteral("tile_")) && s.contains(QStringLiteral("_order"))) {

                qDebug() << s << settings.value(s);
            }
        }

        int i = 0;
        foreach (QObject *d, dataList) {
            if (i == newIndex) {
                settings.setValue("tile_" + current->m_id.toLower() + "_order", i);
                i++;
            }
            QString n = ((DataObject *)d)->m_id;
            if (((DataObject *)d)->name().compare(name)) {
                settings.setValue("tile_" + n.toLower() + "_order", i);
                i++;
            }
        }

        foreach (QString s, settings.allKeys()) {
            if (s.contains(QStringLiteral("tile_")) && s.contains(QStringLiteral("_order"))) {

                qDebug() << s << settings.value(s);
            }
        }

        // sortTiles();
        // dataList.move(oldIndex, newIndex);
        // very dirty, but i needed a way to synchronize QML with C++
        QTimer::singleShot(100, this, &homeform::sortTilesTimeout);
    }
}

void homeform::sortTilesTimeout() { sortTiles(); }

void homeform::deviceConnected(QBluetoothDeviceInfo b) {

    qDebug() << "deviceConnected" << bluetoothManager << engine;
    if (bluetoothManager)
        qDebug() << bluetoothManager->device();

    if (bluetoothManager->device() == nullptr)
        return;

    // if the device reconnects in the same session, the tiles shouldn't be created again
    static bool first = false;
    if (first) {
        return;
    }
    first = true;

    if (b.isValid())
        deviceFound(b.name());

    m_labelHelp = false;
    emit changeLabelHelp(m_labelHelp);

    QSettings settings;

    if (settings.value(QZSettings::pause_on_start, QZSettings::default_pause_on_start).toBool() &&
        bluetoothManager->device()->deviceType() != TREADMILL) {
        Start();
        stopped = true; // when you will press start while you did some kms in pause mode from the beginning, the stats must be resetted
    } else if (settings.value(QZSettings::pause_on_start_treadmill, QZSettings::default_pause_on_start_treadmill)
                   .toBool() &&
               bluetoothManager->device()->deviceType() == TREADMILL) {
        Start_inner(false); // because if you sent the start command to a treadmill it could start the tape
        stopped = true; // when you will press start while you did some kms in pause mode from the beginning, the stats must be resetted
    }

    sortTiles();

    QObject *rootObject = engine->rootObjects().constFirst();
    QObject *home = rootObject->findChild<QObject *>(QStringLiteral("home"));
    QObject::connect(home, SIGNAL(plus_clicked(QString)), this, SLOT(Plus(QString)));
    QObject::connect(home, SIGNAL(minus_clicked(QString)), this, SLOT(Minus(QString)));
    QObject::connect(home, SIGNAL(largeButton_clicked(QString)), this, SLOT(LargeButton(QString)));

    emit workoutNameChanged(workoutName());
    emit instructorNameChanged(instructorName());

#ifdef Q_OS_ANDROID
    if (!settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name)
             .toString()
             .compare(QZSettings::default_heart_rate_belt_name) &&
        !settings.value(QZSettings::ant_heart, QZSettings::default_ant_heart).toBool()) {
        QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/WearableController", "start",
                                                  "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
    }
#endif

    if (settings.value(QZSettings::gears_restore_value, QZSettings::default_gears_restore_value).toBool() ||
        settings.value(QZSettings::restore_specific_gear, QZSettings::default_restore_specific_gear).toBool()) {
        if (bluetoothManager->device()->deviceType() == BIKE) {
            ((bike *)bluetoothManager->device())
                ->setGears(settings.value(QZSettings::gears_current_value, QZSettings::default_gears_current_value)
                               .toDouble());
        } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
            ((elliptical *)bluetoothManager->device())
                ->setGears(settings.value(QZSettings::gears_current_value, QZSettings::default_gears_current_value)
                               .toDouble());
        } else if (bluetoothManager->device()->deviceType() == ROWING) {
            ((rower *)bluetoothManager->device())
                ->setGears(settings.value(QZSettings::gears_current_value, QZSettings::default_gears_current_value)
                               .toDouble());
        }

    }
}

void homeform::deviceFound(const QString &name) {
    if (name.trimmed().isEmpty()) {
        return;
    }

    emit bluetoothDevicesChanged(bluetoothDevices());

    if (m_labelHelp == false) {
        return;
    }

    QSettings settings;
    if (!settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {
        return;
    }
    m_info = name + QStringLiteral(" found");
    emit infoChanged(m_info);
}

void homeform::LargeButton(const QString &name) {
    QSettings settings;
    qDebug() << QStringLiteral("LargeButton") << name;
    if (!bluetoothManager || !bluetoothManager->device())
        return;

    if (bluetoothManager->device()->deviceType() == BIKE || 
        bluetoothManager->device()->deviceType() == ELLIPTICAL ||
        bluetoothManager->device()->deviceType() == ROWING) {
        if (name.startsWith(QStringLiteral("preset_powerzone_"))) {
            double ftp = settings.value(QZSettings::ftp, QZSettings::default_ftp).toDouble();
            int zoneNum = name.right(1).toInt(); // Gets last digit from preset_powerzone_X
            QString zoneSetting = QString("tile_preset_powerzone_%1_value").arg(zoneNum);
            double zoneValue = settings.value(zoneSetting, zoneNum).toDouble();
            double targetWatts = bike::powerZoneValueToWatts(zoneValue, ftp);
            bluetoothManager->device()->changePower(targetWatts);
        } else if (name.contains(QStringLiteral("erg_mode"))) {
            settings.setValue(QZSettings::zwift_erg, !settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool());
        } else if (name.contains(QStringLiteral("preset_resistance_1"))) {
            const resistance_t resistanceValue = settings
                                                     .value(QZSettings::tile_preset_resistance_1_value,
                                                            QZSettings::default_tile_preset_resistance_1_value)
                                                     .toDouble();
            emit manualCscBikeResistanceAdjusted(resistanceValue);
            bluetoothManager->device()->changeResistance(resistanceValue);
        } else if (name.contains(QStringLiteral("preset_resistance_2"))) {
            const resistance_t resistanceValue = settings
                                                     .value(QZSettings::tile_preset_resistance_2_value,
                                                            QZSettings::default_tile_preset_resistance_2_value)
                                                     .toDouble();
            emit manualCscBikeResistanceAdjusted(resistanceValue);
            bluetoothManager->device()->changeResistance(resistanceValue);
        } else if (name.contains(QStringLiteral("preset_resistance_3"))) {
            const resistance_t resistanceValue = settings
                                                     .value(QZSettings::tile_preset_resistance_3_value,
                                                            QZSettings::default_tile_preset_resistance_3_value)
                                                     .toDouble();
            emit manualCscBikeResistanceAdjusted(resistanceValue);
            bluetoothManager->device()->changeResistance(resistanceValue);
        } else if (name.contains(QStringLiteral("preset_resistance_4"))) {
            const resistance_t resistanceValue = settings
                                                     .value(QZSettings::tile_preset_resistance_4_value,
                                                            QZSettings::default_tile_preset_resistance_4_value)
                                                     .toDouble();
            emit manualCscBikeResistanceAdjusted(resistanceValue);
            bluetoothManager->device()->changeResistance(resistanceValue);
        } else if (name.contains(QStringLiteral("preset_resistance_5"))) {
            const resistance_t resistanceValue = settings
                                                     .value(QZSettings::tile_preset_resistance_5_value,
                                                            QZSettings::default_tile_preset_resistance_5_value)
                                                     .toDouble();
            emit manualCscBikeResistanceAdjusted(resistanceValue);
            bluetoothManager->device()->changeResistance(resistanceValue);
        } else if (name.contains(QStringLiteral("preset_inclination_1"))) {
            ((bike *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_1_value,
                                               QZSettings::default_tile_preset_inclination_1_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_1_value,
                                               QZSettings::default_tile_preset_inclination_1_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_2"))) {
            ((bike *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_2_value,
                                               QZSettings::default_tile_preset_inclination_2_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_2_value,
                                               QZSettings::default_tile_preset_inclination_2_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_3"))) {
            ((bike *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_3_value,
                                               QZSettings::default_tile_preset_inclination_3_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_3_value,
                                               QZSettings::default_tile_preset_inclination_3_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_4"))) {
            ((bike *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_4_value,
                                               QZSettings::default_tile_preset_inclination_4_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_4_value,
                                               QZSettings::default_tile_preset_inclination_4_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_5"))) {
            ((bike *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_5_value,
                                               QZSettings::default_tile_preset_inclination_5_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_5_value,
                                               QZSettings::default_tile_preset_inclination_5_value)
                                        .toDouble());
        }
    } else if (bluetoothManager->device()->deviceType() == TREADMILL) {
        if (name.contains(QStringLiteral("preset_speed_1"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeSpeed(
                    settings.value(QZSettings::tile_preset_speed_1_value, QZSettings::default_tile_preset_speed_1_value)
                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_speed_2"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeSpeed(
                    settings.value(QZSettings::tile_preset_speed_2_value, QZSettings::default_tile_preset_speed_2_value)
                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_speed_3"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeSpeed(
                    settings.value(QZSettings::tile_preset_speed_3_value, QZSettings::default_tile_preset_speed_3_value)
                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_speed_4"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeSpeed(
                    settings.value(QZSettings::tile_preset_speed_4_value, QZSettings::default_tile_preset_speed_4_value)
                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_speed_5"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeSpeed(
                    settings.value(QZSettings::tile_preset_speed_5_value, QZSettings::default_tile_preset_speed_5_value)
                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_1"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_1_value,
                                               QZSettings::default_tile_preset_inclination_1_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_1_value,
                                               QZSettings::default_tile_preset_inclination_1_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_2"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_2_value,
                                               QZSettings::default_tile_preset_inclination_2_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_2_value,
                                               QZSettings::default_tile_preset_inclination_2_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_3"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_3_value,
                                               QZSettings::default_tile_preset_inclination_3_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_3_value,
                                               QZSettings::default_tile_preset_inclination_3_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_4"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_4_value,
                                               QZSettings::default_tile_preset_inclination_4_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_4_value,
                                               QZSettings::default_tile_preset_inclination_4_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_5"))) {
            ((treadmill *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_5_value,
                                               QZSettings::default_tile_preset_inclination_5_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_5_value,
                                               QZSettings::default_tile_preset_inclination_5_value)
                                        .toDouble());
        }
    } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
        if (name.contains(QStringLiteral("preset_resistance_1"))) {
            bluetoothManager->device()->changeResistance(settings
                                                             .value(QZSettings::tile_preset_resistance_1_value,
                                                                    QZSettings::default_tile_preset_resistance_1_value)
                                                             .toDouble());
        } else if (name.contains(QStringLiteral("preset_resistance_2"))) {
            bluetoothManager->device()->changeResistance(settings
                                                             .value(QZSettings::tile_preset_resistance_2_value,
                                                                    QZSettings::default_tile_preset_resistance_2_value)
                                                             .toDouble());
        } else if (name.contains(QStringLiteral("preset_resistance_3"))) {
            bluetoothManager->device()->changeResistance(settings
                                                             .value(QZSettings::tile_preset_resistance_3_value,
                                                                    QZSettings::default_tile_preset_resistance_3_value)
                                                             .toDouble());
        } else if (name.contains(QStringLiteral("preset_resistance_4"))) {
            bluetoothManager->device()->changeResistance(settings
                                                             .value(QZSettings::tile_preset_resistance_4_value,
                                                                    QZSettings::default_tile_preset_resistance_4_value)
                                                             .toDouble());
        } else if (name.contains(QStringLiteral("preset_resistance_5"))) {
            bluetoothManager->device()->changeResistance(settings
                                                             .value(QZSettings::tile_preset_resistance_5_value,
                                                                    QZSettings::default_tile_preset_resistance_5_value)
                                                             .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_1"))) {
            ((elliptical *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_1_value,
                                               QZSettings::default_tile_preset_inclination_1_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_1_value,
                                               QZSettings::default_tile_preset_inclination_1_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_2"))) {
            ((elliptical *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_2_value,
                                               QZSettings::default_tile_preset_inclination_2_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_2_value,
                                               QZSettings::default_tile_preset_inclination_2_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_3"))) {
            ((elliptical *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_3_value,
                                               QZSettings::default_tile_preset_inclination_3_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_3_value,
                                               QZSettings::default_tile_preset_inclination_3_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_4"))) {
            ((elliptical *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_4_value,
                                               QZSettings::default_tile_preset_inclination_4_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_4_value,
                                               QZSettings::default_tile_preset_inclination_4_value)
                                        .toDouble());
        } else if (name.contains(QStringLiteral("preset_inclination_5"))) {
            ((elliptical *)bluetoothManager->device())
                ->changeInclination(settings
                                        .value(QZSettings::tile_preset_inclination_5_value,
                                               QZSettings::default_tile_preset_inclination_5_value)
                                        .toDouble(),
                                    settings
                                        .value(QZSettings::tile_preset_inclination_5_value,
                                               QZSettings::default_tile_preset_inclination_5_value)
                                        .toDouble());
        }
    }

    if(name.contains(QStringLiteral("biggearsplus"))) {
        gearUp();
    } else if(name.contains(QStringLiteral("biggearsminus"))) {
        gearDown();
    } else if(name.contains(QStringLiteral("autoVirtualShiftingCruise"))) {
        // Switch to Cruise profile (0)
        settings.setValue(QZSettings::automatic_virtual_shifting_profile, 0);
    } else if(name.contains(QStringLiteral("autoVirtualShiftingClimb"))) {
        // Switch to Climb profile (1)
        settings.setValue(QZSettings::automatic_virtual_shifting_profile, 1);
    } else if(name.contains(QStringLiteral("autoVirtualShiftingSprint"))) {
        // Switch to Sprint profile (2)
        settings.setValue(QZSettings::automatic_virtual_shifting_profile, 2);
    } else if(name.contains(QStringLiteral("powerAvg"))) {
        // Cycle through power averaging modes: Off -> 3s -> 5s -> Off
        bool power3s = settings.value(QZSettings::power_avg_3s, QZSettings::default_power_avg_3s).toBool();
        bool power5s = settings.value(QZSettings::power_avg_5s, QZSettings::default_power_avg_5s).toBool();

        if (!power3s && !power5s) {
            // Currently Off, switch to 3s
            settings.setValue(QZSettings::power_avg_3s, true);
            settings.setValue(QZSettings::power_avg_5s, false);
        } else if (power3s) {
            // Currently 3s, switch to 5s
            settings.setValue(QZSettings::power_avg_3s, false);
            settings.setValue(QZSettings::power_avg_5s, true);
        } else {
            // Currently 5s, switch to Off
            settings.setValue(QZSettings::power_avg_3s, false);
            settings.setValue(QZSettings::power_avg_5s, false);
        }
    }
}

bool homeform::handleKeyboardShortcut(const QString &sequence) {
    const QString normalizedSequence = sequence.trimmed().toUpper();
    if (normalizedSequence.isEmpty() || m_nativeShortcutCaptureSuspended) {
        return false;
    }

    QSettings settings;
    if (!settings.value(QZSettings::shortcuts_enabled, QZSettings::default_shortcuts_enabled).toBool()) {
        return false;
    }

    const auto matches = [&settings, &normalizedSequence](const QString &settingName,
                                                           const QString &defaultValue) {
        return normalizedSequence.compare(settings.value(settingName, defaultValue).toString().trimmed(),
                                          Qt::CaseInsensitive) == 0;
    };

    if (matches(QZSettings::shortcut_speed_plus, QZSettings::default_shortcut_speed_plus)) {
        Plus(QStringLiteral("speed"));
    } else if (matches(QZSettings::shortcut_speed_minus, QZSettings::default_shortcut_speed_minus)) {
        Minus(QStringLiteral("speed"));
    } else if (matches(QZSettings::shortcut_inclination_plus, QZSettings::default_shortcut_inclination_plus)) {
        Plus(QStringLiteral("inclination"));
    } else if (matches(QZSettings::shortcut_inclination_minus, QZSettings::default_shortcut_inclination_minus)) {
        Minus(QStringLiteral("inclination"));
    } else if (matches(QZSettings::shortcut_resistance_plus, QZSettings::default_shortcut_resistance_plus)) {
        Plus(QStringLiteral("resistance"));
    } else if (matches(QZSettings::shortcut_resistance_minus, QZSettings::default_shortcut_resistance_minus)) {
        Minus(QStringLiteral("resistance"));
    } else if (matches(QZSettings::shortcut_peloton_resistance_plus,
                       QZSettings::default_shortcut_peloton_resistance_plus)) {
        Plus(QStringLiteral("peloton_resistance"));
    } else if (matches(QZSettings::shortcut_peloton_resistance_minus,
                       QZSettings::default_shortcut_peloton_resistance_minus)) {
        Minus(QStringLiteral("peloton_resistance"));
    } else if (matches(QZSettings::shortcut_target_resistance_plus,
                       QZSettings::default_shortcut_target_resistance_plus)) {
        Plus(QStringLiteral("target_resistance"));
    } else if (matches(QZSettings::shortcut_target_resistance_minus,
                       QZSettings::default_shortcut_target_resistance_minus)) {
        Minus(QStringLiteral("target_resistance"));
    } else if (matches(QZSettings::shortcut_target_power_plus, QZSettings::default_shortcut_target_power_plus)) {
        Plus(QStringLiteral("target_power"));
    } else if (matches(QZSettings::shortcut_target_power_minus, QZSettings::default_shortcut_target_power_minus)) {
        Minus(QStringLiteral("target_power"));
    } else if (matches(QZSettings::shortcut_target_zone_plus, QZSettings::default_shortcut_target_zone_plus)) {
        Plus(QStringLiteral("target_zone"));
    } else if (matches(QZSettings::shortcut_target_zone_minus, QZSettings::default_shortcut_target_zone_minus)) {
        Minus(QStringLiteral("target_zone"));
    } else if (matches(QZSettings::shortcut_target_speed_plus, QZSettings::default_shortcut_target_speed_plus)) {
        Plus(QStringLiteral("target_speed"));
    } else if (matches(QZSettings::shortcut_target_speed_minus, QZSettings::default_shortcut_target_speed_minus)) {
        Minus(QStringLiteral("target_speed"));
    } else if (matches(QZSettings::shortcut_target_incline_plus,
                       QZSettings::default_shortcut_target_incline_plus)) {
        Plus(QStringLiteral("target_inclination"));
    } else if (matches(QZSettings::shortcut_target_incline_minus,
                       QZSettings::default_shortcut_target_incline_minus)) {
        Minus(QStringLiteral("target_inclination"));
    } else if (matches(QZSettings::shortcut_fan_plus, QZSettings::default_shortcut_fan_plus)) {
        Plus(QStringLiteral("fan"));
    } else if (matches(QZSettings::shortcut_fan_minus, QZSettings::default_shortcut_fan_minus)) {
        Minus(QStringLiteral("fan"));
    } else if (matches(QZSettings::shortcut_peloton_offset_plus,
                       QZSettings::default_shortcut_peloton_offset_plus)) {
        Plus(QStringLiteral("peloton_offset"));
    } else if (matches(QZSettings::shortcut_peloton_offset_minus,
                       QZSettings::default_shortcut_peloton_offset_minus)) {
        Minus(QStringLiteral("peloton_offset"));
    } else if (matches(QZSettings::shortcut_peloton_remaining_plus,
                       QZSettings::default_shortcut_peloton_remaining_plus)) {
        Plus(QStringLiteral("peloton_remaining"));
    } else if (matches(QZSettings::shortcut_peloton_remaining_minus,
                       QZSettings::default_shortcut_peloton_remaining_minus)) {
        Minus(QStringLiteral("peloton_remaining"));
    } else if (matches(QZSettings::shortcut_remaining_time_plus,
                       QZSettings::default_shortcut_remaining_time_plus)) {
        Plus(QStringLiteral("remainingtimetrainprogramrow"));
    } else if (matches(QZSettings::shortcut_remaining_time_minus,
                       QZSettings::default_shortcut_remaining_time_minus)) {
        Minus(QStringLiteral("remainingtimetrainprogramrow"));
    } else if (matches(QZSettings::shortcut_gears_plus, QZSettings::default_shortcut_gears_plus)) {
        Plus(QStringLiteral("gears"));
    } else if (matches(QZSettings::shortcut_gears_minus, QZSettings::default_shortcut_gears_minus)) {
        Minus(QStringLiteral("gears"));
    } else if (matches(QZSettings::shortcut_pid_hr_plus, QZSettings::default_shortcut_pid_hr_plus)) {
        Plus(QStringLiteral("pid_hr"));
    } else if (matches(QZSettings::shortcut_pid_hr_minus, QZSettings::default_shortcut_pid_hr_minus)) {
        Minus(QStringLiteral("pid_hr"));
    } else if (matches(QZSettings::shortcut_ext_incline_plus, QZSettings::default_shortcut_ext_incline_plus)) {
        Plus(QStringLiteral("external_inclination"));
    } else if (matches(QZSettings::shortcut_ext_incline_minus, QZSettings::default_shortcut_ext_incline_minus)) {
        Minus(QStringLiteral("external_inclination"));
    } else if (matches(QZSettings::shortcut_biggears_plus, QZSettings::default_shortcut_biggears_plus)) {
        LargeButton(QStringLiteral("biggearsplus"));
    } else if (matches(QZSettings::shortcut_biggears_minus, QZSettings::default_shortcut_biggears_minus)) {
        LargeButton(QStringLiteral("biggearsminus"));
    } else if (matches(QZSettings::shortcut_avs_cruise, QZSettings::default_shortcut_avs_cruise)) {
        LargeButton(QStringLiteral("autoVirtualShiftingCruise"));
    } else if (matches(QZSettings::shortcut_avs_climb, QZSettings::default_shortcut_avs_climb)) {
        LargeButton(QStringLiteral("autoVirtualShiftingClimb"));
    } else if (matches(QZSettings::shortcut_avs_sprint, QZSettings::default_shortcut_avs_sprint)) {
        LargeButton(QStringLiteral("autoVirtualShiftingSprint"));
    } else if (matches(QZSettings::shortcut_power_avg, QZSettings::default_shortcut_power_avg)) {
        LargeButton(QStringLiteral("powerAvg"));
    } else if (matches(QZSettings::shortcut_erg_mode, QZSettings::default_shortcut_erg_mode)) {
        LargeButton(QStringLiteral("erg_mode"));
    } else if (matches(QZSettings::shortcut_preset_resistance_1,
                       QZSettings::default_shortcut_preset_resistance_1)) {
        LargeButton(QStringLiteral("preset_resistance_1"));
    } else if (matches(QZSettings::shortcut_preset_resistance_2,
                       QZSettings::default_shortcut_preset_resistance_2)) {
        LargeButton(QStringLiteral("preset_resistance_2"));
    } else if (matches(QZSettings::shortcut_preset_resistance_3,
                       QZSettings::default_shortcut_preset_resistance_3)) {
        LargeButton(QStringLiteral("preset_resistance_3"));
    } else if (matches(QZSettings::shortcut_preset_resistance_4,
                       QZSettings::default_shortcut_preset_resistance_4)) {
        LargeButton(QStringLiteral("preset_resistance_4"));
    } else if (matches(QZSettings::shortcut_preset_resistance_5,
                       QZSettings::default_shortcut_preset_resistance_5)) {
        LargeButton(QStringLiteral("preset_resistance_5"));
    } else if (matches(QZSettings::shortcut_preset_speed_1, QZSettings::default_shortcut_preset_speed_1)) {
        LargeButton(QStringLiteral("preset_speed_1"));
    } else if (matches(QZSettings::shortcut_preset_speed_2, QZSettings::default_shortcut_preset_speed_2)) {
        LargeButton(QStringLiteral("preset_speed_2"));
    } else if (matches(QZSettings::shortcut_preset_speed_3, QZSettings::default_shortcut_preset_speed_3)) {
        LargeButton(QStringLiteral("preset_speed_3"));
    } else if (matches(QZSettings::shortcut_preset_speed_4, QZSettings::default_shortcut_preset_speed_4)) {
        LargeButton(QStringLiteral("preset_speed_4"));
    } else if (matches(QZSettings::shortcut_preset_speed_5, QZSettings::default_shortcut_preset_speed_5)) {
        LargeButton(QStringLiteral("preset_speed_5"));
    } else if (matches(QZSettings::shortcut_preset_inclination_1,
                       QZSettings::default_shortcut_preset_inclination_1)) {
        LargeButton(QStringLiteral("preset_inclination_1"));
    } else if (matches(QZSettings::shortcut_preset_inclination_2,
                       QZSettings::default_shortcut_preset_inclination_2)) {
        LargeButton(QStringLiteral("preset_inclination_2"));
    } else if (matches(QZSettings::shortcut_preset_inclination_3,
                       QZSettings::default_shortcut_preset_inclination_3)) {
        LargeButton(QStringLiteral("preset_inclination_3"));
    } else if (matches(QZSettings::shortcut_preset_inclination_4,
                       QZSettings::default_shortcut_preset_inclination_4)) {
        LargeButton(QStringLiteral("preset_inclination_4"));
    } else if (matches(QZSettings::shortcut_preset_inclination_5,
                       QZSettings::default_shortcut_preset_inclination_5)) {
        LargeButton(QStringLiteral("preset_inclination_5"));
    } else if (matches(QZSettings::shortcut_preset_powerzone_1,
                       QZSettings::default_shortcut_preset_powerzone_1)) {
        LargeButton(QStringLiteral("preset_powerzone_1"));
    } else if (matches(QZSettings::shortcut_preset_powerzone_2,
                       QZSettings::default_shortcut_preset_powerzone_2)) {
        LargeButton(QStringLiteral("preset_powerzone_2"));
    } else if (matches(QZSettings::shortcut_preset_powerzone_3,
                       QZSettings::default_shortcut_preset_powerzone_3)) {
        LargeButton(QStringLiteral("preset_powerzone_3"));
    } else if (matches(QZSettings::shortcut_preset_powerzone_4,
                       QZSettings::default_shortcut_preset_powerzone_4)) {
        LargeButton(QStringLiteral("preset_powerzone_4"));
    } else if (matches(QZSettings::shortcut_preset_powerzone_5,
                       QZSettings::default_shortcut_preset_powerzone_5)) {
        LargeButton(QStringLiteral("preset_powerzone_5"));
    } else if (matches(QZSettings::shortcut_preset_powerzone_6,
                       QZSettings::default_shortcut_preset_powerzone_6)) {
        LargeButton(QStringLiteral("preset_powerzone_6"));
    } else if (matches(QZSettings::shortcut_preset_powerzone_7,
                       QZSettings::default_shortcut_preset_powerzone_7)) {
        LargeButton(QStringLiteral("preset_powerzone_7"));
    } else if (matches(QZSettings::shortcut_auto_resistance, QZSettings::default_shortcut_auto_resistance)) {
        toggleAutoResistance();
    } else if (matches(QZSettings::shortcut_lap, QZSettings::default_shortcut_lap)) {
        Lap();
    } else if (matches(QZSettings::shortcut_start_stop, QZSettings::default_shortcut_start_stop)) {
        StartRequested();
    } else if (matches(QZSettings::shortcut_stop, QZSettings::default_shortcut_stop)) {
        StopRequested();
    } else {
        return false;
    }

    return true;
}

 

void homeform::Plus(const QString &name) {
    QSettings settings;

    bool miles = settings.value(QZSettings::miles_unit, QZSettings::default_miles_unit).toBool();
    qDebug() << QStringLiteral("Plus") << name;
    if (name.contains(QStringLiteral("target_speed")) || name.contains(QStringLiteral("target_pace"))) {
        if (bluetoothManager->device()) {

            if (bluetoothManager->device()->deviceType() == TREADMILL) {
                bool treadmill_difficulty_gain_or_offset =
                    settings
                        .value(QZSettings::treadmill_difficulty_gain_or_offset,
                               QZSettings::default_treadmill_difficulty_gain_or_offset)
                        .toBool();

                if (!treadmill_difficulty_gain_or_offset) {
                    bluetoothManager->device()->setDifficult(bluetoothManager->device()->difficult() + 0.03);
                    if (bluetoothManager->device()->difficult() == 0) {
                        bluetoothManager->device()->setDifficult(0.03);
                    }
                } else {
                    bluetoothManager->device()->setDifficultOffset(bluetoothManager->device()->difficultOffset() + 0.1);
                    if (bluetoothManager->device()->difficultOffset() == 0) {
                        bluetoothManager->device()->setDifficultOffset(0.1);
                    }
                }

                ((treadmill *)bluetoothManager->device())
                    ->changeSpeed(((treadmill *)bluetoothManager->device())->lastRawSpeedRequested());
            }
        }
    } else if (name.contains(QStringLiteral("target_inclination"))) {
        if (bluetoothManager->device()) {

            if (bluetoothManager->device()->deviceType() == TREADMILL) {
                bool treadmill_difficulty_gain_or_offset =
                    settings
                        .value(QZSettings::treadmill_difficulty_gain_or_offset,
                               QZSettings::default_treadmill_difficulty_gain_or_offset)
                        .toBool();

                if (!treadmill_difficulty_gain_or_offset) {
                    bluetoothManager->device()->setInclinationDifficult(
                        bluetoothManager->device()->inclinationDifficult() + 0.03);
                    if (bluetoothManager->device()->inclinationDifficult() == 0) {
                        bluetoothManager->device()->setInclinationDifficult(0.03);
                    }
                } else {
                    bluetoothManager->device()->setInclinationDifficultOffset(
                        bluetoothManager->device()->inclinationDifficultOffset() + 0.5);
                    if (bluetoothManager->device()->inclinationDifficultOffset() == 0) {
                        bluetoothManager->device()->setInclinationDifficultOffset(0.5);
                    }
                }

                ((treadmill *)bluetoothManager->device())
                    ->changeInclination(((treadmill *)bluetoothManager->device())->lastRawInclinationRequested(),
                                        ((treadmill *)bluetoothManager->device())->lastRawInclinationRequested());
            }
        }
    } else if (name.contains(QStringLiteral("speed"))) {
        if (bluetoothManager->device() && bluetoothManager->device()->deviceType() == TREADMILL) {
            // round up to the next .5 increment (.0 or .5)
            double speed = ((treadmill *)bluetoothManager->device())->currentSpeed().value();
            double requestedspeed = ((treadmill *)bluetoothManager->device())->requestedSpeed();
            double targetspeed = ((treadmill *)bluetoothManager->device())->currentTargetSpeed();
            qDebug() << QStringLiteral("Current Speed") << speed << QStringLiteral("Current Requested Speed")
                     << requestedspeed << QStringLiteral("Current Target Speed") << targetspeed;
            if (targetspeed != -1)
                speed = targetspeed;
            if (requestedspeed != -1)
                speed = requestedspeed;
            double minStepSpeed = ((treadmill *)bluetoothManager->device())->minStepSpeed();
            double step =
                settings.value(QZSettings::treadmill_step_speed, QZSettings::default_treadmill_step_speed).toDouble();
            if (!miles)
                step = ((double)qRound(step * 10.0)) / 10.0;
            if (step > minStepSpeed)
                minStepSpeed = step;
            int rest = 0;
            if (!miles)
                rest = (minStepSpeed * 10.0) - (((int)(speed * 10.0)) % (uint8_t)(minStepSpeed * 10.0));
            if (rest == 5 || rest == 0)
                speed = speed + minStepSpeed;
            else
                speed = speed + (((double)rest) / 10.0);

            ((treadmill *)bluetoothManager->device())->changeSpeed(speed);
        }
    } else if (name.contains(QStringLiteral("external_inclination"))) {
        double elite_rizer_gain =
            settings.value(QZSettings::elite_rizer_gain, QZSettings::default_elite_rizer_gain).toDouble();
        elite_rizer_gain = elite_rizer_gain + 0.1;
        settings.setValue(QZSettings::elite_rizer_gain, elite_rizer_gain);
    } else if (name.contains(QStringLiteral("inclination"))) {
        if (bluetoothManager->device()) {
            if (bluetoothManager->device()->deviceType() == TREADMILL) {
                double step =
                    settings.value(QZSettings::treadmill_step_incline, QZSettings::default_treadmill_step_incline)
                        .toDouble();
                if (step < ((treadmill *)bluetoothManager->device())->minStepInclination())
                    step = ((treadmill *)bluetoothManager->device())->minStepInclination();
                double perc = ((treadmill *)bluetoothManager->device())->currentInclination().value() + step;
                ((treadmill *)bluetoothManager->device())->changeInclination(perc, perc);
            } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
                double step =
                    settings.value(QZSettings::treadmill_step_incline, QZSettings::default_treadmill_step_incline)
                        .toDouble();
                if (step < ((elliptical *)bluetoothManager->device())->minStepInclination())
                    step = ((elliptical *)bluetoothManager->device())->minStepInclination();
                double perc = ((elliptical *)bluetoothManager->device())->currentInclination().value() + step;
                ((elliptical *)bluetoothManager->device())->changeInclination(perc, perc);
            } else if (bluetoothManager->device()->deviceType() == BIKE) {
                double step =
                    settings.value(QZSettings::treadmill_step_incline, QZSettings::default_treadmill_step_incline)
                        .toDouble();
                ((bike *)bluetoothManager->device())
                    ->changeInclination(((bike *)bluetoothManager->device())->currentInclination().value() + step,
                                        ((bike *)bluetoothManager->device())->currentInclination().value() + step);
            }
        }
    } else if (name.contains(QStringLiteral("pid_hr"))) {
        if (bluetoothManager->device()) {
            QSettings settings;
            QString zoneS =
                settings.value(QZSettings::treadmill_pid_heart_zone, QZSettings::default_treadmill_pid_heart_zone)
                    .toString();
            uint8_t zone =
                settings.value(QZSettings::treadmill_pid_heart_zone, QZSettings::default_treadmill_pid_heart_zone)
                    .toString()
                    .toUInt();

            if (!zoneS.compare(QStringLiteral("Disabled")))
                zone = 0;

            if (zone < 5) {
                zone++;
                settings.setValue(QZSettings::treadmill_pid_heart_zone, QString::number(zone));
            }

            if(trainProgram)
                trainProgram->overrideZoneHRForCurrentRow(zone);
        }
    } else if (name.contains("gears")) {
        if (bluetoothManager->device()) {
            if (bluetoothManager->device()->deviceType() == BIKE) {
                ((bike *)bluetoothManager->device())->gearUp();
            } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
                ((elliptical *)bluetoothManager->device())
                    ->setGears(((elliptical *)bluetoothManager->device())->gears() +
                               settings.value(QZSettings::gears_gain, QZSettings::default_gears_gain).toDouble());
            } else if (bluetoothManager->device()->deviceType() == ROWING) {
                ((rower *)bluetoothManager->device())
                    ->setGears(((rower *)bluetoothManager->device())->gears() +
                               settings.value(QZSettings::gears_gain, QZSettings::default_gears_gain).toDouble());
            }
        }
    } else if (name.contains(QStringLiteral("target_resistance"))) {
        if (bluetoothManager->device()) {

            if (bluetoothManager->device()->deviceType() == BIKE ||
                bluetoothManager->device()->deviceType() == ELLIPTICAL ||
                bluetoothManager->device()->deviceType() == ROWING) {

                bluetoothManager->device()->setDifficult(bluetoothManager->device()->difficult() + 0.03);
                if (bluetoothManager->device()->difficult() == 0) {
                    bluetoothManager->device()->setDifficult(0.03);
                }

                if (bluetoothManager->device()->deviceType() == BIKE) {
                    ((bike *)bluetoothManager->device())
                        ->changeResistance(((bike *)bluetoothManager->device())->currentResistance().value());
                } else if (bluetoothManager->device()->deviceType() == ROWING) {
                    ((rower *)bluetoothManager->device())
                        ->changeResistance(((rower *)bluetoothManager->device())->currentResistance().value());
                } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
                    ((elliptical *)bluetoothManager->device())
                        ->changeResistance(((elliptical *)bluetoothManager->device())->currentResistance().value());
                }
            }
        }
    } else if (name.contains(QStringLiteral("resistance")) || name.contains(QStringLiteral("peloton_resistance"))) {
        if (bluetoothManager->device()) {
            auto dev = bluetoothManager->device();
            double current = dev->currentResistance().value();
            double diff = dev->difficult();
            if (diff == 0) diff = 1.0; // safety
            resistance_t maxRes = dev->maxResistance();

            if (dev->deviceType() == BIKE) {
                double g = ((bike *)dev)->gears();
                double target = current + 1; // device-space target
                int raw = qRound((target - g) / diff);
                if (raw < 1) raw = 1;
                if (raw > maxRes) raw = maxRes;
                if (!name.contains(QStringLiteral("peloton_resistance"))) {
                    emit manualCscBikeResistanceAdjusted(raw);
                }
                ((bike *)dev)->changeResistance(raw);
            } else if (dev->deviceType() == ROWING) {
                double g = ((rower *)dev)->gears();
                double target = current + 1; // device-space target
                int raw = qRound((target - g) / diff);
                if (raw < 1) raw = 1;
                if (raw > maxRes) raw = maxRes;
                ((rower *)dev)->changeResistance(raw);
            } else if (dev->deviceType() == ELLIPTICAL) {
                double g = ((elliptical *)dev)->gears();
                double target = current + 1; // device-space target
                // elliptical::changeResistance does not use difficult(), but keep formula consistent
                int raw = qRound((target - g) / diff);
                if (raw < 1) raw = 1;
                if (raw > maxRes) raw = maxRes;
                ((elliptical *)dev)->changeResistance(raw);
            }
        }
    } else if (name.contains(QStringLiteral("target_power"))) {
        if (bluetoothManager->device()) {
            if (bluetoothManager->device()->deviceType() == BIKE) {
                m_overridePower = true;
                ((bike *)bluetoothManager->device())
                    ->changePower(((bike *)bluetoothManager->device())->lastRequestedPower().value() + powerJog);
                if (trainProgram) {
                    trainProgram->adjustPowerOffsetForTrainingProgram(powerJog);
                }
            } else if (bluetoothManager->device()->deviceType() == TREADMILL) {
                m_overridePower = true;
                ((treadmill *)bluetoothManager->device())
                    ->changePower(((treadmill *)bluetoothManager->device())->lastRequestedPower().value() + powerJog);
                if (trainProgram) {
                    trainProgram->adjustPowerOffsetForTrainingProgram(powerJog);
                }
            } else if (bluetoothManager->device()->deviceType() == ROWING) {
                m_overridePower = true;
                ((rower *)bluetoothManager->device())
                    ->changePower(((rower *)bluetoothManager->device())->lastRequestedPower().value() + powerJog);
                if (trainProgram) {
                    trainProgram->adjustPowerOffsetForTrainingProgram(powerJog);
                }
            }
        }
    } else if (name.contains(QStringLiteral("fan"))) {
        QSettings settings;
        if (bluetoothManager->device()) {
            if (settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable)
                    .toBool() &&
                settings.value(QZSettings::fitmetria_fanfit_mode, QZSettings::default_fitmetria_fanfit_mode)
                    .toString()
                    .compare(QStringLiteral("Manual"))) {
                fanOverride += 10;
            } else if (settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable)
                           .toBool()) {
                bluetoothManager->device()->changeFanSpeed(bluetoothManager->device()->fanSpeed() + 10);
            } else
                bluetoothManager->device()->changeFanSpeed(bluetoothManager->device()->fanSpeed() + 1);
        }
    } else if (name.contains(QStringLiteral("remainingtimetrainprogramrow"))) {
        if (bluetoothManager->device() && trainProgram) {
            trainProgram->increaseElapsedTime(QTime(0, 0, 0).secsTo(trainProgram->currentRowRemainingTime()));
        }
    } else if (name.contains(QStringLiteral("peloton_offset")) || name.contains(QStringLiteral("peloton_remaining"))) {
        if (bluetoothManager->device() && trainProgram) {
            trainProgram->increaseElapsedTime(1);
        }
    } else if (name.contains(QStringLiteral("target_zone"))) {
        QSettings settings;
        double currentFtp = settings.value(QZSettings::ftp, QZSettings::default_ftp).toDouble();
        if (currentFtp > 0 && bluetoothManager->device() && 
            bluetoothManager->device()->deviceType() == BIKE) {
            double currentTargetPower = ((bike *)bluetoothManager->device())->lastRequestedPower().value();
            double powerIncrement = currentFtp * 0.01; // 1% of FTP
            double newTargetPower = currentTargetPower + powerIncrement;
            ((bike *)bluetoothManager->device())->changePower(newTargetPower);
            qDebug() << "Target power increased by" << powerIncrement << "W (1% of FTP) from" << currentTargetPower << "to" << newTargetPower;
        }
    } else {
        qDebug() << name << QStringLiteral("not handled");

        qDebug() << "Plus" << name;
    }
}

void homeform::pelotonOffset_Plus() { Plus(QStringLiteral("peloton_offset")); }

void homeform::pelotonOffset_Minus() { Minus(QStringLiteral("peloton_offset")); }

void homeform::bluetoothDeviceConnected(bluetoothdevice *b) {
    this->innerTemplateManager->start(b);
    this->userTemplateManager->start(b);
#ifndef Q_OS_IOS
    // heart rate received from apple watch while QZ is running on a different device via TCP socket (iphone_socket)
    connect(this, SIGNAL(heartRate(uint8_t)), b, SLOT(heartRate(uint8_t)));
#endif
    if (auto csc = dynamic_cast<cscbike *>(b)) {
        connect(this, &homeform::manualCscBikeResistanceAdjusted, csc, &cscbike::onManualResistanceAdjusted,
                Qt::UniqueConnection);
    }
}

void homeform::bluetoothDeviceDisconnected() {
    this->innerTemplateManager->stop();
    this->userTemplateManager->stop();
}

void homeform::Minus(const QString &name) {
    QSettings settings;
    bool miles = settings.value(QZSettings::miles_unit, QZSettings::default_miles_unit).toBool();
    qDebug() << QStringLiteral("Minus") << name;
    if (name.contains(QStringLiteral("target_speed")) || name.contains(QStringLiteral("target_pace"))) {
        if (bluetoothManager->device()) {

            if (bluetoothManager->device()->deviceType() == TREADMILL) {
                bool treadmill_difficulty_gain_or_offset =
                    settings
                        .value(QZSettings::treadmill_difficulty_gain_or_offset,
                               QZSettings::default_treadmill_difficulty_gain_or_offset)
                        .toBool();

                if (!treadmill_difficulty_gain_or_offset) {
                    bluetoothManager->device()->setDifficult(bluetoothManager->device()->difficult() - 0.03);
                    if (bluetoothManager->device()->difficult() == 0) {
                        bluetoothManager->device()->setDifficult(-0.03);
                    }
                } else {
                    bluetoothManager->device()->setDifficultOffset(bluetoothManager->device()->difficultOffset() - 0.1);
                    if (bluetoothManager->device()->difficultOffset() == 0) {
                        bluetoothManager->device()->setDifficultOffset(-0.1);
                    }
                }

                ((treadmill *)bluetoothManager->device())
                    ->changeSpeed(((treadmill *)bluetoothManager->device())->lastRawSpeedRequested());
            }
        }
    } else if (name.contains(QStringLiteral("target_inclination"))) {
        if (bluetoothManager->device()) {

            if (bluetoothManager->device()->deviceType() == TREADMILL) {
                bool treadmill_difficulty_gain_or_offset =
                    settings
                        .value(QZSettings::treadmill_difficulty_gain_or_offset,
                               QZSettings::default_treadmill_difficulty_gain_or_offset)
                        .toBool();

                if (!treadmill_difficulty_gain_or_offset) {
                    bluetoothManager->device()->setInclinationDifficult(
                        bluetoothManager->device()->inclinationDifficult() - 0.03);
                    if (bluetoothManager->device()->inclinationDifficult() == 0) {
                        bluetoothManager->device()->setInclinationDifficult(-0.03);
                    }
                } else {
                    bluetoothManager->device()->setInclinationDifficultOffset(
                        bluetoothManager->device()->inclinationDifficultOffset() - 0.5);
                    if (bluetoothManager->device()->inclinationDifficultOffset() == 0) {
                        bluetoothManager->device()->setInclinationDifficult(-0.5);
                    }
                }

                ((treadmill *)bluetoothManager->device())
                    ->changeInclination(((treadmill *)bluetoothManager->device())->lastRawInclinationRequested(),
                                        ((treadmill *)bluetoothManager->device())->lastRawInclinationRequested());
            }
        }
    } else if (name.contains(QStringLiteral("speed"))) {
        if (bluetoothManager->device()) {
            if (bluetoothManager->device()->deviceType() == TREADMILL) {
                // round up to the next .5 increment (.0 or .5)
                double speed = ((treadmill *)bluetoothManager->device())->currentSpeed().value();
                double requestedspeed = ((treadmill *)bluetoothManager->device())->requestedSpeed();
                double targetspeed = ((treadmill *)bluetoothManager->device())->currentTargetSpeed();
                qDebug() << QStringLiteral("Current Speed") << speed << QStringLiteral("Current Requested Speed")
                         << requestedspeed << QStringLiteral("Current Target Speed") << targetspeed;
                if (targetspeed != -1)
                    speed = targetspeed;
                if (requestedspeed != -1 && requestedspeed < speed)
                    speed = requestedspeed;
                double minStepSpeed = ((treadmill *)bluetoothManager->device())->minStepSpeed();
                double step = settings.value(QZSettings::treadmill_step_speed, QZSettings::default_treadmill_step_speed)
                                  .toDouble();
                if (!miles)
                    step = ((double)qRound(step * 10.0)) / 10.0;
                if (step > minStepSpeed)
                    minStepSpeed = step;
                int rest = 0;
                if (!miles)
                    rest = (minStepSpeed * 10.0) - (((int)(speed * 10.0)) % (uint8_t)(minStepSpeed * 10.0));
                if (rest == 5 || rest == 0)
                    speed = speed - minStepSpeed;
                else
                    speed = speed - (((double)rest) / 10.0);
                ((treadmill *)bluetoothManager->device())->changeSpeed(speed);
            }
        }
    } else if (name.contains(QStringLiteral("external_inclination"))) {
        double elite_rizer_gain =
            settings.value(QZSettings::elite_rizer_gain, QZSettings::default_elite_rizer_gain).toDouble();
        if (elite_rizer_gain)
            elite_rizer_gain = elite_rizer_gain - 0.1;
        settings.setValue(QZSettings::elite_rizer_gain, elite_rizer_gain);
    } else if (name.contains(QStringLiteral("inclination"))) {
        if (bluetoothManager->device()) {
            if (bluetoothManager->device()->deviceType() == TREADMILL) {
                double step =
                    settings.value(QZSettings::treadmill_step_incline, QZSettings::default_treadmill_step_incline)
                        .toDouble();
                if (step < ((treadmill *)bluetoothManager->device())->minStepInclination())
                    step = ((treadmill *)bluetoothManager->device())->minStepInclination();
                double perc = ((treadmill *)bluetoothManager->device())->currentInclination().value() - step;
                ((treadmill *)bluetoothManager->device())->changeInclination(perc, perc);
            } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
                double step =
                    settings.value(QZSettings::treadmill_step_incline, QZSettings::default_treadmill_step_incline)
                        .toDouble();
                if (step < ((elliptical *)bluetoothManager->device())->minStepInclination())
                    step = ((elliptical *)bluetoothManager->device())->minStepInclination();
                double perc = ((elliptical *)bluetoothManager->device())->currentInclination().value() - step;
                ((elliptical *)bluetoothManager->device())->changeInclination(perc, perc);
            } else if (bluetoothManager->device()->deviceType() == BIKE) {
                double step =
                    settings.value(QZSettings::treadmill_step_incline, QZSettings::default_treadmill_step_incline)
                        .toDouble();
                ((bike *)bluetoothManager->device())
                    ->changeInclination(((bike *)bluetoothManager->device())->currentInclination().value() - step,
                                        ((bike *)bluetoothManager->device())->currentInclination().value() - step);
            }
        }
    } else if (name.contains(QStringLiteral("pid_hr"))) {
        if (bluetoothManager->device()) {
            QSettings settings;
            uint8_t zone =
                settings.value(QZSettings::treadmill_pid_heart_zone, QZSettings::default_treadmill_pid_heart_zone)
                    .toString()
                    .toUInt();
            if (zone > 1) {
                zone--;
                settings.setValue(QZSettings::treadmill_pid_heart_zone, QString::number(zone));
            } else {
                settings.setValue(QZSettings::treadmill_pid_heart_zone, QStringLiteral("Disabled"));
            }

            if(trainProgram)
                trainProgram->overrideZoneHRForCurrentRow(zone);
        }
    } else if (name.contains(QStringLiteral("gears"))) {
        if (bluetoothManager->device()) {
            if (bluetoothManager->device()->deviceType() == BIKE) {
                ((bike *)bluetoothManager->device())->gearDown();
            } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
                ((elliptical *)bluetoothManager->device())
                    ->setGears(((elliptical *)bluetoothManager->device())->gears() -
                               settings.value(QZSettings::gears_gain, QZSettings::default_gears_gain).toDouble());
            } else if (bluetoothManager->device()->deviceType() == ROWING) {
                ((rower *)bluetoothManager->device())
                    ->setGears(((rower *)bluetoothManager->device())->gears() -
                               settings.value(QZSettings::gears_gain, QZSettings::default_gears_gain).toDouble());
            }
        }
    } else if (name.contains(QStringLiteral("target_resistance"))) {
        if (bluetoothManager->device()) {

            if (bluetoothManager->device()->deviceType() == BIKE ||
                bluetoothManager->device()->deviceType() == ELLIPTICAL ||
                bluetoothManager->device()->deviceType() == ROWING) {

                bluetoothManager->device()->setDifficult(bluetoothManager->device()->difficult() - 0.03);
                if (bluetoothManager->device()->difficult() == 0) {
                    bluetoothManager->device()->setDifficult(-0.03);
                }

                if (bluetoothManager->device()->deviceType() == BIKE) {
                    ((bike *)bluetoothManager->device())
                        ->changeResistance(((bike *)bluetoothManager->device())->currentResistance().value());
                } else if (bluetoothManager->device()->deviceType() == ROWING) {
                    ((rower *)bluetoothManager->device())
                        ->changeResistance(((rower *)bluetoothManager->device())->currentResistance().value());
                } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
                    ((elliptical *)bluetoothManager->device())
                        ->changeResistance(((elliptical *)bluetoothManager->device())->currentResistance().value());
                }
            }
        }
    } else if (name.contains(QStringLiteral("resistance")) || name.contains(QStringLiteral("peloton_resistance"))) {
        if (bluetoothManager->device()) {
            auto dev = bluetoothManager->device();
            double current = dev->currentResistance().value();
            double diff = dev->difficult();
            if (diff == 0) diff = 1.0; // safety
            resistance_t maxRes = dev->maxResistance();

            if (dev->deviceType() == BIKE) {
                double g = ((bike *)dev)->gears();
                double target = current - 1; // device-space target
                int raw = qRound((target - g) / diff);
                if (raw < 1) raw = 1;
                if (raw > maxRes) raw = maxRes;
                if (!name.contains(QStringLiteral("peloton_resistance"))) {
                    emit manualCscBikeResistanceAdjusted(raw);
                }
                ((bike *)dev)->changeResistance(raw);
            } else if (dev->deviceType() == ROWING) {
                double g = ((rower *)dev)->gears();
                double target = current - 1; // device-space target
                int raw = qRound((target - g) / diff);
                if (raw < 1) raw = 1;
                if (raw > maxRes) raw = maxRes;
                ((rower *)dev)->changeResistance(raw);
            } else if (dev->deviceType() == ELLIPTICAL) {
                double g = ((elliptical *)dev)->gears();
                double target = current - 1; // device-space target
                // elliptical::changeResistance does not use difficult(), but keep formula consistent
                int raw = qRound((target - g) / diff);
                if (raw < 1) raw = 1;
                if (raw > maxRes) raw = maxRes;
                ((elliptical *)dev)->changeResistance(raw);
            }
        }
    } else if (name.contains(QStringLiteral("target_power"))) {
        if (bluetoothManager->device()) {
            if (bluetoothManager->device()->deviceType() == BIKE) {
                m_overridePower = true;
                ((bike *)bluetoothManager->device())
                    ->changePower(((bike *)bluetoothManager->device())->lastRequestedPower().value() - powerJog);
                if (trainProgram) {
                    trainProgram->adjustPowerOffsetForTrainingProgram(-powerJog);
                }
            } else if (bluetoothManager->device()->deviceType() == TREADMILL) {
                m_overridePower = true;
                ((treadmill *)bluetoothManager->device())
                    ->changePower(((treadmill *)bluetoothManager->device())->lastRequestedPower().value() - powerJog);
                if (trainProgram) {
                    trainProgram->adjustPowerOffsetForTrainingProgram(-powerJog);
                }
            } else if (bluetoothManager->device()->deviceType() == ROWING) {
                m_overridePower = true;
                ((rower *)bluetoothManager->device())
                    ->changePower(((rower *)bluetoothManager->device())->lastRequestedPower().value() - powerJog);
                if (trainProgram) {
                    trainProgram->adjustPowerOffsetForTrainingProgram(-powerJog);
                }
            }
        }
    } else if (name.contains(QStringLiteral("fan"))) {

        if (bluetoothManager->device()) {
            QSettings settings;
            if (settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable)
                    .toBool() &&
                settings.value(QZSettings::fitmetria_fanfit_mode, QZSettings::default_fitmetria_fanfit_mode)
                    .toString()
                    .compare(QStringLiteral("Manual"))) {
                fanOverride -= 10;
            } else if (settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable)
                           .toBool()) {
                bluetoothManager->device()->changeFanSpeed(bluetoothManager->device()->fanSpeed() - 10);
            } else
                bluetoothManager->device()->changeFanSpeed(bluetoothManager->device()->fanSpeed() - 1);
        }
    } else if (name.contains(QStringLiteral("remainingtimetrainprogramrow"))) {
        if (bluetoothManager->device() && trainProgram) {
            trainProgram->goToPreviousRow();
        }
    } else if (name.contains(QStringLiteral("peloton_offset")) || name.contains(QStringLiteral("peloton_remaining"))) {
        if (bluetoothManager->device() && trainProgram) {
            trainProgram->decreaseElapsedTime(1);
        }
    } else if (name.contains(QStringLiteral("target_zone"))) {
        QSettings settings;
        double currentFtp = settings.value(QZSettings::ftp, QZSettings::default_ftp).toDouble();
        if (currentFtp > 0 && bluetoothManager->device() && 
            bluetoothManager->device()->deviceType() == BIKE) {
            double currentTargetPower = ((bike *)bluetoothManager->device())->lastRequestedPower().value();
            double powerDecrement = currentFtp * 0.01; // 1% of FTP
            double newTargetPower = currentTargetPower - powerDecrement;
            if (newTargetPower < 0) newTargetPower = 0; // Prevent negative power
            ((bike *)bluetoothManager->device())->changePower(newTargetPower);
            qDebug() << "Target power decreased by" << powerDecrement << "W (1% of FTP) from" << currentTargetPower << "to" << newTargetPower;
        }
    } else {
        qDebug() << name << QStringLiteral("not handled");
        qDebug() << "Minus" << name;
    }
}

void homeform::Start() { Start_inner(true); }

void homeform::Start_inner(bool send_event_to_device) {
    QSettings settings;
    qDebug() << QStringLiteral("Start pressed - paused") << paused << QStringLiteral("stopped") << stopped;

    m_overridePower = false;

    if (settings.value(QZSettings::tts_enabled, QZSettings::default_tts_enabled).toBool())
        m_speech.say("Start pressed");

    if (!paused && !stopped) {
        paused = true;
        if (bluetoothManager->device() && send_event_to_device) {
            bluetoothManager->device()->stop(paused);
        }
        emit workoutEventStateChanged(bluetoothdevice::PAUSED);
        // Pause Video if running and visible
        if ((trainProgram) && (videoVisible() == true)) {
            QObject *rootObject = engine->rootObjects().constFirst();
            auto *videoPlaybackHalf = rootObject->findChild<QObject *>(QStringLiteral("videoplaybackhalf"));
            auto videoPlaybackHalfPlayer = qvariant_cast<QMediaPlayer *>(videoPlaybackHalf->property("mediaObject"));
            videoPlaybackHalfPlayer->pause();
        }
    } else {
#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
        if(h && !h->appleWatchAppInstalled() && bluetoothManager->device())
            h->startWorkout(bluetoothManager->device()->deviceType());
#endif
#endif
        if (bluetoothManager->device() && send_event_to_device) {
            bluetoothManager->device()->start();
        }

        if (stopped) {
            trainProgram->restart();
            if (bluetoothManager->device()) {

                bluetoothManager->device()->clearStats();
            }
            Session.clear();

#ifdef Q_OS_IOS
            // due to #857
            if (!settings
                     .value(QZSettings::peloton_companion_workout_ocr,
                            QZSettings::default_companion_peloton_workout_ocr)
                     .toBool())
                this->innerTemplateManager->start(bluetoothManager->device());
#endif

            stravaPelotonActivityName = QLatin1String("");
            stravaPelotonInstructorName = QLatin1String("");
            movieFileName = QLatin1String("");
            stravaWorkoutName = QLatin1String("");
            emit workoutNameChanged(workoutName());
            emit instructorNameChanged(instructorName());
            emit workoutEventStateChanged(bluetoothdevice::STARTED);
        } else {
            // if loading a training program (gpx or xml) directly from the startup of QZ, there is no way to start
            // the program otherwise
            if (!trainProgram->isStarted()) {
                qDebug() << QStringLiteral("starting training program from a resume");
                trainProgram->restart();
            }
            emit workoutEventStateChanged(bluetoothdevice::RESUMED);
            // Resume Video if visible
            if ((trainProgram) && (videoVisible() == true)) {
                QObject *rootObject = engine->rootObjects().constFirst();
                auto *videoPlaybackHalf = rootObject->findChild<QObject *>(QStringLiteral("videoplaybackhalf"));
                auto videoPlaybackHalfPlayer =
                    qvariant_cast<QMediaPlayer *>(videoPlaybackHalf->property("mediaObject"));
                videoPlaybackHalfPlayer->play();
            }
        }

        paused = false;
        stopped = false;
    }

    if (settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {

        emit stopIconChanged(stopIcon());
        emit stopTextChanged(stopText());
        emit stopColorChanged(stopColor());
        emit startIconChanged(startIcon());
        emit startTextChanged(startText());
        emit startColorChanged(startColor());
    }

    if (bluetoothManager->device()) {
        bluetoothManager->device()->setPaused(paused | stopped);
    }
}

void homeform::StartFromDevice() {
    qDebug() << QStringLiteral("Physical start button pressed on device");
    Start_inner(false);  // false = don't send command back to device (it already started)
}

void homeform::PauseFromDevice() {
    qDebug() << QStringLiteral("Physical pause button pressed on device");
    Start_inner(false);  // false = don't send command back to device
}

void homeform::StopFromDevice() {
    qDebug() << QStringLiteral("Physical stop button pressed on device - stopping app");
    Stop();
}

void homeform::StartRequested() {
    Start();
    m_stopRequested = false;
    m_startRequested = true;
    emit stopRequestedChanged(m_stopRequested);
    emit startRequestedChanged(m_startRequested);
}

void homeform::StopRequested() {
    m_startRequested = false;
    m_stopRequested = true;
    emit startRequestedChanged(m_startRequested);
    emit stopRequestedChanged(m_stopRequested);
}

void homeform::StopFromTrainProgram(bool paused) {
    Stop();
}

void homeform::Stop() {
    QSettings settings;

    m_startRequested = false;

#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
    if(h && !h->appleWatchAppInstalled())
        h->stopWorkout();
    // End iOS Live Activity when workout stops
    ios_liveactivity::endLiveActivity();
#endif
#endif

    qDebug() << QStringLiteral("Stop pressed - paused") << paused << QStringLiteral("stopped") << stopped;

    if (stopped) {
        qDebug() << QStringLiteral("Stop pressed - already pressed, ignoring...");
        return;
    }

    if (bluetoothManager->device()) {
        if (bluetoothManager->device()->deviceType() == TREADMILL) {
            QTime zero(0, 0, 0, 0);
            if (bluetoothManager->device()->currentSpeed().value() == 0.0 &&
                zero.secsTo(bluetoothManager->device()->elapsedTime()) == 0) {
                qDebug() << QStringLiteral("Stop pressed - nothing to do. Elapsed time is 0 and current speed is 0");
                return;
            }
        }
    }

#ifdef Q_OS_IOS
    // due to #857
    if (!settings.value(QZSettings::peloton_companion_workout_ocr, QZSettings::default_companion_peloton_workout_ocr)
             .toBool())
        this->innerTemplateManager->reinit();
#endif

    if (settings.value(QZSettings::tts_enabled, QZSettings::default_tts_enabled).toBool())
        m_speech.say("Stop pressed");

    if (bluetoothManager->device()) {
        bluetoothManager->device()->stop(false);
    }

    paused = false;
    stopped = true;

    emit workoutEventStateChanged(bluetoothdevice::STOPPED);

    saveSessionAsTrainingProgram();

    if (bluetoothManager->device()) {
        bluetoothManager->device()->setPaused(paused | stopped);
    }

    if (settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {

        emit stopIconChanged(stopIcon());
        emit stopTextChanged(stopText());
        emit stopColorChanged(stopColor());
        emit startIconChanged(startIcon());
        emit startTextChanged(startText());
        emit startColorChanged(startColor());

        // clearing the label on top because if it was running a training program, with stop the program will be terminated
        m_info = workoutName();
        emit infoChanged(m_info);
    }

    if (trainProgram) {
        trainProgram->clearRows();
    }

    if (!m_activeClipboardWorkoutFile.isEmpty() &&
        settings.value(QZSettings::trainprogram_clipboard_workout_enabled,
                       QZSettings::default_trainprogram_clipboard_workout_enabled).toBool()) {
        setClipboardWorkoutDeletePromptRequested(true);
    }
}

void homeform::Lap() {
    qDebug() << QStringLiteral("lap pressed");
    if (bluetoothManager) {
        if (bluetoothManager->device()) {

            bluetoothManager->device()->setLap();
            lapTrigger = true;
            if (trainProgram) {
                trainProgram->advanceLapButtonStep();
            }
        }
    }
}

bool homeform::labelHelp() { return m_labelHelp; }

QString homeform::stopText() {

    QSettings settings;
    if (settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {
        return tr("Stop");
    }
    return QLatin1String("");
}

QString homeform::stopIcon() { return QStringLiteral("icons/icons/stop.png"); }

QString homeform::startText() {

    QSettings settings;
    if (settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {
        if (paused || stopped) {
            return tr("Start");
        } else {
            return tr("Pause");
        }
    }
    return QLatin1String("");
}

QString homeform::startIcon() {

    QSettings settings;
    if (settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {
        if (paused || stopped) {
            return QStringLiteral("icons/icons/start.png");
        } else {
            return QStringLiteral("icons/icons/pause.png");
        }
    }
    return QLatin1String("");
}

void homeform::updateGearsValue() {
    QSettings settings;
    bool gears_zwift_ratio = settings.value(QZSettings::gears_zwift_ratio, QZSettings::default_gears_zwift_ratio).toBool();
    bool gears_custom_table_enabled = settings.value(QZSettings::gears_custom_table_enabled, QZSettings::default_gears_custom_table_enabled).toBool();
    bool zwift_gear_ui_aligned = settings.value(QZSettings::zwift_gear_ui_aligned, QZSettings::default_zwift_gear_ui_aligned).toBool();
    double gear = ((bike *)bluetoothManager->device())->gears();
    double maxGearDefault = ((bike *)bluetoothManager->device())->defaultMaxGears();
    double maxGear = ((bike *)bluetoothManager->device())->maxGears();
    if(zwift_gear_ui_aligned && bluetoothManager && bluetoothManager->device() && ((bike *)bluetoothManager->device())->VirtualBike())
        gear = ((bike *)bluetoothManager->device())->VirtualBike()->currentGear();
    if (gears_custom_table_enabled) {
        this->gears->setValue(QString::number(gear));
        if (((bike *)bluetoothManager->device())->gearsAbsoluteMode()) {
            // The table holds resistance levels here, so the useful second line is the
            // level this gear is asking for, not its distance from neutral.
            this->gears->setSecondLine(
                QStringLiteral("res ") +
                QString::number(((bike *)bluetoothManager->device())->gearsNeutralResistance() +
                                    ((bike *)bluetoothManager->device())->gearsModifier(),
                                'f', 0));
        } else {
            this->gears->setSecondLine(QStringLiteral("offset ") + QString::number(((bike *)bluetoothManager->device())->gearsModifier(), 'f', 1));
        }
    } else if (settings.value(QZSettings::gears_gain, QZSettings::default_gears_gain).toDouble() == 1.0 || gears_zwift_ratio || maxGear < maxGearDefault) {
        this->gears->setValue(QString::number(gear));
        this->gears->setSecondLine(wheelCircumference::gearsInfo(gear));
    } else {
        this->gears->setValue(QString::number(gear, 'f', 1));
        this->gears->setSecondLine(QStringLiteral(""));
    }
}

QString homeform::signal() {
    if (!bluetoothManager) {
        return QStringLiteral("icons/icons/signal-1.png");
    }

    if (!bluetoothManager->device()) {
        return QStringLiteral("icons/icons/signal-1.png");
    }

    int16_t rssi = bluetoothManager->device()->bluetoothDevice.rssi();
    if (rssi > -40) {
        return QStringLiteral("icons/icons/signal-3.png");
    } else if (rssi > -60) {
        return QStringLiteral("icons/icons/signal-2.png");
    }

    return QStringLiteral("icons/icons/signal-1.png");
}

void homeform::updateRtssOsd() {
    bluetoothdevice *device = bluetoothManager ? bluetoothManager->device() : nullptr;
    if (!device) {
        rtssOsd.publish(QStringLiteral("QZ: no device"));
        return;
    }

    QSettings settings;
    const bool erg = settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool();

    // Only bikes and rowers carry a gear; everything else just gets the rest.
    QString gearLine;
    if (device->deviceType() == BIKE) {
        gearLine = QStringLiteral("Gear: %1\n").arg(((bike *)device)->gears());
    } else if (device->deviceType() == ROWING) {
        gearLine = QStringLiteral("Gear: %1\n").arg(((rower *)device)->gears());
    }

    rtssOsd.publish(gearLine + QStringLiteral("ERG: %1\nResistance: %2")
                                   .arg(erg ? QStringLiteral("ON") : QStringLiteral("OFF"))
                                   .arg(device->currentResistance().value()));
}

void homeform::update() {

    QSettings settings;
    double currentHRZone = 1;
    double ftpZone = 1;

    // Timer jitter detection (same logic as trainprogram::scheduler)
    QDateTime now = QDateTime::currentDateTime();
    qint64 msecsElapsed = lastUpdateCall.msecsTo(now);
    
    // Reset jitter if it's getting too large
    if (qAbs(currentUpdateJitter) > 5000) {
        currentUpdateJitter = 0;
    }
    
    currentUpdateJitter += msecsElapsed - 1000;
    lastUpdateCall = now;

    qDebug() << "homeform::update fired!" << "elapsed:" << msecsElapsed << "jitter:" << currentUpdateJitter;

    if (settings.status() != QSettings::NoError) {
        qDebug() << "!!!!QSETTINGS ERROR!" << settings.status();
    }

    if ((paused || stopped) &&
        settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {

        emit stopIconChanged(stopIcon());
        emit stopTextChanged(stopText());
        emit startIconChanged(startIcon());
        emit startTextChanged(startText());
        emit startColorChanged(startColor());
        emit stopColorChanged(stopColor());
    }

    if (bluetoothManager->device()) {

        double inclination = 0;
        double resistance = 0;
        double watts = 0;
        double pace = 0;
        double peloton_resistance = 0;
        uint8_t cadence = 0;
        uint32_t totalStrokes = 0;
        double avgStrokesRate = 0;
        double maxStrokesRate = 0;
        double avgStrokesLength = 0;
        double strideLength = 0;
        double groundContact = 0;
        double verticalOscillation = 0;
        double stepCount = 0;

        bool miles = settings.value(QZSettings::miles_unit, QZSettings::default_miles_unit).toBool();
        bool weight_kg_unit = settings.value(QZSettings::weight_kg_unit, QZSettings::default_weight_kg_unit).toBool();
        double ftpSetting = settings.value(QZSettings::ftp, QZSettings::default_ftp).toDouble();
        const bool tileWattColorEnabled =
            settings.value(QZSettings::tile_watt_color_enabled, QZSettings::default_tile_watt_color_enabled).toBool();
        const bool tilePaceColorEnabled =
            settings.value(QZSettings::tile_pace_color_enabled, QZSettings::default_tile_pace_color_enabled).toBool();
        const auto setWattValueFontColor = [this, tileWattColorEnabled](const QString &color) {
            this->watt->setValueFontColor(tileWattColorEnabled ? color : QStringLiteral("white"));
        };
        const auto setPaceValueFontColor = [this, tilePaceColorEnabled](const QString &color) {
            this->pace->setValueFontColor(tilePaceColorEnabled ? color : QStringLiteral("white"));
        };
        if (!tileWattColorEnabled) {
            setWattValueFontColor(QStringLiteral("white"));
        }
        if (!tilePaceColorEnabled) {
            setPaceValueFontColor(QStringLiteral("white"));
        }
        double unit_conversion = 1.0;
        double meter_feet_conversion = 1.0;
        double cm_inches_conversion = 1.0;        
        uint8_t treadmill_pid_heart_zone =
            settings.value(QZSettings::treadmill_pid_heart_zone, QZSettings::default_treadmill_pid_heart_zone)
                .toString()
                .toUInt();
        QString treadmill_pid_heart_zone_string =
            settings.value(QZSettings::treadmill_pid_heart_zone, QZSettings::default_treadmill_pid_heart_zone)
                .toString();
        if (!treadmill_pid_heart_zone_string.compare(QStringLiteral("Disabled")))
            treadmill_pid_heart_zone = 0;
        if (trainProgram && trainProgram->currentRow().zoneHR >= 0)
            treadmill_pid_heart_zone = trainProgram->currentRow().zoneHR;

        if (miles) {
            unit_conversion = 0.621371;
            meter_feet_conversion = 3.28084;
            cm_inches_conversion = 0.393701;
        }

        // Get the time spent in each zone
        uint32_t seconds_zone1 = bluetoothManager->device()->secondsForHeartZone(0);
        uint32_t seconds_zone2 = bluetoothManager->device()->secondsForHeartZone(1);
        uint32_t seconds_zone3 = bluetoothManager->device()->secondsForHeartZone(2);
        uint32_t seconds_zone4 = bluetoothManager->device()->secondsForHeartZone(3);
        uint32_t seconds_zone5 = bluetoothManager->device()->secondsForHeartZone(4);

        // Check if individual mode is enabled
        bool individual_mode = settings.value(QZSettings::tile_hr_time_in_zone_individual_mode, QZSettings::default_tile_hr_time_in_zone_individual_mode).toBool();

        uint32_t display_zone1, display_zone2, display_zone3, display_zone4, display_zone5;
        
        if (individual_mode) {
            // Individual mode: show only time in specific zone
            display_zone1 = seconds_zone1;
            display_zone2 = seconds_zone2;
            display_zone3 = seconds_zone3;
            display_zone4 = seconds_zone4;
            display_zone5 = seconds_zone5;
        } else {
            // Progressive mode: show cumulative time (time in this zone or higher)
            display_zone1 = seconds_zone1 + seconds_zone2 + seconds_zone3 + seconds_zone4 + seconds_zone5;
            display_zone2 = seconds_zone2 + seconds_zone3 + seconds_zone4 + seconds_zone5;
            display_zone3 = seconds_zone3 + seconds_zone4 + seconds_zone5;
            display_zone4 = seconds_zone4 + seconds_zone5;
            display_zone5 = seconds_zone5;
        }

        // Update labels based on mode
        if (individual_mode) {
            // Individual mode: show specific zone labels
            tile_hr_time_in_zone_1->setName(QStringLiteral("HR Zone 1"));
            tile_hr_time_in_zone_2->setName(QStringLiteral("HR Zone 2"));
            tile_hr_time_in_zone_3->setName(QStringLiteral("HR Zone 3"));
            tile_hr_time_in_zone_4->setName(QStringLiteral("HR Zone 4"));
            tile_hr_time_in_zone_5->setName(QStringLiteral("HR Zone 5"));
        } else {
            // Progressive mode: show cumulative zone labels
            tile_hr_time_in_zone_1->setName(QStringLiteral("HR Zone 1+"));
            tile_hr_time_in_zone_2->setName(QStringLiteral("HR Zone 2+"));
            tile_hr_time_in_zone_3->setName(QStringLiteral("HR Zone 3+"));
            tile_hr_time_in_zone_4->setName(QStringLiteral("HR Zone 4+"));
            tile_hr_time_in_zone_5->setName(QStringLiteral("HR Zone 5"));
        }

        // Update the UI for each tile
        tile_hr_time_in_zone_1->setValue(QTime(0, 0, 0).addSecs(display_zone1).toString("h:mm:ss"));
        tile_hr_time_in_zone_2->setValue(QTime(0, 0, 0).addSecs(display_zone2).toString("h:mm:ss"));
        tile_hr_time_in_zone_3->setValue(QTime(0, 0, 0).addSecs(display_zone3).toString("h:mm:ss"));
        tile_hr_time_in_zone_4->setValue(QTime(0, 0, 0).addSecs(display_zone4).toString("h:mm:ss"));
        tile_hr_time_in_zone_5->setValue(QTime(0, 0, 0).addSecs(display_zone5).toString("h:mm:ss"));

               // Set colors based on the zone
        tile_hr_time_in_zone_1->setValueFontColor(QStringLiteral("lightsteelblue"));
        tile_hr_time_in_zone_2->setValueFontColor(QStringLiteral("green"));
        tile_hr_time_in_zone_3->setValueFontColor(QStringLiteral("yellow"));
        tile_hr_time_in_zone_4->setValueFontColor(QStringLiteral("orange"));
        tile_hr_time_in_zone_5->setValueFontColor(QStringLiteral("red"));

        // Get the time spent in each heat zone
        uint32_t heat_seconds_zone1 = bluetoothManager->device()->secondsForHeatZone(0);
        uint32_t heat_seconds_zone2 = bluetoothManager->device()->secondsForHeatZone(1);
        uint32_t heat_seconds_zone3 = bluetoothManager->device()->secondsForHeatZone(2);
        uint32_t heat_seconds_zone4 = bluetoothManager->device()->secondsForHeatZone(3);

        // Calculate cumulative times (time in this heat zone or higher)
        //uint32_t heat_seconds_zone1_plus = heat_seconds_zone1 + heat_seconds_zone2 + heat_seconds_zone3 + heat_seconds_zone4;
        //uint32_t heat_seconds_zone2_plus = heat_seconds_zone2 + heat_seconds_zone3 + heat_seconds_zone4;
        //uint32_t heat_seconds_zone3_plus = heat_seconds_zone3 + heat_seconds_zone4;
        //uint32_t heat_seconds_zone4_plus = heat_seconds_zone4; // Zone 4 is already just zone 4

        // Update the UI for each heat tile
        tile_heat_time_in_zone_1->setValue(QTime(0, 0, 0).addSecs(heat_seconds_zone1).toString("h:mm:ss"));
        tile_heat_time_in_zone_2->setValue(QTime(0, 0, 0).addSecs(heat_seconds_zone2).toString("h:mm:ss"));
        tile_heat_time_in_zone_3->setValue(QTime(0, 0, 0).addSecs(heat_seconds_zone3).toString("h:mm:ss"));
        tile_heat_time_in_zone_4->setValue(QTime(0, 0, 0).addSecs(heat_seconds_zone4).toString("h:mm:ss"));

        // Set colors based on the heat zone
        tile_heat_time_in_zone_1->setValueFontColor(QStringLiteral("lightblue"));
        tile_heat_time_in_zone_2->setValueFontColor(QStringLiteral("yellow"));
        tile_heat_time_in_zone_3->setValueFontColor(QStringLiteral("orange"));
        tile_heat_time_in_zone_4->setValueFontColor(QStringLiteral("red"));

        emit signalChanged(signal());
        emit currentSpeedChanged(bluetoothManager->device()->currentSpeed().value());
        speed->setValue(QString::number(bluetoothManager->device()->currentSpeed().value() * unit_conversion, 'f', 1));
        speed->setSecondLine(
            QStringLiteral("AVG: ") +
            QString::number((bluetoothManager->device())->currentSpeed().average() * unit_conversion, 'f', 1) +
            QStringLiteral(" MAX: ") +
            QString::number((bluetoothManager->device())->currentSpeed().max() * unit_conversion, 'f', 1));
        // Heart rate display - show as percentage if enabled
        if (settings.value(QZSettings::tile_heart_show_as_percent, QZSettings::default_tile_heart_show_as_percent).toBool()) {
            double currentHR = bluetoothManager->device()->currentHeart().value();
            double maxHR = heartRateMax();
            double hrPercent = (currentHR / maxHR) * 100.0;
            heart->setValue(QString::number(hrPercent, 'f', 0) + "%");
        } else {
            heart->setValue(QString::number(bluetoothManager->device()->currentHeart().value(), 'f', 0));
        }
        hrv->setValue(QString::number(bluetoothManager->device()->currentHRV().value(), 'f', 2));
        hrv->setSecondLine(QStringLiteral("AVG: ") +
                          QString::number(bluetoothManager->device()->currentHRV().average(), 'f', 2));
      

        bool activeOnly = settings.value(QZSettings::calories_active_only, QZSettings::default_calories_active_only).toBool();
        calories->setValue(QString::number(bluetoothManager->device()->calories().value(), 'f', 0));
        double caloriesPerMinute =
            (activeOnly ? bluetoothManager->device()->activeCalories().rate1s()
                        : bluetoothManager->device()->calories().rate1s()) *
            60.0;
        if (caloriesPerMinute < 0)
            caloriesPerMinute = 0;
        calories->setSecondLine(QString::number(caloriesPerMinute, 'f', 1) + " /min");
        if (!settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable).toBool())
            fan->setValue(QString::number(bluetoothManager->device()->fanSpeed()));
        else
            fan->setValue(QString::number(qRound(((double)bluetoothManager->device()->fanSpeed()) / 10.0) * 10.0));
        jouls->setValue(QString::number(bluetoothManager->device()->jouls().value() / 1000.0, 'f', 1));
        jouls->setSecondLine(QString::number(bluetoothManager->device()->jouls().rate1s() / 1000.0 * 60.0, 'f', 1) +
                             " /min");
        elapsed->setValue(bluetoothManager->device()->elapsedTime().toString(QStringLiteral("h:mm:ss")));
        moving_time->setValue(bluetoothManager->device()->movingTime().toString(QStringLiteral("h:mm:ss")));

        coreTemperature->setValue(QString::number(bluetoothManager->device()->CoreBodyTemperature.value(), 'f', 1) + "C");
        coreTemperature->setSecondLine(QString::number(bluetoothManager->device()->SkinTemperature.value(), 'f', 1) + "C HSI:" + QString::number(bluetoothManager->device()->HeatStrainIndex.value(), 'f', 1));
        
        // Update heat zone based on Heat Strain Index
        bluetoothManager->device()->setHeatZone(bluetoothManager->device()->HeatStrainIndex.value());

        if (trainProgram) {
            // sync the video with the zwo workout file
            if (videoVisible() == true && !bluetoothManager->device()->currentCordinate().isValid()) {
                QObject *rootObject = engine->rootObjects().constFirst();
                auto *videoPlaybackHalf = rootObject->findChild<QObject *>(QStringLiteral("videoplaybackhalf"));
                auto videoPlaybackHalfPlayer =
                    qvariant_cast<QMediaPlayer *>(videoPlaybackHalf->property("mediaObject"));
                double videoTimeStampSeconds = (double)videoPlaybackHalfPlayer->position() / 1000.0;
                QTime videoCurrent = QTime(0, 0, videoTimeStampSeconds);
                int delta = trainProgram->totalElapsedTime().secsTo(videoCurrent);
                if (qAbs(delta) > 1) {
                    videoPlaybackHalfPlayer->setPosition(QTime(0, 0, 0).secsTo(trainProgram->totalElapsedTime()) *
                                                         1000.0);
                }
            }

            peloton_offset->setValue(QString::number(trainProgram->offsetElapsedTime()) + QStringLiteral(" sec."));
            peloton_remaining->setValue(trainProgram->remainingTime().toString("h:mm:ss"));
            peloton_remaining->setSecondLine(QString::number(trainProgram->offsetElapsedTime()) +
                                             QStringLiteral(" sec."));
            remaningTimeTrainingProgramCurrentRow->setValue(
                trainProgram->currentRowRemainingTime().toString(QStringLiteral("h:mm:ss")));
            remaningTimeTrainingProgramCurrentRow->setSecondLine(
                trainProgram->currentRowElapsedTime().toString(QStringLiteral("h:mm:ss")) +
                QStringLiteral(" (") + QString::number(trainProgram->currentLogicalStep()) +
                QStringLiteral("/") + QString::number(trainProgram->totalLogicalSteps()) +
                QStringLiteral(")"));
            targetMets->setValue(QString::number(trainProgram->currentTargetMets(), 'f', 1));
            trainrow next = trainProgram->getRowFromCurrent(1);
            trainrow next_1 = trainProgram->getRowFromCurrent(2);
            if (next.duration.second() != 0 || next.duration.minute() != 0 || next.duration.hour() != 0 ||
                next.distance != -1 || next.waitForLap || next.HRabove > 0 || next.HRbelow > 0) {
                QString duration = next.duration.toString(QStringLiteral("mm:ss"));
                if(next.distance != -1) {
                    duration = QString::number(next.distance, 'f' , 1);
                } else if (next.waitForLap) {
                    duration = QStringLiteral("Lap");
                } else if (next.HRabove > 0) {
                    duration = QStringLiteral(">") + QString::number(next.HRabove) + QStringLiteral(" bpm");
                } else if (next.HRbelow > 0) {
                    duration = QStringLiteral("<") + QString::number(next.HRbelow) + QStringLiteral(" bpm");
                }
                if (next.requested_peloton_resistance != -1) {
                    nextRows->setValue(QStringLiteral("PR") + QString::number(next.requested_peloton_resistance));
                    nextRows->setSecondLine(duration);
                } else if (next.resistance != -1) {
                    nextRows->setValue(QStringLiteral("R") + QString::number(next.resistance));
                    nextRows->setSecondLine(duration);
                } else if (next.zoneHR != -1) {
                    nextRows->setValue(QStringLiteral("HR") + QString::number(next.zoneHR));
                    nextRows->setSecondLine(duration);
                } else if (next.HRmin != -1 && next.HRmax != -1) {
                    nextRows->setValue(QStringLiteral("HR") + QString::number(next.HRmin) + QStringLiteral("-") +
                                       QString::number(next.HRmax));
                    nextRows->setSecondLine(duration);
                } else if (next.speed != -1 && next.inclination != -200) {
                    nextRows->setValue(QStringLiteral("S") + QString::number(next.speed, 'f' , 1) + QStringLiteral("I") +
                                       QString::number(next.inclination, 'f' , 1));
                    nextRows->setSecondLine(duration);
                } else if (next.speed != -1) {
                    nextRows->setValue(QStringLiteral("S") + QString::number(next.speed, 'f' , 1));
                    nextRows->setSecondLine(duration);
                } else if (next.inclination != -200) {
                    nextRows->setValue(QStringLiteral("I") + QString::number(next.inclination, 'f' , 1));
                    nextRows->setSecondLine(duration);
                } else if (next.power != -1) {
                    double ftpPerc = (next.power / ftpSetting) * 100.0;
                    uint8_t ftpZone = 1;
                    if (ftpPerc < 56) {
                        ftpZone = 1;
                    } else if (ftpPerc < 76) {
                        ftpZone = 2;
                    } else if (ftpPerc < 91) {
                        ftpZone = 3;
                    } else if (ftpPerc < 106) {
                        ftpZone = 4;
                    } else if (ftpPerc < 121) {
                        ftpZone = 5;
                    } else if (ftpPerc < 151) {
                        ftpZone = 6;
                    } else {
                        ftpZone = 7;
                    }
                    nextRows->setValue(QStringLiteral("Z") + QString::number(ftpZone) + QStringLiteral(" ") +
                                       duration);
                    if (next_1.duration.second() != 0 || next_1.duration.minute() != 0 || next_1.duration.hour() != 0 || next_1.distance != -1) {
                        QString duration_1 = next_1.duration.toString(QStringLiteral("mm:ss"));
                        if(next_1.distance != -1) {
                            duration_1 = QString::number(next_1.distance, 'f' , 1);
                        }
                        if (next_1.requested_peloton_resistance != -1)
                            nextRows->setSecondLine(
                                QStringLiteral("PR") + QString::number(next_1.requested_peloton_resistance) +
                                QStringLiteral(" ") + duration_1);
                        else if (next_1.resistance != -1)
                            nextRows->setSecondLine(QStringLiteral("R") + QString::number(next_1.resistance) +
                                                    QStringLiteral(" ") +
                                                    duration_1);
                        else if (next_1.power != -1) {
                            double ftpPerc = (next_1.power / ftpSetting) * 100.0;
                            uint8_t ftpZone = 1;
                            if (ftpPerc < 56) {
                                ftpZone = 1;
                            } else if (ftpPerc < 76) {
                                ftpZone = 2;
                            } else if (ftpPerc < 91) {
                                ftpZone = 3;
                            } else if (ftpPerc < 106) {
                                ftpZone = 4;
                            } else if (ftpPerc < 121) {
                                ftpZone = 5;
                            } else if (ftpPerc < 151) {
                                ftpZone = 6;
                            } else {
                                ftpZone = 7;
                            }
                            nextRows->setSecondLine(QStringLiteral("Z") + QString::number(ftpZone) +
                                                    QStringLiteral(" ") +
                                                    duration_1);
                        }
                    } else {
                        nextRows->setSecondLine(QStringLiteral("N/A"));
                    }
                }
            } else {
                nextRows->setValue(QStringLiteral("N/A"));
            }
        }
        mets->setValue(QString::number(bluetoothManager->device()->currentMETS().value(), 'f', 1));
        mets->setSecondLine(
            QStringLiteral("AVG: ") + QString::number(bluetoothManager->device()->currentMETS().average(), 'f', 1) +
            QStringLiteral("MAX: ") + QString::number(bluetoothManager->device()->currentMETS().max(), 'f', 1));
        lapElapsed->setValue(bluetoothManager->device()->lapElapsedTime().toString(QStringLiteral("h:mm:ss")));
        lapElapsed->setSecondLine(QString::number(bluetoothManager->device()->lapOdometer() * unit_conversion, 'f', 2));
        avgWatt->setValue(QString::number(bluetoothManager->device()->wattsMetric().average(), 'f', 0));
        avgWattLap->setValue(QString::number(bluetoothManager->device()->wattsMetric().lapAverage(), 'f', 0));
        wattKg->setValue(QString::number(bluetoothManager->device()->wattKg().value(), 'f', 1));
        wattKg->setSecondLine(
            QStringLiteral("AVG: ") + QString::number(bluetoothManager->device()->wattKg().average(), 'f', 1) +
            QStringLiteral("MAX: ") + QString::number(bluetoothManager->device()->wattKg().max(), 'f', 1));
        QLocale locale = QLocale::system();

        // Format the time based on the locale
        QString timeFormat = locale.timeFormat(QLocale::ShortFormat);
        bool usesAMPMFormat = timeFormat.toUpper().contains("A");
        QDateTime currentTime = QDateTime::currentDateTime();

        QString formattedTime;
        if (usesAMPMFormat) {
            // The locale uses 12-hour format with AM/PM
            formattedTime = currentTime.toString("h:mm:ss AP");
        } else {
            // The locale uses 24-hour format
            formattedTime = currentTime.toString("H:mm:ss");
        }
        datetime->setValue(formattedTime);
        watts = bluetoothManager->device()->wattsMetricforUI();
        watt->setValue(QString::number(watts, 'f', 0));
        weightLoss->setValue(QString::number((miles && !weight_kg_unit) ? bluetoothManager->device()->weightLoss() * 35.274
                                                   : bluetoothManager->device()->weightLoss(),
                                             'f', 2));

        cadence = bluetoothManager->device()->currentCadence().value();
        this->cadence->setValue(QString::number(cadence));
        this->cadence->setSecondLine(
            QStringLiteral("AVG: ") +
            QString::number(((bike *)bluetoothManager->device())->currentCadence().average(), 'f', 0) +
            QStringLiteral(" MAX: ") +
            QString::number(((bike *)bluetoothManager->device())->currentCadence().max(), 'f', 0));


#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
        if (settings.value(QZSettings::volume_change_gears, QZSettings::default_volume_change_gears).toBool()) {
            lockscreen h;
            static double volumeLast = -1;
            double currentVolume = h.getVolume() * 10.0;
            qDebug() << "volume" << volumeLast << currentVolume;
            QSettings settings;
            bool gears_volume_debouncing = settings.value(QZSettings::gears_volume_debouncing, QZSettings::default_gears_volume_debouncing).toBool();
            if (volumeLast == -1)
                qDebug() << "volume init";
            else if (volumeLast > currentVolume) {
                double diff = volumeLast - currentVolume;
                for (int i = 0; i < diff; i++) {
                    Minus(QStringLiteral("gears"));
                    if(gears_volume_debouncing) {
                        i = diff;
                        break;
                    }
                }
            } else if (volumeLast < currentVolume) {
                double diff = currentVolume - volumeLast;
                for (int i = 0; i < diff; i++) {
                    Plus(QStringLiteral("gears"));
                    if(gears_volume_debouncing) {
                        i = diff;
                        break;
                    }
                }
            }
            volumeLast = currentVolume;
        }
#endif
#endif

        if (bluetoothManager->device()->deviceType() == TREADMILL) {
            double _rss = ((treadmill *)bluetoothManager->device())->runningStressScore();
            odometer->setValue(QString::number(bluetoothManager->device()->odometer() * unit_conversion, 'f', 2));
            if (bluetoothManager->device()->currentSpeed().value()) {
                pace = 10000 / (((treadmill *)bluetoothManager->device())->currentPace().second() +
                                (((treadmill *)bluetoothManager->device())->currentPace().minute() * 60));
                if (pace < 0) {
                    pace = 0;
                }
            } else {

                pace = 0;
            }
            strideLength = ((treadmill *)bluetoothManager->device())->currentStrideLength().value();
            groundContact = ((treadmill *)bluetoothManager->device())->currentGroundContact().value();
            verticalOscillation = ((treadmill *)bluetoothManager->device())->currentVerticalOscillation().value();
            stepCount = ((treadmill *)bluetoothManager->device())->currentStepCount().value();
            inclination = ((treadmill *)bluetoothManager->device())->currentInclination().value();
            if (((treadmill *)bluetoothManager->device())->currentSpeed().value() > 2)
                this->pace->setValue(
                    ((treadmill *)bluetoothManager->device())->currentPace().toString(QStringLiteral("m:ss")));
            else
                this->pace->setValue("N/A");
            this->pace->setSecondLine(
                QStringLiteral("AVG: ") +
                ((treadmill *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")) +
                QStringLiteral(" MAX: ") +
                ((treadmill *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setValue(
                ((treadmill *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setSecondLine(
                QStringLiteral("MAX: ") +
                ((treadmill *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            const double adjustedSpeed =
                ((treadmill *)bluetoothManager->device())->gradeAdjustedSpeed(
                    ((treadmill *)bluetoothManager->device())->currentSpeed().value(), inclination);
            if (((treadmill *)bluetoothManager->device())->currentSpeed().value() > 2 && adjustedSpeed > 0) {
                this->grade_adjusted_pace->setValue(
                    ((treadmill *)bluetoothManager->device())->speedToPace(adjustedSpeed).toString(QStringLiteral("m:ss")));
            } else {
                this->grade_adjusted_pace->setValue(QStringLiteral("N/A"));
            }
            this->grade_adjusted_pace->setSecondLine(QStringLiteral("Incl: ") +
                                                     QString::number(inclination, 'f', 1) + QStringLiteral("%"));
            this->target_power->setValue(
                QString::number(((treadmill *)bluetoothManager->device())->lastRequestedPower().value(), 'f', 0));
            if (trainProgram && trainProgram->isStarted() && trainProgram->powerOffsetForTrainingProgram() != 0) {
                this->target_power->setSecondLine(
                    QStringLiteral("%1%2W")
                        .arg(trainProgram->powerOffsetForTrainingProgram() > 0 ? QStringLiteral("+")
                                                                              : QStringLiteral(""))
                        .arg(trainProgram->powerOffsetForTrainingProgram()));
            } else {
                this->target_power->setSecondLine(QStringLiteral(""));
            }
            this->inclination->setValue(QString::number(inclination, 'f', 1));
            this->inclination->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentInclination().average(), 'f', 1) +
                QStringLiteral(" MAX: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentInclination().max(), 'f', 1));

            this->stepCount->setValue(QString::number(
                ((treadmill *)bluetoothManager->device())->currentStepCount().value(), 'f', 0));
            this->rss->setValue(QString::number(_rss, 'f', 0));

            this->instantaneousStrideLengthCM->setValue(QString::number(strideLength, 'f', 0));
            this->instantaneousStrideLengthCM->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentStrideLength().average(), 'f', 0) +
                QStringLiteral(" MAX: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentStrideLength().max(), 'f', 0));

            this->groundContactMS->setValue(QString::number(groundContact, 'f', 0));
            this->groundContactMS->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentGroundContact().average(), 'f', 0) +
                QStringLiteral(" MAX: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentGroundContact().max(), 'f', 0));

            this->verticalOscillationMM->setValue(QString::number(verticalOscillation, 'f', 0));
            this->verticalOscillationMM->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentVerticalOscillation().average(), 'f',
                                0) +
                QStringLiteral(" MAX: ") +
                QString::number(((treadmill *)bluetoothManager->device())->currentVerticalOscillation().max(), 'f', 0));

            // if there is no training program, the color is based on presets
            if (!trainProgram || trainProgram->currentRow().speed == -1 || trainProgram->currentRow().upper_speed == -1) {
                if (bluetoothManager->device()->currentSpeed().value() < 9) {
                    speed->setValueFontColor(QStringLiteral("white"));
                    setPaceValueFontColor(QStringLiteral("white"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 10) {
                    speed->setValueFontColor(QStringLiteral("limegreen"));
                    setPaceValueFontColor(QStringLiteral("limegreen"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 11) {
                    speed->setValueFontColor(QStringLiteral("gold"));
                    setPaceValueFontColor(QStringLiteral("gold"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 12) {
                    speed->setValueFontColor(QStringLiteral("orange"));
                    setPaceValueFontColor(QStringLiteral("orange"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 13) {
                    speed->setValueFontColor(QStringLiteral("darkorange"));
                    setPaceValueFontColor(QStringLiteral("darkorange"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 14) {
                    speed->setValueFontColor(QStringLiteral("orangered"));
                    setPaceValueFontColor(QStringLiteral("orangered"));
                } else {
                    speed->setValueFontColor(QStringLiteral("red"));
                    setPaceValueFontColor(QStringLiteral("red"));
                }
            } else {
                // Round speeds to 1 decimal place before comparison to avoid overly strict matching
                double currentSpeed = round(bluetoothManager->device()->currentSpeed().value() * 10.0) / 10.0;
                double upperSpeed = round(trainProgram->currentRow().upper_speed * 10.0) / 10.0;
                double lowerSpeed = round(trainProgram->currentRow().lower_speed * 10.0) / 10.0;

                // Check if speed is in target zone (green)
                if (currentSpeed <= upperSpeed && currentSpeed >= lowerSpeed) {
                    this->target_zone->setValueFontColor(QStringLiteral("limegreen"));
                    setPaceValueFontColor(QStringLiteral("limegreen"));
                }
                // Check if speed is close to target zone (orange)
                else if (currentSpeed <= (upperSpeed + 0.2) && currentSpeed >= (lowerSpeed - 0.2)) {
                    this->target_zone->setValueFontColor(QStringLiteral("orange"));
                    setPaceValueFontColor(QStringLiteral("orange"));
                }
                // Speed is out of range (red)
                else {
                    this->target_zone->setValueFontColor(QStringLiteral("red"));
                    setPaceValueFontColor(QStringLiteral("red"));
                }
            }

            // Zones 2-4 used to be renamed Brisk/Power/Max for a walking workout. The
            // only thing that ever reported one was the Peloton class metadata, so the
            // running names are the only ones reachable now.
            switch (trainProgram->currentRow().pace_intensity) {
            case 0:
                this->target_zone->setValue(tr("Rec."));
                break;
            case 1:
                this->target_zone->setValue(tr("Easy"));
                break;
            case 2:
                this->target_zone->setValue(tr("Moder."));
                break;
            case 3:
                this->target_zone->setValue(tr("Chall."));
                break;
            case 4:
                this->target_zone->setValue(tr("Hard"));
                break;
            case 5:
                this->target_zone->setValue(tr("V.Hard"));
                break;
            case 6:
                this->target_zone->setValue(tr("Max"));
                break;
            default:
                this->target_zone->setValue(tr("N/A"));
                break;
            }

            if (trainProgram) {
                // in order to see the target pace of a peloton workout even if the speed force for treadmill is disabled
                this->target_pace->setValue(
                            ((treadmill *)bluetoothManager->device())->speedToPace(trainProgram->currentRow().speed).toString(QStringLiteral("m:ss")));
                this->target_pace->setSecondLine(((treadmill *)bluetoothManager->device())
                                                     ->speedToPace(trainProgram->currentRow().lower_speed)
                                                     .toString(QStringLiteral("m:ss")) +
                                                 " - " +
                                                 ((treadmill *)bluetoothManager->device())
                                                     ->speedToPace(trainProgram->currentRow().upper_speed)
                                                     .toString(QStringLiteral("m:ss")));
            } else {
                this->target_pace->setValue(
                    ((treadmill *)bluetoothManager->device())->lastRequestedPace().toString(QStringLiteral("m:ss")));
            }
            this->target_speed->setValue(QString::number(
                ((treadmill *)bluetoothManager->device())->lastRequestedSpeed().value() * unit_conversion, 'f', 1));
            this->target_speed->setSecondLine(QString::number(bluetoothManager->device()->difficult() * 100.0, 'f', 0) +
                                              QStringLiteral("% @0%=") +
                                              QString::number(bluetoothManager->device()->difficult(), 'f', 0));
            this->target_incline->setValue(
                QString::number(((treadmill *)bluetoothManager->device())->lastRequestedInclination().value(), 'f', 1));
            this->target_incline->setSecondLine(
                QString::number(bluetoothManager->device()->inclinationDifficult() * 100.0, 'f', 0) +
                QStringLiteral("% @0%=") + QString::number(bluetoothManager->device()->inclinationDifficult(), 'f', 0));

            // originally born for #470. When the treadmill reaches the 0 speed it enters in the pause mode
            // so this logic should care about sync the treadmill state to the UI state
            if (((treadmill *)bluetoothManager->device())->autoPauseWhenSpeedIsZero() &&
                bluetoothManager->device()->currentSpeed().value() == 0 && paused == false && stopped == false) {
                qDebug() << QStringLiteral("autoPauseWhenSpeedIsZero!");
                Start_inner(false);
            } else if (((treadmill *)bluetoothManager->device())->autoStartWhenSpeedIsGreaterThenZero() &&
                       bluetoothManager->device()->currentSpeed().value() > 0 && (paused == true || stopped == true)) {
                qDebug() << QStringLiteral("autoStartWhenSpeedIsGreaterThenZero!");
                Start_inner(false);
            }
        } else if (bluetoothManager->device()->deviceType() == STAIRCLIMBER) {
            odometer->setValue(QString::number(bluetoothManager->device()->odometer() * unit_conversion, 'f', 2));
            stepCount = ((stairclimber *)bluetoothManager->device())->currentStepCount().value();
            inclination = ((stairclimber *)bluetoothManager->device())->currentInclination().value();
            if (((stairclimber *)bluetoothManager->device())->currentSpeed().value() > 2)
                this->pace->setValue(
                    ((stairclimber *)bluetoothManager->device())->currentPace().toString(QStringLiteral("m:ss")));
            else
                this->pace->setValue("N/A");
            this->pace->setSecondLine(
                QStringLiteral("AVG: ") +
                ((stairclimber *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")) +
                QStringLiteral(" MAX: ") +
                ((stairclimber *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setValue(
                ((stairclimber *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setSecondLine(
                QStringLiteral("MAX: ") +
                ((stairclimber *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            this->inclination->setValue(QString::number(inclination, 'f', 1));
            this->inclination->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((stairclimber *)bluetoothManager->device())->currentInclination().average(), 'f', 1) +
                QStringLiteral(" MAX: ") +
                QString::number(((stairclimber *)bluetoothManager->device())->currentInclination().max(), 'f', 1));

            this->stepCount->setValue(QString::number(
                ((stairclimber *)bluetoothManager->device())->currentStepCount().value(), 'f', 0));

                   // if there is no training program, the color is based on presets
            if (!trainProgram || trainProgram->currentRow().speed == -1 || trainProgram->currentRow().upper_speed == -1) {
                if (bluetoothManager->device()->currentSpeed().value() < 9) {
                    speed->setValueFontColor(QStringLiteral("white"));
                    setPaceValueFontColor(QStringLiteral("white"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 10) {
                    speed->setValueFontColor(QStringLiteral("limegreen"));
                    setPaceValueFontColor(QStringLiteral("limegreen"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 11) {
                    speed->setValueFontColor(QStringLiteral("gold"));
                    setPaceValueFontColor(QStringLiteral("gold"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 12) {
                    speed->setValueFontColor(QStringLiteral("orange"));
                    setPaceValueFontColor(QStringLiteral("orange"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 13) {
                    speed->setValueFontColor(QStringLiteral("darkorange"));
                    setPaceValueFontColor(QStringLiteral("darkorange"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 14) {
                    speed->setValueFontColor(QStringLiteral("orangered"));
                    setPaceValueFontColor(QStringLiteral("orangered"));
                } else {
                    speed->setValueFontColor(QStringLiteral("red"));
                    setPaceValueFontColor(QStringLiteral("red"));
                }
            } else {
                // Round speeds to 1 decimal place before comparison to avoid overly strict matching
                double currentSpeed = round(bluetoothManager->device()->currentSpeed().value() * 10.0) / 10.0;
                double upperSpeed = round(trainProgram->currentRow().upper_speed * 10.0) / 10.0;
                double lowerSpeed = round(trainProgram->currentRow().lower_speed * 10.0) / 10.0;

                       // Check if speed is in target zone (green)
                if (currentSpeed <= upperSpeed && currentSpeed >= lowerSpeed) {
                    this->target_zone->setValueFontColor(QStringLiteral("limegreen"));
                    setPaceValueFontColor(QStringLiteral("limegreen"));
                }
                // Check if speed is close to target zone (orange)
                else if (currentSpeed <= (upperSpeed + 0.2) && currentSpeed >= (lowerSpeed - 0.2)) {
                    this->target_zone->setValueFontColor(QStringLiteral("orange"));
                    setPaceValueFontColor(QStringLiteral("orange"));
                }
                // Speed is out of range (red)
                else {
                    this->target_zone->setValueFontColor(QStringLiteral("red"));
                    setPaceValueFontColor(QStringLiteral("red"));
                }
            }

            switch (trainProgram->currentRow().pace_intensity) {
            case 0:
                this->target_zone->setValue(tr("Rec."));
                break;
            case 1:
                this->target_zone->setValue(tr("Easy"));
                break;
            case 2:
                this->target_zone->setValue(tr("Moder."));
                break;
            case 3:
                this->target_zone->setValue(tr("Chall."));
                break;
            case 4:
                this->target_zone->setValue(tr("Hard"));
                break;
            case 5:
                this->target_zone->setValue(tr("V.Hard"));
                break;
            case 6:
                this->target_zone->setValue(tr("Max"));
                break;
            default:
                this->target_zone->setValue(tr("N/A"));
                break;
            }
        } else if (bluetoothManager->device()->deviceType() == BIKE) {

            bool pelotoncadence =
                settings.value(QZSettings::bike_cadence_sensor, QZSettings::default_bike_cadence_sensor).toBool();

            if (!pelotoncadence) {
                inclination = ((bike *)bluetoothManager->device())->currentInclination().value();
                this->inclination->setValue(QString::number(inclination, 'f', 1));
                this->inclination->setSecondLine(
                    QStringLiteral("AVG: ") +
                    QString::number(((bike *)bluetoothManager->device())->currentInclination().average(), 'f', 1) +
                    QStringLiteral(" MAX: ") +
                    QString::number(((bike *)bluetoothManager->device())->currentInclination().max(), 'f', 1));
            }
            if (bluetoothManager->externalInclination())
                extIncline->setValue(
                    QString::number(bluetoothManager->externalInclination()->currentInclination().value(), 'f', 1));
            double elite_rizer_gain =
                settings.value(QZSettings::elite_rizer_gain, QZSettings::default_elite_rizer_gain).toDouble();
            ergMode->setLargeButtonColor(settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool() ? "#008000" :"#8B0000");
            
            // Update automatic virtual shifting tile colors based on active profile
            int currentProfile = settings.value(QZSettings::automatic_virtual_shifting_profile, QZSettings::default_automatic_virtual_shifting_profile).toInt();
            autoVirtualShiftingCruise->setLargeButtonColor(currentProfile == 0 ? QStringLiteral("green") : QStringLiteral("red"));
            autoVirtualShiftingClimb->setLargeButtonColor(currentProfile == 1 ? QStringLiteral("green") : QStringLiteral("red"));
            autoVirtualShiftingSprint->setLargeButtonColor(currentProfile == 2 ? QStringLiteral("green") : QStringLiteral("red"));

            // Update power averaging tile text and color based on active mode
            bool power3s = settings.value(QZSettings::power_avg_3s, QZSettings::default_power_avg_3s).toBool();
            bool power5s = settings.value(QZSettings::power_avg_5s, QZSettings::default_power_avg_5s).toBool();
            if (!power3s && !power5s) {
                powerAvg->setLargeButtonLabel(QStringLiteral("Off"));
                powerAvg->setLargeButtonColor(QStringLiteral("grey"));
            } else if (power3s) {
                powerAvg->setLargeButtonLabel(QStringLiteral("3s avg"));
                powerAvg->setLargeButtonColor(QStringLiteral("green"));
            } else {
                powerAvg->setLargeButtonLabel(QStringLiteral("5s avg"));
                powerAvg->setLargeButtonColor(QStringLiteral("blue"));
            }

            extIncline->setSecondLine(QStringLiteral("Gain: ") + QString::number(elite_rizer_gain, 'f', 1));
            odometer->setValue(QString::number(bluetoothManager->device()->odometer() * unit_conversion, 'f', 2));
            resistance = ((bike *)bluetoothManager->device())->currentResistance().value();
            peloton_resistance = ((bike *)bluetoothManager->device())->pelotonResistance().value();
            this->peloton_resistance->setValue(QString::number(peloton_resistance, 'f', 0));
            this->target_resistance->setValue(
                QString::number(((bike *)bluetoothManager->device())->lastRequestedResistance().value(), 'f', 0));
            this->target_peloton_resistance->setValue(QString::number(
                ((bike *)bluetoothManager->device())->lastRequestedPelotonResistance().value(), 'f', 0));
            this->target_cadence->setValue(
                QString::number(((bike *)bluetoothManager->device())->lastRequestedCadence().value(), 'f', 0));
            this->target_power->setValue(
                QString::number(((bike *)bluetoothManager->device())->lastRequestedPower().value(), 'f', 0));
            if (trainProgram && trainProgram->isStarted() && trainProgram->powerOffsetForTrainingProgram() != 0) {
                this->target_power->setSecondLine(
                    QStringLiteral("%1%2W")
                        .arg(trainProgram->powerOffsetForTrainingProgram() > 0 ? QStringLiteral("+")
                                                                              : QStringLiteral(""))
                        .arg(trainProgram->powerOffsetForTrainingProgram()));
            } else {
                this->target_power->setSecondLine(QStringLiteral(""));
            }
            this->resistance->setValue(QString::number(resistance, 'f', 0));
            updateGearsValue();

            this->resistance->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((bike *)bluetoothManager->device())->currentResistance().average(), 'f', 0) +
                QStringLiteral(" MAX: ") +
                QString::number(((bike *)bluetoothManager->device())->currentResistance().max(), 'f', 0));
            this->peloton_resistance->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((bike *)bluetoothManager->device())->pelotonResistance().average(), 'f', 0) +
                QStringLiteral(" MAX: ") +
                QString::number(((bike *)bluetoothManager->device())->pelotonResistance().max(), 'f', 0));
            this->target_resistance->setSecondLine(
                QString::number(bluetoothManager->device()->difficult() * 100.0, 'f', 0) + QStringLiteral("% @0%=") +
                QString::number(
                    bluetoothManager->device()->difficult() *
                        settings.value(QZSettings::bike_resistance_gain_f, QZSettings::default_bike_resistance_gain_f)
                            .toDouble() +
                        settings.value(QZSettings::bike_resistance_offset, QZSettings::default_bike_resistance_offset)
                            .toDouble(),
                    'f', 0));

            this->steeringAngle->setValue(
                QString::number(((bike *)bluetoothManager->device())->currentSteeringAngle().value(), 'f', 1));

            if ((!trainProgram || (trainProgram && !trainProgram->isStarted())) &&
                !((bike *)bluetoothManager->device())->ergModeSupportedAvailableBySoftware() &&
                ((bike *)bluetoothManager->device())->lastRequestedPower().value() > 0 && m_overridePower) {
                qDebug() << QStringLiteral("using target power tile for ERG workout manually");
                ((bike *)bluetoothManager->device())
                    ->changePower(((bike *)bluetoothManager->device())->lastRequestedPower().value());
            }

        } else if (bluetoothManager->device()->deviceType() == ROWING) {
            if (bluetoothManager->device()->currentSpeed().value()) {
                pace = 10000 / (((rower *)bluetoothManager->device())->currentPace().second() +
                                (((rower *)bluetoothManager->device())->currentPace().minute() * 60));
                if (pace < 0) {
                    pace = 0;
                }
            } else {

                pace = 0;
            }

            this->gears->setValue(QString::number(((rower *)bluetoothManager->device())->gears()));
            this->pace_last500m->setValue(
                ((rower *)bluetoothManager->device())->lastPace500m().toString(QStringLiteral("m:ss")));

            this->pace->setValue(((rower *)bluetoothManager->device())->currentPace().toString(QStringLiteral("m:ss")));
            this->pace->setSecondLine(
                QStringLiteral("AVG: ") +
                ((rower *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")) +
                QStringLiteral(" MAX: ") +
                ((rower *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setValue(
                ((rower *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setSecondLine(
                QStringLiteral("MAX: ") +
                ((rower *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            this->target_pace->setValue(
                ((rower *)bluetoothManager->device())->lastRequestedPace().toString(QStringLiteral("m:ss")));
            if (trainProgram) {
                this->target_pace->setSecondLine(((rower *)bluetoothManager->device())
                                                     ->speedToPace(trainProgram->currentRow().lower_speed)
                                                     .toString(QStringLiteral("m:ss")) +
                                                 " - " +
                                                 ((rower *)bluetoothManager->device())
                                                     ->speedToPace(trainProgram->currentRow().upper_speed)
                                                     .toString(QStringLiteral("m:ss")));

                if (((rower *)bluetoothManager->device())->lastRequestedCadence().value() > 0) {
                    if (bluetoothManager->device()->currentSpeed().value() <= trainProgram->currentRow().upper_speed &&
                        bluetoothManager->device()->currentSpeed().value() >= trainProgram->currentRow().lower_speed) {
                        this->target_zone->setValueFontColor(QStringLiteral("limegreen"));
                        setPaceValueFontColor(QStringLiteral("limegreen"));
                    } else if (bluetoothManager->device()->currentSpeed().value() <=
                                   (trainProgram->currentRow().upper_speed + 0.2) &&
                               bluetoothManager->device()->currentSpeed().value() >=
                                   (trainProgram->currentRow().lower_speed - 0.2)) {
                        this->target_zone->setValueFontColor(QStringLiteral("orange"));
                        setPaceValueFontColor(QStringLiteral("orange"));
                    } else {
                        this->target_zone->setValueFontColor(QStringLiteral("red"));
                        setPaceValueFontColor(QStringLiteral("red"));
                    }
                } else {
                    this->target_zone->setValueFontColor(QStringLiteral("white"));
                    setPaceValueFontColor(QStringLiteral("white"));
                }
                switch (trainProgram->currentRow().pace_intensity) {
                case 0:
                    this->target_zone->setValue(tr("Rec."));
                    break;
                case 1:
                    this->target_zone->setValue(tr("Easy"));
                    break;
                case 2:
                    this->target_zone->setValue(tr("Moder."));
                    break;
                case 3:
                    this->target_zone->setValue(tr("Chall."));
                    break;
                case 4:
                    this->target_zone->setValue(tr("Max"));
                    break;
                default:
                    this->target_zone->setValue(tr("N/A"));
                    break;
                }
            }
            odometer->setValue(QString::number(bluetoothManager->device()->odometer() * 1000.0, 'f', 0));
            resistance = ((rower *)bluetoothManager->device())->currentResistance().value();
            peloton_resistance = ((rower *)bluetoothManager->device())->pelotonResistance().value();
            totalStrokes = ((rower *)bluetoothManager->device())->currentStrokesCount().value();
            avgStrokesRate = ((rower *)bluetoothManager->device())->currentCadence().average();
            maxStrokesRate = ((rower *)bluetoothManager->device())->currentCadence().max();
            avgStrokesLength = ((rower *)bluetoothManager->device())->currentStrokesLength().average();
            this->strokesCount->setValue(
                QString::number(((rower *)bluetoothManager->device())->currentStrokesCount().value(), 'f', 0));
            this->strokesLength->setValue(
                QString::number(((rower *)bluetoothManager->device())->currentStrokesLength().value(), 'f', 2));

            this->target_speed->setValue(QString::number(
                ((rower *)bluetoothManager->device())->lastRequestedSpeed().value() * unit_conversion, 'f', 1));

            this->peloton_resistance->setValue(QString::number(peloton_resistance, 'f', 0));
            this->target_resistance->setValue(
                QString::number(((rower *)bluetoothManager->device())->lastRequestedResistance().value(), 'f', 0));
            this->target_peloton_resistance->setValue(QString::number(
                ((rower *)bluetoothManager->device())->lastRequestedPelotonResistance().value(), 'f', 0));
            this->target_cadence->setValue(
                QString::number(((rower *)bluetoothManager->device())->lastRequestedCadence().value(), 'f', 0));
            this->target_power->setValue(
                QString::number(((rower *)bluetoothManager->device())->lastRequestedPower().value(), 'f', 0));
            if (trainProgram && trainProgram->isStarted() && trainProgram->powerOffsetForTrainingProgram() != 0) {
                this->target_power->setSecondLine(
                    QStringLiteral("%1%2W")
                        .arg(trainProgram->powerOffsetForTrainingProgram() > 0 ? QStringLiteral("+")
                                                                              : QStringLiteral(""))
                        .arg(trainProgram->powerOffsetForTrainingProgram()));
            } else {
                this->target_power->setSecondLine(QStringLiteral(""));
            }
            this->resistance->setValue(QString::number(resistance, 'f', 0));

            this->resistance->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((rower *)bluetoothManager->device())->currentResistance().average(), 'f', 0) +
                QStringLiteral(" MAX: ") +
                QString::number(((rower *)bluetoothManager->device())->currentResistance().max(), 'f', 0));
            this->peloton_resistance->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((rower *)bluetoothManager->device())->pelotonResistance().average(), 'f', 0) +
                QStringLiteral(" MAX: ") +
                QString::number(((rower *)bluetoothManager->device())->pelotonResistance().max(), 'f', 0));
            this->target_resistance->setSecondLine(
                QString::number(bluetoothManager->device()->difficult() * 100.0, 'f', 0) + QStringLiteral("% @0%=") +
                QString::number(
                    bluetoothManager->device()->difficult() *
                        settings.value(QZSettings::bike_resistance_gain_f, QZSettings::default_bike_resistance_gain_f)
                            .toDouble() *
                        settings.value(QZSettings::bike_resistance_offset, QZSettings::default_bike_resistance_offset)
                            .toDouble(),
                    'f', 0));
            this->strokesLength->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((rower *)bluetoothManager->device())->currentStrokesLength().average(), 'f', 1) +
                QStringLiteral(" MAX: ") +
                QString::number(((rower *)bluetoothManager->device())->currentStrokesLength().max(), 'f', 1));

            // if there is no training program, the color is based on presets
            if (!trainProgram || trainProgram->currentRow().speed == -1) {
                if (bluetoothManager->device()->currentSpeed().value() < 8) {
                    speed->setValueFontColor(QStringLiteral("white"));
                    setPaceValueFontColor(QStringLiteral("white"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 10) {
                    speed->setValueFontColor(QStringLiteral("limegreen"));
                    setPaceValueFontColor(QStringLiteral("limegreen"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 11) {
                    speed->setValueFontColor(QStringLiteral("gold"));
                    setPaceValueFontColor(QStringLiteral("gold"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 12) {
                    speed->setValueFontColor(QStringLiteral("orange"));
                    setPaceValueFontColor(QStringLiteral("orange"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 13) {
                    speed->setValueFontColor(QStringLiteral("darkorange"));
                    setPaceValueFontColor(QStringLiteral("darkorange"));
                } else if (bluetoothManager->device()->currentSpeed().value() < 14) {
                    speed->setValueFontColor(QStringLiteral("orangered"));
                    setPaceValueFontColor(QStringLiteral("orangered"));
                } else {
                    speed->setValueFontColor(QStringLiteral("red"));
                    setPaceValueFontColor(QStringLiteral("red"));
                }
            }

        } else if (bluetoothManager->device()->deviceType() == JUMPROPE) {
                odometer->setValue(QString::number(bluetoothManager->device()->odometer() * unit_conversion, 'f', 2));
                if (bluetoothManager->device()->currentSpeed().value()) {
                    pace = 10000 / (((treadmill *)bluetoothManager->device())->currentPace().second() +
                                    (((treadmill *)bluetoothManager->device())->currentPace().minute() * 60));
                    if (pace < 0) {
                        pace = 0;
                    }
                } else {

                    pace = 0;
                }
                stepCount = ((jumprope *)bluetoothManager->device())->JumpsCount.value();
                inclination = ((jumprope *)bluetoothManager->device())->JumpsSequence.value();
                if (((jumprope *)bluetoothManager->device())->currentSpeed().value() > 2)
                    this->pace->setValue(
                        ((jumprope *)bluetoothManager->device())->currentPace().toString(QStringLiteral("m:ss")));
                else
                    this->pace->setValue("N/A");
                this->pace->setSecondLine(
                    QStringLiteral("AVG: ") +
                    ((jumprope *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")) +
                    QStringLiteral(" MAX: ") +
                    ((jumprope *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
                this->avg_pace->setValue(
                    ((jumprope *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")));
                this->avg_pace->setSecondLine(
                    QStringLiteral("MAX: ") +
                    ((jumprope *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
                this->inclination->setValue(QString::number(inclination, 'f', 0));
                this->inclination->setSecondLine("");
                this->stepCount->setValue(QString::number(stepCount, 'f', 0));

                // Sequence of jumps resetted and number of jumps > 0, so i have to start a new lap
                if(inclination == 0 && ((jumprope *)bluetoothManager->device())->JumpsCount.lapValue() > 0)
                    lapTrigger = true;

        } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {

            if (((elliptical *)bluetoothManager->device())->currentSpeed().value() > 2)
                this->pace->setValue(
                    ((elliptical *)bluetoothManager->device())->currentPace().toString(QStringLiteral("m:ss")));
            else
                this->pace->setValue("N/A");
            this->pace->setSecondLine(
                QStringLiteral("AVG: ") +
                ((elliptical *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")) +
                QStringLiteral(" MAX: ") +
                ((elliptical *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setValue(
                ((elliptical *)bluetoothManager->device())->averagePace().toString(QStringLiteral("m:ss")));
            this->avg_pace->setSecondLine(
                QStringLiteral("MAX: ") +
                ((elliptical *)bluetoothManager->device())->maxPace().toString(QStringLiteral("m:ss")));
            odometer->setValue(QString::number(bluetoothManager->device()->odometer() * unit_conversion, 'f', 2));
            resistance = ((elliptical *)bluetoothManager->device())->currentResistance().value();
            peloton_resistance = ((elliptical *)bluetoothManager->device())->pelotonResistance().value();
            this->peloton_resistance->setValue(QString::number(peloton_resistance, 'f', 0));
            this->target_resistance->setValue(
                QString::number(((elliptical *)bluetoothManager->device())->lastRequestedResistance().value(), 'f', 0));
            this->target_peloton_resistance->setValue(QString::number(
                ((elliptical *)bluetoothManager->device())->lastRequestedPelotonResistance().value(), 'f', 0));
            this->resistance->setValue(QString::number(resistance, 'f', 0));
            this->peloton_resistance->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((elliptical *)bluetoothManager->device())->pelotonResistance().average(), 'f', 0) +
                QStringLiteral(" MAX: ") +
                QString::number(((elliptical *)bluetoothManager->device())->pelotonResistance().max(), 'f', 0));
            this->target_resistance->setSecondLine(
                QString::number(bluetoothManager->device()->difficult() * 100.0, 'f', 0) + QStringLiteral("% @0%=") +
                QString::number(
                    bluetoothManager->device()->difficult() *
                        settings.value(QZSettings::bike_resistance_gain_f, QZSettings::default_bike_resistance_gain_f)
                            .toDouble() *
                        settings.value(QZSettings::bike_resistance_offset, QZSettings::default_bike_resistance_offset)
                            .toDouble(),
                    'f', 0));
            inclination = ((elliptical *)bluetoothManager->device())->currentInclination().value();
            this->inclination->setValue(QString::number(inclination, 'f', 1));
            this->inclination->setSecondLine(
                QStringLiteral("AVG: ") +
                QString::number(((elliptical *)bluetoothManager->device())->currentInclination().average(), 'f', 1) +
                QStringLiteral(" MAX: ") +
                QString::number(((elliptical *)bluetoothManager->device())->currentInclination().max(), 'f', 1));

            this->gears->setValue(QString::number(((elliptical *)bluetoothManager->device())->gears()));
            this->target_speed->setValue(QString::number(
                ((elliptical *)bluetoothManager->device())->lastRequestedSpeed().value() * unit_conversion, 'f', 1));

            this->target_cadence->setValue(
                QString::number(((elliptical *)bluetoothManager->device())->lastRequestedCadence().value(), 'f', 0));
        }

        // Common elevation and negative elevation handling for all device types
        // Negative elevation gain (descent)
        this->negative_inclination->setValue(
            QString::number(bluetoothManager->device()->negativeElevationGain().value() *
                            meter_feet_conversion, 'f', (miles ? 0 : 1)));
        // Check if speed and inclination are not zero before showing rate
        if (bluetoothManager->device()->currentSpeed().value() > 0 &&
            bluetoothManager->device()->currentInclination().value() < 0) {
            this->negative_inclination->setSecondLine(
                QString::number(bluetoothManager->device()->negativeElevationGain().rate1s() * 60.0 *
                                    meter_feet_conversion,
                                'f', (miles ? 0 : 1)) +
                " /min");
        } else {
            this->negative_inclination->setSecondLine("");
        }

        // Elevation gain (ascent)
        elevation->setValue(
            QString::number(bluetoothManager->device()->elevationGain().value() * meter_feet_conversion,
                            'f', (miles ? 0 : 1)));
        // Check if speed and inclination are not zero before showing rate
        if (bluetoothManager->device()->currentSpeed().value() > 0 &&
            bluetoothManager->device()->currentInclination().value() > 0) {
            elevation->setSecondLine(
                QString::number(bluetoothManager->device()->elevationGain().rate1s() * 60.0 *
                                    meter_feet_conversion,
                                'f', (miles ? 0 : 1)) +
                " /min");
        } else {
            elevation->setSecondLine("");
        }

        watt->setSecondLine(
            QStringLiteral("AVG: ") + QString::number((bluetoothManager->device())->wattsMetric().average(), 'f', 0) +
            QStringLiteral(" MAX: ") + QString::number((bluetoothManager->device())->wattsMetric().max(), 'f', 0));

        if (trainProgram) {
            int8_t lower_requested_peloton_resistance = trainProgram->currentRow().lower_requested_peloton_resistance;
            int8_t upper_requested_peloton_resistance = trainProgram->currentRow().upper_requested_peloton_resistance;
            double lower_requested_peloton_resistance_to_bike_resistance = 0;
            if (bluetoothManager->device()->deviceType() == BIKE)
                lower_requested_peloton_resistance_to_bike_resistance =
                    ((bike *)bluetoothManager->device())->pelotonToBikeResistance(lower_requested_peloton_resistance);
            else if (bluetoothManager->device()->deviceType() == ROWING)
                lower_requested_peloton_resistance_to_bike_resistance =
                    ((rower *)bluetoothManager->device())->pelotonToBikeResistance(lower_requested_peloton_resistance);
            else if (bluetoothManager->device()->deviceType() == ELLIPTICAL)
                lower_requested_peloton_resistance_to_bike_resistance =
                    ((elliptical *)bluetoothManager->device())
                        ->pelotonToEllipticalResistance(lower_requested_peloton_resistance);

            if (lower_requested_peloton_resistance != -1) {
                this->target_peloton_resistance->setSecondLine(
                    QStringLiteral("MIN: ") + QString::number(lower_requested_peloton_resistance, 'f', 0) +
                    QStringLiteral(" MAX: ") + QString::number(upper_requested_peloton_resistance, 'f', 0));
            } else {
                this->target_peloton_resistance->setSecondLine(QLatin1String(""));
            }

            if (settings
                    .value(QZSettings::tile_peloton_resistance_color_enabled,
                           QZSettings::default_tile_peloton_resistance_color_enabled)
                    .toBool()) {
                if (lower_requested_peloton_resistance == -1) {
                    this->peloton_resistance->setValueFontColor(QStringLiteral("white"));
                } else if (resistance < lower_requested_peloton_resistance_to_bike_resistance) {
                    // we need to compare the real resistance and not the peloton resistance because most of the bikes
                    // have a 1:3 conversion so this compare will be always true even if the actual resistance is the
                    // same #1608
                    this->peloton_resistance->setValueFontColor(QStringLiteral("red"));
                } else if (((int8_t)qRound(peloton_resistance)) <= upper_requested_peloton_resistance) {
                    this->peloton_resistance->setValueFontColor(QStringLiteral("limegreen"));
                } else {
                    this->peloton_resistance->setValueFontColor(QStringLiteral("orange"));
                }
            }

            int16_t lower_cadence = trainProgram->currentRow().lower_cadence;
            int16_t upper_cadence = trainProgram->currentRow().upper_cadence;
            if (lower_cadence != -1) {
                this->target_cadence->setSecondLine(QStringLiteral("MIN: ") + QString::number(lower_cadence, 'f', 0) +
                                                    QStringLiteral(" MAX: ") + QString::number(upper_cadence, 'f', 0));
            } else {
                this->target_cadence->setSecondLine(QLatin1String(""));
            }

            if (settings.value(QZSettings::tile_cadence_color_enabled, QZSettings::default_tile_cadence_color_enabled)
                    .toBool()) {
                if (lower_cadence == -1) {
                    this->cadence->setValueFontColor(QStringLiteral("white"));
                } else if (cadence < lower_cadence) {
                    this->cadence->setValueFontColor(QStringLiteral("red"));
                } else if (cadence <= upper_cadence) {
                    this->cadence->setValueFontColor(QStringLiteral("limegreen"));
                } else {
                    this->cadence->setValueFontColor(QStringLiteral("orange"));
                }
            }
        }

        double ftpPerc = 0;
        QString ftpMinW = QStringLiteral("0");
        QString ftpMaxW = QStringLiteral("0");
        double requestedPerc = 0;
        double requestedZone = 1;
        QString requestedMinW = QStringLiteral("0");
        QString requestedMaxW = QStringLiteral("0");

        if (ftpSetting > 0) {
            ftpPerc = (watts / ftpSetting) * 100.0;
            if (bluetoothManager->device()->deviceType() == BIKE) {
                requestedPerc =
                    (((bike *)bluetoothManager->device())->lastRequestedPower().value() / ftpSetting) * 100.0;
            } else if (bluetoothManager->device()->deviceType() == ROWING) {
                requestedPerc =
                    (((rower *)bluetoothManager->device())->lastRequestedPower().value() / ftpSetting) * 100.0;
            }
        }
        if (ftpPerc < 56) {
            ftpMinW = QString::number(0, 'f', 0);
            ftpMaxW = QString::number(ftpSetting * 0.55, 'f', 0);
            ftpZone = 1;
            ftpZone += (ftpPerc / 56);
            if (ftpZone >= 1.95) { // double precision could cause unwanted approximation
                ftpZone = 1.9;
            }
            ftp->setValueFontColor(QStringLiteral("white"));
            setWattValueFontColor(QStringLiteral("white"));
        } else if (ftpPerc < 76) {

            ftpMinW = QString::number((ftpSetting * 0.55) + 1, 'f', 0);
            ftpMaxW = QString::number(ftpSetting * 0.75, 'f', 0);
            ftpZone = 2;
            ftpZone += ((ftpPerc - 56) / 20);
            if (ftpZone >= 2.95) { // double precision could cause unwanted approximation
                ftpZone = 2.9;
            }
            ftp->setValueFontColor(QStringLiteral("limegreen"));
            setWattValueFontColor(QStringLiteral("limegreen"));
        } else if (ftpPerc < 91) {

            ftpMinW = QString::number((ftpSetting * 0.75) + 1, 'f', 0);
            ftpMaxW = QString::number(ftpSetting * 0.90, 'f', 0);
            ftpZone = 3;
            ftpZone += ((ftpPerc - 76) / 15);
            if (ftpZone >= 3.95) { // double precision could cause unwanted approximation
                ftpZone = 3.9;
            }
            ftp->setValueFontColor(QStringLiteral("gold"));
            setWattValueFontColor(QStringLiteral("gold"));
        } else if (ftpPerc < 106) {

            ftpMinW = QString::number((ftpSetting * 0.90) + 1, 'f', 0);
            ftpMaxW = QString::number(ftpSetting * 1.05, 'f', 0);
            ftpZone = 4;
            ftpZone += ((ftpPerc - 91) / 15);
            if (ftpZone >= 4.95) { // double precision could cause unwanted approximation
                ftpZone = 4.9;
            }
            ftp->setValueFontColor(QStringLiteral("orange"));
            setWattValueFontColor(QStringLiteral("orange"));
        } else if (ftpPerc < 121) {

            ftpMinW = QString::number((ftpSetting * 1.05) + 1, 'f', 0);
            ftpMaxW = QString::number(ftpSetting * 1.20, 'f', 0);
            ftpZone = 5;
            ftpZone += ((ftpPerc - 106) / 15);
            if (ftpZone >= 5.95) { // double precision could cause unwanted approximation
                ftpZone = 5.9;
            }
            ftp->setValueFontColor(QStringLiteral("darkorange"));
            setWattValueFontColor(QStringLiteral("darkorange"));
        } else if (ftpPerc < 151) {

            ftpMinW = QString::number((ftpSetting * 1.20) + 1, 'f', 0);
            ftpMaxW = QString::number(ftpSetting * 1.50, 'f', 0);
            ftpZone = 6;
            ftpZone += ((ftpPerc - 121) / 30);
            if (ftpZone >= 6.95) { // double precision could cause unwanted approximation
                ftpZone = 6.9;
            }
            ftp->setValueFontColor(QStringLiteral("orangered"));
            setWattValueFontColor(QStringLiteral("orangered"));
        } else {

            ftpMinW = QString::number((ftpSetting * 1.50) + 1, 'f', 0);
            ftpMaxW = QStringLiteral("∞");
            ftpZone = 7;

            ftp->setValueFontColor(QStringLiteral("red"));
            setWattValueFontColor(QStringLiteral("red"));
        }
        bluetoothManager->device()->setPowerZone(ftpZone);
        ftp->setValue(QStringLiteral("Z") + QString::number(ftpZone, 'f', 1));
        ftp->setSecondLine(ftpMinW + QStringLiteral("-") + ftpMaxW + QStringLiteral("W ") +
                           QString::number(ftpPerc, 'f', 0) + QStringLiteral("%"));

        if (bluetoothManager->device()->deviceType() == BIKE ||
            (bluetoothManager->device()->deviceType() == ROWING &&
             (!trainProgram || trainProgram->currentRow().pace_intensity == -1))) {
            if (requestedPerc < 56) {

                requestedMinW = QString::number(0, 'f', 0);
                requestedMaxW = QString::number(ftpSetting * 0.55, 'f', 0);
                requestedZone = 1;
                requestedZone += (requestedPerc / 56);
                if (requestedZone >= 2) { // double precision could cause unwanted approximation
                    requestedZone = 1.9999;
                }
                target_zone->setValueFontColor(QStringLiteral("white"));
            } else if (requestedPerc < 76) {

                requestedMinW = QString::number((ftpSetting * 0.55) + 1, 'f', 0);
                requestedMaxW = QString::number(ftpSetting * 0.75, 'f', 0);
                requestedZone = 2;
                requestedZone += ((requestedPerc - 56) / 20);
                if (requestedZone >= 3) { // double precision could cause unwanted approximation
                    requestedZone = 2.9999;
                }
                target_zone->setValueFontColor(QStringLiteral("limegreen"));
            } else if (requestedPerc < 91) {

                requestedMinW = QString::number((ftpSetting * 0.75) + 1, 'f', 0);
                requestedMaxW = QString::number(ftpSetting * 0.90, 'f', 0);
                requestedZone = 3;
                requestedZone += ((requestedPerc - 76) / 15);
                if (requestedZone >= 4) { // double precision could cause unwanted approximation
                    requestedZone = 3.9999;
                }
                target_zone->setValueFontColor(QStringLiteral("gold"));
            } else if (requestedPerc < 106) {

                requestedMinW = QString::number((ftpSetting * 0.90) + 1, 'f', 0);
                requestedMaxW = QString::number(ftpSetting * 1.05, 'f', 0);
                requestedZone = 4;
                requestedZone += ((requestedPerc - 91) / 15);
                if (requestedZone >= 5) { // double precision could cause unwanted approximation
                    requestedZone = 4.9999;
                }
                target_zone->setValueFontColor(QStringLiteral("orange"));
            } else if (requestedPerc < 121) {

                requestedMinW = QString::number((ftpSetting * 1.05) + 1, 'f', 0);
                requestedMaxW = QString::number(ftpSetting * 1.20, 'f', 0);
                requestedZone = 5;
                requestedZone += ((requestedPerc - 106) / 15);
                if (requestedZone >= 6) { // double precision could cause unwanted approximation
                    requestedZone = 5.9999;
                }
                target_zone->setValueFontColor(QStringLiteral("darkorange"));
            } else if (requestedPerc < 151) {

                requestedMinW = QString::number((ftpSetting * 1.20) + 1, 'f', 0);
                requestedMaxW = QString::number(ftpSetting * 1.50, 'f', 0);
                requestedZone = 6;
                requestedZone += ((requestedPerc - 121) / 30);
                if (requestedZone >= 7) { // double precision could cause unwanted approximation
                    requestedZone = 6.9999;
                }
                target_zone->setValueFontColor(QStringLiteral("orangered"));
            } else {

                requestedMinW = QString::number((ftpSetting * 1.50) + 1, 'f', 0);
                requestedMaxW = QStringLiteral("∞");
                requestedZone = 7;

                target_zone->setValueFontColor(QStringLiteral("red"));
            }
            bluetoothManager->device()->setTargetPowerZone(requestedZone);
            target_zone->setValue(QStringLiteral("Z") + QString::number(requestedZone, 'f', 1));
            target_zone->setSecondLine(requestedMinW + QStringLiteral("-") + requestedMaxW + QStringLiteral("W ") +
                                       QString::number(requestedPerc, 'f', 0) + QStringLiteral("%"));
        }

        QString Z;
        double maxHeartRate = heartRateMax();
        double percHeartRate = (bluetoothManager->device()->currentHeart().value() * 100) / maxHeartRate;
        double currentHRZoneDisplay =
            interpolatedHeartZone(percHeartRate,
                                  settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1)
                                      .toDouble(),
                                  settings.value(QZSettings::heart_rate_zone2, QZSettings::default_heart_rate_zone2)
                                      .toDouble(),
                                  settings.value(QZSettings::heart_rate_zone3, QZSettings::default_heart_rate_zone3)
                                      .toDouble(),
                                  settings.value(QZSettings::heart_rate_zone4, QZSettings::default_heart_rate_zone4)
                                      .toDouble());
        double hrCurrentZoneRangeMin = 0;
        double hrCurrentZoneRangeMax = maxHeartRate;

        if (percHeartRate <
            settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1).toDouble()) {
            currentHRZone = 1;
            currentHRZone +=
                (percHeartRate /
                 settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1).toDouble());
            if (currentHRZone >= 2) { // double precision could cause unwanted approximation
                currentHRZone = 1.9999;
            }
            hrCurrentZoneRangeMax =
                ((settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1).toDouble() *
                  maxHeartRate) /
                 100) -
                1;
            heart->setValueFontColor(QStringLiteral("lightsteelblue"));
        } else if (percHeartRate <
                   settings.value(QZSettings::heart_rate_zone2, QZSettings::default_heart_rate_zone2).toDouble()) {
            currentHRZone = 2;
            currentHRZone +=
                ((percHeartRate -
                  settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1).toDouble()) /
                 (settings.value(QZSettings::heart_rate_zone2, QZSettings::default_heart_rate_zone2).toDouble() -
                  settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1).toDouble()));
            if (currentHRZone >= 3) { // double precision could cause unwanted approximation
                currentHRZone = 2.9999;
            }
            hrCurrentZoneRangeMin =
                (settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1).toDouble() *
                 maxHeartRate) /
                100;
            hrCurrentZoneRangeMax =
                ((settings.value(QZSettings::heart_rate_zone2, QZSettings::default_heart_rate_zone2).toDouble() *
                  maxHeartRate) /
                 100) -
                1;
            heart->setValueFontColor(QStringLiteral("green"));
        } else if (percHeartRate <
                   settings.value(QZSettings::heart_rate_zone3, QZSettings::default_heart_rate_zone3).toDouble()) {
            currentHRZone = 3;
            currentHRZone +=
                ((percHeartRate -
                  settings.value(QZSettings::heart_rate_zone2, QZSettings::default_heart_rate_zone2).toDouble()) /
                 (settings.value(QZSettings::heart_rate_zone3, QZSettings::default_heart_rate_zone3).toDouble() -
                  settings.value(QZSettings::heart_rate_zone2, QZSettings::default_heart_rate_zone2).toDouble()));
            if (currentHRZone >= 4) { // double precision could cause unwanted approximation
                currentHRZone = 3.9999;
            }
            hrCurrentZoneRangeMin =
                (settings.value(QZSettings::heart_rate_zone2, QZSettings::default_heart_rate_zone2).toDouble() *
                 maxHeartRate) /
                100;
            hrCurrentZoneRangeMax =
                ((settings.value(QZSettings::heart_rate_zone3, QZSettings::default_heart_rate_zone3).toDouble() *
                  maxHeartRate) /
                 100) -
                1;
            heart->setValueFontColor(QStringLiteral("yellow"));
        } else if (percHeartRate <
                   settings.value(QZSettings::heart_rate_zone4, QZSettings::default_heart_rate_zone4).toDouble()) {
            currentHRZone = 4;
            currentHRZone +=
                ((percHeartRate -
                  settings.value(QZSettings::heart_rate_zone3, QZSettings::default_heart_rate_zone3).toDouble()) /
                 (settings.value(QZSettings::heart_rate_zone4, QZSettings::default_heart_rate_zone4).toDouble() -
                  settings.value(QZSettings::heart_rate_zone3, QZSettings::default_heart_rate_zone3).toDouble()));
            if (currentHRZone >= 5) { // double precision could cause unwanted approximation
                currentHRZone = 4.9999;
            }
            hrCurrentZoneRangeMin =
                (settings.value(QZSettings::heart_rate_zone3, QZSettings::default_heart_rate_zone3).toDouble() *
                 maxHeartRate) /
                100;
            hrCurrentZoneRangeMax =
                ((settings.value(QZSettings::heart_rate_zone4, QZSettings::default_heart_rate_zone4).toDouble() *
                  maxHeartRate) /
                 100) -
                1;
            heart->setValueFontColor(QStringLiteral("orange"));
        } else {
            currentHRZone = 5;
            heart->setValueFontColor(QStringLiteral("red"));
            hrCurrentZoneRangeMin =
                (settings.value(QZSettings::heart_rate_zone4, QZSettings::default_heart_rate_zone4).toDouble() *
                 maxHeartRate) /
                100;
        }
        pidHR->setValue(QString::number(treadmill_pid_heart_zone));
        pidHR->setSecondLine(QString::number(hrCurrentZoneRangeMin) + "-" + QString::number(hrCurrentZoneRangeMax));
        switch (treadmill_pid_heart_zone) {
        case 5:
            pidHR->setValueFontColor(QStringLiteral("red"));
            break;
        case 4:
            pidHR->setValueFontColor(QStringLiteral("orange"));
            break;
        case 3:
            pidHR->setValueFontColor(QStringLiteral("yellow"));
            break;
        case 2:
            pidHR->setValueFontColor(QStringLiteral("green"));
            break;
        case 1:
            pidHR->setValueFontColor(QStringLiteral("lightsteelblue"));
            break;
        default:
        case 0:
            pidHR->setValueFontColor(QStringLiteral("white"));
            break;
        }
        bluetoothManager->device()->setHeartZone(currentHRZone);
        Z = QStringLiteral("Z") + QString::number(currentHRZoneDisplay, 'f', 1);

        // Heart rate second line - show as percentage if enabled
        if (settings.value(QZSettings::tile_heart_show_as_percent, QZSettings::default_tile_heart_show_as_percent).toBool()) {
            double maxHR = heartRateMax();
            double avgHRPercent = ((bluetoothManager->device())->currentHeart().average() / maxHR) * 100.0;
            double maxHRPercent = ((bluetoothManager->device())->currentHeart().max() / maxHR) * 100.0;
            heart->setSecondLine(Z + QStringLiteral(" AVG: ") +
                                 QString::number(avgHRPercent, 'f', 0) + "%" +
                                 QStringLiteral(" MAX: ") +
                                 QString::number(maxHRPercent, 'f', 0) + "%");
        } else {
            heart->setSecondLine(Z + QStringLiteral(" AVG: ") +
                                 QString::number((bluetoothManager->device())->currentHeart().average(), 'f', 0) +
                                 QStringLiteral(" MAX: ") +
                                 QString::number((bluetoothManager->device())->currentHeart().max(), 'f', 0));
        }

        /*
                if(trainProgram)
                {
                    trainProgramElapsedTime->setText(trainProgram->totalElapsedTime().toString("hh:mm:ss"));
                    trainProgramCurrentRowElapsedTime->setText(trainProgram->currentRowElapsedTime().toString("hh:mm:ss"));
                    trainProgramDuration->setText(trainProgram->duration().toString("hh:mm:ss"));

                    double distance = trainProgram->totalDistance();
                    if(distance > 0)
                    {
                        trainProgramTotalDistance->setText(QString::number(distance));
                    }
                    else
                        trainProgramTotalDistance->setText("N/A");
                }
        */

        qDebug() << "homeform::update tiles updated!";

#ifdef Q_OS_ANDROID
        if (settings.value(QZSettings::ant_cadence, QZSettings::default_ant_cadence).toBool() &&
            KeepAwakeHelper::antObject(false)) {
            double v = bluetoothManager->device()->currentSpeed().value();
            v *= settings.value(QZSettings::ant_speed_gain, QZSettings::default_ant_speed_gain).toDouble();
            v += settings.value(QZSettings::ant_speed_offset, QZSettings::default_ant_speed_offset).toDouble();
            KeepAwakeHelper::antObject(false)->callMethod<void>("setCadenceSpeedPower", "(FII)V", (float)v, (int)watts,
                                                                (int)cadence);

            long distanceMeters = (long)(bluetoothManager->device()->odometer() * 1000.0);
            
            // Get heart rate
            int heartRate = (int)bluetoothManager->device()->currentHeart().value();
            
            // Calculate elapsed time in seconds
            double elapsedTimeSeconds = (double)(bluetoothManager->device()->elapsedTime().second() +
                (bluetoothManager->device()->elapsedTime().minute() * 60) +
                (bluetoothManager->device()->elapsedTime().hour() * 3600));
            
            // Get resistance and inclination values
            int resistance = 0;
            double inclination = 0.0;
            int antEquipmentType = 0x19; // ANT+ FE Trainer/Stationary Bike
            int strokeCount = 0;
            
            if (bluetoothManager->device()->deviceType() == BIKE) {
                resistance = (int)((bike*)bluetoothManager->device())->currentResistance().value();
                inclination = ((bike*)bluetoothManager->device())->currentInclination().value();
            } else if (bluetoothManager->device()->deviceType() == ELLIPTICAL) {
                resistance = (int)((elliptical*)bluetoothManager->device())->currentResistance().value();
                inclination = ((elliptical*)bluetoothManager->device())->currentInclination().value();
            } else if (bluetoothManager->device()->deviceType() == ROWING) {
                antEquipmentType = 0x16; // ANT+ FE Rower
                resistance = (int)((rower*)bluetoothManager->device())->currentResistance().value();
                strokeCount = (int)((rower*)bluetoothManager->device())->currentStrokesCount().value();
            }
            
            // Call the extended metrics update via JNI
            KeepAwakeHelper::antObject(false)->callMethod<void>("updateBikeTransmitterExtendedMetrics", 
                "(JIDIDII)V",
                distanceMeters, 
                heartRate, 
                elapsedTimeSeconds, 
                resistance, 
                inclination,
                antEquipmentType,
                strokeCount);
        }
#endif

        if (settings.value(QZSettings::trainprogram_random, QZSettings::default_trainprogram_random).toBool()) {
            if (!paused && !stopped) {

                static QRandomGenerator r;
                r.seed(QDateTime::currentDateTime().toMSecsSinceEpoch());
                static uint32_t last_seconds = 0;
                uint32_t seconds = bluetoothManager->device()->elapsedTime().second() +
                                   (bluetoothManager->device()->elapsedTime().minute() * 60) +
                                   (bluetoothManager->device()->elapsedTime().hour() * 3600);
                if ((seconds / 60) <
                    settings.value(QZSettings::trainprogram_total, QZSettings::default_trainprogram_total).toUInt()) {
                    qDebug() << QStringLiteral("trainprogram random seconds ") + QString::number(seconds) +
                                    QStringLiteral(" last_change ") + QString::number(last_seconds) +
                                    QStringLiteral(" period ") +
                                    QString::number(settings
                                        .value(QZSettings::trainprogram_period_seconds,
                                               QZSettings::default_trainprogram_period_seconds)
                                        .toUInt());
                    if (last_seconds == 0 ||
                        ((seconds - last_seconds) >= settings
                                                         .value(QZSettings::trainprogram_period_seconds,
                                                                QZSettings::default_trainprogram_period_seconds)
                                                         .toUInt())) {
                        bool done = false;

                        if (bluetoothManager->device()->deviceType() == TREADMILL &&
                            ((treadmill *)bluetoothManager->device())->currentSpeed().value() > 0.0f) {
                            double speed = settings
                                               .value(QZSettings::trainprogram_speed_min,
                                                      QZSettings::default_trainprogram_speed_min)
                                               .toDouble();
                            double incline = settings
                                                 .value(QZSettings::trainprogram_incline_min,
                                                        QZSettings::default_trainprogram_incline_min)
                                                 .toDouble();
                            if (!speed) {
                                speed = 1.0;
                            }
                            if (settings.value(QZSettings::trainprogram_speed_min,
                                               QZSettings::default_trainprogram_speed_min)
                                        .toDouble() != 0 &&
                                settings.value(QZSettings::trainprogram_speed_min,
                                               QZSettings::default_trainprogram_speed_min)
                                        .toDouble() < settings
                                                          .value(QZSettings::trainprogram_speed_max,
                                                                 QZSettings::default_trainprogram_speed_max)
                                                          .toDouble()) {
                                speed =
                                    (double)r.bounded((uint32_t)(settings
                                                                     .value(QZSettings::trainprogram_speed_min,
                                                                            QZSettings::default_trainprogram_speed_min)
                                                                     .toDouble() *
                                                                 10.0),
                                                      (uint32_t)(settings
                                                                     .value(QZSettings::trainprogram_speed_max,
                                                                            QZSettings::default_trainprogram_speed_max)
                                                                     .toDouble() *
                                                                 10.0)) /
                                    10.0;
                            }
                            if (settings
                                    .value(QZSettings::trainprogram_incline_min,
                                           QZSettings::default_trainprogram_incline_min)
                                    .toDouble() < settings
                                                      .value(QZSettings::trainprogram_incline_max,
                                                             QZSettings::default_trainprogram_incline_max)
                                                      .toDouble()) {
                                incline = (double)r.bounded(
                                              (uint32_t)(settings
                                                             .value(QZSettings::trainprogram_incline_min,
                                                                    QZSettings::default_trainprogram_incline_min)
                                                             .toDouble() *
                                                         10.0),
                                              (uint32_t)(settings
                                                             .value(QZSettings::trainprogram_incline_max,
                                                                    QZSettings::default_trainprogram_incline_max)
                                                             .toDouble() *
                                                         10.0)) /
                                          10.0;
                            }
                            ((treadmill *)bluetoothManager->device())->changeSpeedAndInclination(speed, incline);
                            done = true;
                        } else if (bluetoothManager->device()->deviceType() == BIKE) {
                            double resistance = settings
                                                    .value(QZSettings::trainprogram_resistance_min,
                                                           QZSettings::default_trainprogram_resistance_min)
                                                    .toUInt();
                            if (settings
                                    .value(QZSettings::trainprogram_resistance_min,
                                           QZSettings::default_trainprogram_resistance_min)
                                    .toUInt() < settings
                                                    .value(QZSettings::trainprogram_resistance_max,
                                                           QZSettings::default_trainprogram_resistance_max)
                                                    .toUInt()) {
                                resistance =
                                    (double)r.bounded(settings
                                                          .value(QZSettings::trainprogram_resistance_min,
                                                                 QZSettings::default_trainprogram_resistance_min)
                                                          .toUInt(),
                                                      settings
                                                          .value(QZSettings::trainprogram_resistance_max,
                                                                 QZSettings::default_trainprogram_resistance_max)
                                                          .toUInt());
                            }
                            ((bike *)bluetoothManager->device())->changeResistance(resistance);

                            done = true;
                        } else if (bluetoothManager->device()->deviceType() == ROWING) {
                            double resistance = settings
                                                    .value(QZSettings::trainprogram_resistance_min,
                                                           QZSettings::default_trainprogram_resistance_min)
                                                    .toUInt();
                            if (settings
                                    .value(QZSettings::trainprogram_resistance_min,
                                           QZSettings::default_trainprogram_resistance_min)
                                    .toUInt() < settings
                                                    .value(QZSettings::trainprogram_resistance_max,
                                                           QZSettings::default_trainprogram_resistance_max)
                                                    .toUInt()) {
                                resistance =
                                    (double)r.bounded(settings
                                                          .value(QZSettings::trainprogram_resistance_min,
                                                                 QZSettings::default_trainprogram_resistance_min)
                                                          .toUInt(),
                                                      settings
                                                          .value(QZSettings::trainprogram_resistance_max,
                                                                 QZSettings::default_trainprogram_resistance_max)
                                                          .toUInt());
                            }
                            ((rower *)bluetoothManager->device())->changeResistance(resistance);

                            done = true;
                        }

                        if (done) {
                            if (last_seconds == 0) {

                                r.seed(QDateTime::currentDateTime().currentMSecsSinceEpoch());
                                last_seconds = 1; // in order to avoid to re-enter here again if the user doesn't ride
                            } else {

                                last_seconds = seconds;
                            }
                        }
                    }
                } else if (bluetoothManager->device()->currentSpeed().value() > 0) {
                    if (bluetoothManager->device()->deviceType() == TREADMILL) {

                        ((treadmill *)bluetoothManager->device())->changeSpeedAndInclination(0, 0);
                    } else if (bluetoothManager->device()->deviceType() == BIKE) {

                        ((bike *)bluetoothManager->device())->changeResistance(1);
                    } else if (bluetoothManager->device()->deviceType() == ROWING) {

                        ((rower *)bluetoothManager->device())->changeResistance(1);
                    }
                }
            }
        } else if (!settings.value(QZSettings::treadmill_pid_heart_zone, QZSettings::default_treadmill_pid_heart_zone)
                        .toString()
                        .contains(QStringLiteral("Disabled")) ||
                   (trainProgram && trainProgram->currentRow().zoneHR >= 0)) {
            static uint32_t last_seconds_pid_heart_zone = 0;
            static uint32_t pid_heart_zone_small_inc_counter = 0;
            uint32_t seconds = bluetoothManager->device()->elapsedTime().second() +
                               (bluetoothManager->device()->elapsedTime().minute() * 60) +
                               (bluetoothManager->device()->elapsedTime().hour() * 3600);
            uint8_t delta = 10;
            bool trainprogram_pid_pushy = settings.value(QZSettings::trainprogram_pid_pushy, QZSettings::default_trainprogram_pid_pushy).toBool();
            double trainprogram_pid_hr_pushy_zone_limit = settings.value(QZSettings::trainprogram_pid_hr_pushy_zone_limit, QZSettings::default_trainprogram_pid_hr_pushy_zone_limit).toDouble();
            double trainprogram_pid_hr_recovery_zone_limit = settings.value(QZSettings::trainprogram_pid_hr_recovery_zone_limit, QZSettings::default_trainprogram_pid_hr_recovery_zone_limit).toDouble();
            bool fromTrainProgram = trainProgram && trainProgram->currentRow().zoneHR >= 0;
            double maxSpeed = 30;
            double minSpeed = 0;
            int8_t maxResistance = 100;
            static double lastInclination = 0;
            static double lastWattage = 0;

            if (fromTrainProgram) {
                delta = trainProgram->currentRow().loopTimeHR;
            }

            if (bluetoothManager->device()->deviceType() == TREADMILL &&
                !settings.value(QZSettings::trainprogram_pid_ignore_inclination, QZSettings::default_trainprogram_pid_ignore_inclination).toBool() &&
                bluetoothManager->device()->currentInclination().value() != lastInclination && lastWattage != 0) {
                last_seconds_pid_heart_zone = seconds;

                double weightKg = settings.value(QZSettings::weight, QZSettings::default_weight).toFloat();
                double newspeed = 0;
                double bestSpeed = 0.1;
                double bestDifference = fabs(((treadmill *)bluetoothManager->device())->wattsCalc(weightKg, bestSpeed, bluetoothManager->device()->currentInclination().value()) - lastWattage);
                for (int speed = 1; speed <= 300; speed++) {
                    double s = ((double)speed) / 10.0;
                    double thisDifference = fabs(((treadmill *)bluetoothManager->device())->wattsCalc(weightKg, s, bluetoothManager->device()->currentInclination().value()) - lastWattage);
                    if (thisDifference < bestDifference) {
                        bestDifference = thisDifference;
                        bestSpeed = s;
                    }
                }
                // Now bestSpeed is the speed closest to the desired wattage
                newspeed = bestSpeed;
                qDebug() << QStringLiteral("changing speed to") << newspeed << "due to inclination changed";
                ((treadmill *)bluetoothManager->device())->changeSpeedAndInclination(newspeed, ((treadmill *)bluetoothManager->device())->currentInclination().value());
            }

            lastInclination = bluetoothManager->device()->currentInclination().value();
            lastWattage = bluetoothManager->device()->wattsMetric().value();

            if (last_seconds_pid_heart_zone == 0 || ((seconds - last_seconds_pid_heart_zone) >= delta)) {

                last_seconds_pid_heart_zone = seconds;

                uint8_t zone =
                    settings.value(QZSettings::treadmill_pid_heart_zone, QZSettings::default_treadmill_pid_heart_zone)
                        .toString()
                        .toUInt();
                if (fromTrainProgram) {
                    zone = trainProgram->currentRow().zoneHR;
                    if (zone > 0) {
                        settings.setValue(QZSettings::treadmill_pid_heart_zone, QString::number(zone));                        
                    } else {
                        settings.setValue(QZSettings::treadmill_pid_heart_zone, QStringLiteral("Disabled"));
                    }
                    if (trainProgram->currentRow().maxSpeed > 0) {
                        maxSpeed = trainProgram->currentRow().maxSpeed;
                    }
                    if (trainProgram->currentRow().minSpeed > 0) {
                        minSpeed = trainProgram->currentRow().minSpeed;
                    }
                    if (trainProgram->currentRow().maxResistance > 0) {
                        maxResistance = trainProgram->currentRow().maxResistance;
                    }
                }

                if (!stopped && !paused && bluetoothManager->device()->currentHeart().value() && zone > 0 &&
                    bluetoothManager->device()->currentSpeed().value() > 0.0f) {
                    // Skip HR PID adjustments for a period after training program changes speed
                    // This prevents race conditions where HR PID overwrites training program speed changes
                    qint64 msSinceSpeedChange = lastTrainingProgramSpeedChange.msecsTo(QDateTime::currentDateTime());
                    bool recentSpeedChange = (msSinceSpeedChange < (delta * 1000));
                    
                    if (!recentSpeedChange) {
                    if (bluetoothManager->device()->deviceType() == TREADMILL) {

                        const double step = 0.2;
                        double currentSpeed = ((treadmill *)bluetoothManager->device())->currentSpeed().value();
                        if (zone < ((uint8_t)currentHRZone) && currentSpeed > minSpeed) {
                            double newSpeed = std::max(currentSpeed - step, minSpeed);
                            ((treadmill *)bluetoothManager->device())
                                ->changeSpeedAndInclination(
                                    newSpeed,
                                    ((treadmill *)bluetoothManager->device())->currentInclination().value());
                            pid_heart_zone_small_inc_counter = 0;
                        } else if (zone > ((uint8_t)currentHRZone) && currentSpeed < maxSpeed) {
                            double newSpeed = std::min(currentSpeed + step, maxSpeed);
                            ((treadmill *)bluetoothManager->device())
                                ->changeSpeedAndInclination(
                                    newSpeed,
                                    ((treadmill *)bluetoothManager->device())->currentInclination().value());
                            pid_heart_zone_small_inc_counter = 0;
                        } else if (trainprogram_pid_pushy) {
                            double pushyZoneLimit = (double)zone + trainprogram_pid_hr_pushy_zone_limit;
                            // Slowdown threshold is symmetric: midpoint between pushyZoneLimit and zone top
                            // e.g. pushy=0.8: slowdown at zone+0.9, neutral band [0.8, 0.9]
                            double pushySlowdownThreshold = (double)zone + (1.0 + trainprogram_pid_hr_pushy_zone_limit) / 2.0;
                            double pushyHRZone = currentHRZone;
                            if (zone == 1) {
                                double zone1Limit =
                                    settings.value(QZSettings::heart_rate_zone1, QZSettings::default_heart_rate_zone1)
                                        .toDouble();
                                double zone1LowerLimit = qBound(0.0, trainprogram_pid_hr_recovery_zone_limit, zone1Limit - 1.0);
                                double effectiveZone1Width = zone1Limit - zone1LowerLimit;
                                if (effectiveZone1Width > 0.0) {
                                    double maxHeartRate = heartRateMax();
                                    double currentHRPercent =
                                        (bluetoothManager->device()->currentHeart().value() * 100.0) / maxHeartRate;
                                    pushyHRZone =
                                        1.0 + ((currentHRPercent - zone1LowerLimit) / effectiveZone1Width);
                                    pushyHRZone = qBound(1.0, pushyHRZone, 1.9999);
                                }
                            }
                            double distanceToNextZone = ((double)zone + 1.0) - pushyHRZone;
                            if (pushyHRZone > pushySlowdownThreshold && currentSpeed > minSpeed) {
                                double newSpeed = std::max(currentSpeed - step, minSpeed);
                                ((treadmill *)bluetoothManager->device())
                                    ->changeSpeedAndInclination(
                                        newSpeed,
                                        ((treadmill *)bluetoothManager->device())->currentInclination().value());
                                pid_heart_zone_small_inc_counter = 0;
                            } else if (pushyHRZone < pushyZoneLimit && distanceToNextZone > 0.0 && currentSpeed < maxSpeed) {
                                pid_heart_zone_small_inc_counter++;
                                if (pid_heart_zone_small_inc_counter > (10 * distanceToNextZone)) {
                                    double newSpeed = std::min(currentSpeed + step, maxSpeed);
                                    ((treadmill *)bluetoothManager->device())
                                        ->changeSpeedAndInclination(
                                            newSpeed,
                                            ((treadmill *)bluetoothManager->device())->currentInclination().value());
                                    pid_heart_zone_small_inc_counter = 0;
                                }
                            } else {
                                pid_heart_zone_small_inc_counter++;
                            }
                        }
                    } else if (bluetoothManager->device()->deviceType() == BIKE) {
                        bool ergMode = ((bike*)bluetoothManager->device())->ergModeSupportedAvailableByHardware();
                        bool inclinationAvailable = ((bike*)bluetoothManager->device())->inclinationAvailableBySoftware();

                        if(ergMode) {
                            // Use power control for bikes with erg mode support
                            double step = settings.value(QZSettings::pid_heart_zone_erg_mode_watt_step, QZSettings::default_pid_heart_zone_erg_mode_watt_step).toInt();
                            double current_target_watt = ((bike *)bluetoothManager->device())->lastRequestedPower().value();
                            if (zone < ((uint8_t)currentHRZone)) {
                                ((bike *)bluetoothManager->device())->changePower(current_target_watt - step);
                                pid_heart_zone_small_inc_counter = 0;
                            } else if (zone > ((uint8_t)currentHRZone)) {
                                ((bike *)bluetoothManager->device())->changePower(current_target_watt + step);
                                pid_heart_zone_small_inc_counter = 0;
                            } else if(trainprogram_pid_pushy) {
                                pid_heart_zone_small_inc_counter++;
                                if (pid_heart_zone_small_inc_counter > (5 * fabs(((float)zone) - currentHRZone))) {
                                    ((bike *)bluetoothManager->device())->changePower(current_target_watt + step);
                                    pid_heart_zone_small_inc_counter = 0;
                                }
                            }
                        } else if(inclinationAvailable) {
                            // Use inclination control for bikes without erg mode but with inclination support (e.g., ftmsbike)
                            double step = 0.5;
                            double currentInclination = ((bike *)bluetoothManager->device())->currentInclination().value();
                            if (zone < ((uint8_t)currentHRZone)) {
                                ((bike *)bluetoothManager->device())->changeInclination(currentInclination - step, currentInclination - step);
                                pid_heart_zone_small_inc_counter = 0;
                            } else if (zone > ((uint8_t)currentHRZone)) {
                                ((bike *)bluetoothManager->device())->changeInclination(currentInclination + step, currentInclination + step);
                                pid_heart_zone_small_inc_counter = 0;
                            } else if(trainprogram_pid_pushy) {
                                pid_heart_zone_small_inc_counter++;
                                if (pid_heart_zone_small_inc_counter > (5 * fabs(((float)zone) - currentHRZone))) {
                                    ((bike *)bluetoothManager->device())->changeInclination(currentInclination + step, currentInclination + step);
                                    pid_heart_zone_small_inc_counter = 0;
                                }
                            }
                        } else {
                            // Fallback to resistance control for bikes without erg mode or inclination
                            double step = 1;
                            bool ergMode = ((bike*)bluetoothManager->device())->ergModeSupportedAvailableBySoftware();
                            if(ergMode) {
                                step = settings.value(QZSettings::pid_heart_zone_erg_mode_watt_step, QZSettings::default_pid_heart_zone_erg_mode_watt_step).toInt();
                            }
                            resistance_t currentResistance =
                                ((bike *)bluetoothManager->device())->currentResistance().value();
                            double current_target_watt = ((bike *)bluetoothManager->device())->lastRequestedPower().value();
                            if (zone < ((uint8_t)currentHRZone)) {
                                if(ergMode)
                                    ((bike *)bluetoothManager->device())->changePower(current_target_watt - step);
                                else
                                    ((bike *)bluetoothManager->device())->changeResistance(currentResistance - step);
                                pid_heart_zone_small_inc_counter = 0;
                            } else if (zone > ((uint8_t)currentHRZone) && ((maxResistance >= currentResistance + step && !ergMode) || ergMode)) {
                                if(ergMode)
                                    ((bike *)bluetoothManager->device())->changePower(current_target_watt + step);
                                else
                                    ((bike *)bluetoothManager->device())->changeResistance(currentResistance + step);
                                pid_heart_zone_small_inc_counter = 0;
                            } else if(trainprogram_pid_pushy) {
                                pid_heart_zone_small_inc_counter++;
                                if (pid_heart_zone_small_inc_counter > (5 * fabs(((float)zone) - currentHRZone))) {
                                    if(ergMode)
                                        ((bike *)bluetoothManager->device())->changePower(current_target_watt + step);
                                    else
                                        ((bike *)bluetoothManager->device())->changeResistance(currentResistance + step);
                                    pid_heart_zone_small_inc_counter = 0;
                                }
                            }
                        }
                    } else if (bluetoothManager->device()->deviceType() == ROWING) {

                        const int step = 1;
                        resistance_t currentResistance =
                            ((rower *)bluetoothManager->device())->currentResistance().value();
                        if (zone < ((uint8_t)currentHRZone)) {

                            ((rower *)bluetoothManager->device())->changeResistance(currentResistance - step);
                        } else if (zone > ((uint8_t)currentHRZone)) {

                            ((rower *)bluetoothManager->device())->changeResistance(currentResistance + step);
                        }
                    }
                    }  // Close the if (!recentSpeedChange) block
                }
            }
        } else if ((settings.value(QZSettings::treadmill_pid_heart_min, QZSettings::default_treadmill_pid_heart_min)
                            .toInt() > 0 &&
                    settings.value(QZSettings::treadmill_pid_heart_max, QZSettings::default_treadmill_pid_heart_max)
                            .toInt() > 0) ||
                   (trainProgram && trainProgram->currentRow().HRmin > 0 && trainProgram->currentRow().HRmax > 0)) {
            static uint32_t last_seconds_pid_heart_zone = 0;
            static uint32_t pid_heart_zone_small_inc_counter = 0;
            bool trainprogram_pid_pushy = settings.value(QZSettings::trainprogram_pid_pushy, QZSettings::default_trainprogram_pid_pushy).toBool();
            uint32_t seconds = bluetoothManager->device()->elapsedTime().second() +
                               (bluetoothManager->device()->elapsedTime().minute() * 60) +
                               (bluetoothManager->device()->elapsedTime().hour() * 3600);
            uint8_t delta = 10;
            bool fromTrainProgram =
                trainProgram && trainProgram->currentRow().HRmin > 0 && trainProgram->currentRow().HRmax > 0;
            double maxSpeed = 30;
            double minSpeed = 0;
            int8_t maxResistance = 100;

            if (fromTrainProgram) {
                delta = trainProgram->currentRow().loopTimeHR;
            }

            if (last_seconds_pid_heart_zone == 0 || ((seconds - last_seconds_pid_heart_zone) >= delta)) {

                last_seconds_pid_heart_zone = seconds;

                int16_t hrmin =
                    settings.value(QZSettings::treadmill_pid_heart_min, QZSettings::default_treadmill_pid_heart_min)
                        .toInt();
                int16_t hrmax =
                    settings.value(QZSettings::treadmill_pid_heart_max, QZSettings::default_treadmill_pid_heart_max)
                        .toInt();
                if (fromTrainProgram) {
                    hrmin = trainProgram->currentRow().HRmin;
                    hrmax = trainProgram->currentRow().HRmax;
                    if (trainProgram->currentRow().maxSpeed > 0) {
                        maxSpeed = trainProgram->currentRow().maxSpeed;
                    }
                    if (trainProgram->currentRow().minSpeed > 0) {
                        minSpeed = trainProgram->currentRow().minSpeed;
                    }
                    if (trainProgram->currentRow().maxResistance > 0) {
                        maxResistance = trainProgram->currentRow().maxResistance;
                    }
                }

                if (hrmax == 0 || hrmax == -1)
                    hrmax = 220;

                if (!stopped && !paused && bluetoothManager->device()->currentHeart().value() &&
                    bluetoothManager->device()->currentSpeed().value() > 0.0f) {
                    qDebug() << QStringLiteral("PID HR Control - HR:") << bluetoothManager->device()->currentHeart().average20s()
                             << QStringLiteral("HRmin:") << hrmin << QStringLiteral("HRmax:") << hrmax
                             << QStringLiteral("fromTrainProgram:") << fromTrainProgram;

                    // Skip HR PID adjustments for a period after training program changes speed
                    // This prevents race conditions where HR PID overwrites training program speed changes
                    qint64 msSinceSpeedChange = lastTrainingProgramSpeedChange.msecsTo(QDateTime::currentDateTime());
                    bool recentSpeedChange = (msSinceSpeedChange < (delta * 1000));
                    
                    if (!recentSpeedChange) {
                    if (bluetoothManager->device()->deviceType() == TREADMILL) {

                        const double step = 0.2;
                        double currentSpeed = ((treadmill *)bluetoothManager->device())->currentSpeed().value();
                        qDebug() << QStringLiteral("TREADMILL PID HR - currentSpeed:") << currentSpeed
                                 << QStringLiteral("minSpeed:") << minSpeed << QStringLiteral("maxSpeed:") << maxSpeed;

                        if (hrmax < bluetoothManager->device()->currentHeart().average20s() &&
                            currentSpeed > minSpeed) {
                            double newSpeed = std::max(currentSpeed - step, minSpeed);
                            qDebug() << QStringLiteral("TREADMILL PID HR - HR > HRmax, DECREASING speed from")
                                     << currentSpeed << QStringLiteral("to") << newSpeed;
                            ((treadmill *)bluetoothManager->device())
                                ->changeSpeedAndInclination(
                                    newSpeed,
                                    ((treadmill *)bluetoothManager->device())->currentInclination().value());
                            pid_heart_zone_small_inc_counter = 0;
                        } else if (hrmin > bluetoothManager->device()->currentHeart().average20s() &&
                                   currentSpeed < maxSpeed) {
                            double newSpeed = std::min(currentSpeed + step, maxSpeed);
                            qDebug() << QStringLiteral("TREADMILL PID HR - HR < HRmin, INCREASING speed from")
                                     << currentSpeed << QStringLiteral("to") << newSpeed;
                            ((treadmill *)bluetoothManager->device())
                                ->changeSpeedAndInclination(

                                    newSpeed,
                                    ((treadmill *)bluetoothManager->device())->currentInclination().value());
                            pid_heart_zone_small_inc_counter = 0;
                        } else if (currentSpeed < maxSpeed &&
                                   hrmax >= bluetoothManager->device()->currentHeart().average20s() && trainprogram_pid_pushy) {
                            qDebug() << QStringLiteral("TREADMILL PID HR - PUSHY mode, counter:") << pid_heart_zone_small_inc_counter
                                     << QStringLiteral("threshold:") << (30 / abs(hrmax - bluetoothManager->device()->currentHeart().average20s()));
                            pid_heart_zone_small_inc_counter++;
                            if (pid_heart_zone_small_inc_counter > (30 / abs(hrmax - bluetoothManager->device()->currentHeart().average20s()))) {
                                double newSpeed = std::min(currentSpeed + step, maxSpeed);
                                qDebug() << QStringLiteral("TREADMILL PID HR - PUSHY triggered, INCREASING speed from")
                                         << currentSpeed << QStringLiteral("to") << newSpeed;
                                ((treadmill *)bluetoothManager->device())
                                    ->changeSpeedAndInclination(
                                        newSpeed,
                                        ((treadmill *)bluetoothManager->device())->currentInclination().value());
                                pid_heart_zone_small_inc_counter = 0;
                            }
                        } else {
                            qDebug() << QStringLiteral("TREADMILL PID HR - No action taken (in zone or at limits)");
                        }
                    } else if (bluetoothManager->device()->deviceType() == BIKE) {

                        bool ergMode = ((bike*)bluetoothManager->device())->ergModeSupportedAvailableByHardware();
                        bool inclinationAvailable = ((bike*)bluetoothManager->device())->inclinationAvailableBySoftware();

                        if (ergMode) {
                            // Use power control for bikes with erg mode support
                            const int step = settings.value(QZSettings::pid_heart_zone_erg_mode_watt_step, QZSettings::default_pid_heart_zone_erg_mode_watt_step).toInt();
                            double current_target_watt = ((bike *)bluetoothManager->device())->lastRequestedPower().value();
                            qDebug() << QStringLiteral("BIKE PID HR - ergMode enabled, currentPower:") << current_target_watt;

                            if (hrmax < bluetoothManager->device()->currentHeart().average20s()) {
                                qDebug() << QStringLiteral("BIKE PID HR - HR > HRmax, DECREASING power from")
                                         << current_target_watt << QStringLiteral("to") << (current_target_watt - step);
                                ((bike *)bluetoothManager->device())->changePower(current_target_watt - step);
                            } else if (hrmin > bluetoothManager->device()->currentHeart().average20s()) {
                                qDebug() << QStringLiteral("BIKE PID HR - HR < HRmin, INCREASING power from")
                                         << current_target_watt << QStringLiteral("to") << (current_target_watt + step);
                                ((bike *)bluetoothManager->device())->changePower(current_target_watt + step);
                            } else {
                                qDebug() << QStringLiteral("BIKE PID HR - No action taken (in zone or at limits)");
                            }
                        } else if (inclinationAvailable) {
                            // Use inclination control for bikes without erg mode but with inclination support (e.g., ftmsbike)
                            const double step = 0.5;
                            double currentInclination = ((bike *)bluetoothManager->device())->currentInclination().value();
                            qDebug() << QStringLiteral("BIKE PID HR - Using inclination control, currentInclination:") << currentInclination;

                            if (hrmax < bluetoothManager->device()->currentHeart().average20s()) {
                                qDebug() << QStringLiteral("BIKE PID HR - HR > HRmax, DECREASING inclination from")
                                         << currentInclination << QStringLiteral("to") << (currentInclination - step);
                                ((bike *)bluetoothManager->device())->changeInclination(currentInclination - step, currentInclination - step);
                            } else if (hrmin > bluetoothManager->device()->currentHeart().average20s()) {
                                qDebug() << QStringLiteral("BIKE PID HR - HR < HRmin, INCREASING inclination from")
                                         << currentInclination << QStringLiteral("to") << (currentInclination + step);
                                ((bike *)bluetoothManager->device())->changeInclination(currentInclination + step, currentInclination + step);
                            } else {
                                qDebug() << QStringLiteral("BIKE PID HR - No action taken (in zone or at limits)");
                            }
                        } else {
                            const int step = 1;
                            resistance_t currentResistance =
                                ((bike *)bluetoothManager->device())->currentResistance().value();
                            qDebug() << QStringLiteral("BIKE PID HR - currentResistance:") << currentResistance
                                     << QStringLiteral("maxResistance:") << maxResistance;

                            if (hrmax < bluetoothManager->device()->currentHeart().average20s()) {
                                qDebug() << QStringLiteral("BIKE PID HR - HR > HRmax, DECREASING resistance from")
                                         << currentResistance << QStringLiteral("to") << (currentResistance - step);
                                ((bike *)bluetoothManager->device())->changeResistance(currentResistance - step);
                            } else if (hrmin > bluetoothManager->device()->currentHeart().average20s() &&
                                       currentResistance < maxResistance) {
                                resistance_t newResistance = std::min(static_cast<resistance_t>(currentResistance + step), static_cast<resistance_t>(maxResistance));
                                qDebug() << QStringLiteral("BIKE PID HR - HR < HRmin, INCREASING resistance from")
                                         << currentResistance << QStringLiteral("to") << newResistance;
                                ((bike *)bluetoothManager->device())->changeResistance(newResistance);
                            } else {
                                qDebug() << QStringLiteral("BIKE PID HR - No action taken (in zone or at limits)");
                            }
                        }
                    } else if (bluetoothManager->device()->deviceType() == ROWING) {

                        const int step = 1;
                        resistance_t currentResistance =
                            ((rower *)bluetoothManager->device())->currentResistance().value();
                        qDebug() << QStringLiteral("ROWING PID HR - currentResistance:") << currentResistance;

                        if (hrmax < bluetoothManager->device()->currentHeart().average20s()) {
                            qDebug() << QStringLiteral("ROWING PID HR - HR > HRmax, DECREASING resistance from")
                                     << currentResistance << QStringLiteral("to") << (currentResistance - step);
                            ((rower *)bluetoothManager->device())->changeResistance(currentResistance - step);
                        } else if (hrmin > bluetoothManager->device()->currentHeart().average20s()) {
                            qDebug() << QStringLiteral("ROWING PID HR - HR < HRmin, INCREASING resistance from")
                                     << currentResistance << QStringLiteral("to") << (currentResistance + step);
                            ((rower *)bluetoothManager->device())->changeResistance(currentResistance + step);
                         } else {
                             qDebug() << QStringLiteral("ROWING PID HR - No action taken (in zone or at limits)");
                         }
                     }
                    }  // Close the if (!recentSpeedChange) block
                }
            }
        }

        if (settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable).toBool()) {
            if (!settings.value(QZSettings::fitmetria_fanfit_mode, QZSettings::default_fitmetria_fanfit_mode)
                     .toString()
                     .compare(QStringLiteral("Manual"))) {
                // do nothing here, the user change the fan value with the tile
            } else if (paused || stopped) {
                qDebug() << QStringLiteral("fitmetria_fanfit paused or stopped mode");
                bluetoothManager->device()->changeFanSpeed(0);
            }
            // Heart Mode
            else if (!settings.value(QZSettings::fitmetria_fanfit_mode, QZSettings::default_fitmetria_fanfit_mode)
                          .toString()
                          .compare(QStringLiteral("Heart"))) {
                qDebug() << QStringLiteral("fitmetria_fanfit heart mode")
                         << bluetoothManager->device()->currentHeart().value();
                const uint8_t min = 80;
                uint8_t v = 0;
                if (bluetoothManager->device()->currentHeart().value() > min && maxHeartRate > min)
                    v = ((bluetoothManager->device()->currentHeart().value() - min) * 100.0) /
                        (double)(maxHeartRate - min);
                bluetoothManager->device()->changeFanSpeed(v + fanOverride);
            }
            // Power Mode
            else if (!settings.value(QZSettings::fitmetria_fanfit_mode, QZSettings::default_fitmetria_fanfit_mode)
                          .toString()
                          .compare(QStringLiteral("Power"))) {
                qDebug() << QStringLiteral("fitmetria_fanfit power mode") << watts;
                const double percOverFtp = 1.20;
                const double min = 50;
                uint8_t v = 0;
                double a = (double)(((ftpSetting * percOverFtp) - min));
                if (watts >= min && a > 0)
                    v = ((watts - min) * 100.0) / a;
                bluetoothManager->device()->changeFanSpeed(v + fanOverride);
            }
            // Wind mode
            else if (!settings.value(QZSettings::fitmetria_fanfit_mode, QZSettings::default_fitmetria_fanfit_mode)
                          .toString()
                          .compare(QStringLiteral("Wind"))) {
                // Todo
                qDebug() << QStringLiteral("fitmetria_fanfit wind mode");
                // bluetoothManager->device()->changeFanSpeed((ftpZone - 1) * 1.5);
            }
        }

        if (!stopped && !paused) {
            if(settings.value(QZSettings::autolap_distance, QZSettings::default_autolap_distance).toDouble() != 0) {
                if (bluetoothManager->device()->currentDistance().lapValue() >=
                    settings.value(QZSettings::autolap_distance, QZSettings::default_autolap_distance).toDouble()) {
                        qDebug() << QStringLiteral("Autolap based on distance");
                        Lap();
                        setToastRequested("AutoLap " + QString::number(settings.value(QZSettings::autolap_distance, QZSettings::default_autolap_distance).toDouble(), 'f', 1));
                }
            }

            if (settings.value(QZSettings::tts_enabled, QZSettings::default_tts_enabled).toBool()) {
                static double tts_speed_played = 0;
                bool description =
                    settings.value(QZSettings::tts_description_enabled, QZSettings::default_tts_description_enabled)
                        .toBool();
                if (m_speech.state() == QTextToSpeech::Ready) {
                    if (++tts_summary_count >=
                        settings.value(QZSettings::tts_summary_sec, QZSettings::default_tts_summary_sec).toInt()) {
                        tts_summary_count = 0;

                        QString s;
                        if (settings.value(QZSettings::tts_act_speed, QZSettings::default_tts_act_speed).toBool())
                            s.append(
                                (description ? tr(", speed ") : ",") +
                                (!miles ? QString::number(bluetoothManager->device()->currentSpeed().value(), 'f', 1) +
                                              (description ? tr(" kilometers per hour") : "")
                                        : QString::number(bluetoothManager->device()->currentSpeed().value() *
                                                              unit_conversion,
                                                          'f', 1)) +
                                (description ? tr(" miles per hour") : ""));
                        if (settings.value(QZSettings::tts_avg_speed, QZSettings::default_tts_avg_speed).toBool())
                            s.append((description ? tr(", Average speed ") : ",") +
                                     (!miles ? QString::number(bluetoothManager->device()->currentSpeed().average(),
                                                               'f', 1) +
                                                   (description ? tr("kilometers per hour") : "")
                                             : QString::number(bluetoothManager->device()->currentSpeed().average() *
                                                                   unit_conversion,
                                                               'f', 1)) +
                                     (description ? tr(" miles per hour") : ""));
                        if (settings.value(QZSettings::tts_max_speed, QZSettings::default_tts_max_speed).toBool())
                            s.append((description ? tr(", Max speed ") : ",") +
                                     (!miles
                                          ? QString::number(bluetoothManager->device()->currentSpeed().max(), 'f', 1) +
                                                (description ? tr(" kilometers per hour") : "")
                                          : QString::number(bluetoothManager->device()->currentSpeed().max() *
                                                                unit_conversion,
                                                            'f', 1)) +
                                     (description ? tr(" miles per hour") : ""));
                        if (settings.value(QZSettings::tts_act_inclination, QZSettings::default_tts_act_inclination)
                                .toBool())
                            s.append((description ? tr(", inclination ") : ",") +
                                     QString::number(bluetoothManager->device()->currentInclination().value(), 'f', 1));
                        if (settings.value(QZSettings::tts_act_cadence, QZSettings::default_tts_act_cadence).toBool())
                            s.append((description ? tr(", cadence ") : ",") +
                                     QString::number(bluetoothManager->device()->currentCadence().value(), 'f', 0));
                        if (settings.value(QZSettings::tts_avg_cadence, QZSettings::default_tts_avg_cadence).toBool())
                            s.append((description ? tr(", Average cadence ") : ",") +
                                     QString::number(bluetoothManager->device()->currentCadence().average(), 'f', 0));
                        if (settings.value(QZSettings::tts_max_cadence, QZSettings::default_tts_max_cadence /* true */)
                                .toBool())
                            s.append((description ? tr(", Max cadence ") : ",") +
                                     QString::number(bluetoothManager->device()->currentCadence().max()));
                        if (settings.value(QZSettings::tts_act_elevation, QZSettings::default_tts_act_elevation)
                                .toBool())
                            s.append(
                                (description ? tr(", elevation ") : ",") +
                                (!miles ? QString::number(bluetoothManager->device()->elevationGain().value(), 'f', 1) +
                                              (description ? tr(" meters") : "")
                                        : QString::number(bluetoothManager->device()->elevationGain().value() *
                                                              meter_feet_conversion,
                                                          'f', 1)) +
                                (description ? tr(" feet") : ""));
                        if (settings.value(QZSettings::tts_act_calories, QZSettings::default_tts_act_calories).toBool())
                            s.append((description ? tr(", calories burned ") : ",") +
                                     QString::number(bluetoothManager->device()->calories().value(), 'f', 0));
                        if (settings.value(QZSettings::tts_act_odometer, QZSettings::default_tts_act_odometer).toBool())
                            s.append((description ? tr(", distance ") : ",") +
                                     (!miles ? QString::number(bluetoothManager->device()->odometer(), 'f', 1) +
                                                   (description ? tr("kilometers") : "")
                                             : QString::number(bluetoothManager->device()->odometer() * unit_conversion,
                                                               'f', 1)) +
                                     (description ? tr(" miles") : ""));
                        if (settings.value(QZSettings::tts_act_target_pace, QZSettings::default_tts_act_target_pace)
                                .toBool()) {
                            if (bluetoothManager->device()->deviceType() == ROWING)
                                s.append((description ? tr(", pace ") : ",") + ((rower *)bluetoothManager->device())
                                                                                   ->lastRequestedPace()
                                                                                   .toString(QStringLiteral("m:ss")));
                            else if (bluetoothManager->device()->deviceType() == TREADMILL)
                                s.append((description ? tr(", pace ") : ",") + ((treadmill *)bluetoothManager->device())
                                                                                   ->lastRequestedPace()
                                                                                   .toString(QStringLiteral("m:ss")));
                        }
                        if (settings.value(QZSettings::tts_act_pace, QZSettings::default_tts_act_pace).toBool())
                            s.append((description ? tr(", pace ") : ",") +
                                     bluetoothManager->device()->currentPace().toString(QStringLiteral("m:ss")));
                        if (settings.value(QZSettings::tts_avg_pace, QZSettings::default_tts_avg_pace).toBool())
                            s.append((description ? tr(", pace ") : ",") +
                                     bluetoothManager->device()->averagePace().toString(QStringLiteral("m:ss")));
                        if (settings.value(QZSettings::tts_max_pace, QZSettings::default_tts_max_pace).toBool())
                            s.append((description ? tr(", pace ") : ",") +
                                     bluetoothManager->device()->maxPace().toString(QStringLiteral("m:ss")));
                        if (settings.value(QZSettings::tts_act_resistance, QZSettings::default_tts_act_resistance)
                                .toBool())
                            s.append((description ? tr(", resistance ") : ",") +
                                     QString::number(bluetoothManager->device()->currentResistance().value(), 'f', 0));
                        if (settings.value(QZSettings::tts_avg_resistance, QZSettings::default_tts_avg_resistance)
                                .toBool())
                            s.append(
                                (description ? tr(", average resistance ") : ",") +
                                QString::number(bluetoothManager->device()->currentResistance().average(), 'f', 0));
                        if (settings.value(QZSettings::tts_max_resistance, QZSettings::default_tts_max_resistance)
                                .toBool())
                            s.append((description ? tr(", max resistance ") : ",") +
                                     QString::number(bluetoothManager->device()->currentResistance().max(), 'f', 0));
                        if (settings.value(QZSettings::tts_act_watt, QZSettings::default_tts_act_watt).toBool())
                            s.append((description ? tr(", watt ") : ",") +
                                     QString::number(bluetoothManager->device()->wattsMetric().value(), 'f', 0));
                        if (settings.value(QZSettings::tts_avg_watt, QZSettings::default_tts_avg_watt).toBool())
                            s.append((description ? tr(", average watt ") : ",") +
                                     QString::number(bluetoothManager->device()->wattsMetric().average(), 'f', 0));
                        if (settings.value(QZSettings::tts_max_watt, QZSettings::default_tts_max_watt).toBool())
                            s.append((description ? tr(", max watt ") : ",") +
                                     QString::number(bluetoothManager->device()->wattsMetric().max(), 'f', 0));
                        if (settings.value(QZSettings::tts_act_ftp, QZSettings::default_tts_act_ftp /* true */)
                                .toBool())
                            s.append((description ? QStringLiteral(", ftp ") : QStringLiteral(",")) + QString::number(ftpZone, 'f', 1));
                        if (settings.value(QZSettings::tts_act_heart, QZSettings::default_tts_act_heart).toBool())
                            s.append((description ? tr(", heart rate ") : ",") +
                                     QString::number(bluetoothManager->device()->currentHeart().value(), 'f', 0));
                        if (settings.value(QZSettings::tts_avg_heart, QZSettings::default_tts_avg_heart).toBool())
                            s.append((description ? tr(", average heart rate ") : ",") +
                                     QString::number(bluetoothManager->device()->currentHeart().average(), 'f', 0));
                        if (settings.value(QZSettings::tts_max_heart, QZSettings::default_tts_max_heart).toBool())
                            s.append((description ? tr(", max heart rate ") : ",") +
                                     QString::number(bluetoothManager->device()->currentHeart().max(), 'f', 0));
                        if (settings.value(QZSettings::tts_act_jouls, QZSettings::default_tts_act_jouls).toBool())
                            s.append((description ? tr(", jouls ") : ",") +
                                     QString::number(bluetoothManager->device()->jouls().max(), 'f', 0));
                        if (settings.value(QZSettings::tts_act_elapsed, QZSettings::default_tts_act_elapsed).toBool())
                            s.append((description ? tr(", elapsed ") : ",") +
                                     QString::number(bluetoothManager->device()->elapsedTime().minute()) +
                                     (description ? tr(" minutes ") : "") +
                                     QString::number(bluetoothManager->device()->elapsedTime().second()) +
                                     (description ? tr(" seconds") : ""));
                        if (settings
                                .value(QZSettings::tts_act_peloton_resistance,
                                       QZSettings::default_tts_act_peloton_resistance)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == BIKE)
                            s.append((description ? tr(", peloton resistance ") : ",") +
                                     QString::number(((bike *)bluetoothManager->device())->pelotonResistance().value(),
                                                     'f', 0));
                        if (settings
                                .value(QZSettings::tts_avg_peloton_resistance,
                                       QZSettings::default_tts_avg_peloton_resistance)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == BIKE)
                            s.append((description ? tr(", average peloton resistance ") : ",") +
                                     QString::number(
                                         ((bike *)bluetoothManager->device())->pelotonResistance().average(), 'f', 0));
                        if (settings
                                .value(QZSettings::tts_max_peloton_resistance,
                                       QZSettings::default_tts_max_peloton_resistance)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == BIKE)
                            s.append((description ? tr(", max peloton resistance ") : ",") +
                                     QString::number(((bike *)bluetoothManager->device())->pelotonResistance().max(),
                                                     'f', 0));
                        if (settings
                                .value(QZSettings::tts_act_target_peloton_resistance,
                                       QZSettings::default_tts_act_target_peloton_resistance)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == BIKE)
                            s.append((description ? tr(", target peloton resistance ") : ",") +
                                     QString::number(
                                         ((bike *)bluetoothManager->device())->lastRequestedPelotonResistance().value(),
                                         'f', 0));
                        if (settings
                                .value(QZSettings::tts_act_target_cadence, QZSettings::default_tts_act_target_cadence)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == BIKE)
                            s.append((description ? tr(", target cadence ") : ",") +
                                     QString::number(
                                         ((bike *)bluetoothManager->device())->lastRequestedCadence().value(), 'f', 0));
                        if (settings.value(QZSettings::tts_act_target_power, QZSettings::default_tts_act_target_power)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == BIKE)
                            s.append((description ? tr(", target power ") : ",") +
                                     QString::number(((bike *)bluetoothManager->device())->lastRequestedPower().value(),
                                                     'f', 0));
                        if (settings.value(QZSettings::tts_act_target_zone, QZSettings::default_tts_act_target_zone)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == BIKE)
                            s.append((description ? tr(", target zone ") : ",") +
                                     QString::number(requestedZone, 'f', 1));
                        if (settings.value(QZSettings::tts_act_target_speed, QZSettings::default_tts_act_target_speed)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == TREADMILL)
                            s.append(
                                (description ? tr(", target speed ") : ",") +
                                (!miles ? QString::number(
                                              ((treadmill *)bluetoothManager->device())->lastRequestedSpeed().value(),
                                              'f', 1) +
                                              (description ? tr(" kilometers per hour") : "")
                                        : QString::number(
                                              ((treadmill *)bluetoothManager->device())->lastRequestedSpeed().value() *
                                                  unit_conversion,
                                              'f', 1)) +
                                (description ? tr(" miles per hour") : ""));
                        if (settings
                                .value(QZSettings::tts_act_target_incline, QZSettings::default_tts_act_target_incline)
                                .toBool() &&
                            bluetoothManager->device()->deviceType() == TREADMILL)
                            s.append((description ? tr(", target incline ") : ",") +
                                     QString::number(
                                         ((treadmill *)bluetoothManager->device())->lastRequestedInclination().value(),
                                         'f', 1));
                        if (settings.value(QZSettings::tts_act_watt_kg, QZSettings::default_tts_act_watt_kg).toBool())
                            s.append((description ? tr(", watt for kilograms ") : ",") +
                                     QString::number(bluetoothManager->device()->wattKg().value(), 'f', 1));
                        if (settings.value(QZSettings::tts_avg_watt_kg, QZSettings::default_tts_avg_watt_kg).toBool())
                            s.append((description ? tr(", average watt for kilograms") : ",") +
                                     QString::number(bluetoothManager->device()->wattKg().average(), 'f', 1));
                        if (settings.value(QZSettings::tts_max_watt_kg, QZSettings::default_tts_max_watt_kg).toBool())
                            s.append((description ? tr(", max watt for kilograms") : ",") +
                                     QString::number(bluetoothManager->device()->wattKg().max(), 'f', 1));

                        qDebug() << "tts" << s;
                        m_speech.say(s);
                    } else if (bluetoothManager->device()->deviceType() == TREADMILL &&
                               bluetoothManager->device()->currentSpeed().value() != tts_speed_played &&
                               settings.value(QZSettings::tts_act_speed, QZSettings::default_tts_act_speed).toBool()) {
                        tts_speed_played = bluetoothManager->device()->currentSpeed().value();
                        QString s;
                        s.append((description ? tr("speed changed to") : "") +
                                 (!miles ? QString::number(bluetoothManager->device()->currentSpeed().value(), 'f', 1) +
                                               (description ? tr(" kilometers per hour") : "")
                                         : QString::number(bluetoothManager->device()->currentSpeed().value() *
                                                               unit_conversion,
                                                           'f', 1)) +
                                 (description ? tr(" miles per hour") : ""));
                        qDebug() << "tts" << s;
                        m_speech.say(s);
                    }
                }
            }

            bool treadmill_direct_distance = settings.value(QZSettings::treadmill_direct_distance, QZSettings::default_treadmill_direct_distance).toBool();
            double distance1s = 0;
            if (treadmill_direct_distance) {
                distance1s = bluetoothManager->device()->odometer();
            } else {
                if(bluetoothManager->device()->currentSpeed().value() > 0 && !isinf(bluetoothManager->device()->currentSpeed().value()))
                    bluetoothManager->device()->addCurrentDistance1s((bluetoothManager->device()->currentSpeed().value() / 3600.0));
                distance1s = bluetoothManager->device()->currentDistance1s().value();
            }

            qDebug() << "Current Distance 1s:" << distance1s << bluetoothManager->device()->currentSpeed().value() << watts;

            // Calculate current elapsed time in seconds
            uint32_t currentElapsedSeconds = bluetoothManager->device()->elapsedTime().second() +
                (bluetoothManager->device()->elapsedTime().minute() * 60) +
                (bluetoothManager->device()->elapsedTime().hour() * 3600);

            if (Session.empty()) {
                currentUpdateJitter = 0;
            }

            // Check for timer jitter gaps and fill missing SessionLine records (same logic as trainprogram)
            if (!Session.empty() && qAbs(currentUpdateJitter) > 1000) {
                if (currentUpdateJitter > 1000) {
                    // We are late... fill the missing seconds with SessionLine records
                    int missedSeconds = currentUpdateJitter / 1000;
                    qDebug() << "Timer jitter detected: filling" << missedSeconds << "missing SessionLine records";
                    
                    // Create SessionLine records for each missed second using current device values
                    uint32_t lastRecordedTime = Session.last().elapsedTime;
                    for (int i = 1; i <= missedSeconds; i++) {
                        SessionLine gapFill(
                            bluetoothManager->device()->currentSpeed().value(), inclination, distance1s,
                            watts, resistance, peloton_resistance, (uint8_t)bluetoothManager->device()->currentHeart().value(),
                            pace, cadence, bluetoothManager->device()->calories().value(),
                            bluetoothManager->device()->elevationGain().value(),
                            bluetoothManager->device()->negativeElevationGain().value(),
                            lastRecordedTime + i,  // Fill each missing second
                            lapTrigger, totalStrokes, avgStrokesRate, maxStrokesRate, avgStrokesLength,
                            bluetoothManager->device()->currentCordinate(), strideLength, groundContact, verticalOscillation, stepCount,
                            target_cadence->value().toDouble(), target_power->value().toDouble(), target_resistance->value().toDouble(),
                            target_incline->value().toDouble(), target_speed->value().toDouble(),
                            bluetoothManager->device()->CoreBodyTemperature.value(), bluetoothManager->device()->SkinTemperature.value(), bluetoothManager->device()->HeatStrainIndex.value(),
                            0.0, QList<double>());
                        
                        Session.append(gapFill);
                        qDebug() << "Added gap-filling SessionLine for elapsed time:" << (lastRecordedTime + i);
                    }
                    
                    // Adjust jitter counter (same as trainprogram)
                    currentUpdateJitter -= (missedSeconds * 1000);
                } else if (currentUpdateJitter < -1000) {
                    // We are early (negative jitter)... remove excess SessionLine records
                    int excessSeconds = (-currentUpdateJitter) / 1000;
                    qDebug() << "Negative timer jitter detected: removing" << excessSeconds << "excess SessionLine records";
                    
                    // Remove excess SessionLine records from the end
                    for (int i = 0; i < excessSeconds && !Session.empty(); i++) {
                        Session.removeLast();
                        qDebug() << "Removed excess SessionLine record";
                    }
                    
                    // Adjust jitter counter (same as trainprogram)
                    currentUpdateJitter += (excessSeconds * 1000);
                }
            }

            SessionLine s(
                bluetoothManager->device()->currentSpeed().value(), inclination, distance1s,
                watts, resistance, peloton_resistance, (uint8_t)bluetoothManager->device()->currentHeart().value(),
                pace, cadence, bluetoothManager->device()->calories().value(),
                bluetoothManager->device()->elevationGain().value(),
                bluetoothManager->device()->negativeElevationGain().value(),
                currentElapsedSeconds,

                lapTrigger, totalStrokes, avgStrokesRate, maxStrokesRate, avgStrokesLength,
                bluetoothManager->device()->currentCordinate(), strideLength, groundContact, verticalOscillation, stepCount,
                target_cadence->value().toDouble(), target_power->value().toDouble(), target_resistance->value().toDouble(),
                target_incline->value().toDouble(), target_speed->value().toDouble(),
                bluetoothManager->device()->CoreBodyTemperature.value(), bluetoothManager->device()->SkinTemperature.value(), bluetoothManager->device()->HeatStrainIndex.value(),
                bluetoothManager->device()->currentHRV().value(),
                bluetoothManager->device()->getRRIntervalsAndClear());

            Session.append(s);

            if (lapTrigger) {
                lapTrigger = false;
            }

#ifndef Q_OS_IOS
            if (iphone_socket && iphone_socket->state() == QAbstractSocket::ConnectedState) {
                QSettings mdns_settings;
                bool activeOnly = mdns_settings.value(QZSettings::calories_active_only, QZSettings::default_calories_active_only).toBool();
                
                QString toSend =
                    "SENDER=PAD#HR=" + QString::number(bluetoothManager->device()->currentHeart().value()) +
                    "#KCAL=" + QString::number(bluetoothManager->device()->calories().value()) +
                    (activeOnly ? "#TOTALKCAL=" + QString::number(bluetoothManager->device()->totalCalories().value()) : "") +
                    "#BCAD=" + QString::number(bluetoothManager->device()->currentCadence().value()) +
                    "#SPD=" + QString::number(bluetoothManager->device()->currentSpeed().value()) +
                    "#PWR=" + QString::number(bluetoothManager->device()->wattsMetric().value()) +
                    "#CAD=" + QString::number(bluetoothManager->device()->currentCadence().value()) +
                    "#ODO=" + QString::number(bluetoothManager->device()->odometer()) + "#";
                int write = iphone_socket->write(toSend.toLocal8Bit(), toSend.length());
                qDebug() << "iphone_socket send " << write << toSend;
            }
#endif
        }
        emit workoutStartDateChanged(workoutStartDate());
    }

    emit changeOfdevice();
    emit changeOflap();

    // Last, so the overlay shows the values this tick settled on.
    updateRtssOsd();
}

bool homeform::getDevice() {

    static bool toggle = false;
    if (!this->bluetoothManager->device()) {

        // toggling the bluetooth icon
        toggle = !toggle;
        return toggle;
    }
    return this->bluetoothManager->device()->connected();
}

bool homeform::getLap() {
    if (!this->bluetoothManager->device()) {

        return false;
    }
    return true;
}

QString homeform::getFileNameFromContentUri(const QString &uriString) {
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

QString homeform::copyAndroidContentsURI(QUrl file, QString subfolder) {
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

void homeform::profile_open_clicked(const QUrl &fileName) {
#ifdef Q_OS_ANDROID
    const QString copiedFile = copyAndroidContentsURI(fileName, "profiles");
    if (!copiedFile.isEmpty()) {
        loadSettings(QUrl::fromLocalFile(copiedFile));
    }
#else
    QFile file(QQmlFile::urlToLocalFileOrQrc(fileName));
    QFileInfo fileInfo(file);
    bool r = file.copy(getWritableAppDir() + "profiles/" + fileInfo.fileName());
    qDebug() << "profile copy" << r << getWritableAppDir() + "profiles/" + fileInfo.fileName();
#endif
}

void homeform::trainprogram_open_other_folder(const QUrl &fileName) {
    const QString copiedFile = copyAndroidContentsURI(fileName, "training");
    if (!copiedFile.isEmpty()) {
        trainprogram_open_clicked(QUrl::fromLocalFile(copiedFile));
    }
}

void homeform::gpx_open_other_folder(const QUrl &fileName) {
    const QString copiedFile = copyAndroidContentsURI(fileName, "gpx");
    if (!copiedFile.isEmpty()) {
        gpx_open_clicked(QUrl::fromLocalFile(copiedFile));
    }
}

bool homeform::startTrainingProgramFromFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        return false;
    }
    QUrl url(filePath);
    if (!url.isValid() || (!url.isLocalFile() && url.scheme().isEmpty())) {
        url = QUrl::fromLocalFile(filePath);
    }
    QString localPath = QQmlFile::urlToLocalFileOrQrc(url);
    if (localPath.isEmpty() || !QFile::exists(localPath)) {
        return false;
    }
    trainprogram_open_clicked(QUrl::fromLocalFile(localPath));
    return true;
}

void homeform::openAndroidDocumentPicker(const QString &kind) {
#ifdef Q_OS_ANDROID
    int requestCode = 0;
    QString mimeType = QStringLiteral("*/*");
    QString destinationDir;
    if (kind == QStringLiteral("profile")) {
        requestCode = AndroidDocumentPickerProfileRequestCode;
        destinationDir = getWritableAppDir() + QStringLiteral("profiles/");
    } else if (kind == QStringLiteral("training")) {
        requestCode = AndroidDocumentPickerTrainingRequestCode;
        mimeType = QStringLiteral("*/*");
        destinationDir = getWritableAppDir() + QStringLiteral("training/");
    } else if (kind == QStringLiteral("gpx")) {
        requestCode = AndroidDocumentPickerGpxRequestCode;
        mimeType = QStringLiteral("*/*");
        destinationDir = getWritableAppDir() + QStringLiteral("gpx/");
    } else if (kind == QStringLiteral("settings")) {
        requestCode = AndroidDocumentPickerSettingsRequestCode;
        destinationDir = getWritableAppDir() + QStringLiteral("settings/");
    } else {
        qWarning() << "Unknown Android document picker kind" << kind;
        return;
    }

    QAndroidJniObject javaMimeType = QAndroidJniObject::fromString(mimeType);
    QAndroidJniObject javaDestinationDir = QAndroidJniObject::fromString(destinationDir);
    QtAndroid::androidActivity().callMethod<void>("openDocumentPicker", "(Ljava/lang/String;ILjava/lang/String;)V",
                                                  javaMimeType.object<jstring>(), requestCode,
                                                  javaDestinationDir.object<jstring>());
    if (clearAndroidJniException("CustomQtActivity.openDocumentPicker")) {
        return;
    }
#else
    Q_UNUSED(kind)
#endif
}

void homeform::handleAndroidDocumentPicked(int requestCode, const QString &localPath) {
#ifdef Q_OS_ANDROID
    if (localPath.isEmpty()) {
        qWarning() << "Android document picker returned empty local path for request code" << requestCode;
        return;
    }

    const QUrl localUrl = QUrl::fromLocalFile(localPath);
    QString kind;
    switch (requestCode) {
    case AndroidDocumentPickerProfileRequestCode:
        kind = QStringLiteral("profile");
        break;
    case AndroidDocumentPickerTrainingRequestCode:
        kind = QStringLiteral("training");
        break;
    case AndroidDocumentPickerGpxRequestCode:
        kind = QStringLiteral("gpx");
        break;
    case AndroidDocumentPickerSettingsRequestCode:
        kind = QStringLiteral("settings");
        break;
    default:
        qWarning() << "Unknown Android document picker request code" << requestCode << localPath;
        return;
    }

    emit androidDocumentPicked(kind, localUrl);
#else
    Q_UNUSED(requestCode)
    Q_UNUSED(localPath)
#endif
}

bool homeform::deleteTrainingProgramFile(const QString &fileUrl) {
    if (fileUrl.isEmpty()) {
        return false;
    }

    QUrl url(fileUrl);
    if (!url.isValid() || (!url.isLocalFile() && url.scheme().isEmpty())) {
        url = QUrl::fromLocalFile(fileUrl);
    }

    const QString localPath = QQmlFile::urlToLocalFileOrQrc(url);
    QFileInfo fileInfo(localPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }

    const QString trainingRoot = QDir(homeform::getWritableAppDir() + QStringLiteral("training")).canonicalPath();
    const QString canonicalFilePath = fileInfo.canonicalFilePath();
    if (trainingRoot.isEmpty() || canonicalFilePath.isEmpty() ||
        !canonicalFilePath.startsWith(trainingRoot + QStringLiteral("/"), Qt::CaseInsensitive)) {
        qDebug() << "deleteTrainingProgramFile: refusing to delete outside training folder" << localPath;
        return false;
    }

    if (!QFile::remove(canonicalFilePath)) {
        return false;
    }

    QFile markerFile(fileInfo.absolutePath() + QStringLiteral("/.deleted_") + fileInfo.fileName());
    if (markerFile.open(QIODevice::WriteOnly)) {
        markerFile.write("This file was intentionally deleted by the user");
        markerFile.close();
    }
    return true;
}

void homeform::trainprogram_open_clicked(const QUrl &fileName) {
    qDebug() << QStringLiteral("trainprogram_open_clicked") << fileName;

    QFile file(QQmlFile::urlToLocalFileOrQrc(fileName));

    if (!file.fileName().isEmpty()) {
        {
            if (previewTrainProgram) {
                delete previewTrainProgram;
                previewTrainProgram = 0;
            }
            if (trainProgram) {
                delete trainProgram;
            }

            trainProgram = trainprogram::load(file.fileName(), bluetoothManager, file.fileName().right(3).toUpper());

            QString movieName = file.fileName().left(file.fileName().length() - 3) + "mp4";
            if (QFile::exists(movieName)) {
                qDebug() << movieName << QStringLiteral("exist!");
                movieFileName = QUrl::fromLocalFile(movieName);
                emit videoPathChanged(movieFileName);
                setVideoIconVisible(true);
                setVideoRate(1);
                trainingProgram()->setVideoAvailable(true);
            } else {
                qDebug() << movieName << QStringLiteral("doesn't exist!");
                movieFileName = "";
                setVideoIconVisible(false);
                trainingProgram()->setVideoAvailable(false);
            }

            stravaWorkoutName = QFileInfo(file.fileName()).baseName();
            stravaPelotonInstructorName = QStringLiteral("");
            emit workoutNameChanged(workoutName());
            emit instructorNameChanged(instructorName());

            QSettings settings;
            if (settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {
                m_info = workoutName();
                emit infoChanged(m_info);
            }
        }

        trainProgramSignals();
    }
}

void homeform::trainprogram_autostart_requested() {
    qDebug() << QStringLiteral("trainprogram_autostart_requested");

    bluetoothdevice *dev = nullptr;
    if (bluetoothManager) {
        dev = bluetoothManager->device();
    }

    if (dev && !dev->isPaused()) {
        // Device is running, call Start() twice (pause then start)
        QMetaObject::invokeMethod(this, "Start", Qt::QueuedConnection);
        QThread::msleep(200);
        QMetaObject::invokeMethod(this, "Start", Qt::QueuedConnection);
    } else {
        // Device is paused/stopped, call Start() once
        QMetaObject::invokeMethod(this, "Start", Qt::QueuedConnection);
    }
}

void homeform::checkClipboardForWorkout() {
    QClipboard *clipboard = QApplication::clipboard();
    const QString clipboardText = clipboard ? clipboard->text().trimmed() : QString();
    const QByteArray currentHash = QCryptographicHash::hash(clipboardText.toUtf8(), QCryptographicHash::Sha1);

    if (currentHash == m_lastClipboardWorkoutHash) {
        return;
    }

    m_lastClipboardWorkoutHash = currentHash;
    m_clipboardWorkoutPromptFile.clear();
    m_clipboardWorkoutPromptName.clear();
    emit clipboardWorkoutPromptNameChanged(m_clipboardWorkoutPromptName);
    setClipboardWorkoutPromptRequested(false);

    if (clipboardText.isEmpty()) {
        return;
    }

    const QString rootName = firstXmlElementName(clipboardText);
    if (rootName.isEmpty()) {
        return;
    }

    QString displayName;
    QString filePath;
    QList<trainrow> rows;
    const bool looksLikeZwo = rootName.compare(QStringLiteral("workout_file"), Qt::CaseInsensitive) == 0 ||
                              rootName.compare(QStringLiteral("Workout"), Qt::CaseInsensitive) == 0;

    if (looksLikeZwo) {
        QString description;
        QString tags;
        rows = zwiftworkout::load(clipboardText.toUtf8(), &description, &tags);
        if (rows.isEmpty()) {
            return;
        }
        filePath = uniqueClipboardWorkoutPath(QStringLiteral("zwo"), &displayName);
        if (!writeClipboardWorkoutFile(filePath, clipboardText)) {
            return;
        }
    } else {
        filePath = uniqueClipboardWorkoutPath(QStringLiteral("xml"), &displayName);
        if (!writeClipboardWorkoutFile(filePath, clipboardText)) {
            return;
        }

        BLUETOOTH_TYPE dtype = BLUETOOTH_TYPE::BIKE;
        if (bluetoothManager && bluetoothManager->device()) {
            dtype = bluetoothManager->device()->deviceType();
        }
        rows = trainprogram::loadXML(filePath, dtype);
        if (rows.isEmpty()) {
            QFile::remove(filePath);
            return;
        }
    }

    m_clipboardWorkoutPromptFile = filePath;
    m_clipboardWorkoutPromptName = displayName;
    emit clipboardWorkoutPromptNameChanged(m_clipboardWorkoutPromptName);
    setClipboardWorkoutPromptRequested(true);
}

void homeform::clipboard_accept_workout_prompt() {
    m_activeClipboardWorkoutFile = m_clipboardWorkoutPromptFile;
    clipboard_dismiss_workout_prompt();
}

void homeform::clipboard_dismiss_workout_prompt() {
    m_clipboardWorkoutPromptFile.clear();
    m_clipboardWorkoutPromptName.clear();
    emit clipboardWorkoutPromptNameChanged(m_clipboardWorkoutPromptName);
    setClipboardWorkoutPromptRequested(false);
}

void homeform::clipboard_delete_finished_workout() {
    if (!m_activeClipboardWorkoutFile.isEmpty()) {
        QFile::remove(m_activeClipboardWorkoutFile);
        m_activeClipboardWorkoutFile.clear();
    }
    setClipboardWorkoutDeletePromptRequested(false);
}

void homeform::clipboard_keep_finished_workout() {
    m_activeClipboardWorkoutFile.clear();
    setClipboardWorkoutDeletePromptRequested(false);
}

void homeform::trainprogram_preview(const QUrl &fileName) {
    qDebug() << QStringLiteral("trainprogram_preview") << fileName;

    QFile file(QQmlFile::urlToLocalFileOrQrc(fileName));
    QString fileNameLocal = getFileNameFromContentUri(file.fileName());
    qDebug() << fileNameLocal;
    if (!fileNameLocal.isEmpty()) {
        {
            if (previewTrainProgram) {
                delete previewTrainProgram;
                previewTrainProgram = 0;
            }
            previewTrainProgram = trainprogram::load(file.fileName(), bluetoothManager, fileNameLocal.right(3).toUpper());
            emit previewWorkoutPointsChanged(preview_workout_points());
            emit previewWorkoutDescriptionChanged(previewWorkoutDescription());
            emit previewWorkoutTagsChanged(previewWorkoutTags());
        }
    }
}


void homeform::trainprogram_zwo_loaded(const QString &s) {
    qDebug() << QStringLiteral("trainprogram_zwo_loaded") << s;
    trainProgram = new trainprogram(zwiftworkout::loadJSON(s), bluetoothManager);
    if (trainProgram) {
        QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains(QStringLiteral("name"))) {
                stravaPelotonActivityName = obj[QStringLiteral("name")].toString();
                stravaPelotonInstructorName = QStringLiteral("");
                emit workoutNameChanged(workoutName());
                emit instructorNameChanged(instructorName());

                QSettings settings;
                if (!settings.value(QZSettings::top_bar_enabled, QZSettings::default_top_bar_enabled).toBool()) {
                    return;
                }
                m_info = workoutName();
                emit infoChanged(m_info);
            }
        }
    }
    trainProgramSignals();
}

void homeform::gpx_save_clicked() {

    QString path = getWritableAppDir();

    if (bluetoothManager->device()) {
        gpx::save(path + QDateTime::currentDateTime().toString().replace(QStringLiteral(":"), QStringLiteral("_")) +
                      QStringLiteral(".gpx"),
                  Session, bluetoothManager->device()->deviceType());
    }
}

void homeform::saveSessionAsTrainingProgram() {
    if (Session.isEmpty()) {
        return;
    }

    QString path = getWritableAppDir();
    bluetoothdevice *dev = bluetoothManager->device();
    if (!dev) {
        return;
    }

    // Determine subdirectory based on device type
    QString subdir;
    if (dev->deviceType() == BIKE) {
        subdir = "ride/";
    } else if (dev->deviceType() == TREADMILL) {
        subdir = "run/";
    } else if (dev->deviceType() == ROWING) {
        subdir = "row/";
    } else {
        subdir = "workout/";
    }

    // Create the subdirectory if it doesn't exist
    QDir dir(path + subdir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString filename = path + subdir + 
                      QDateTime::currentDateTime().toString().replace(QStringLiteral(":"), QStringLiteral("_")) +
                      QStringLiteral("_session.xml");

    // Convert Session data to trainrow format
    QList<trainrow> rows;
    for (int i = 0; i < Session.size(); i++) {
        const SessionLine &sessionLine = Session[i];
        trainrow row;
        
        // Set duration to 1 second since we collect data every second
        row.duration = QTime(0, 0, 1);
        
        // Set target values based on device type
        if (dev->deviceType() == BIKE) {
            if (sessionLine.target_watt > 0) {
                row.power = static_cast<int32_t>(sessionLine.target_watt);
            }
            if (sessionLine.target_cadence > 0) {
                row.cadence = static_cast<int16_t>(sessionLine.target_cadence);
            }
            if (sessionLine.target_resistance >= 0) {
                row.resistance = sessionLine.target_resistance;
            }
        } else if (dev->deviceType() == TREADMILL) {
            if (sessionLine.target_speed > 0) {
                row.speed = sessionLine.target_speed;
            }
            if (sessionLine.target_inclination >= -50) {
                row.inclination = sessionLine.target_inclination;
            }
        }
        
        rows.append(row);
    }

    // Save the XML file
    if (trainprogram::saveXML(filename, rows, dev ? dev->deviceType() : UNKNOWN)) {
        lastTrainProgramFileSaved = filename;
        qDebug() << "Session saved as training program:" << filename;
    }
}

#ifdef Q_OS_ANDROID
static void healthConnectWriteWorkout(const QList<SessionLine> &session, bluetoothdevice *dev, const QString &workoutName) {
    if (!dev || session.isEmpty()) {
        return;
    }

    QJsonArray samples;
    for (const SessionLine &line : session) {
        if (!line.time.isValid()) {
            continue;
        }

        QJsonObject sample;
        sample.insert(QStringLiteral("time"), static_cast<double>(line.time.toMSecsSinceEpoch()));
        sample.insert(QStringLiteral("speed"), line.speed);
        sample.insert(QStringLiteral("inclination"), line.inclination);
        sample.insert(QStringLiteral("distance"), line.distance);
        sample.insert(QStringLiteral("watt"), static_cast<int>(line.watt));
        sample.insert(QStringLiteral("resistance"), line.resistance);
        sample.insert(QStringLiteral("peloton_resistance"), line.peloton_resistance);
        sample.insert(QStringLiteral("heart"), static_cast<int>(line.heart));
        sample.insert(QStringLiteral("pace"), line.pace);
        sample.insert(QStringLiteral("cadence"), static_cast<int>(line.cadence));
        sample.insert(QStringLiteral("calories"), line.calories);
        sample.insert(QStringLiteral("elevationGain"), line.elevationGain);
        sample.insert(QStringLiteral("negativeElevationGain"), line.negativeElevationGain);
        sample.insert(QStringLiteral("elapsedTime"), static_cast<int>(line.elapsedTime));
        sample.insert(QStringLiteral("totalStrokes"), static_cast<int>(line.totalStrokes));
        sample.insert(QStringLiteral("avgStrokesRate"), line.avgStrokesRate);
        sample.insert(QStringLiteral("maxStrokesRate"), line.maxStrokesRate);
        sample.insert(QStringLiteral("avgStrokesLength"), line.avgStrokesLength);
        sample.insert(QStringLiteral("instantaneousStrideLengthCM"), line.instantaneousStrideLengthCM);
        sample.insert(QStringLiteral("groundContactMS"), line.groundContactMS);
        sample.insert(QStringLiteral("verticalOscillationMM"), line.verticalOscillationMM);
        sample.insert(QStringLiteral("stepCount"), line.stepCount);
        sample.insert(QStringLiteral("coreTemp"), line.coreTemp);
        sample.insert(QStringLiteral("bodyTemp"), line.bodyTemp);
        sample.insert(QStringLiteral("heatStrainIndex"), line.heatStrainIndex);
        sample.insert(QStringLiteral("hrv"), line.hrv);

        if (line.coordinate.isValid()) {
            sample.insert(QStringLiteral("latitude"), line.coordinate.latitude());
            sample.insert(QStringLiteral("longitude"), line.coordinate.longitude());
            sample.insert(QStringLiteral("altitude"), line.coordinate.altitude());
        }

        samples.append(sample);
    }

    if (samples.isEmpty()) {
        return;
    }

    QAndroidJniObject activity = QAndroidJniObject::callStaticObjectMethod(
        "org/qtproject/qt5/android/QtNative", "activity", "()Landroid/app/Activity;");
    if (!activity.isValid()) {
        qDebug() << "Health Connect upload skipped: Android activity is not available";
        return;
    }

    const QString title = workoutName.isEmpty() ? QStringLiteral("QZ workout") : workoutName;
    QAndroidJniObject jTitle = QAndroidJniObject::fromString(title);
    QAndroidJniObject jDeviceName = QAndroidJniObject::fromString(dev->bluetoothDevice.name());
    QAndroidJniObject jSamplesJson =
        QAndroidJniObject::fromString(QString::fromUtf8(QJsonDocument(samples).toJson(QJsonDocument::Compact)));

    QAndroidJniObject::callStaticMethod<void>(
        "org/cagnulen/qdomyoszwift/HealthConnectHelper", "writeWorkoutJson",
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;)V",
        activity.object<jobject>(), jTitle.object<jstring>(), static_cast<jint>(dev->deviceType()),
        jDeviceName.object<jstring>(), jSamplesJson.object<jstring>());
}
#endif

void homeform::gpx_open_clicked(const QUrl &fileName) {
    qDebug() << QStringLiteral("gpx_open_clicked") << fileName;

    QFile file(QQmlFile::urlToLocalFileOrQrc(fileName));

    stravaWorkoutName = QFileInfo(file.fileName()).baseName();
    if (!file.fileName().isEmpty()) {
        {
            if (trainProgram) {

                delete trainProgram;
            }

            // KML to GPX https://www.gpsvisualizer.com/elevation
            gpx g;
            QList<trainrow> list;
            auto g_list = g.open(file.fileName(), bluetoothManager->device() ? bluetoothManager->device()->deviceType() : BIKE);
            if (bluetoothManager->device())
                bluetoothManager->device()->setGPXFile(file.fileName());
            gpx_altitude_point_for_treadmill last;
            quint32 i = 0;
            list.reserve(g_list.size() + 1);
            for (const auto &p : g_list) {
                trainrow r;
                if (p.speed > 0 && i > 0) {
                    QGeoCoordinate p1(last.latitude, last.longitude);
                    QGeoCoordinate p2(p.latitude, p.longitude, p.elevation);
                    r.azimuth = p1.azimuthTo(p2);
                    r.speed = p.speed;
                    r.distance = p.distance;
                    r.duration = QTime(0, 0, 0, 0);
                    r.duration = r.duration.addSecs(p.seconds);
                    r.forcespeed = true;

                    r.altitude = last.elevation;
                    r.inclination = p.inclination;
                    r.latitude = last.latitude;
                    r.longitude = last.longitude;
                    r.gpxElapsed = QTime(0, 0, 0).addSecs(p.seconds);

                    list.append(r);

                } else {
                    if (i > 0) {
                        QGeoCoordinate p1(last.latitude, last.longitude);
                        QGeoCoordinate p2(p.latitude, p.longitude, p.elevation);
                        r.azimuth = p1.azimuthTo(p2);
                        r.distance = p.distance;
                        r.altitude = last.elevation;
                        r.inclination = p.inclination;
                        r.latitude = last.latitude;
                        r.longitude = last.longitude;
                        r.gpxElapsed = QTime(0, 0, 0).addSecs(p.seconds);

                        list.append(r);
                    }
                }

                last = p;
                i++;
            }
            setMapsVisible(true);
            if (g.getVideoURL().isEmpty() == false) {
                movieFileName = QUrl(g.getVideoURL());
                emit videoPathChanged(movieFileName);
                setVideoIconVisible(true);
            } else if (QFile::exists(file.fileName().replace(".gpx", ".mp4"))) {
                movieFileName = QUrl::fromLocalFile(file.fileName().replace(".gpx", ".mp4"));
                emit videoPathChanged(movieFileName);
                setVideoIconVisible(true);
            }
            trainProgram = new trainprogram(list, bluetoothManager, nullptr, nullptr, videoIconVisible());
        }

        trainProgramSignals();
    }
}

void homeform::gpxpreview_open_clicked(const QUrl &fileName) {
    qDebug() << QStringLiteral("gpxpreview_open_clicked") << fileName;

    QFile file(QQmlFile::urlToLocalFileOrQrc(fileName));
    qDebug() << file.fileName();

    if (!file.fileName().isEmpty()) {
        gpx g;
        // Force no loop for preview to show actual GPX distance
        auto g_list = g.open(file.fileName(), bluetoothManager->device() ? bluetoothManager->device()->deviceType() : BIKE, true);
        gpx_preview.clearPath();
        for (const auto &p : g_list) {
            gpx_preview.addCoordinate(QGeoCoordinate(p.latitude, p.longitude, p.elevation));
        }
        // Set distance BEFORE setGeoPath to ensure QML onGeopathChanged has correct value
        pathController.setDistance(g.getTotalDistance());
        pathController.setGeoPath(gpx_preview);
        pathController.setCenter(gpx_preview.center());
    }
}

QStringList homeform::bluetoothDevices() {

    QStringList r;
    r.append(QStringLiteral("Disabled"));
    r.append(QStringLiteral("Wifi"));

    // Collect named devices and sort by RSSI descending (strongest signal first)
    QList<QBluetoothDeviceInfo> sorted;
    for (const QBluetoothDeviceInfo &b : qAsConst(bluetoothManager->devices)) {
        if (!b.name().trimmed().isEmpty()) {
            sorted.append(b);
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const QBluetoothDeviceInfo &a, const QBluetoothDeviceInfo &b) {
        return a.rssi() > b.rssi();
    });

    for (const QBluetoothDeviceInfo &b : qAsConst(sorted)) {
        // Convert RSSI to proximity %: -40 dBm = 100%, -100 dBm = 0%
        int proximity = qBound(0, (int)((b.rssi() + 100) * 100 / 60), 100);
        r.append(QString("%1 (%2%)").arg(b.name()).arg(proximity));
    }
    return r;
}

QStringList homeform::metrics() { return bluetoothdevice::metrics(); }

void homeform::clearWebViewCache() {
#ifdef Q_OS_ANDROID
    QtAndroid::runOnAndroidThread([] {
        QAndroidJniObject cookieManager = QAndroidJniObject::callStaticObjectMethod(
            "android/webkit/CookieManager",
            "getInstance",
            "()Landroid/webkit/CookieManager;");
        if (cookieManager.isValid()) {
            cookieManager.callMethod<void>("removeAllCookies", "(Landroid/webkit/ValueCallback;)V",
                                          static_cast<jobject>(nullptr));
            cookieManager.callMethod<void>("flush", "()V");
        }
        QAndroidJniEnvironment env;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        qDebug() << "Android: WebView cookies cleared";
    });
#endif
#ifdef Q_OS_IOS
#ifndef IO_UNDER_QT
    lockscreen::clearWebViewCache();
    qDebug() << "iOS: WebView cache cleared";
#endif
#endif
}

bool homeform::generalPopupVisible() { return m_generalPopupVisible; }

void homeform::setGeneralPopupVisible(bool value) {

    m_generalPopupVisible = value;
    emit generalPopupVisibleChanged(m_generalPopupVisible);
}

bool homeform::licensePopupVisible() { return m_LicensePopupVisible; }

void homeform::setLicensePopupVisible(bool value) {

    m_LicensePopupVisible = value;
    emit licensePopupVisibleChanged(m_LicensePopupVisible);
}

bool homeform::mapsVisible() { return m_MapsVisible; }

void homeform::setMapsVisible(bool value) {

    m_MapsVisible = value;
    emit mapsVisibleChanged(m_MapsVisible);
}

bool homeform::videoIconVisible() { return m_VideoIconVisible; }

void homeform::setVideoIconVisible(bool value) {

    m_VideoIconVisible = value;
    emit videoIconVisibleChanged(m_VideoIconVisible);
}

int homeform::videoPosition() { return m_VideoPosition; }

void homeform::setVideoPosition(int value) {

    m_VideoPosition = value;
    emit videoPositionChanged(m_VideoPosition);
}

double homeform::videoRate() { return m_VideoRate; }

void homeform::setVideoRate(double value) {

    m_VideoRate = value;
    emit videoRateChanged(m_VideoRate);
}

#if defined(Q_OS_ANDROID)

QString homeform::getBluetoothName()
{
    QAndroidJniObject bluetoothAdapter = QAndroidJniObject::callStaticObjectMethod(
        "android/bluetooth/BluetoothAdapter",
        "getDefaultAdapter",
        "()Landroid/bluetooth/BluetoothAdapter;");
    
    if (bluetoothAdapter.isValid()) {
        QAndroidJniObject name = bluetoothAdapter.callObjectMethod(
            "getName",
            "()Ljava/lang/String;");
        
        if (name.isValid()) {
            return name.toString();
        }
    }
    
    return QString();
}

QString homeform::getAndroidDataAppDir() {
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
}
#endif

quint64 homeform::cryptoKeySettingsProfiles() {
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

void homeform::saveSettings(const QUrl &filename) {
    Q_UNUSED(filename)
    QString path = getWritableAppDir();

    QDir().mkdir(path + QStringLiteral("settings/"));
    QSettings settings;
    QSettings settings2Save(path + QStringLiteral("settings/settings_") +
                                settings.value(QZSettings::profile_name).toString() + QStringLiteral("_") +
                                QDateTime::currentDateTime().toString("yyyyMMddhhmmss") + QStringLiteral(".qzs"),
                            QSettings::IniFormat);
    auto settigsAllKeys = settings.allKeys();
    for (const QString &s : qAsConst(settigsAllKeys)) {
        if (!s.contains(QZSettings::cryptoKeySettingsProfiles)) {
            if (!s.contains(QStringLiteral("password")) && !s.contains(QStringLiteral("token"))) {
                settings2Save.setValue(s, settings.value(s));
            } else {
                SimpleCrypt crypt;
                crypt.setKey(cryptoKeySettingsProfiles());
                settings2Save.setValue(s, crypt.encryptToString(settings.value(s).toString()));
            }
        }
    }
}

void homeform::loadSettings(const QUrl &filename) {

    QFile file(QQmlFile::urlToLocalFileOrQrc(filename));
    QString settingsFile = file.fileName();
#ifdef Q_OS_ANDROID
    const QString copiedSettingsFile = copyAndroidContentsURI(filename, "settings");
    if (!copiedSettingsFile.isEmpty()) {
        settingsFile = copiedSettingsFile;
    }
#endif

    qDebug() << "homeform::loadSettings" << file.fileName();

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
    
    // Emit signal when settings are loaded as they might contain user profile changes
    if (homeform::singleton()) {
        emit homeform::singleton()->userProfileChanged();
    }
}

void homeform::deleteSettings(const QUrl &filename) { QFile(filename.toLocalFile()).remove(); }
void homeform::restoreSettings() { 
    QZSettings::restoreAll(); 
    // Emit signal when settings are restored as this might affect user profiles
    emit userProfileChanged();
}

QString homeform::getProfileDir() {
    QString path = getWritableAppDir() + "profiles";
    QDir().mkdir(path);
    return path;
}

void homeform::saveProfile(QString profilename) {
    qDebug() << "homeform::saveProfile";
    QString path = getProfileDir();

    QSettings settings;
    settings.setValue(QZSettings::profile_name, profilename);
    QSettings settings2Save(path + "/" + profilename + QStringLiteral(".qzs"), QSettings::IniFormat);
    auto settigsAllKeys = settings.allKeys();
    for (const QString &s : qAsConst(settigsAllKeys)) {
        if (!s.contains(QZSettings::cryptoKeySettingsProfiles)) {
            if (!s.contains(QStringLiteral("password")) && !s.contains(QStringLiteral("token"))) {
                settings2Save.setValue(s, settings.value(s));
            } else {
                SimpleCrypt crypt;
                crypt.setKey(cryptoKeySettingsProfiles());
                settings2Save.setValue(s, crypt.encryptToString(settings.value(s).toString()));
            }
        }
    }
}

void homeform::restart() {
    qApp->quit();
#if !defined(Q_OS_DARWIN) && !defined(Q_OS_IOS) && !defined(Q_OS_WINRT)
    QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
#endif
}

double homeform::heartRateMax() {
    QSettings settings;
    double maxHeartRate = 220.0 - settings.value(QZSettings::age, QZSettings::default_age).toDouble();

    if (settings.value(QZSettings::heart_max_override_enable, QZSettings::default_heart_max_override_enable).toBool())
        maxHeartRate =
            settings.value(QZSettings::heart_max_override_value, QZSettings::default_heart_max_override_value)
                .toDouble();
    if (maxHeartRate == 0) {
        maxHeartRate = 190.0;
    }
    return maxHeartRate;
}

void homeform::clearFiles() {
    QString path = homeform::getWritableAppDir();
    QDir dir(path);
    QFileInfoList list = dir.entryInfoList(QDir::Files);
    foreach (QFileInfo f, list) {
        if (!f.suffix().toLower().compare("log") || !f.suffix().toLower().compare("jpg") ||
            !f.suffix().toLower().compare("fit") || !f.suffix().toLower().compare("png")) {
            QFile::remove(f.filePath());
        }
    }
}

int homeform::preview_workout_points() {
    if (previewTrainProgram) {
        QTime d = previewTrainProgram->duration();
        return (d.hour() * 3600) + (d.minute() * 60) + d.second();
    }
    return 0;
}

#if defined(LICENSE) && (defined(Q_OS_WIN) || (defined(Q_OS_MAC) && !defined(Q_OS_IOS)) || defined(Q_OS_ANDROID))
void homeform::licenseReply(QNetworkReply *reply) {
    QString r = reply->readAll();
    qDebug() << r;
    if (r.contains("OK")) {
        tLicense.stop();
    } else {
        licenseRequest();
    }
}

void homeform::licenseRequest() {
    QTimer::singleShot(30000, this, [this]() {
        QSettings settings;
        if (!mgr) {
            mgr = new QNetworkAccessManager(this);
            connect(mgr, &QNetworkAccessManager::finished, this, &homeform::licenseReply);
        }
        QUrl url(QStringLiteral("http://robertoviola.cloud:4010/?supporter=") +
                 settings.value(QZSettings::user_email, "").toString());
        QNetworkRequest request(url);
        mgr->get(request);
    });
}

void homeform::licenseTimeout() { setLicensePopupVisible(true); }
#endif

void homeform::changeTimestamp(QTime source, QTime actual) {
    QSettings settings;
    // only needed if a gpx is loaded and the video is visible, otherwise do nothing.
    if ((trainProgram) && (videoVisible() == true)) {
        QObject *rootObject = engine->rootObjects().constFirst();
        auto *videoPlaybackHalf = rootObject->findChild<QObject *>(QStringLiteral("videoplaybackhalf"));
        auto videoPlaybackHalfPlayer = qvariant_cast<QMediaPlayer *>(videoPlaybackHalf->property("mediaObject"));
        double videoTimeStampSeconds = (double)videoPlaybackHalfPlayer->position() / 1000.0;
        // Check for time differences between Video and gpx Data
        if (videoTimeStampSeconds != 0.0) {
            double videoLengthSeconds = ((double)(videoPlaybackHalfPlayer->duration() / 1000.0));
            double trainProgramLengthSeconds = ((double)(trainProgram->TotalGPXSecs()));
            int recordingFactor = 1;

            // if Video is > 60 secs Shorter it will be a speed adjusted one
            if ((trainProgramLengthSeconds - videoLengthSeconds) >= 60.0) {
                double recfac = ((trainProgramLengthSeconds / videoLengthSeconds) + 0.5);
                recordingFactor = ((int)(recfac));
                qDebug() << "Video Recording Factor" << recordingFactor << trainProgramLengthSeconds
                         << videoLengthSeconds << (videoLengthSeconds * ((double)(recordingFactor)))
                         << videoTimeStampSeconds << (videoTimeStampSeconds * ((double)(recordingFactor)));
                videoLengthSeconds = (videoLengthSeconds * ((double)(recordingFactor)));
                videoTimeStampSeconds = (videoTimeStampSeconds * ((double)(recordingFactor)));
            }

            // check if there is a difference >= 1 second
            if ((fabs(videoLengthSeconds - trainProgramLengthSeconds)) >= 1.0) {
                // correct Video TimeStamp by difference
                videoTimeStampSeconds = (videoTimeStampSeconds - videoLengthSeconds + trainProgramLengthSeconds);
            }

            qDebug() << videoTimeStampSeconds;
            // Video was just displayed, set the start Position
            if (videoMustBeReset) {
                double videoStartPos =
                    ((double)(QTime(0, 0, 0).secsTo(source)) + videoLengthSeconds - trainProgramLengthSeconds);
                // if videoStartPos is negativ the Video is shorter then the GPX. Wait for the gpx to reach a point
                // where the Video can be played
                if (videoStartPos >= 0.0) {
                    videoTimeStampSeconds = (videoStartPos - videoLengthSeconds + trainProgramLengthSeconds);
                    videoStartPos = videoStartPos / ((double)(recordingFactor));
                    qDebug() << "SetVideoStartPosition" << (videoStartPos * 1000.0);
                    videoPlaybackHalfPlayer->setPosition(videoStartPos * 1000.0);
                    videoMustBeReset = false;
                }
            }
            // Video is started now, calculate and set the Rate
            if (!videoMustBeReset) {
                // calculate and set the new Video Rate
                double rate = trainProgram->TimeRateFromGPX(
                    ((double)QTime(0, 0, 0).msecsTo(source)) / 1000.0, videoTimeStampSeconds,
                    bluetoothManager->device()->currentSpeed().average5s(), recordingFactor);
                rate = rate / ((double)(recordingFactor));
                setVideoRate(rate);
            } else {
                qDebug() << "videoMustBeReset = True";
            }
        } else {
            qDebug() << "videoTimeStampSeconds = 0";
        }
    }

    if (!videoVisible()) {
        // set the maximum Speed that the player can reached based on the Video speed.
        // When Video is not displayed (or not displayed any longer) remove the Limit
        if (bluetoothManager->device()->deviceType() == BIKE) {
            bike *dev = (bike *)bluetoothManager->device();
            dev->setSpeedLimit(0);
        }
        // Prepare for a possible Video play. Set the Start Position to 1 and a Rate so low that only a few frames
        // are played
        setVideoPosition(1);
        setVideoRate(0.01);
        videoMustBeReset = true;
    }
}

void homeform::videoSeekPosition(int ms) {
    QObject *rootObject = engine->rootObjects().constFirst();
    auto *videoPlaybackHalf = rootObject->findChild<QObject *>(QStringLiteral("videoplaybackhalf"));
    auto videoPlaybackHalfPlayer = qvariant_cast<QMediaPlayer *>(videoPlaybackHalf->property("mediaObject"));
    videoPlaybackHalfPlayer->setPosition(ms);
}

#ifdef Q_OS_ANDROID
extern "C" {
    JNIEXPORT void JNICALL
    Java_org_cagnulen_qdomyoszwift_ChannelService_nativeSetResistance(JNIEnv *env, jclass clazz, jint resistance) {
        qDebug() << "Native: ANT+ Setting resistance to:" << resistance;
        
        if (homeform::singleton()->bluetoothManager && homeform::singleton()->bluetoothManager->device()) {
            BLUETOOTH_TYPE deviceType = homeform::singleton()->bluetoothManager->device()->deviceType();
            
            if (deviceType == BIKE || 
                deviceType == ROWING || 
                deviceType == ELLIPTICAL) {
                
                bike* b = dynamic_cast<bike*>(homeform::singleton()->bluetoothManager->device());
                if (b) {
                    resistance_t maxRes = b->maxResistance();
                    resistance_t scaledResistance = (resistance_t)((resistance / 100.0) * maxRes);
                    if (scaledResistance < 1) scaledResistance = 1;
                    if (scaledResistance > maxRes) scaledResistance = maxRes;

                    qDebug() << "Native: ANT+ Max resistance:" << maxRes << "Scaled resistance:" << scaledResistance;

                    b->changeResistance(scaledResistance);
                }
                qDebug() << "Applied ANT+ resistance change:" << resistance;
            } else {
                qDebug() << "Device type does not support resistance change";
            }
        } else {
            qDebug() << "No bluetooth device connected";
        }
    }

    JNIEXPORT void JNICALL
    Java_org_cagnulen_qdomyoszwift_ChannelService_nativeSetPower(JNIEnv *env, jclass clazz, jint power) {
        qDebug() << "Native: ANT+ Setting power to:" << power << "W";
        
        if (homeform::singleton()->bluetoothManager && homeform::singleton()->bluetoothManager->device()) {
            BLUETOOTH_TYPE deviceType = homeform::singleton()->bluetoothManager->device()->deviceType();
            
            if (deviceType == BIKE || 
                deviceType == ROWING || 
                deviceType == ELLIPTICAL ||
                deviceType == TREADMILL) {
                
                homeform::singleton()->bluetoothManager->device()->changePower(power);
                qDebug() << "Applied ANT+ power change:" << power << "W";
            } else {
                qDebug() << "Device type does not support power change";
            }
        } else {
            qDebug() << "No bluetooth device connected";
        }
    }

    JNIEXPORT void JNICALL
    Java_org_cagnulen_qdomyoszwift_ChannelService_nativeSetInclination(JNIEnv *env, jclass clazz, jdouble inclination) {
        qDebug() << "Native: ANT+ Setting inclination to:" << inclination << "%";
        
        if (homeform::singleton()->bluetoothManager && homeform::singleton()->bluetoothManager->device()) {
            BLUETOOTH_TYPE deviceType = homeform::singleton()->bluetoothManager->device()->deviceType();
            
            if (deviceType == BIKE || 
                deviceType == TREADMILL || 
                deviceType == ELLIPTICAL) {
                
                homeform::singleton()->bluetoothManager->device()->changeInclination(inclination, inclination);
                qDebug() << "Applied ANT+ inclination change:" << inclination << "%";
            } else {
                qDebug() << "Device type does not support inclination change";
            }
        } else {
            qDebug() << "No bluetooth device connected";
        }
    }
}
#endif
// Force rebuild for Q_INVOKABLE changes
