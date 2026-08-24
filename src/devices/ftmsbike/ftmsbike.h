#ifndef FTMSBIKE_H
#define FTMSBIKE_H

#include <QBluetoothDeviceDiscoveryAgent>
#include <QtBluetooth/qlowenergyadvertisingdata.h>
#include <QtBluetooth/qlowenergyadvertisingparameters.h>
#include <QtBluetooth/qlowenergycharacteristic.h>
#include <QtBluetooth/qlowenergycharacteristicdata.h>
#include <QtBluetooth/qlowenergycontroller.h>
#include <QtBluetooth/qlowenergydescriptordata.h>
#include <QtBluetooth/qlowenergyservice.h>
#include <QtBluetooth/qlowenergyservicedata.h>
#include <QtCore/qbytearray.h>
#include <QtCore/qqueue.h>

#ifndef Q_OS_ANDROID
#include <QtCore/qcoreapplication.h>
#else
#include <QtGui/qguiapplication.h>
#endif
#include <QtCore/qlist.h>
#include <QtCore/qmutex.h>
#include <QtCore/qscopedpointer.h>
#include <QtCore/qtimer.h>

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QString>

#include <vector>

#include "wheelcircumference.h"
#include "devices/bike.h"
#include "devices/ftmsbike/ftmscontrolpointhandshake.h"
#include "devices/ftmsbike/resistanceslewlimiter.h"
#include "devices/ftmsbike/servicesubscriptionplan.h"
#include "inclinationresistancetable.h"

#ifdef Q_OS_IOS
#include "ios/lockscreen.h"
#endif

enum FtmsControlPointCommand {
    FTMS_REQUEST_CONTROL = 0x00,
    FTMS_RESET,
    FTMS_SET_TARGET_SPEED,
    FTMS_SET_TARGET_INCLINATION,
    FTMS_SET_TARGET_RESISTANCE_LEVEL,
    FTMS_SET_TARGET_POWER,
    FTMS_SET_TARGET_HEARTRATE,
    FTMS_START_RESUME,
    FTMS_STOP_PAUSE,
    FTMS_SET_TARGETED_EXP_ENERGY,
  FTMS_SET_TARGETED_STEPS,
    FTMS_SET_TARGETED_STRIDES,
    FTMS_SET_TARGETED_DISTANCE,
    FTMS_SET_TARGETED_TIME,
    FTMS_SET_TARGETED_TIME_TWO_HR_ZONES,
    FTMS_SET_TARGETED_TIME_THREE_HR_ZONES,
    FTMS_SET_TARGETED_TIME_FIVE_HR_ZONES,
    FTMS_SET_INDOOR_BIKE_SIMULATION_PARAMS,
    FTMS_SET_WHEEL_CIRCUMFERENCE,
    FTMS_SPIN_DOWN_CONTROL,
    FTMS_SET_TARGETED_CADENCE,
    FTMS_RESPONSE_CODE = 0x80
};

enum FtmsResultCode {
    FTMS_SUCCESS = 0x01,
    FTMS_NOT_SUPPORTED,
    FTMS_INVALID_PARAMETER,
    FTMS_OPERATION_FAILED,
    FTMS_CONTROL_NOT_PERMITTED
};

class ftmsbike : public bike {
    Q_OBJECT
  public:
    ftmsbike(bool noWriteResistance, bool noHeartService, int8_t bikeResistanceOffset, double bikeResistanceGain);
    ~ftmsbike();
    bool connected() override;
    resistance_t pelotonToBikeResistance(int pelotonResistance) override;
    resistance_t maxResistance() override { return max_resistance; }
    resistance_t resistanceFromPowerRequest(uint16_t power) override;
    void changePower(int32_t power) override;
    double maxGears() override;
    double minGears() override;
    void enableManualResistancePowerAdjustment(resistance_t resistance);

    // Most FTMS bikes can use QZ's software ERG emulation, but FS-YK devices
    // should stay on direct resistance control because it doesn't send the current resistance value and it conflicts
    // with the PID HR method
    bool ergModeSupportedAvailableBySoftware() override { return !FS_YK; }
    bool inclinationAvailableBySoftware() override { return !resistance_lvl_mode; }

  private:
  protected:
    /**
     * @brief One queued write.
     *
     * Protected rather than private, and declared here rather than beside the other seams,
     * because performWrite() takes one and writeQueue below is typed on it: a test subclass
     * cannot override a method whose parameter type it is not allowed to name, and the type
     * has to be visible before the queue that holds it.
     */
    struct WriteRequest {
        QByteArray data;
        QString info;
        bool disable_log = false;
        bool wait_for_response = false;
        QLowEnergyService *service = nullptr;
        QLowEnergyCharacteristic characteristic;
        bool write_without_response = false;
    };

  private:
    bool writeCharacteristic(uint8_t *data, uint8_t data_len, const QString &info, bool disable_log = false,
                             bool wait_for_response = false);
    void writeCharacteristicZwiftPlay(uint8_t *data, uint8_t data_len, const QString &info, bool disable_log = false,
                             bool wait_for_response = false);
    bool enqueueWrite(QLowEnergyService *service, const QLowEnergyCharacteristic &characteristic, uint8_t *data,
                      uint8_t data_len, const QString &info, bool disable_log, bool wait_for_response,
                      bool write_without_response);
    void processWriteQueue();
    void completeCurrentWrite();
    void zwiftPlayInit();
    void startDiscover();
    void setWheelDiameter(double diameter);
    uint16_t watts() override;
    void init();
    void forceResistance(resistance_t requestResistance);
    void initHandshakeTick();
    void commandResistance(resistance_t requestResistance);
    void configureResistanceSlew(QSettings &settings);
    void resistanceSlewTick();
    bool ergResistanceAccepted(resistance_t newResistance);
    void forcePower(int16_t requestPower);
    void forceInclination(double requestInclination);
    void sendZwiftPlayInclination(double inclination);
    uint16_t wattsFromResistance(double resistance);

    QTimer *refresh;
    
    // Gear modification constants
    static constexpr int GEARS_SLOPE_MULTIPLIER = 50;

    QList<QLowEnergyService *> gattCommunicationChannelService;
    QLowEnergyCharacteristic gattWriteCharControlPointId;
    QLowEnergyService *gattFTMSService = nullptr;

    QLowEnergyCharacteristic zwiftPlayWriteChar;
    QLowEnergyService *zwiftPlayService = nullptr;

    // MOK Fitness bikes don't use the FTMS control point (0x2AD9) to change resistance,
    // they need a raw command written to a proprietary characteristic (0xFFF2) instead.
    QLowEnergyCharacteristic gattWriteCharMokFitnessId;
    QLowEnergyService *gattMokFitnessService = nullptr;

    uint8_t sec1Update = 0;
    QByteArray lastPacket;
    QByteArray lastPacketFromFTMS;
    QDateTime lastRefreshCharacteristicChangedPower = QDateTime::currentDateTime();
    QDateTime lastRefreshCharacteristicChanged2AD2 = QDateTime::currentDateTime();
    QDateTime lastRefreshCharacteristicChanged2ACE = QDateTime::currentDateTime();
    QDateTime lastDomyosResistanceCommand = QDateTime::currentDateTime().addSecs(-60);
    QDateTime domyosResistanceRetryAfter = QDateTime::currentDateTime().addSecs(-60);
    bool ftmsFrameReceived = false;
    uint8_t firstStateChanged = 0;
    int8_t bikeResistanceOffset = 4;
    double bikeResistanceGain = 1.0;
    double lastGearValue = -1;
    int max_resistance = 100;

    bool initDone = false;
    bool initRequest = false;
    // Set when the stack refuses a write outright rather than the bike ignoring it.
    // On Windows a lapsed bond denies every write while reads keep being served from
    // the OS GATT cache, so the connection looks healthy and only the handshake
    // notices. Distinguishes "console never answers" from "we were never allowed to
    // ask" when the handshake ends degraded.
    bool writeAccessDenied = false;
    // Deliberately not cleared on reconnect, unlike writeAccessDenied: dropping the
    // pairing is a one-shot remedy, and a bond that lapses again after being rebuilt
    // is a different problem that tearing the record down repeatedly will not fix.
    bool bondRepairAttempted = false;
    // Which services still need subscribing, and whether it is time to subscribe
    // any of them. The decision lives in a header with no Qt in it because it has
    // been got wrong twice in opposite directions and neither error was reachable
    // from a test - see servicesubscriptionplan.h and its suite. Reset in
    // serviceScanDone(), which is where the service objects are rebuilt.
    ServiceSubscriptionPlan subscriptionPlan;

    /** The current service objects, as the plan sees them. */
    std::vector<ServiceView> serviceViews() const;
    /** The plan's ids are the service pointers; they are opaque to it. */
    static uint64_t serviceId(const QLowEnergyService *s) { return reinterpret_cast<uint64_t>(s); }

    // serviceScanDone() no longer discovers details as it creates each service, so
    // the gate in stateChanged() now genuinely waits for every service. That is
    // correct, but it removes the accidental guarantee of forward progress the
    // partial-list behaviour used to provide: one service whose discovery never
    // resolves would otherwise hold every other service's subscription hostage,
    // and a dropped discovery request is plausible on Windows given the stale
    // GATT cache already seen there. This fires if that happens, and forces the
    // pass. A firing watchdog is a bug report, not routine operation.
    QTimer serviceDiscoveryWatchdog;
    static constexpr int SERVICE_DISCOVERY_WATCHDOG_MS = 10000;

    // Reconnection used to be attempted the instant the controller went
    // unconnected, which with the bike off - or held by another central - is a
    // tight loop against the radio that fills the log with nothing.
    static constexpr int RECONNECT_INITIAL_MS = 1000;
    static constexpr int RECONNECT_MAX_MS = 30000;
    // Qt cannot tell "another central owns it" from "out of range" - the OS does
    // not report why an attempt failed - so this is a guess offered after enough
    // failures that it is worth guessing. Undiagnosable but common beats silent.
    static constexpr int MULTI_CENTRAL_WARN_AFTER = 5;
    QTimer reconnectTimer;
    int reconnectDelayMs = RECONNECT_INITIAL_MS;
    int consecutiveConnectFailures = 0;
    bool multiCentralToastShown = false;

    /** Tear down the service objects and everything pointing into them. */
    void discardServiceObjects();

    bool noWriteResistance = false;
    bool noHeartService = false;

    bool powerForced = false;
    resistance_t m_lastErgResistance = 0;

    // Sequences REQUEST_CONTROL then START_RESUME, each behind its acknowledgement, so the
    // start cannot arrive before the bike has granted control. See ftmsbike::init().
    ftmsControlPointHandshake initHandshake;

    // Rate limiter for resistance commands, so the target never outruns the magnets.
    // Inert unless resistance_slew_up/_down are configured.
    resistanceSlewLimiter resistanceSlew;

    // How long a single-level ERG change has to be asked for before it is acted on. Anything
    // larger is a real move and is not delayed. See ergResistanceAccepted().
    static constexpr qint64 ergSingleLevelHoldMs = 3000;
    resistance_t m_ergPendingResistance = 0;
    qint64 m_ergPendingSince = 0;
    bool manualResistancePowerAdjustmentActive = false;
    bool manualResistancePowerAdjustmentToastShown = false;
    resistance_t manualResistanceTarget = 1;

    bool resistance_lvl_mode = false;
    bool resistance_received = false;
    bool native_resistance_received = false;
    QDateTime calculatedResistanceFallbackSince;
    inclinationResistanceTable _inclinationResistanceTable;

    // D500V2 workaround: track if we're awaiting start simulation command after request control
    bool awaiting_start_simulation_after_request_control = false;
    resistance_t lastDomyosRequestedResistance = -1;

    bool DU30_bike = false;
    bool ICSE = false;
    bool DOMYOS = false;
    bool D500V2 = false;
    bool _3G_Cardio_RB = false;
    bool SCH_190U = false;
    bool SCH_290R = false;
    bool D2RIDE = false;
    bool WATTBIKE = false;
    bool VFSPINBIKE = false;
    bool DIRETO_XR = false;
    bool JFBK5_0 = false;
    bool BIKE_ = false;
    bool SMB1 = false;
    bool LYDSTO = false;
    bool DMASUN = false;
    bool SL010 = false;
    bool REEBOK = false;
    bool TITAN_7000 = false;
    bool T2 = false;
    bool FIT_BK = false;
    bool YS_G1MPLUS = false;
    bool EXPERT_SX9 = false;
    bool THINK_X = false;
    bool WLT8828 = false;
    bool VANRYSEL_HT = false;
    bool MAGNUS = false;
    bool MRK_S26C = false;
    bool MRK_S28 = false;
    bool MRK_S36C = false;
    bool HAMMER = false;
    bool YPBM = false;
    bool SPORT01 = false;
    bool FS_YK = false;
    bool S18 = false;
    bool ZIPRO_RAVE = false;
    bool SPEEDRACEX = false;
    bool USDC_D700 = false;
    bool TOPUTURE_TEB5 = false;
    bool SMARTBIKE_3DIGIT = false;
    bool MOK_FITNESS = false;

    uint8_t secondsToResetTimer = 5;

    int16_t T2_lastGear = 0;

    uint8_t battery_level = 0;

    bool wattReceived = false;
    bool gearInclinationSent = false;

    QQueue<WriteRequest> writeQueue;
    bool isWriting = false;
    bool currentWriteWaitingForResponse = false;
    QLowEnergyService *currentWriteService = nullptr;
    QTimer *writeTimeoutTimer = nullptr;

    uint16_t oldLastCrankEventTime = 0;
    uint16_t oldCrankRevs = 0;
    QDateTime lastGoodCadence = QDateTime::currentDateTime();

#ifdef Q_OS_IOS
    lockscreen *h = 0;
#endif

  Q_SIGNALS:
    void disconnected();
    void debug(QString string);

  protected:
    /**
     * @brief The notification handler, addressable without a QLowEnergyCharacteristic.
     *
     * Layer B of docs/fork/VIRTUAL-BIKE.md exists because of one fact: a test cannot
     * fabricate a QLowEnergyCharacteristic. It has no public way to set its UUID - the data
     * is filled in by QLowEnergyServicePrivate - so a default-constructed one reports a null
     * UUID and every branch of the handler misses. Taking the UUID as an argument is what
     * makes the shipped parser reachable from a test.
     *
     * @param characteristicUuid What the notification arrived on.
     * @param fromService The QObject that emitted it, compared against currentWriteService to
     *        decide whether this notification completes a write in flight. The Qt slot passes
     *        sender(); a test passes whatever it is pretending to be.
     */
    virtual void handleNotification(const QBluetoothUuid &characteristicUuid,
                                    const QByteArray &newValue, QObject *fromService);

    /**
     * @brief Does a link exist at all? Default: m_control has been built.
     *
     * update() dereferences m_control, so an object with no controller is inert. These two
     * are the whole reason a test can drive update() - see VIRTUAL-BIKE.md, Layer B.
     */
    virtual bool linkExists() const;

    /** @brief The controller's state, or Unconnected when there is no controller. */
    virtual QLowEnergyController::ControllerState linkState() const;

    /**
     * @brief Is the control point there to be written to? Default: gattFTMSService exists.
     *
     * The outbound path defends itself at three levels - this, enqueueTargetValid() and
     * writeTargetReady() - and all three ask, directly or not, for a QLowEnergyCharacteristic
     * a test cannot build. Each is its own virtual because each guards a different thing, and
     * collapsing them would change what production checks.
     */
    virtual bool controlPointReady() const;

    /** @brief Can a write be queued for this target? Default: both are valid. */
    virtual bool enqueueTargetValid(QLowEnergyService *service,
                                    const QLowEnergyCharacteristic &characteristic) const;

    /** @brief Is @p request's target ready to be written to? Default: its service is discovered. */
    virtual bool writeTargetReady(const WriteRequest &request) const;

    /** @brief Put @p data on the wire. Default: service->writeCharacteristic(). */
    virtual void performWrite(const WriteRequest &request, const QByteArray &data);

    /**
     * @brief Apply @p device's name-derived profile: resistance mode, ERG support, ceiling.
     *
     * Called by deviceDiscovered() before the controller is built. Split out so a test can
     * be a *particular* bike without a radio - see VIRTUAL-BIKE.md, Layer B.
     */
    void applyDeviceProfile(const QBluetoothDeviceInfo &device);

  public slots:
    void deviceDiscovered(const QBluetoothDeviceInfo &device);

  private slots:

    void characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);
    void characteristicWritten(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);
    void descriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &newValue);
    void characteristicRead(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);
    void descriptorRead(const QLowEnergyDescriptor &descriptor, const QByteArray &newValue);
    void stateChanged(QLowEnergyService::ServiceState state);
    // The work stateChanged() does once every service has resolved. Split out so
    // the discovery watchdog can force it when one never does.
    void subscribeToServices();
    void serviceDiscoveryTimeout();
    void controllerStateChanged(QLowEnergyController::ControllerState state);

    void serviceDiscovered(const QBluetoothUuid &gatt);
    void serviceScanDone(void);
    bool shouldUseCalculatedResistanceFallback(const QDateTime &now);
    void update();
    void error(QLowEnergyController::Error err);
    void errorService(QLowEnergyService::ServiceError);
    void ftmsCharacteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);
};

#endif // FTMSBIKE_H
