#include "bluetooth.h"
#include "qznotify.h"
#include "mywhooshlink.h"
#include <QBluetoothLocalDevice>
#include <QRegularExpression>
#include <QDateTime>
#include <QFile>
#include <QMetaEnum>

#ifdef Q_OS_ANDROID
#include "androidactivityresultreceiver.h"
#include "keepawakehelper.h"
#include <QAndroidJniObject>
#endif

static void updateDiscoveredDevice(QList<QBluetoothDeviceInfo> &devices, const QBluetoothDeviceInfo &device) {
    QMutableListIterator<QBluetoothDeviceInfo> i(devices);
    while (i.hasNext()) {
        const QBluetoothDeviceInfo existing = i.next();
        if (SAME_BLUETOOTH_DEVICE(existing, device)) {
            if (!device.name().isEmpty() || existing.name().isEmpty()) {
                i.setValue(device);
            } else {
                QBluetoothDeviceInfo updated = existing;
                updated.setCached(device.isCached());
                updated.setRssi(device.rssi());
                updated.setCoreConfigurations(device.coreConfigurations());
                updated.setDeviceUuid(device.deviceUuid());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                updated.setServiceUuids(device.serviceUuids());
#else
                updated.setServiceUuids(device.serviceUuids().toVector());
#endif
                const auto manufacturerData = device.manufacturerData();
                for (auto it = manufacturerData.cbegin(); it != manufacturerData.cend(); ++it) {
                    updated.setManufacturerData(it.key(), it.value());
                }
                i.setValue(updated);
            }
            return;
        }
    }

    devices.append(device);
}

bluetooth::bluetooth(const discoveryoptions &options)
    : bluetooth(options.logs, options.deviceName, options.noWriteResistance, options.noHeartService,
                options.pollDeviceTime, options.noConsole, options.testResistance, options.bikeResistanceOffset,
                options.bikeResistanceGain, options.startDiscovery) {}

bluetooth::bluetooth(bool logs, const QString &deviceName, bool noWriteResistance, bool noHeartService,
                     uint32_t pollDeviceTime, bool noConsole, bool testResistance, int8_t bikeResistanceOffset,
                     double bikeResistanceGain, bool startDiscovery) {
    QSettings settings;
    QLoggingCategory::setFilterRules(QStringLiteral("qt.bluetooth* = true"));
    filterDevice = deviceName;
    this->testResistance = testResistance;
    this->noWriteResistance = noWriteResistance;
    this->noHeartService = noHeartService;
    this->pollDeviceTime = pollDeviceTime;
    this->noConsole = noConsole;
    this->logs = logs;
    this->bikeResistanceGain = bikeResistanceGain;
    this->bikeResistanceOffset = bikeResistanceOffset;

    this->useDiscovery = startDiscovery;

    // The bike that is not there (docs/fork/VIRTUAL-BIKE.md). Built here, before any of the
    // discovery machinery exists, and discovery is then not started at all: there is nothing
    // to find, and a scan running alongside would only be able to replace it.
    //
    // Deliberately not routed through a synthetic deviceDiscovered() the way upstream's fake
    // devices were. That route is what needed the 15-second watchdog to unstick it, both were
    // deleted together in 2c39c5d, and faking an advertisement for a device that never
    // advertised is how it got complicated in the first place.
    {
        QSettings settings;
        if (settings.value(QZSettings::simulated_bike, QZSettings::default_simulated_bike).toBool()) {
            const QString ride =
                settings.value(QZSettings::simulated_bike_ride, QZSettings::default_simulated_bike_ride).toString();
            debug(QStringLiteral("simulated bike enabled, skipping discovery"));
            simulatedBike = new simulatedbike(noWriteResistance, noHeartService, ride);
            connect(simulatedBike, &bluetoothdevice::connectedAndDiscovered, this,
                    &bluetooth::connectedAndDiscovered);
            connect(simulatedBike, &simulatedbike::debug, this, &bluetooth::debug);

            // The device exists from here - device() must not return null while the rest of
            // startup runs - but announcing it here would announce it to nobody. This
            // constructor runs from main() around a hundred lines before homeform is built,
            // and homeform is what connects to deviceConnected and bluetoothDeviceConnected.
            // A real device is discovered from a radio callback, which by definition happens
            // after the event loop is running and therefore after homeform exists; a device
            // built during construction has no such luck, and both signals went into the
            // void. The visible result was an app with no tiles: homeform::deviceConnected()
            // is what clears the help label and builds the session, and it never ran.
            //
            // A zero timer is the whole fix: it fires on the first turn of the event loop,
            // which main() does not reach until homeform is constructed and connected.
            QTimer::singleShot(0, this, [this]() {
                if (!simulatedBike)
                    return;
                // homeform calls deviceFound() with this name, so the scenario's `bike`
                // directive is what the user sees it identify as.
                QString name = QString::fromStdString(simulatedBike->scenario().bike());
                if (name.isEmpty())
                    name = QStringLiteral("Simulated Bike");
                QBluetoothDeviceInfo info(QBluetoothAddress(quint64(1)), name, 0);
                info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
                emit deviceConnected(info);
                this->signalBluetoothDeviceConnected(simulatedBike);
            });

            this->discoveryAgent = nullptr;
            return;
        }
    }

    if (!startDiscovery) {
        this->discoveryAgent = nullptr;
        return;
    }

#if !defined(WIN32) && !defined(Q_OS_IOS)
    if (QBluetoothLocalDevice::allDevices().isEmpty()) {
        debug(QStringLiteral("no bluetooth dongle found!"));
    } else

#endif
    {
        // Create a discovery agent and connect to its signals
        discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
        connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &bluetooth::deviceDiscovered);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
        connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this, &bluetooth::deviceUpdated);

#endif
        connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &bluetooth::canceled);
        // Connected on Windows too, as of 2026-08-24. finished() ends with startDiscovery(),
        // so it *is* the scan cycle; excluding Windows meant discovery ran once and stopped
        // for good when the agent hit its timeout. BUILDING-ON-WINDOWS.md recorded that as
        // "discovery is effectively one-shot at launch, so the bike must be advertising
        // before QZ starts", which was tolerable while nothing ever went back to scanning.
        //
        // bluetooth::rescan() does go back, and the 21:01 session shows what that was worth
        // without this: the rescan started a scan at 21:02:30, the agent stopped at 21:03:10
        // - forty seconds, Qt's default LE timeout, since setLowEnergyDiscoveryTimeout() is
        // also skipped here - and QZ then displayed "searching" for eight minutes while
        // scanning for none of them.
        //
        // finished() is already prepared for this platform: its first act after the
        // one-shot guard is `#ifdef Q_OS_WIN if (this->device()) return;`, so a claimed
        // device makes it a no-op rather than letting it re-enter the claim loop.
        connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, &bluetooth::finished);
        // The 15s discovery watchdog that used to be armed here existed solely to
        // unstick fake and IP-based devices on platforms where the discovery agent's
        // finished() never fires. Every one of those devices is gone, so the watchdog
        // had no remaining caller and would only have risked cutting a genuine, slow
        // scan short.

        // Start a discovery
#ifndef Q_OS_WIN
        discoveryAgent->setLowEnergyDiscoveryTimeout(10000);
#endif
        this->startDiscovery();

#ifdef Q_OS_WIN
        // A device Windows is already connected to never advertises, and Qt's WinRT
        // discovery only reports what its advertisement watcher hears - so the trainer
        // is invisible to discovery for exactly as long as the OS holds the link.
        // Measured on this bike: unpaired and silent, FromBluetoothAddressAsync still
        // returns it by address and GetGattServicesAsync(Uncached) enumerates all six
        // services. Windows re-establishes that link on its own the moment the radio
        // comes up, so on Windows this is the normal case rather than an edge one.
        //
        // So: give discovery its chance, and if it comes up empty, hand the stored
        // address to the very same deviceDiscovered() path. Nothing about device
        // matching is duplicated - it is the same call discovery would have made.
        connect(&discoveryTimeout, &QTimer::timeout, this, &bluetooth::connectToLastDeviceIfIdle);
        discoveryTimeout.setSingleShot(true);
        discoveryTimeout.start(15000);
#endif
    }
}

#ifdef Q_OS_WIN
void bluetooth::connectToLastDeviceIfIdle() {
    if (device()) // discovery got there first, which is the happy path
        return;

    QSettings settings;
    const QString name =
        settings.value(QZSettings::bluetooth_lastdevice_name, QZSettings::default_bluetooth_lastdevice_name).toString();
    const QString address = settings
                                .value(QZSettings::bluetooth_lastdevice_address,
                                       QZSettings::default_bluetooth_lastdevice_address)
                                .toString();
    if (name.isEmpty() || address.isEmpty()) {
        debug(QStringLiteral("no previous device to fall back on"));
        return;
    }

    const QBluetoothAddress lastAddress(address);
    if (lastAddress.isNull()) {
        debug(QStringLiteral("stored device address is not usable: ") + address);
        return;
    }

    debug(QStringLiteral("discovery found nothing; trying the last device directly: ") + name + QStringLiteral(" ") +
          address);

    // The core configuration is not decoration. A QBluetoothDeviceInfo built from
    // the address/name constructor reports UnknownCoreConfiguration, and Qt 6.8.2's
    // WinRT backend dereferences a null through that path - the first attempt at
    // this crashed in Qt6Bluetooth.dll with 0xc0000005 the instant the controller
    // was created. Saying Low Energy explicitly is what discovery would have set.
    QBluetoothDeviceInfo info(lastAddress, name, 0);
    info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    deviceDiscovered(info);
}
#endif

bluetooth::~bluetooth() {

    /*if(device())
    {
        device()->disconnectBluetooth();
    }*/
}

void bluetooth::signalBluetoothDeviceConnected(bluetoothdevice *b) { emit this->bluetoothDeviceConnected(b); }

void bluetooth::finished() {
    if (discoveryFinishedHandled)
        return;
    discoveryFinishedHandled = true;
    discoveryTimeout.stop();

    debug(QStringLiteral("BTLE scanning finished"));

    QSettings settings;
    // The synthetic deviceDiscovered(QBluetoothDeviceInfo()) that used to fire here
    // served the non-BLE devices - the IP trainers, the ANT bikes, the USB rower and
    // the fake devices. None of them exist any more, so there is nothing left to fake
    // a discovery for.

    if (device()) {
        qDebug() << QStringLiteral("bluetooth::finished but discoveryAgent is not active");
        return;
    }

    QString heartRateBeltName =
        settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();
    bool csc_as_bike =
        settings.value(QZSettings::cadence_sensor_as_bike, QZSettings::default_cadence_sensor_as_bike).toBool();
    QString cscName =
        settings.value(QZSettings::cadence_sensor_name, QZSettings::default_cadence_sensor_name).toString();
    QString powerSensorName =
        settings.value(QZSettings::power_sensor_name, QZSettings::default_power_sensor_name).toString();
    QString eliteRizerName =
        settings.value(QZSettings::elite_rizer_name, QZSettings::default_elite_rizer_name).toString();
    QString eliteSterzoSmartName =
        settings.value(QZSettings::elite_sterzo_smart_name, QZSettings::default_elite_sterzo_smart_name).toString();
    bool cscFound = cscName.startsWith(QStringLiteral("Disabled")) && !csc_as_bike;
    bool powerSensorFound = powerSensorName.startsWith(QStringLiteral("Disabled"));
    bool eliteRizerFound = eliteRizerName.startsWith(QStringLiteral("Disabled"));
    bool eliteSterzoSmartFound = eliteSterzoSmartName.startsWith(QStringLiteral("Disabled"));
    bool heartRateBeltFound = heartRateBeltName.startsWith(QStringLiteral("Disabled"));

    // since i can have multiple fanfit i can't wait more because i don't have the full list of the fanfit
    // devices connected to QZ. edit: let's wait at the last one item
    bool fitmetriaFanfitFound =
        !settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable).toBool();

    bool zwiftDeviceFound =
        !settings.value(QZSettings::zwift_click, QZSettings::default_zwift_click).toBool() && !settings.value(QZSettings::zwift_play, QZSettings::default_zwift_play).toBool();

    bool sramDeviceFound = !settings.value(QZSettings::sram_axs_controller, QZSettings::default_sram_axs_controller).toBool();

    bool cycplusBC2DeviceFound =
        !settings.value(QZSettings::cycplus_bc2_controller, QZSettings::default_cycplus_bc2_controller).toBool();

    bool thinkriderDeviceFound = !settings.value(QZSettings::thinkrider_controller, QZSettings::default_thinkrider_controller).toBool();

    if ((!heartRateBeltFound && !heartRateBeltAvaiable()) ||
        (!cscFound && !cscSensorAvaiable()) || (!powerSensorFound && !powerSensorAvaiable()) ||
        (!eliteRizerFound && !eliteRizerAvaiable()) || (!eliteSterzoSmartFound && !eliteSterzoSmartAvaiable()) ||
        (!fitmetriaFanfitFound && !fitmetriaFanfitAvaiable()) ||
        (!zwiftDeviceFound && !zwiftDeviceAvaiable()) ||
        (!sramDeviceFound && !sramDeviceAvaiable()) ||
        (!cycplusBC2DeviceFound && !cycplusBC2DeviceAvaiable()) ||
        (!thinkriderDeviceFound && !thinkriderDeviceAvaiable())) {

        // force heartRateBelt off
        forceHeartBeltOffForTimeout = true;
    }

    this->startDiscovery();
}

void bluetooth::startDiscovery() {

    if (!this->useDiscovery)
        return;

    // Every scan gets its own completion. finished() latches a one-shot guard so that a timeout
    // and the agent's own signal cannot both be handled for the same scan; leaving it latched
    // across scans meant the second completion was swallowed along with its restart, so discovery
    // stopped for good about twenty seconds after launch and a bike powered on later was never
    // seen. Clearing it here - and only here - keeps the double-handling guard intact within a
    // scan while letting the cycle continue. finished() still returns early once a device has been
    // found, so a connected session never scans over itself.
    discoveryFinishedHandled = false;

    // Classic Bluetooth discovery was here for four devices - the Technogym MyRun, the
    // TRX Route Key, the BH Spada 2 and the iConcept elliptical - and every one of them
    // has been deleted. The trainer this fork talks to is BLE.
    discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void bluetooth::stopDiscovery() {
    if (this->discoveryAgent)
        this->discoveryAgent->stop();
    else
        qDebug() << "bluetooth::stopDiscovery() called when discoveryAgent is not defined. ";
}

void bluetooth::canceled() {
    debug(QStringLiteral("BTLE scanning stops"));

    emit searchingStop();
}

void bluetooth::debug(const QString &text) {
    if (logs) {

        qDebug() << text;
    }
}

bool bluetooth::gymModeEnabled() const {
    QSettings settings;
    return settings.value(QZSettings::gym_mode, QZSettings::default_gym_mode).toBool();
}

bool bluetooth::cscSensorAvaiable() {

    QSettings settings;
    bool csc_as_bike =
        settings.value(QZSettings::cadence_sensor_as_bike, QZSettings::default_cadence_sensor_as_bike).toBool();
    QString cscName =
        settings.value(QZSettings::cadence_sensor_name, QZSettings::default_cadence_sensor_name).toString();

    if (csc_as_bike) {
        return false;
    }

    for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
        if (!cscName.compare(b.name())) {

            return true;
        }
    }
    return false;
}

bool bluetooth::fitmetriaFanfitAvaiable() {

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (!b.name().compare("FITFAN-", Qt::CaseInsensitive)) {
            return true;
        } else if (b.name().toUpper().startsWith("HEADWIND ")) {
            return true;
        }
    }
    return false;
}

bool bluetooth::zwiftDeviceAvaiable() {

    uint8_t count = 0;
    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (b.name().toUpper().startsWith("ZWIFT ")) {
            count++;
            if(count >= 2)
                return true;
        }
    }
    return false;
}

bool bluetooth::sramDeviceAvaiable() {

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (b.name().toUpper().startsWith("SRAM ")) {
           return true;
        }
    }
    return false;
}

bool bluetooth::thinkriderDeviceAvaiable() {

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (b.name().toUpper().startsWith("THINK VS") || b.name().toUpper().startsWith("THINKRIDER")) {
           return true;
        }
    }
    return false;
}

bool bluetooth::cycplusBC2DeviceAvaiable() {

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (b.name().toUpper().contains("BC2") ||
            deviceHasService(b, QBluetoothUuid(QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e")))) {
           return true;
        }
    }
    return false;
}


bool bluetooth::powerSensorAvaiable() {

    QSettings settings;
    QString powerSensorName =
        settings.value(QZSettings::power_sensor_name, QZSettings::default_power_sensor_name).toString();

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (!powerSensorName.compare(b.name())) {

            return true;
        }
    }
    return false;
}

bool bluetooth::eliteRizerAvaiable() {

    QSettings settings;
    QString eliteRizerName =
        settings.value(QZSettings::elite_rizer_name, QZSettings::default_elite_rizer_name).toString();

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (!eliteRizerName.compare(b.name())) {

            return true;
        }
    }
    return false;
}

bool bluetooth::eliteSterzoSmartAvaiable() {

    QSettings settings;
    QString eliteSterzoSmartName =
        settings.value(QZSettings::elite_sterzo_smart_name, QZSettings::default_elite_sterzo_smart_name).toString();

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (!eliteSterzoSmartName.compare(b.name())) {

            return true;
        }
    }
    return false;
}

bool bluetooth::heartRateBeltAvaiable() {

    QSettings settings;
    QString heartRateBeltName =
        settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();

    Q_FOREACH (QBluetoothDeviceInfo b, devices) {
        if (!heartRateBeltName.compare(b.name())) {

            return true;
        }
    }
    return false;
}

void bluetooth::setLastBluetoothDevice(const QBluetoothDeviceInfo &b) {
    QSettings settings;
    settings.setValue(QZSettings::bluetooth_lastdevice_name, b.name());
#ifndef Q_OS_IOS
    settings.setValue(QZSettings::bluetooth_lastdevice_address, b.address().toString());
#else
    settings.setValue(QZSettings::bluetooth_lastdevice_address, b.deviceUuid().toString());
#endif
}

// this doesn't work on Windows. So be careful!
bool bluetooth::deviceHasService(const QBluetoothDeviceInfo &device, QBluetoothUuid service) {
    foreach(QBluetoothUuid s, device.serviceUuids()) {
        if(s == service) {
            return true;
        }
    }
    return false;
}

void bluetooth::deviceDiscovered(const QBluetoothDeviceInfo &device) {

    QSettings settings;
    QString heartRateBeltName =
        settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();
    bool heartRateBeltFound = heartRateBeltName.startsWith(QStringLiteral("Disabled"));
    bool sramDeviceFound = !settings.value(QZSettings::sram_axs_controller, QZSettings::default_sram_axs_controller).toBool();
    bool zwiftDeviceFound =
        !settings.value(QZSettings::zwift_click, QZSettings::default_zwift_click).toBool() && !settings.value(QZSettings::zwift_play, QZSettings::default_zwift_play).toBool();
    bool cycplusBC2DeviceFound =
        !settings.value(QZSettings::cycplus_bc2_controller, QZSettings::default_cycplus_bc2_controller).toBool();
    bool thinkriderDeviceFound = !settings.value(QZSettings::thinkrider_controller, QZSettings::default_thinkrider_controller).toBool();
    bool fitmetriaFanfitFound =
        !settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable).toBool();
    bool toorx_ftms = settings.value(QZSettings::toorx_ftms, QZSettings::default_toorx_ftms).toBool();
    bool csc_as_bike =
        settings.value(QZSettings::cadence_sensor_as_bike, QZSettings::default_cadence_sensor_as_bike).toBool();
    QString cscName =
        settings.value(QZSettings::cadence_sensor_name, QZSettings::default_cadence_sensor_name).toString();
    bool cscFound = cscName.startsWith(QStringLiteral("Disabled")) || csc_as_bike;
    bool hammerRacerS = settings.value(QZSettings::hammer_racer_s, QZSettings::default_hammer_racer_s).toBool();
    QString powerSensorName =
        settings.value(QZSettings::power_sensor_name, QZSettings::default_power_sensor_name).toString();
    QString eliteRizerName =
        settings.value(QZSettings::elite_rizer_name, QZSettings::default_elite_rizer_name).toString();
    QString eliteSterzoSmartName =
        settings.value(QZSettings::elite_sterzo_smart_name, QZSettings::default_elite_sterzo_smart_name).toString();
    bool powerSensorFound = powerSensorName.startsWith(QStringLiteral("Disabled"));
    bool eliteRizerFound = eliteRizerName.startsWith(QStringLiteral("Disabled"));
    bool eliteSterzoSmartFound = eliteSterzoSmartName.startsWith(QStringLiteral("Disabled"));
        
    bool manufacturerDeviceFound = false;
    QString ftms_bike = settings.value(QZSettings::ftms_bike, QZSettings::default_ftms_bike).toString();
    bool saris_trainer = settings.value(QZSettings::saris_trainer, QZSettings::default_saris_trainer).toBool();
    if (!heartRateBeltFound) {

        heartRateBeltFound = heartRateBeltAvaiable();
    }
    if (!fitmetriaFanfitFound) {

        fitmetriaFanfitFound = fitmetriaFanfitAvaiable();
    }
    if (!zwiftDeviceFound) {

        zwiftDeviceFound = zwiftDeviceAvaiable();
    }
    if(!sramDeviceFound) {

        sramDeviceFound = sramDeviceAvaiable();
    }
    if(!cycplusBC2DeviceFound) {

        cycplusBC2DeviceFound = cycplusBC2DeviceAvaiable();
    }
    if(!thinkriderDeviceFound) {

        thinkriderDeviceFound = thinkriderDeviceAvaiable();
    }
    if (!cscFound) {

        cscFound = cscSensorAvaiable();
    }
    if (!powerSensorFound) {

        powerSensorFound = powerSensorAvaiable();
    }
    if (!eliteRizerFound) {

        eliteRizerFound = eliteRizerAvaiable();
    }
    if (!eliteSterzoSmartFound) {

        eliteSterzoSmartFound = eliteSterzoSmartAvaiable();
    }

#ifdef Q_OS_IOS
    // Schwinn bikes on iOS allows to be connected to several instances, so in this way
    // QZ will remember the address and will try to connect to it
    QString b =
        settings.value(QZSettings::bluetooth_lastdevice_name, QZSettings::default_bluetooth_lastdevice_name).toString();
    qDebug() << "last device name (IC BIKE workaround)" << b;
    
#endif

    QVector<quint16> ids = device.manufacturerIds();
    qDebug() << "manufacturerData" << ids;
    foreach (quint16 id, ids) {
        qDebug() << id << device.manufacturerData(id).toHex(' ');

#ifdef Q_OS_ANDROID
        // yesoul bike on android 13 doesn't send anymore the name
        
#endif
    }

    if (manufacturerDeviceFound == false) {
        updateDiscoveredDevice(devices, device);
    }

    emit deviceFound(device.name());
    qDebug() << QStringLiteral("Found new device: ") << device.name() << QStringLiteral(" (") << device.address().toString() <<
          ')' << " " << device.majorDeviceClass() << QStringLiteral(":") << device.minorDeviceClass() << device.serviceUuids()
#if defined(Q_OS_DARWIN) || defined(Q_OS_IOS)
            << device.deviceUuid();
#endif
    ;

    // not required for mobile I guess
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    if(!uiLoaded) {
        qDebug() << "UI not yet loaded";
        return;
    }
#endif    

    if (onlyDiscover)
        return;

    if (gymModeEnabled() && gymModeSessionDevice.isEmpty())
        return;

#ifdef Q_OS_WIN
    if (this->device()) {
        qDebug() << QStringLiteral("bluetooth::finished but discoveryAgent is not active");
        return;
    }
#endif

    bool searchDevices = (heartRateBeltFound && cscFound && powerSensorFound && eliteRizerFound &&
                          eliteSterzoSmartFound && fitmetriaFanfitFound && zwiftDeviceFound && sramDeviceFound &&
                          cycplusBC2DeviceFound && thinkriderDeviceFound) ||
                         forceHeartBeltOffForTimeout;

    if (searchDevices) {
        const QString effectiveFilterDevice =
            !gymModeSessionDevice.isEmpty() ? gymModeSessionDevice : filterDevice;
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {

            bool filter = true;
            if (!effectiveFilterDevice.isEmpty() && !effectiveFilterDevice.startsWith(QStringLiteral("Disabled"))) {

                filter = (b.name().compare(effectiveFilterDevice, Qt::CaseInsensitive) == 0);
            }
            if (((csc_as_bike && b.name().startsWith(cscName)) ||
                        b.name().toUpper().startsWith(QStringLiteral("JOROTO-BK-")) ||
                        (b.name().toUpper().startsWith(QStringLiteral("BGYM")) && b.name().length() == 8)) &&
                       !cscBike && filter) {
                this->setLastBluetoothDevice(b);
                this->stopDiscovery();
                cscBike = new cscbike(noWriteResistance, noHeartService, false);
                emit deviceConnected(b);
                connect(cscBike, &bluetoothdevice::connectedAndDiscovered, this, &bluetooth::connectedAndDiscovered);
                // connect(cscBike, SIGNAL(disconnected()), this, SLOT(restart()));
                connect(cscBike, &cscbike::debug, this, &bluetooth::debug);
                cscBike->deviceDiscovered(b);
                // connect(this, SIGNAL(searchingStop()), cscBike, SLOT(searchingStop())); //NOTE: Commented due to #358
                if (this->discoveryAgent && !this->discoveryAgent->isActive()) {
                    emit searchingStop();
                }
                this->signalBluetoothDeviceConnected(cscBike);
            } else if (((b.name().startsWith("FS-") && hammerRacerS) ||
                        (b.name().toUpper().startsWith(QStringLiteral("ICONSOLE+")) && toorx_ftms ) ||
                        (b.name().toUpper().startsWith("DI") && b.name().length() == 2) || // Elite smart trainer #1682
                        (b.name().toUpper().startsWith("DHZ-")) ||                         // JK fitness 577
                        (b.name().toUpper().startsWith("MKSM")) ||                         // MKSM3600036
                        (b.name().toUpper().startsWith("YS_C1_")) ||                       // Yesoul C1H
                        (b.name().toUpper().startsWith("YS_G1_")) ||                       // Yesoul S3
						(b.name().toUpper().startsWith("YS_M1P_")) ||                      // Yesoul M1
                        (b.name().toUpper().startsWith("YS_G1MPLUS")) ||                   // Yesoul G1M Plus
                        (b.name().toUpper().startsWith("YS_G1MMAX")) ||                    // Yesoul G1M Max
                        (b.name().toUpper().startsWith("YS_A")) ||                         // Yesoul A6 and A1
                        (b.name().toUpper().startsWith("DS25-")) ||                        // Bodytone DS25
                        (b.name().toUpper().startsWith("3G CARDIO ")) ||
                        (b.name().toUpper().startsWith("ZWIFT HUB")) || 
                        ((b.name().toUpper().startsWith("MAGNUS ")) && deviceHasService(b, QBluetoothUuid((quint16)0x1826))) ||
                        (b.name().toUpper().startsWith("HAMMER ") && !saris_trainer) ||      // HAMMER 64123
                        (b.name().toUpper().startsWith("FLXCY-")) ||                         // Pro FlexBike
                        (b.name().toUpper().startsWith("QB-WC01")) ||                        // Nexgim QB-C01 smart bike
                        (b.name().toUpper().startsWith("XBR55")) ||                          // Sprint XBR555
                        (b.name().toUpper().startsWith("ECHO_BIKE_")) ||                     // Rogue echo bike V3.0
                        (b.name().toUpper().startsWith("EW-JS-")) ||                         // EW-JS-4990
                        (b.name().toUpper().startsWith("DT-") && b.name().length() >= 14) || // SOLE SB700
                        (b.name().toUpper().startsWith("YSV") && b.name().length() == 9) ||  // YSV100783
                        (b.name().toUpper().startsWith("URSB") && b.name().length() == 7) || // URSB005
                        (b.name().toUpper().startsWith("DBF") && b.name().length() == 6) ||  // DBF135
                        (b.name().toUpper().startsWith("KSU") && b.name().length() == 7) ||  // KSU1102
                        (b.name().toUpper().startsWith("MERACH-MR667-")) ||
                        (b.name().toUpper().startsWith("DS60-")) ||
                        (b.name().toUpper().startsWith("BIKE-")) ||
                        (b.name().toUpper().startsWith("M9-")) ||
                        (b.name().toUpper().startsWith("SPAX-BK-")) ||
                        (b.name().toUpper().startsWith("YSV1")) ||
                        (b.name().toUpper().startsWith("VOLT") && b.name().length() == 4) ||
                        (b.name().toUpper().startsWith("VICTORY")) ||
                        (b.name().toUpper().startsWith("CECOTEC")) ||       // Cecotec DrumFit Indoor 10000 MagnoMotor Connected #2420
                        (b.name().toUpper().startsWith("WATTBIKE")) ||
                        (b.name().toUpper().startsWith("ZYCLEZBIKE")) ||
						(b.name().toUpper().startsWith("ZBIKE2.0")) ||
                        (b.name().toUpper().startsWith("WAVEFIT-")) ||
                        (b.name().toUpper().startsWith("KETTLERBLE")) ||
                        (b.name().toUpper().startsWith("JAS_C3")) ||
                        (b.name().toUpper().startsWith("SCH_190U")) ||
                        (b.name().toUpper().startsWith("SCH_290R")) ||
                        (b.name().toUpper().startsWith("RAVE WHITE")) ||
                        (b.name().toUpper().startsWith("DOMYOS-BIKING-")) ||
                        (b.name().startsWith(QStringLiteral("Domyos-Bike")) && deviceHasService(b, QBluetoothUuid((quint16)0x1826)) && !settings.value(QZSettings::domyosbike_notfmts, QZSettings::default_domyosbike_notfmts).toBool()) ||
                        (b.name().toUpper().startsWith("F") && b.name().toUpper().endsWith("ARROW")) || // FI9110 Arrow, https://www.fitnessdigital.it/bicicletta-smart-bike-ion-fitness-arrow-connect/p/10022863/ IO Fitness Arrow
                        (b.name().toUpper().startsWith("ICSE") && b.name().length() == 4) ||
                        (b.name().toUpper().startsWith("TUO") && b.name().length() == 3) ||
                        (b.name().toUpper().startsWith("FLX") && b.name().length() == 10) ||
                        (b.name().toUpper().startsWith("CSRB") && b.name().length() == 11) ||
                        (b.name().toUpper().startsWith("DU30-")) ||                          // BodyTone du30
                        (b.name().toUpper().startsWith("BIKZU_")) ||
                        (b.name().toUpper().startsWith("WLT8828")) ||                        
                        (b.name().toUpper().startsWith("HARISON-X15")) ||
                        (b.name().toUpper().startsWith("FEIVON V2")) ||
                        (b.name().toUpper().startsWith("FELVON V2")) ||
                        (b.name().toUpper().startsWith("JUSTO")) ||
                        (b.name().toUpper().startsWith("MYCYCLE ")) ||
                        (b.name().toUpper().startsWith("T2 ")) ||                        
                        (b.name().compare(QStringLiteral("S18"), Qt::CaseInsensitive) == 0) ||
                        (b.name().toUpper().startsWith("RC-MAX-")) ||
                        (b.name().toUpper().startsWith("TPS-SPBIKE-2.0")) ||
                        (b.name().toUpper().startsWith("NEO BIKE SMART")) ||
                        (b.name().toUpper().startsWith("ZDRIVE")) ||
                        (b.name().toUpper().startsWith("TUNTURI E60-")) ||
                        (b.name().toUpper().startsWith("TUNTURI F40-")) ||
                        (b.name().toUpper().startsWith("JFBK5.0")) ||
                        (b.name().toUpper().startsWith("NEO 3M ")) ||
                        (b.name().toUpper().startsWith("JFBK7.0")) ||
                        (b.name().toUpper().startsWith("SPEED RACE S")) ||
                        (b.name().toUpper().startsWith("SPEEDRACEX")) ||
                        (b.name().toUpper().startsWith("POOBOO")) ||
                        (b.name().toUpper().startsWith("ZYCLE ZPRO")) ||
                        (b.name().toUpper().startsWith("SM720I")) ||
                        (b.name().toUpper().startsWith("H9115 LYON")) ||
                        (b.name().toUpper().startsWith("AVANTI")) ||
                        (b.name().toUpper().startsWith("T300P_")) ||
                        (b.name().toUpper().startsWith("T200_")) ||
                        (b.name().toUpper().startsWith("BZ9110 ")) ||
                        (b.name().toUpper().startsWith("CFC") && b.name().length() == 14) || // CFC31231004349
                        (b.name().toUpper().startsWith("TITAN 7000")) ||
                        (b.name().toUpper().startsWith("LYDSTO")) ||
                        (b.name().toUpper().startsWith("CYCLO_")) ||
                        (b.name().toUpper().startsWith("SL010-")) ||
                        (b.name().toUpper().startsWith("EXPERT-SX9")) ||                                                 
                        (b.name().toUpper().startsWith("MRK-S26S-")) ||
                        (b.name().toUpper().startsWith("MRK-S26C-")) ||
                        (b.name().toUpper().startsWith("MRK-S28-")) ||
                        (b.name().toUpper().startsWith("MRK-S38-")) ||
                        (b.name().toUpper().startsWith("ROBX")) ||
                        (b.name().toUpper().startsWith("ORLAUF_ARES")) ||
                        (b.name().toUpper().startsWith("SPEEDMAGPRO")) ||                        
                        (b.name().toUpper().startsWith("XCX-")) ||
                        (b.name().toUpper().startsWith("SMARTBIKE-")) ||
                        (b.name().toUpper().startsWith("D500V2")) ||
                        (b.name().toUpper().startsWith("FBIKE-HEAVY-PRO")) ||
                        (b.name().toUpper().startsWith("NEO BIKE PLUS ")) ||
                        (b.name().toUpper().startsWith(QStringLiteral("PM5")) && !b.name().toUpper().endsWith(QStringLiteral("SKI")) && !b.name().toUpper().endsWith(QStringLiteral("ROW"))) || 
                        (b.name().toUpper().startsWith("L-") && b.name().length() == 11) ||
                        (b.name().toUpper().startsWith("DMASUN-") && b.name().toUpper().endsWith("-BIKE")) ||
                        (b.name().toUpper().startsWith(QStringLiteral("FIT-BK-"))) ||
                        (b.name().toUpper().startsWith("VFSPINBIKE")) ||
                        (b.name().toUpper().startsWith("RIVO COG")) ||
                        (b.name().toUpper().startsWith("RAVE")) ||
                        (b.name().toUpper().startsWith("TOPUTURE-")) ||
						(b.name().toUpper().startsWith("TOPUTURE TEB")) ||
                        (b.name().toUpper().startsWith("BESP-")) ||  // FITFIU BESP 250 indoor bike
                        (b.name().toUpper().startsWith("GLT") && deviceHasService(b, QBluetoothUuid((quint16)0x1826))) ||
                        (b.name().toUpper().startsWith("SPORT01-") && deviceHasService(b, QBluetoothUuid((quint16)0x1826))) || // Labgrey Magnetic Exercise Bike https://www.amazon.co.uk/dp/B0CXMF1NPY?_encoding=UTF8&psc=1&ref=cm_sw_r_cp_ud_dp_PE420HA7RD7WJBZPN075&ref_=cm_sw_r_cp_ud_dp_PE420HA7RD7WJBZPN075&social_share=cm_sw_r_cp_ud_dp_PE420HA7RD7WJBZPN075&skipTwisterOG=1
                        (b.name().toUpper().startsWith("FS-YK-")) ||
						(b.name().toUpper().startsWith("T600E_")) ||
                        (b.name().toUpper().startsWith("SPEEDBIKE S2")) || // Maxxus Speedbike S2
						(b.name().toUpper().startsWith("B56-")) || // Titan Life B56 bike
                        ((b.name().toUpper().startsWith(QStringLiteral("HT")) && (b.name().length() == 10) &&
                          !ftms_bike.contains(QZSettings::default_ftms_bike))) ||
                        (b.name().toUpper().startsWith("ZUMO")) || (b.name().toUpper().startsWith("XS08-")) ||
                        (b.name().toUpper().startsWith("B94")) || (b.name().toUpper().startsWith("STAGES BIKE")) ||
                        (b.name().toUpper().startsWith("SUITO")) || (b.name().toUpper().startsWith("D2RIDE")) ||
                        (b.name().toUpper().startsWith("DIRETO X")) || (b.name().toUpper().startsWith("MERACH-667-")) ||
                        (b.name().toUpper().startsWith("USDC-D700-")) ||
						(b.name().toUpper().startsWith("RCR-")) || // Van Rysel RCR (Decathlon)
                        !b.name().compare(ftms_bike, Qt::CaseInsensitive) || (b.name().toUpper().startsWith("SMB1")) ||
                        (b.name().toUpper().startsWith("UBIKE FTMS")) || (b.name().toUpper().startsWith("INRIDE")) ||
                        (b.name().toUpper().startsWith("INCONDI")) || // inCondi S150i
                        (b.name().toUpper().startsWith("YPBM") && b.name().length() == 10) ||
                        QRegularExpression(QStringLiteral("^XQ\\d{10}$"), QRegularExpression::CaseInsensitiveOption)
                            .match(b.name())
                            .hasMatch()) &&
                        !QRegularExpression(QStringLiteral("^ADIDAS\\d{4,}$"), QRegularExpression::CaseInsensitiveOption)
                            .match(b.name())
                            .hasMatch() &&
                       !ftmsBike && filter) {
                this->setLastBluetoothDevice(b);
                this->stopDiscovery();
                ftmsBike = new ftmsbike(noWriteResistance, noHeartService, bikeResistanceOffset, bikeResistanceGain);
                emit deviceConnected(b);
                connect(ftmsBike, &bluetoothdevice::connectedAndDiscovered, this, &bluetooth::connectedAndDiscovered);
                // connect(trxappgateusb, SIGNAL(disconnected()), this, SLOT(restart()));
                connect(ftmsBike, &ftmsbike::debug, this, &bluetooth::debug);
                // Queued, and not optional: rescan() deletes ftmsBike, and this signal is
                // emitted from inside one of its own methods. Delivering it directly would
                // free the object the call stack is standing on.
                connect(ftmsBike, &ftmsbike::deviceHasNoFtmsService, this, &bluetooth::rescan,
                        Qt::QueuedConnection);
                ftmsBike->deviceDiscovered(b);
                this->signalBluetoothDeviceConnected(ftmsBike);
            } else if (b.name().toUpper().startsWith(QStringLiteral("CORE ")) && !coreSensor) {
                // *** SPECIAL DEVICE ****
                coreSensor = new coresensor();
                connect(coreSensor, &coresensor::debug, this, &bluetooth::debug);
                coreSensor->deviceDiscovered(b);
                connect(coreSensor, &coresensor::coreBodyTemperatureChanged, this->device(), &bluetoothdevice::coreBodyTemperature);
                connect(coreSensor, &coresensor::skinTemperatureChanged, this->device(), &bluetoothdevice::skinTemperature);
                connect(coreSensor, &coresensor::heatStrainIndexChanged, this->device(), &bluetoothdevice::heatStrainIndex);
            }
        }
    }
}

void bluetooth::connectedAndDiscovered() {

    qDebug() << "bluetooth::connectedAndDiscovered()";

    static bool firstConnected = true;
    QSettings settings;
    QString heartRateBeltName =
        settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();
    bool csc_as_bike =
        settings.value(QZSettings::cadence_sensor_as_bike, QZSettings::default_cadence_sensor_as_bike).toBool();
    QString cscName =
        settings.value(QZSettings::cadence_sensor_name, QZSettings::default_cadence_sensor_name).toString();
    QString powerSensorName =
        settings.value(QZSettings::power_sensor_name, QZSettings::default_power_sensor_name).toString();
    QString eliteRizerName =
        settings.value(QZSettings::elite_rizer_name, QZSettings::default_elite_rizer_name).toString();
    QString eliteSterzoSmartName =
        settings.value(QZSettings::elite_sterzo_smart_name, QZSettings::default_elite_sterzo_smart_name).toString();
    bool fitmetriaFanfitEnabled =
        settings.value(QZSettings::fitmetria_fanfit_enable, QZSettings::default_fitmetria_fanfit_enable).toBool();

    // only at the first very connection, setting the user default resistance
    if (device() && firstConnected && device()->deviceType() == BIKE &&
        settings.value(QZSettings::bike_resistance_start, QZSettings::default_bike_resistance_start).toUInt() != 1) {
        qobject_cast<bike *>(device())->changeResistance(
            settings.value(QZSettings::bike_resistance_start, QZSettings::default_bike_resistance_start).toUInt());
    }

    if (heartRateBeltName.startsWith(QStringLiteral("Disabled"))) {
        if (!settings.value(QZSettings::hrm_lastdevice_name, QZSettings::default_hrm_lastdevice_name)
                 .toString()
                 .isEmpty()) {
            settings.setValue(QZSettings::hrm_lastdevice_name, "");
        }
        if (!settings.value(QZSettings::hrm_lastdevice_address, QZSettings::default_hrm_lastdevice_address)
                 .toString()
                 .isEmpty()) {
            settings.setValue(QZSettings::hrm_lastdevice_address, "");
        }
    }

    if (this->device() != nullptr) {

#ifdef Q_OS_IOS
        if (settings.value(QZSettings::ios_cache_heart_device, QZSettings::default_ios_cache_heart_device).toBool()) {
            QString heartRateBeltName =
                settings.value(QZSettings::heart_rate_belt_name, QZSettings::default_heart_rate_belt_name).toString();
            QString b =
                settings.value(QZSettings::hrm_lastdevice_name, QZSettings::default_hrm_lastdevice_name).toString();
            qDebug() << "last hrm name" << b;
            if (!b.compare(heartRateBeltName) && b.length()) {

                heartRateBelt = new heartratebelt();
                // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

                connect(heartRateBelt, SIGNAL(debug(QString)), this, SLOT(debug(QString)));
                connect(heartRateBelt, SIGNAL(heartRate(uint8_t)), this->device(), SLOT(heartRate(uint8_t)));
                connect(heartRateBelt, SIGNAL(rrIntervalReceived(double)), this->device(), SLOT(rrIntervalReceived(double)));
                QBluetoothDeviceInfo bt;
                bt.setDeviceUuid(QBluetoothUuid(
                    settings.value(QZSettings::hrm_lastdevice_address, QZSettings::default_hrm_lastdevice_address)
                        .toString()));
                qDebug() << "UUID" << bt.deviceUuid();
                heartRateBelt->deviceDiscovered(bt);
            }
        }
#endif
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if (((b.name().startsWith(heartRateBeltName))) && !heartRateBelt &&
                !heartRateBeltName.startsWith(QStringLiteral("Disabled"))) {
                settings.setValue(QZSettings::hrm_lastdevice_name, b.name());

#ifndef Q_OS_IOS
                settings.setValue(QZSettings::hrm_lastdevice_address, b.address().toString());
#else
                settings.setValue(QZSettings::hrm_lastdevice_address, b.deviceUuid().toString());
#endif
                heartRateBelt = new heartratebelt();
                // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

                connect(heartRateBelt, &heartratebelt::debug, this, &bluetooth::debug);
                connect(heartRateBelt, &heartratebelt::heartRate, this->device(), &bluetoothdevice::heartRate);
                connect(heartRateBelt, &heartratebelt::rrIntervalReceived, this->device(), &bluetoothdevice::rrIntervalReceived);
                heartRateBelt->deviceDiscovered(b);
                QzNotify::toast(b.name() + " (HR sensor) connected!");
                break;
            }
        }

        if (fitmetriaFanfitEnabled) {
            for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
                if (((b.name().startsWith("FITFAN-"))) && !fitmetria_fanfit_isconnected(b)) {
                    fitmetria_fanfit *f = new fitmetria_fanfit(this->device());

                    connect(f, &fitmetria_fanfit::debug, this, &bluetooth::debug);

                    connect(this->device(), SIGNAL(fanSpeedChanged(uint8_t)), f, SLOT(fanSpeedRequest(uint8_t)));

                    f->deviceDiscovered(b);
                    fitmetriaFanfit.append(f);
                    break;
                } else if (((b.name().toUpper().startsWith("HEADWIND "))) && !fitmetria_fanfit_isconnected(b)) {
                    wahookickrheadwind *f = new wahookickrheadwind(this->device());

                    connect(f, &wahookickrheadwind::debug, this, &bluetooth::debug);

                    connect(this->device(), SIGNAL(fanSpeedChanged(uint8_t)), f, SLOT(fanSpeedRequest(uint8_t)));

                    f->deviceDiscovered(b);
                    wahookickrHeadWind.append(f);
                    continue;
                } else if (((b.name().toUpper().startsWith("ARIA")) && b.name().length() == 4) && !fitmetria_fanfit_isconnected(b)) {
                    eliteariafan *f = new eliteariafan(this->device());

                    connect(f, &eliteariafan::debug, this, &bluetooth::debug);

                    connect(this->device(), SIGNAL(fanSpeedChanged(uint8_t)), f, SLOT(fanSpeedRequest(uint8_t)));

                    f->deviceDiscovered(b);
                    eliteAriaFan.append(f);
                    break;
                }
            }
        }

        if (!csc_as_bike) {
            for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
                if (((b.name().startsWith(cscName))) && !cadenceSensor &&
                    !cscName.startsWith(QStringLiteral("Disabled"))) {
                    settings.setValue(QZSettings::csc_sensor_lastdevice_name, b.name());

#ifndef Q_OS_IOS
                    settings.setValue(QZSettings::csc_sensor_address, b.address().toString());
#else
                    settings.setValue(QZSettings::csc_sensor_address, b.deviceUuid().toString());
#endif
                    cadenceSensor = new cscbike(false, false, true);
                    // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

                    connect(cadenceSensor, &cscbike::debug, this, &bluetooth::debug);
                    connect(cadenceSensor, &bluetoothdevice::cadenceChanged, this->device(),
                            &bluetoothdevice::cadenceSensor);
                    cadenceSensor->deviceDiscovered(b);
                    QzNotify::toast(b.name() + " (cadence sensor) connected!");
                    break;
                }
            }
        }
    }

    {
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if (((b.name().startsWith(powerSensorName))) && !powerSensor &&
                !powerSensorName.startsWith(QStringLiteral("Disabled"))) {
                settings.setValue(QZSettings::power_sensor_lastdevice_name, b.name());

#ifndef Q_OS_IOS
                settings.setValue(QZSettings::power_sensor_address, b.address().toString());
#else
                settings.setValue(QZSettings::power_sensor_address, b.deviceUuid().toString());
#endif
                if (device() && device()->deviceType() == BIKE) {
                    powerSensor = new stagesbike(false, false, true);
                    // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

                    connect(powerSensor, &stagesbike::debug, this, &bluetooth::debug);
                    connect(powerSensor, &bluetoothdevice::powerChanged, this->device(), &bluetoothdevice::powerSensor);
                    connect(powerSensor, &bluetoothdevice::cadenceChanged, this->device(),
                            &bluetoothdevice::cadenceSensor);
                    powerSensor->deviceDiscovered(b);
                }

                QzNotify::toast(b.name() + " (power sensor) connected!");

                break;
            }
        }
    }

    for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
        if (((b.name().startsWith(eliteRizerName))) && !eliteRizer &&
            !eliteRizerName.startsWith(QStringLiteral("Disabled"))) {
            settings.setValue(QZSettings::elite_rizer_lastdevice_name, b.name());

#ifndef Q_OS_IOS
            settings.setValue(QZSettings::elite_rizer_address, b.address().toString());
#else
            settings.setValue(QZSettings::elite_rizer_address, b.deviceUuid().toString());
#endif
            eliteRizer = new eliterizer(false, false);
            // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

            connect(eliteRizer, &eliterizer::debug, this, &bluetooth::debug);
            connect(eliteRizer, &eliterizer::steeringAngleChanged, (bike *)this->device(), &bike::changeSteeringAngle);
            connect(this->device(), &bluetoothdevice::inclinationChanged, eliteRizer,
                    &eliterizer::changeInclinationRequested);
            eliteRizer->deviceDiscovered(b);
            break;
        }
    }

    for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
        if (((b.name().startsWith(eliteSterzoSmartName))) && !eliteSterzoSmart &&
            !eliteSterzoSmartName.startsWith(QStringLiteral("Disabled")) && this->device() &&
            this->device()->deviceType() == BIKE) {
            settings.setValue(QZSettings::elite_sterzo_smart_lastdevice_name, b.name());

#ifndef Q_OS_IOS
            settings.setValue(QZSettings::elite_sterzo_smart_address, b.address().toString());
#else
            settings.setValue(QZSettings::elite_sterzo_smart_address, b.deviceUuid().toString());
#endif
            eliteSterzoSmart = new elitesterzosmart(false, false);
            // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

            connect(eliteSterzoSmart, &elitesterzosmart::debug, this, &bluetooth::debug);
            connect(eliteSterzoSmart, &eliterizer::steeringAngleChanged, (bike *)this->device(),
                    &bike::changeSteeringAngle);
            eliteSterzoSmart->deviceDiscovered(b);
            break;
        }
    }

    if(settings.value(QZSettings::sram_axs_controller, QZSettings::default_sram_axs_controller).toBool()) {
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if (((b.name().toUpper().startsWith("SRAM "))) && !sramAXSController && this->device() &&
                    this->device()->deviceType() == BIKE) {

                sramAXSController = new sramaxscontroller();
                // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

                connect(sramAXSController, &sramaxscontroller::debug, this, &bluetooth::debug);
                connect(sramAXSController, &sramaxscontroller::plus, (bike*)this->device(), &bike::gearUp);
                connect(sramAXSController, &sramaxscontroller::minus, (bike*)this->device(), &bike::gearDown);
                sramAXSController->deviceDiscovered(b);
                QzNotify::toast("SRAM Connected!");
                break;
            }
        }
    }

    if(settings.value(QZSettings::zwift_click, QZSettings::default_zwift_click).toBool() &&
            this->device() && this->device()->deviceType() == BIKE) {
        bool zwiftplay_swap = settings.value(QZSettings::zwiftplay_swap, QZSettings::default_zwiftplay_swap).toBool();

        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if (!b.name().toUpper().startsWith("ZWIFT CLICK"))
                continue;

            int mfgByte = -1;
            if (b.manufacturerData(2378).size() > 0) {
                mfgByte = int(b.manufacturerData(2378).at(0));
                qDebug() << "Zwift Click manufacturer type" << mfgByte;
            } else {
                qDebug() << "manufacturer not found for ZWIFT CLICK";
            }

            // byte 11 = v1; -1 (no mfg data) treated as v1 only for the first device found
            if ((mfgByte == 11) || (mfgByte == -1 && !zwiftClickRemote)) {
                // v1: single device, type NONE
                if (!zwiftClickRemote) {
                    zwiftClickRemote = new zwiftclickremote(this->device(), AbstractZapDevice::ZWIFT_PLAY_TYPE::NONE);
                    connect(zwiftClickRemote, &zwiftclickremote::debug, this, &bluetooth::debug);
                    connect(zwiftClickRemote->playDevice, &ZwiftPlayDevice::plus, this, [this]() {
                        auto *myWhoosh = MyWhooshLink::instance();
                        if (myWhoosh && myWhoosh->isEnabled() && myWhoosh->overrideLocalGears()) {
                            myWhoosh->handleGearUp(true);
                        } else if (this->device() && this->device()->deviceType() == BIKE) {
                            static_cast<bike *>(this->device())->gearUp();
                        }
                    });
                    connect(zwiftClickRemote->playDevice, &ZwiftPlayDevice::minus, this, [this]() {
                        auto *myWhoosh = MyWhooshLink::instance();
                        if (myWhoosh && myWhoosh->isEnabled() && myWhoosh->overrideLocalGears()) {
                            myWhoosh->handleGearDown(true);
                        } else if (this->device() && this->device()->deviceType() == BIKE) {
                            static_cast<bike *>(this->device())->gearDown();
                        }
                    });
                    zwiftClickRemote->deviceDiscovered(b);
                    QzNotify::toast("Zwift Click Connected!");
                }
            } else if (zwiftPlayDevice.size() < 2) {
                // v2: two devices with LEFT/RIGHT designation
                // known bytes: 3/7 = LEFT, others = RIGHT; unknown (-1) uses ordinal
                AbstractZapDevice::ZWIFT_PLAY_TYPE type;
                if (mfgByte == 3 || mfgByte == 7)
                    type = AbstractZapDevice::ZWIFT_PLAY_TYPE::LEFT;
                else
                    type = zwiftPlayDevice.isEmpty() ? AbstractZapDevice::ZWIFT_PLAY_TYPE::LEFT : AbstractZapDevice::ZWIFT_PLAY_TYPE::RIGHT;
                zwiftPlayDevice.append(new zwiftclickremote(this->device(), type));
                connect(zwiftPlayDevice.last(), &zwiftclickremote::debug, this, &bluetooth::debug);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftUp, this, &bluetooth::zwiftPlayLeftUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftDown, this, &bluetooth::zwiftPlayLeftDown);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftLeft, this, &bluetooth::zwiftPlayLeftLeft);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftRight, this, &bluetooth::zwiftPlayLeftRight);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftShoulder, this, &bluetooth::zwiftPlayLeftShoulder);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPower, this, &bluetooth::zwiftPlayLeftPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPaddle, this, &bluetooth::zwiftPlayLeftPaddle);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftShiftUp, this, &bluetooth::zwiftRideLeftShiftUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftShiftDown, this, &bluetooth::zwiftRideLeftShiftDown);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftPower, this, &bluetooth::zwiftRideLeftPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftPowerUp, this, &bluetooth::zwiftRideLeftPowerUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftOnOff, this, &bluetooth::zwiftRideLeftOnOff);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightY, this, &bluetooth::zwiftPlayRightY);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightZ, this, &bluetooth::zwiftPlayRightZ);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightA, this, &bluetooth::zwiftPlayRightA);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightB, this, &bluetooth::zwiftPlayRightB);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightShoulder, this, &bluetooth::zwiftPlayRightShoulder);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPower, this, &bluetooth::zwiftPlayRightPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPaddle, this, &bluetooth::zwiftPlayRightPaddle);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightZAlt, this, &bluetooth::zwiftRideRightZAlt);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightShiftUp, this, &bluetooth::zwiftRideRightShiftUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightShiftDown, this, &bluetooth::zwiftRideRightShiftDown);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightPower, this, &bluetooth::zwiftRideRightPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightPowerUp, this, &bluetooth::zwiftRideRightPowerUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightOnOff, this, &bluetooth::zwiftRideRightOnOff);
                connect(zwiftPlayDevice.last()->playDevice, &ZwiftPlayDevice::plus, this, [this]() {
                    auto *myWhoosh = MyWhooshLink::instance();
                    if (myWhoosh && myWhoosh->isEnabled() && myWhoosh->overrideLocalGears()) {
                        myWhoosh->handleGearUp(true);
                    } else if (this->device() && this->device()->deviceType() == BIKE) {
                        static_cast<bike *>(this->device())->gearUp();
                    }
                });
                connect(zwiftPlayDevice.last()->playDevice, &ZwiftPlayDevice::minus, this, [this]() {
                    auto *myWhoosh = MyWhooshLink::instance();
                    if (myWhoosh && myWhoosh->isEnabled() && myWhoosh->overrideLocalGears()) {
                        myWhoosh->handleGearDown(true);
                    } else if (this->device() && this->device()->deviceType() == BIKE) {
                        static_cast<bike *>(this->device())->gearDown();
                    }
                });
                if (MyWhooshLink::instance() && MyWhooshLink::instance()->isEnabled()) {
                    auto *myWhoosh = MyWhooshLink::instance();
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftUp, myWhoosh, &MyWhooshLink::handleLeftUp);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftDown, myWhoosh, &MyWhooshLink::handleLeftDown);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftLeft, myWhoosh, &MyWhooshLink::handleLeftLeft);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftRight, myWhoosh, &MyWhooshLink::handleLeftRight);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftShoulder, myWhoosh, &MyWhooshLink::handleLeftShoulder);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPower, myWhoosh, &MyWhooshLink::handleLeftPower);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPaddle, myWhoosh, &MyWhooshLink::handleLeftPaddle);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftPower, myWhoosh, &MyWhooshLink::handleLeftPower);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightY, myWhoosh, &MyWhooshLink::handleRightY);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightZ, myWhoosh, &MyWhooshLink::handleRightZ);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightZAlt, myWhoosh, &MyWhooshLink::handleRightZ);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightA, myWhoosh, &MyWhooshLink::handleRightA);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightB, myWhoosh, &MyWhooshLink::handleRightB);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightShoulder, myWhoosh, &MyWhooshLink::handleRightShoulder);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPower, myWhoosh, &MyWhooshLink::handleRightPower);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPaddle, myWhoosh, &MyWhooshLink::handleRightPaddle);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightPower, myWhoosh, &MyWhooshLink::handleRightPower);
                }
                if((zwiftPlayDevice.last()->typeZap == AbstractZapDevice::LEFT && !zwiftplay_swap) ||
                   (zwiftPlayDevice.last()->typeZap == AbstractZapDevice::RIGHT && zwiftplay_swap)) {
                    connect((bike*)this->device(), &bike::gearOkUp, this, &bluetooth::gearUp);
                    connect((bike*)this->device(), &bike::gearFailedUp, this, &bluetooth::gearFailedUp);
                } else {
                    connect((bike*)this->device(), &bike::gearOkDown, this, &bluetooth::gearDown);
                    connect((bike*)this->device(), &bike::gearFailedDown, this, &bluetooth::gearFailedDown);
                }
                zwiftPlayDevice.last()->deviceDiscovered(b);
                QzNotify::toast("Zwift Click v2 Connected!");
            }
        }
    }

    if(settings.value(QZSettings::thinkrider_controller, QZSettings::default_thinkrider_controller).toBool()) {
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if (((b.name().toUpper().startsWith("THINK VS")) || (b.name().toUpper().startsWith("THINKRIDER"))) && !thinkriderController && this->device() &&
                    this->device()->deviceType() == BIKE) {

                thinkriderController = new thinkridercontroller(this->device());

                connect(thinkriderController, &thinkridercontroller::debug, this, &bluetooth::debug);
                connect(thinkriderController, &thinkridercontroller::plus, (bike*)this->device(), &bike::gearUp);
                connect(thinkriderController, &thinkridercontroller::minus, (bike*)this->device(), &bike::gearDown);
                thinkriderController->deviceDiscovered(b);
                QzNotify::toast("Thinkrider Controller Connected!");
                break;
            }
        }
    }

    if(settings.value(QZSettings::cycplus_bc2_controller, QZSettings::default_cycplus_bc2_controller).toBool()) {
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if ((b.name().toUpper().contains("BC2") ||
                    deviceHasService(b, QBluetoothUuid(QStringLiteral("6e400001-b5a3-f393-e0a9-e50e24dcca9e")))) &&
                    !cycplusBC2Controller && this->device() &&
                    this->device()->deviceType() == BIKE) {

                cycplusBC2Controller = new cycplusbc2controller(this->device());

                connect(cycplusBC2Controller, &cycplusbc2controller::debug, this, &bluetooth::debug);
                connect(cycplusBC2Controller, &cycplusbc2controller::plus, (bike*)this->device(), &bike::gearUp);
                connect(cycplusBC2Controller, &cycplusbc2controller::minus, (bike*)this->device(), &bike::gearDown);
                cycplusBC2Controller->deviceDiscovered(b);
                QzNotify::toast("CYCPLUS BC2 Connected!");
                break;
            }
        }
    }

    if(settings.value(QZSettings::zwift_play, QZSettings::default_zwift_play).toBool()) {
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if (((b.name().toUpper().startsWith("SQUARE"))) && !eliteSquareController && this->device() &&
                this->device()->deviceType() == BIKE) {

                eliteSquareController = new elitesquarecontroller(this->device());
                // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

                connect(eliteSquareController, &elitesquarecontroller::debug, this, &bluetooth::debug);
                connect(eliteSquareController, &elitesquarecontroller::plus, (bike*)this->device(), &bike::gearUp);
                connect(eliteSquareController, &elitesquarecontroller::minus, (bike*)this->device(), &bike::gearDown);
                eliteSquareController->deviceDiscovered(b);
                QzNotify::toast("Elite Square Connected!");
                break;
            }
        }
    }

    if(settings.value(QZSettings::zwift_play, QZSettings::default_zwift_play).toBool()) {
        bool zwiftplay_swap = settings.value(QZSettings::zwiftplay_swap, QZSettings::default_zwiftplay_swap).toBool();
        for (const QBluetoothDeviceInfo &b : qAsConst(devices)) {
            if ((((b.name().toUpper().startsWith("ZWIFT PLAY"))) || b.name().toUpper().startsWith("ZWIFT RIDE") || b.name().toUpper().startsWith("ZWIFT SF2")) && zwiftPlayDevice.size() < 2 && this->device() &&
                    this->device()->deviceType() == BIKE) {

                if(b.manufacturerData(2378).size() > 0) {
                    qDebug() << "this should be 3 or 2. is it? " << int(b.manufacturerData(2378).at(0));
                    zwiftPlayDevice.append(new zwiftclickremote(this->device(),
                                     int(b.manufacturerData(2378).at(0)) == 3 || int(b.manufacturerData(2378).at(0)) == 7 ? AbstractZapDevice::ZWIFT_PLAY_TYPE::LEFT : AbstractZapDevice::ZWIFT_PLAY_TYPE::RIGHT));
                } else {
                    qDebug() << "manufacturer not found for ZWIFT CLICK";
                    zwiftPlayDevice.append(new zwiftclickremote(this->device(),
                                     zwiftPlayDevice.length() == 0 ? AbstractZapDevice::ZWIFT_PLAY_TYPE::LEFT : AbstractZapDevice::ZWIFT_PLAY_TYPE::RIGHT));

                }
                // connect(heartRateBelt, SIGNAL(disconnected()), this, SLOT(restart()));

                connect(zwiftPlayDevice.last(), &zwiftclickremote::debug, this, &bluetooth::debug);
                connect(zwiftPlayDevice.last()->playDevice, &ZwiftPlayDevice::plus, this, &bluetooth::zwiftPlayPlus);
                connect(zwiftPlayDevice.last()->playDevice, &ZwiftPlayDevice::minus, this, &bluetooth::zwiftPlayMinus);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftUp, this, &bluetooth::zwiftPlayLeftUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftDown, this, &bluetooth::zwiftPlayLeftDown);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftLeft, this, &bluetooth::zwiftPlayLeftLeft);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftRight, this, &bluetooth::zwiftPlayLeftRight);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftShoulder, this, &bluetooth::zwiftPlayLeftShoulder);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPower, this, &bluetooth::zwiftPlayLeftPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPaddle, this, &bluetooth::zwiftPlayLeftPaddle);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftShiftUp, this, &bluetooth::zwiftRideLeftShiftUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftShiftDown, this, &bluetooth::zwiftRideLeftShiftDown);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftPower, this, &bluetooth::zwiftRideLeftPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftPowerUp, this, &bluetooth::zwiftRideLeftPowerUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftOnOff, this, &bluetooth::zwiftRideLeftOnOff);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightY, this, &bluetooth::zwiftPlayRightY);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightZ, this, &bluetooth::zwiftPlayRightZ);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightA, this, &bluetooth::zwiftPlayRightA);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightB, this, &bluetooth::zwiftPlayRightB);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightShoulder, this, &bluetooth::zwiftPlayRightShoulder);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPower, this, &bluetooth::zwiftPlayRightPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPaddle, this, &bluetooth::zwiftPlayRightPaddle);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightZAlt, this, &bluetooth::zwiftRideRightZAlt);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightShiftUp, this, &bluetooth::zwiftRideRightShiftUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightShiftDown, this, &bluetooth::zwiftRideRightShiftDown);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightPower, this, &bluetooth::zwiftRideRightPower);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightPowerUp, this, &bluetooth::zwiftRideRightPowerUp);
                connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightOnOff, this, &bluetooth::zwiftRideRightOnOff);
                connect(zwiftPlayDevice.last()->playDevice, &ZwiftPlayDevice::plus, this, [this]() {
                    auto *myWhoosh = MyWhooshLink::instance();
                    if (myWhoosh && myWhoosh->isEnabled() && myWhoosh->overrideLocalGears()) {
                        myWhoosh->handleGearUp(true);
                    } else if (this->device() && this->device()->deviceType() == BIKE) {
                        static_cast<bike *>(this->device())->gearUp();
                    }
                });
                connect(zwiftPlayDevice.last()->playDevice, &ZwiftPlayDevice::minus, this, [this]() {
                    auto *myWhoosh = MyWhooshLink::instance();
                    if (myWhoosh && myWhoosh->isEnabled() && myWhoosh->overrideLocalGears()) {
                        myWhoosh->handleGearDown(true);
                    } else if (this->device() && this->device()->deviceType() == BIKE) {
                        static_cast<bike *>(this->device())->gearDown();
                    }
                });
                if (MyWhooshLink::instance() && MyWhooshLink::instance()->isEnabled()) {
                    auto *myWhoosh = MyWhooshLink::instance();
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftUp, myWhoosh, &MyWhooshLink::handleLeftUp);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftDown, myWhoosh, &MyWhooshLink::handleLeftDown);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftLeft, myWhoosh, &MyWhooshLink::handleLeftLeft);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftRight, myWhoosh, &MyWhooshLink::handleLeftRight);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftShoulder, myWhoosh, &MyWhooshLink::handleLeftShoulder);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPower, myWhoosh, &MyWhooshLink::handleLeftPower);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::leftPaddle, myWhoosh, &MyWhooshLink::handleLeftPaddle);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideLeftPower, myWhoosh, &MyWhooshLink::handleLeftPower);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightY, myWhoosh, &MyWhooshLink::handleRightY);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightZ, myWhoosh, &MyWhooshLink::handleRightZ);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightZAlt, myWhoosh, &MyWhooshLink::handleRightZ);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightA, myWhoosh, &MyWhooshLink::handleRightA);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightB, myWhoosh, &MyWhooshLink::handleRightB);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightShoulder, myWhoosh, &MyWhooshLink::handleRightShoulder);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPower, myWhoosh, &MyWhooshLink::handleRightPower);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rightPaddle, myWhoosh, &MyWhooshLink::handleRightPaddle);
                    connect(zwiftPlayDevice.last()->playDevice, &AbstractZapDevice::rideRightPower, myWhoosh, &MyWhooshLink::handleRightPower);
                }
                if((zwiftPlayDevice.last()->typeZap == AbstractZapDevice::LEFT && !zwiftplay_swap) ||
                   (zwiftPlayDevice.last()->typeZap == AbstractZapDevice::RIGHT && zwiftplay_swap)) {
                    connect((bike*)this->device(), &bike::gearOkUp, this, &bluetooth::gearUp);
                    connect((bike*)this->device(), &bike::gearFailedUp, this, &bluetooth::gearFailedUp);
                } else {
                    connect((bike*)this->device(), &bike::gearOkDown, this, &bluetooth::gearDown);
                    connect((bike*)this->device(), &bike::gearFailedDown, this, &bluetooth::gearFailedDown);
                }
                zwiftPlayDevice.last()->deviceDiscovered(b);
                QzNotify::toast("Zwift Play/Ride Connected!");
            }
        }
    }
#ifdef Q_OS_ANDROID
    if (settings.value(QZSettings::android_notification, QZSettings::default_android_notification).toBool()) {
        QAndroidJniObject javaNotification = QAndroidJniObject::fromString("QZ is running!");
        QAndroidJniObject::callStaticMethod<void>(
            "org/cagnulen/qdomyoszwift/NotificationClient", "notify", "(Landroid/content/Context;Ljava/lang/String;)V",
            QtAndroid::androidContext().object(), javaNotification.object<jstring>());
    }
#endif

#ifdef Q_OS_ANDROID
    if (settings.value(QZSettings::peloton_workout_ocr, QZSettings::default_peloton_workout_ocr).toBool() ||
        settings.value(QZSettings::peloton_bike_ocr, QZSettings::default_peloton_bike_ocr).toBool() ||
        settings.value(QZSettings::zwift_ocr, QZSettings::default_zwift_ocr).toBool()) {
        AndroidActivityResultReceiver *a = new AndroidActivityResultReceiver();
        QAndroidJniObject MediaProjectionManager = QtAndroid::androidActivity().callObjectMethod(
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            QAndroidJniObject::fromString("media_projection").object<jstring>());
        QAndroidJniObject intent =
            MediaProjectionManager.callObjectMethod("createScreenCaptureIntent", "()Landroid/content/Intent;");
        QtAndroid::startActivity(intent, 100, a);
    }
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (settings.value(QZSettings::garmin_companion, QZSettings::default_garmin_companion).toBool()) {
#ifdef Q_OS_ANDROID
        QAndroidJniObject::callStaticMethod<void>("org/cagnulen/qdomyoszwift/Garmin", "init",
                                                  "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
#else
#ifndef IO_UNDER_QT
        if (!h) {
            h = new lockscreen();
            h->garminconnect_init();
        }
#endif
#endif
    }
#endif

#ifdef Q_OS_IOS
    // in order to allow to populate the tiles with the IC BIKE auto connect feature
    if (firstConnected) {
        QBluetoothDeviceInfo bt;
        QString b = settings.value(QZSettings::bluetooth_lastdevice_name, QZSettings::default_bluetooth_lastdevice_name)
                        .toString();
        bt.setDeviceUuid(QBluetoothUuid(
            settings.value(QZSettings::bluetooth_lastdevice_address, QZSettings::default_bluetooth_lastdevice_address)
                .toString()));
        // set name method doesn't exist
        emit(deviceConnected(bt));
    }
#endif

    firstConnected = false;
}

void bluetooth::gearUp() {
    QSettings settings;
    bool zwiftplay_swap = settings.value(QZSettings::zwiftplay_swap, QZSettings::default_zwiftplay_swap).toBool();
    foreach(zwiftclickremote* p, zwiftPlayDevice) {
        if((p->typeZap == AbstractZapDevice::LEFT && !zwiftplay_swap) || (p->typeZap == AbstractZapDevice::RIGHT && zwiftplay_swap)) {
            p->vibrate(0x20);
            return;
        }
    }
}

void bluetooth::gearDown() {
    QSettings settings;
    bool zwiftplay_swap = settings.value(QZSettings::zwiftplay_swap, QZSettings::default_zwiftplay_swap).toBool();
    foreach(zwiftclickremote* p, zwiftPlayDevice) {
        if((p->typeZap == AbstractZapDevice::RIGHT && !zwiftplay_swap) || (p->typeZap == AbstractZapDevice::LEFT && zwiftplay_swap)) {
            p->vibrate(0x20);
            return;
        }
    }
}

void bluetooth::gearFailedUp() {
    QSettings settings;
    bool zwiftplay_swap = settings.value(QZSettings::zwiftplay_swap, QZSettings::default_zwiftplay_swap).toBool();
    foreach(zwiftclickremote* p, zwiftPlayDevice) {
        if((p->typeZap == AbstractZapDevice::LEFT && !zwiftplay_swap) || (p->typeZap == AbstractZapDevice::RIGHT && zwiftplay_swap)) {
            p->vibrate(0x60);
            return;
        }
    }
}

void bluetooth::gearFailedDown() {
    QSettings settings;
    bool zwiftplay_swap = settings.value(QZSettings::zwiftplay_swap, QZSettings::default_zwiftplay_swap).toBool();
    foreach(zwiftclickremote* p, zwiftPlayDevice) {
        if((p->typeZap == AbstractZapDevice::RIGHT && !zwiftplay_swap) || (p->typeZap == AbstractZapDevice::LEFT && zwiftplay_swap)) {
            p->vibrate(0x60);
            return;
        }
    }
}

void bluetooth::heartRate(uint8_t heart) { Q_UNUSED(heart) }

void bluetooth::selectGymModeDevice(const QString &deviceName) {
    QString normalizedDeviceName = deviceName.trimmed();
    normalizedDeviceName.remove(QRegularExpression(QStringLiteral(" \\(\\d+%\\)$")));

    if (normalizedDeviceName.isEmpty() ||
        !normalizedDeviceName.compare(QStringLiteral("Disabled"), Qt::CaseInsensitive) ||
        !normalizedDeviceName.compare(QStringLiteral("Wifi"), Qt::CaseInsensitive)) {
        return;
    }

    gymModeSessionDevice = normalizedDeviceName;
    onlyDiscover = false;
    restart();
}

void bluetooth::rescan() {
    // Neutral wording on purpose: this has two callers now. The rider pressing Search is
    // one; the driver concluding its address is wrong is the other, and it does not read
    // well in a log to be told a rider asked for something they did not. Each caller says
    // why immediately before this line.
    qDebug() << QStringLiteral("bluetooth::rescan - tearing the device down and scanning again");
    userRequestedRescan = true;
    restart();
    userRequestedRescan = false;
}

void bluetooth::restart() {

    QSettings settings;

    if (onlyDiscover) {

        onlyDiscover = false;
        this->startDiscovery();
        return;
    }

    // bluetooth_no_reconnection exists to make QZ exit rather than loop against a bike
    // that is not coming back. Applying it to a deliberate button press would quit the
    // app under the hand of someone who just asked it to look harder, so a rescan the
    // rider asked for is exempt.
    if (!userRequestedRescan &&
        settings.value(QZSettings::bluetooth_no_reconnection, QZSettings::default_bluetooth_no_reconnection).toBool()) {
        exit(EXIT_SUCCESS);
    }

    devices.clear();

    emit this->bluetoothDeviceDisconnected();

    
    
    
    
    
    
    
    
#ifndef Q_OS_IOS
    
#endif
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    if (cscBike) {

        delete cscBike;
        cscBike = nullptr;
    }
    
    
    
        
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
#ifndef Q_OS_IOS
    
    	
    
    
    
        
#endif
    
    
    
    if (ftmsBike) {

        delete ftmsBike;
        ftmsBike = nullptr;
    }
    
    
    
    
    
    
    
    
    if (heartRateBelt) {

        // heartRateBelt->disconnectBluetooth(); // to test
        delete heartRateBelt;
        heartRateBelt = nullptr;
    }
    if (fitmetriaFanfit.length()) {

        foreach (fitmetria_fanfit *f, fitmetriaFanfit) {
            delete f;
            f = nullptr;
        }
        fitmetriaFanfit.clear();
    }
    if (wahookickrHeadWind.length()) {

        foreach (wahookickrheadwind *f, wahookickrHeadWind) {
            delete f;
            f = nullptr;
        }
        wahookickrHeadWind.clear();
    }
    if (eliteAriaFan.length()) {

        foreach (eliteariafan *f, eliteAriaFan) {
            delete f;
            f = nullptr;
        }
        eliteAriaFan.clear();
    }
    if (cadenceSensor) {

        // heartRateBelt->disconnectBluetooth(); // to test
        delete cadenceSensor;
        cadenceSensor = nullptr;
    }
    if (powerSensor) {

        // heartRateBelt->disconnectBluetooth(); // to test
        delete powerSensor;
        powerSensor = nullptr;
    }
    if (eliteRizer) {

        // heartRateBelt->disconnectBluetooth(); // to test
        delete eliteRizer;
        eliteRizer = nullptr;
    }
    if (eliteSterzoSmart) {

        // heartRateBelt->disconnectBluetooth(); // to test
        delete eliteSterzoSmart;
        eliteSterzoSmart = nullptr;
    }
    this->startDiscovery();
}

bluetoothdevice *bluetooth::device() {
    if (simulatedBike) {
        return simulatedBike;
    } else if (cscBike) {
        return cscBike;
    } else if (ftmsBike) {
        return ftmsBike;
    }
    return nullptr;
}

bool bluetooth::handleSignal(int signal) {
    if (signal == SIGNALS::SIG_INT) {
        qDebug() << QStringLiteral("SIGINT");
        exit(EXIT_SUCCESS);
    }
    // Let the signal propagate as though we had not been there
    return false;
}

bool bluetooth::fitmetria_fanfit_isconnected(const QBluetoothDeviceInfo &device) {
    foreach (fitmetria_fanfit *f, fitmetriaFanfit) {
        if (SAME_BLUETOOTH_DEVICE(device, f->bluetoothDevice))
            return true;
    }
    foreach (wahookickrheadwind *f, wahookickrHeadWind) {
        if (SAME_BLUETOOTH_DEVICE(device, f->bluetoothDevice))
            return true;
    }
    foreach (eliteariafan *f, eliteAriaFan) {
        if (SAME_BLUETOOTH_DEVICE(device, f->bluetoothDevice))
            return true;
    }
    return false;
}

#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
void bluetooth::deviceUpdated(const QBluetoothDeviceInfo &device, QBluetoothDeviceInfo::Fields updateFields) {

    debug("deviceUpdated " + device.name() + " " + QString::number(static_cast<int>(updateFields)));
}
#endif
