#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <QBluetoothDeviceDiscoveryAgent>
#include <QFile>
#include <QObject>
#include <QtBluetooth/qlowenergyadvertisingdata.h>
#include <QtBluetooth/qlowenergyadvertisingparameters.h>
#include <QtBluetooth/qlowenergycharacteristic.h>
#include <QtBluetooth/qlowenergycharacteristicdata.h>

#include <QtBluetooth/qlowenergycontroller.h>
#include <QtBluetooth/qlowenergydescriptordata.h>
#include <QtBluetooth/qlowenergyservice.h>
#include <QtBluetooth/qlowenergyservicedata.h>

#include <QtCore/qbytearray.h>
#include <QtCore/qloggingcategory.h>

#include "devices/discoveryoptions.h"
#include "qzsettings.h"

// The abstract device types. These used to arrive transitively through whichever
// concrete device header happened to include them; with the device zoo gone they
// have to be named.
#include "devices/bike.h"
#include "devices/bluetoothdevice.h"
#include "devices/coresensor/coresensor.h"
#include "devices/cscbike/cscbike.h"

#include "devices/eliteariafan/eliteariafan.h"
#include "devices/eliterizer/eliterizer.h"
#include "devices/elitesquarecontroller/elitesquarecontroller.h"
#include "devices/elitesterzosmart/elitesterzosmart.h"
#include "devices/fitmetria_fanfit/fitmetria_fanfit.h"

#include "devices/ftmsbike/ftmsbike.h"
#include "devices/heartratebelt/heartratebelt.h"
#include "signalhandler.h"
#include "devices/simulatedbike/simulatedbike.h"
// Kept for the generic BLE power meter: a bike paired with a separate power
// sensor gets a stagesbike as the sensor, not as the bike. See the
// power_sensor_name branch in bluetooth::connectedAndDiscovered().
#include "devices/stagesbike/stagesbike.h"


#include "devices/sramAXSController/sramAXSController.h"



#include "templateinfosenderbuilder.h"
#include "devices/wahookickrheadwind/wahookickrheadwind.h"

#include "zwift_play/zwiftPlayDevice.h"
#include "zwift_play/zwiftclickremote.h"
#include "devices/cycplusbc2controller/cycplusbc2controller.h"
#include "devices/thinkridercontroller/thinkridercontroller.h"

#ifdef Q_OS_IOS
#include "ios/lockscreen.h"
#endif

class bluetooth : public QObject, public SignalHandler {

    Q_OBJECT
  public:
    bluetooth(const discoveryoptions &options);
    explicit bluetooth(bool logs, const QString &deviceName = QLatin1String(""), bool noWriteResistance = false,
                       bool noHeartService = false, uint32_t pollDeviceTime = 200, bool noConsole = false,
                       bool testResistance = false, int8_t bikeResistanceOffset = 4, double bikeResistanceGain = 1.0,
                       bool startDiscovery = true);
    ~bluetooth();
    bluetoothdevice *device();
    bluetoothdevice *externalInclination() { return eliteRizer; }
    bluetoothdevice *heartRateDevice() { return heartRateBelt; }
    QList<QBluetoothDeviceInfo> devices;
    bool onlyDiscover = false;
    // Set once a UI tree has loaded and connected to deviceConnected. Device
    // discovery is deferred until then, because a device found before anything is
    // listening emits into the void. Named for homeform until phase 7c; either tree
    // sets it now.
    volatile bool uiLoaded = false;

  private:
    bool useDiscovery = false;
    QFile *debugCommsLog = nullptr;
    QBluetoothDeviceDiscoveryAgent *discoveryAgent = nullptr;
    coresensor* coreSensor = nullptr;
    cscbike *cscBike = nullptr;
    ftmsbike *ftmsBike = nullptr;
    // The bike that is not there. Built in the constructor when simulated_bike is set, in
    // place of discovery rather than as a result of it - see the note there.
    simulatedbike *simulatedBike = nullptr;
    heartratebelt *heartRateBelt = nullptr;
    cscbike *cadenceSensor = nullptr;
    stagesbike *powerSensor = nullptr;
    eliterizer *eliteRizer = nullptr;
    elitesterzosmart *eliteSterzoSmart = nullptr;
    QList<fitmetria_fanfit *> fitmetriaFanfit;
    QList<wahookickrheadwind *> wahookickrHeadWind;
    QList<eliteariafan *> eliteAriaFan;
    QList<zwiftclickremote* > zwiftPlayDevice;
    zwiftclickremote* zwiftClickRemote = nullptr;
    cycplusbc2controller* cycplusBC2Controller = nullptr;
    thinkridercontroller* thinkriderController = nullptr;
    sramaxscontroller* sramAXSController = nullptr;
    elitesquarecontroller* eliteSquareController = nullptr;
    QString filterDevice = QLatin1String("");
    QString gymModeSessionDevice = QLatin1String("");

    bool testResistance = false;
    bool noWriteResistance = false;
    bool noHeartService = false;
    bool noConsole = false;
    bool logs = true;
    uint32_t pollDeviceTime = 200;
    int8_t bikeResistanceOffset = 4;
    double bikeResistanceGain = 1.0;
    bool forceHeartBeltOffForTimeout = false;

    /**
     * @brief Start the Bluetooth discovery agent.
     */
    void startDiscovery();

    /**
     * @brief Stop the Bluetooth discovery agent.
     */
    void stopDiscovery();

    bool handleSignal(int signal) override;
    bool deviceHasService(const QBluetoothDeviceInfo &device, QBluetoothUuid service);
    bool heartRateBeltAvaiable();
    bool cscSensorAvaiable();
    bool powerSensorAvaiable();
    bool eliteRizerAvaiable();
    bool eliteSterzoSmartAvaiable();
    bool fitmetriaFanfitAvaiable();
    bool zwiftDeviceAvaiable();
    bool sramDeviceAvaiable();
    bool cycplusBC2DeviceAvaiable();
    bool thinkriderDeviceAvaiable();
    bool fitmetria_fanfit_isconnected(const QBluetoothDeviceInfo &device);
    bool gymModeEnabled() const;

    QTimer discoveryTimeout;
    bool discoveryFinishedHandled = false;

#ifdef Q_OS_WIN
    /**
     * @brief Connect to the last known device when discovery has found nothing.
     * On Windows a device the OS is already connected to does not advertise, so
     * discovery cannot see it however long it runs. See the call site.
     */
    void connectToLastDeviceIfIdle();
#endif

#ifdef Q_OS_IOS
    lockscreen *h = nullptr;
#endif

    /**
     * @brief Store the name and other info in the settings.
     * @param b The bluetooth device info.
     */
    void setLastBluetoothDevice(const QBluetoothDeviceInfo &b);
    void signalBluetoothDeviceConnected(bluetoothdevice *b);
  signals:
    void deviceConnected(QBluetoothDeviceInfo b);
    void deviceFound(QString name);
    void searchingStop();

    void bluetoothDeviceConnected(bluetoothdevice *b);
    void bluetoothDeviceDisconnected();
    void zwiftClickPlus();
    void zwiftClickMinus();
    void zwiftPlayPlus();
    void zwiftPlayMinus();
    void zwiftPlayLeftUp(bool pressed);
    void zwiftPlayLeftDown(bool pressed);
    void zwiftPlayLeftLeft(bool pressed);
    void zwiftPlayLeftRight(bool pressed);
    void zwiftPlayLeftShoulder(bool pressed);
    void zwiftPlayLeftPower(bool pressed);
    void zwiftPlayLeftPaddle(int value);
    void zwiftRideLeftShiftUp(bool pressed);
    void zwiftRideLeftShiftDown(bool pressed);
    void zwiftRideLeftPower(bool pressed);
    void zwiftRideLeftPowerUp(bool pressed);
    void zwiftRideLeftOnOff(bool pressed);
    void zwiftPlayRightY(bool pressed);
    void zwiftPlayRightZ(bool pressed);
    void zwiftPlayRightA(bool pressed);
    void zwiftPlayRightB(bool pressed);
    void zwiftPlayRightShoulder(bool pressed);
    void zwiftPlayRightPower(bool pressed);
    void zwiftPlayRightPaddle(int value);
    void zwiftRideRightZAlt(bool pressed);
    void zwiftRideRightShiftUp(bool pressed);
    void zwiftRideRightShiftDown(bool pressed);
    void zwiftRideRightPower(bool pressed);
    void zwiftRideRightPowerUp(bool pressed);
    void zwiftRideRightOnOff(bool pressed);
  public slots:
    void restart();
    void selectGymModeDevice(const QString &deviceName);
    void debug(const QString &string);
    void heartRate(uint8_t heart);
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
  private slots:
#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
    void deviceUpdated(const QBluetoothDeviceInfo &device, QBluetoothDeviceInfo::Fields updateFields);
#endif
    void canceled();
    void finished();
    void connectedAndDiscovered();
    void gearDown();
    void gearUp();
    void gearFailedDown();
    void gearFailedUp();

  signals:
};

#endif // BLUETOOTH_H
