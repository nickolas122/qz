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

    LinkStatus linkStatus() const override;
    void retryNow() override;

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
    // Five minutes after the bike went away, stop trying. The limit is wall-clock rather
    // than a count of attempts, and measurement has made the case stronger than the
    // arithmetic that first argued it: a failed connect on the WinRT backend takes about
    // 23 seconds to come back, so the backoff's first four steps are invisible against it
    // and the real period between attempts was 25, 26, 27, 31 and 37 seconds
    // (debug-Mon_Aug_24_14_14_06_2026.log). Five minutes is therefore seven or eight
    // attempts here and would be fourteen on a stack that failed instantly - the same
    // ceiling meaning two different things. "Try 11" tells a rider nothing about how much
    // patience is left; "lost 4 minutes ago" does, on every platform. Retrying past this
    // point is not recovery, it is a radio kept warm for a bike that has been switched off.
    static constexpr qint64 RECONNECT_CEILING_MS = 300000;
    QTimer reconnectTimer;
    int reconnectDelayMs = RECONNECT_INITIAL_MS;
    int consecutiveConnectFailures = 0;
    bool multiCentralToastShown = false;

    // --- the link went quiet but nobody hung up -----------------------------------
    //
    // Everything above is driven by QLowEnergyController::stateChanged, and on
    // Windows/Qt 6 that signal does not always arrive. Three sessions on 2026-08-24
    // (C:\QZ\lite-version, build 69009d2, WinRT backend) have the peripheral calling
    // cancelConnection() and QZ receiving nothing at all: no UnconnectedState, no
    // controller error, no service state change. The controller sits in
    // DiscoveredState for as long as you care to look at it, so the reconnect ladder
    // - which works, and is proven to work by the same day's third log - is simply
    // never armed. The only exit was restarting the app.
    //
    // So the driver decides for itself. Indoor Bike Data is what a live link is made
    // of; if it stops arriving on a controller that still claims to be connected, we
    // hang up on our own side and let the existing UnconnectedState path do the rest.
    // No new retry logic - this only supplies the trigger Windows will not.
    /** How long 0x2AD2 may be absent on a Discovered link before we hang up. */
    static constexpr qint64 DATA_STALL_MS = 10000;
    /** With the write queue also timing out, the diagnosis is certain and can be quicker. */
    static constexpr qint64 DATA_STALL_CORROBORATED_MS = 2000;
    /** Unacknowledged writes needed before they count as corroboration. */
    static constexpr int WRITE_TIMEOUTS_FOR_STALL = 3;
    /** Re-issue a teardown this long after one that the stack never completed. */
    static constexpr qint64 STALL_TEARDOWN_REISSUE_MS = 5000;
    /**
     * How long a link that has connected but never delivered a frame is given, measured
     * from ConnectedState. Longer than DATA_STALL_MS because service discovery and the
     * control-point handshake both happen inside it - in practice about two seconds, so
     * this is generous on purpose.
     *
     * This being a longer budget rather than an exemption is the whole point, and the
     * first version got it wrong: it disarmed the watchdog entirely until a frame had
     * arrived, so a reconnect that landed on a link Windows called Discovered and that
     * never delivered anything was never torn down again. The 15:40 session on
     * 2026-08-24 sat in exactly that state for 45 seconds until the app was killed -
     * the same dead end as before, moved one step later. A link that is not delivering
     * is not a link, however new it is.
     */
    static constexpr qint64 FIRST_FRAME_GRACE_MS = 15000;
    /**
     * Has *this* link delivered a frame? Chooses the budget above, and gates the
     * write-timeout corroboration - which is evidence about a link that was working and
     * stopped, not about one that never started.
     */
    bool everReceivedFrame = false;
    /**
     * Has this object *ever* delivered, on any link? Never cleared. Kept for the record
     * a log reader wants; it is deliberately *not* what gates the automatic rescan.
     *
     * It used to be. That guard read "never rescan a driver that has produced a ride",
     * which sounds prudent and was wrong: the 20:10 session on 2026-08-24 delivered data,
     * the bike stopped, and QZ then connected six times to a device with no Fitness
     * Machine service without ever escalating - the backoff climbing to 30 s and the only
     * exit being the five-minute ceiling. To reach that state the link must have dropped
     * and reconnected onto the wrong device three times over; no ride survives that, so
     * the guard was protecting nothing and costing five minutes.
     */
    bool everDeliveredThisSession = false;
    /** Connections that completed discovery without a 0x1826 service, in a row. */
    int consecutiveNoFtmsConnections = 0;
    /**
     * How many of those before concluding the address itself is wrong. Reconnecting can
     * only reach the same device again; only discovery can find a different one.
     */
    static constexpr int NO_FTMS_BEFORE_RESCAN = 3;
    /**
     * How stale the data has to be before an automatic rescan is allowed. The rescan
     * deletes this object and the virtual bike with it, so a training app loses its
     * connection - which is right once the trainer is gone and wrong while it is not.
     * On a session that never had data this delays nothing, because lastFrameEverAt is
     * invalid and the test short-circuits. On one that did, it dominates: the 21:01 session
     * escalated on the fifth no-FTMS connection rather than the third, about thirty seconds
     * after the last frame, which is the intent rather than a surprise.
     */
    static constexpr qint64 RESCAN_MIN_DATA_AGE_MS = 30000;
    /**
     * When a frame last actually arrived. Invalid until one ever does, and never touched
     * by anything else.
     *
     * It exists because lastRefreshCharacteristicChanged2AD2 cannot answer this. That
     * member is the *display* clock and is deliberately rebased to now on every connect,
     * so a fresh link does not open reading "No data for 143 s" - which means by the time
     * serviceScanDone() runs, a few hundred milliseconds later, it always reports about
     * 0.4 s however long the bike has really been gone. The first version of the rescan
     * guard asked it how stale the data was and was therefore never satisfiable: the 20:32
     * session on 2026-08-24 made twelve consecutive no-FTMS connections without once
     * escalating, and took the full five minutes to the ceiling instead.
     *
     * Two different questions were sharing one timestamp. They get one each now.
     */
    QDateTime lastFrameEverAt;
    /** Set once the decision to abandon this address is taken, to stop scheduling retries. */
    bool abandoningAddress = false;
    /** When we last called disconnectFromDevice() over a stall. Invalid if never. */
    QDateTime stallTeardownAt;
    /** Consecutive writes the bike never acknowledged. Reset by any write that lands. */
    int consecutiveWriteTimeouts = 0;
    /** A frame arrived: clear the teardown grace, and on the first one end the outage. */
    void noteLinkIsDelivering();
    /** True while the controller claims Discovered but no data has arrived for the budget. */
    bool linkHasStalled() const;
    /** Hang up on our own side so the reconnect ladder gets its UnconnectedState. */
    void tearDownStalledLink();

    // The link's own view of itself, for LinkStatus. Kept here rather than derived from
    // m_control->state() because two of the phases are ours and not Qt's: GaveUp has no
    // QLowEnergyController equivalent, and Lost has to outlive the controller returning
    // to UnconnectedState, which is where a reconnect starts from.
    LinkStatus::Phase linkPhase = LinkStatus::Idle;
    /** When the link last dropped. Invalid while it is up. What the ceiling is measured from. */
    QDateTime linkLostAt;
    int servicesSeen = 0;
    /** Cleared on a fresh connect, so the ceiling toast is once per outage, not per attempt. */
    bool gaveUpToastShown = false;
    /** 0x2A19 is optional. Without this, a battery that has never been read is 0%. */
    bool batteryLevelKnown = false;

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

    // How far off target the level we are already on has to read before ERG is allowed to
    // move off it. See the hysteresis in resistanceFromPowerRequest().
    static constexpr double ergPowerHysteresisWatts = 5.0;

    // The last level the power table picked, before the gear offset and the difficulty gain
    // are applied. The hysteresis compares against this, and it is what a freewheeling rider
    // holds: both are questions about the table, which speaks in raw levels.
    resistance_t m_lastErgRawResistance = 0;

    // The cadence the power table is inverted against - smoothed, unlike Cadence.value().
    uint16_t ergCadence();
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
     * @brief Hang up on our own side. Default: m_control->disconnectFromDevice().
     *
     * A seam for the same reason as the four above: the stall watchdog's whole output is
     * this one call, and without somewhere to observe it the decision to make it - which
     * is the part with the arithmetic in it - could not be tested at all.
     */
    virtual void closeLink();

    /**
     * @brief Milliseconds since the last Indoor Bike Data frame.
     *
     * Default: measured from lastRefreshCharacteristicChanged2AD2. The seam exists because
     * the alternative for a test is to wait ten real seconds, and a suite that waits is a
     * suite nobody runs.
     */
    virtual qint64 msSinceLastFrame() const;

    /**
     * @brief Milliseconds since a frame really arrived, or -1 if one never has.
     *
     * The counterpart to msSinceLastFrame(), and the distinction is the whole point: that
     * one is the display clock and is rebased on every connect, this one is not touched by
     * anything but a frame. Anything asking how long the bike has been gone wants this.
     */
    qint64 msSinceRealFrame() const;

    /**
     * @brief Forget what belonged to the link that just ended, on connecting a new one.
     *
     * Extracted from the controller's connected handler so that what a connection does and
     * does not reset is one readable list rather than a lambda - and so a test can ask
     * whether connecting rewinds a clock it has no business touching.
     */
    void resetForNewLink();

    /**
     * @brief Apply @p device's name-derived profile: resistance mode, ERG support, ceiling.
     *
     * Called by deviceDiscovered() before the controller is built. Split out so a test can
     * be a *particular* bike without a radio - see VIRTUAL-BIKE.md, Layer B.
     */
    void applyDeviceProfile(const QBluetoothDeviceInfo &device);

  Q_SIGNALS:
    /**
     * Discovery keeps completing against a device with no Fitness Machine service, so the
     * address this driver was given is wrong and reconnecting to it can only find the same
     * wrong device again. Only discovery can find a different one, and this driver does not
     * own the discovery agent - bluetooth does.
     *
     * Connect it queued. The slot deletes this object.
     */
    void deviceHasNoFtmsService();

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
