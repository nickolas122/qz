#include "devicetestdataindex.h"
#include "deviceindex.h"

#include "bluetooth.h"
#include "devicenamepatterngroup.h"
#include "bluetoothdevicetestdata.h"
#include "bluetoothdevicetestdatabuilder.h"
#include "devicediscoveryinfo.h"
#include "qzsettings.h"




bool DeviceTestDataIndex::isInitialized = false;

/**
 * @brief hex2bytes Converts a hexadecimal string to bytes, 2 characters at a time.
 * @param s An hexadecimal string e.g. "023F4A" to  { 0x02, 0x3F, 0x4A }
 */
static QByteArray hex2bytes(const std::string& s)
{
    QByteArray v;

    for (size_t i = 0; i < s.length(); i +=2)
    {
        std::string slice(s, i, 2);
        uint8_t value = std::stoul(slice, 0, 16);
        v.append(value);
    }
    return v;
}


QMap<QString,const BluetoothDeviceTestData*> DeviceTestDataIndex::testData;


const std::vector<QString> DeviceTestDataIndex::Names() {
    std::vector<QString> result;

    for(auto key : testData.keys())
        result.push_back(key);

    return result;
}

const std::vector<const BluetoothDeviceTestData *> DeviceTestDataIndex::TestData() {
    std::vector<const BluetoothDeviceTestData*> result;

    for(auto item : testData)
        result.push_back(item);

    return result;
}

BluetoothDeviceTestDataBuilder *  DeviceTestDataIndex::RegisterNewDeviceTestData(const QString& name)
{
    auto existing = testData.value(name, nullptr);
    if(existing)
        delete existing;
    BluetoothDeviceTestDataBuilder * result = new BluetoothDeviceTestDataBuilder(name);
    testData.insert(name, result);
    return result;
}

const BluetoothDeviceTestData *DeviceTestDataIndex::GetTestData(const QString &name) {
    if(!isInitialized)
        throw std::invalid_argument("Device test data is not initialized.");

    return testData.value(name, nullptr);
}


QMultiMap<DeviceTypeId, const BluetoothDeviceTestData*> DeviceTestDataIndex::WhereExpects(const std::unordered_set<DeviceTypeId> &typeIds) {
    QMultiMap<DeviceTypeId, const BluetoothDeviceTestData*> result;

    if(typeIds.empty())
        return result;

    for(auto item : qAsConst(testData)) {
        if(typeIds.count(item->ExpectedDeviceType()))
            result.insert(item->ExpectedDeviceType(), item);
    }

    return result;
}

void DeviceTestDataIndex::Initialize() {

    if(isInitialized)
        return;

    const QString testIP = "1.2.3.4";

    // CSC Bike (Named)
    QString cscBikeName = "CyclingSpeedCadenceBike-";
    RegisterNewDeviceTestData(DeviceIndex::CSCBike_Named)
        ->expectDevice<cscbike>()        
        ->acceptDeviceName(cscBikeName, DeviceNameComparison::StartsWith)
        ->rejectDeviceName("X" + cscBikeName, DeviceNameComparison::Exact)
        ->configureSettingsWith([cscBikeName](DeviceDiscoveryInfo &info, bool enable) -> void {
            info.setValue(QZSettings::cadence_sensor_name, enable ? cscBikeName : "Disabled");
            info.setValue(QZSettings::cadence_sensor_as_bike, enable);
        });

    cscBikeName = "CyclingSpeedCadenceBike-";
    RegisterNewDeviceTestData(DeviceIndex::CSCBike)
        ->expectDevice<cscbike>()
        ->acceptDeviceName(QStringLiteral("JOROTO-BK-"), DeviceNameComparison::StartsWithIgnoreCase)
        ->configureSettingsWith(
            [cscBikeName](const DeviceDiscoveryInfo &info, bool enable, std::vector<DeviceDiscoveryInfo> &configurations)->void
            {
                DeviceDiscoveryInfo config(info);

                if(enable) {
                    // If the Bluetooth name doesn't match the one being tested, but if csc_as_bike is enabled in the settings,
                    // and the bluetooth name does match the cscName in the settings, the device will be detected anyway,
                    // so prevent this by not including that specific configuration
                    //
                    // In order for the search to actually happen, the cscName has to be "Disabled" or csc_as_bike must be true.
                    /*
                                                    config.setValue(QZSettings::csc_as_bike, true);
                                                    config.setValue(QZSettings::cscName, cscBikeName);
                                                    configurations.push_back(config);
                                                    */

                    config.setValue(QZSettings::cadence_sensor_name, "Disabled");
                    config.setValue(QZSettings::cadence_sensor_as_bike, true);
                    configurations.push_back(config);

                    config.setValue(QZSettings::cadence_sensor_name, "Disabled");
                    config.setValue(QZSettings::cadence_sensor_as_bike, false);
                    configurations.push_back(config);

                    config.setValue(QZSettings::cadence_sensor_as_bike, true);
                    config.setValue(QZSettings::cadence_sensor_name,"NOT "+cscBikeName);
                    configurations.push_back(config);
                }
                else  {
                    // prevent the search
                    config.setValue(QZSettings::cadence_sensor_as_bike, false);
                    config.setValue(QZSettings::cadence_sensor_name, "NOT "+cscBikeName);
                    configurations.push_back(config);
                }
            });


    // TODO: check if this is actually used
    // Elite Sterzo Smart
    RegisterNewDeviceTestData(DeviceIndex::EliteSterzoSmart)
        ->expectDevice<elitesterzosmart>()
        ->disable("Unable to detect with current logic");

    // FTMS Bike general
    auto ftmsBikeConfigureExclusions = {
        DeviceTestDataIndex::GetTypeId<stagesbike>()
    };

    // FTMS Bike Hammer Racer S
    RegisterNewDeviceTestData(DeviceIndex::FTMSBikeHammerRacerS)
        ->expectDevice<ftmsbike>()        
        ->acceptDeviceName("FS-", DeviceNameComparison::StartsWith)
        ->configureSettingsWith(QZSettings::hammer_racer_s)
        ->excluding(ftmsBikeConfigureExclusions);

    // FTMS Bike Hammer 64123
    RegisterNewDeviceTestData(DeviceIndex::FTMSBikeHammer)
        ->expectDevice<ftmsbike>()
        ->acceptDeviceName("HAMMER ", DeviceNameComparison::StartsWithIgnoreCase)
        ->configureSettingsWith(
            [](const DeviceDiscoveryInfo &info, bool enable, std::vector<DeviceDiscoveryInfo> &configurations) -> void
            {
                DeviceDiscoveryInfo config(info);

                if (enable) {
                    config.setValue(QZSettings::power_sensor_as_bike, false);
                    config.setValue(QZSettings::saris_trainer, false);
                    configurations.push_back(config);
                } else {
                for(int x = 1; x<=3; x++) {
                    config.setValue(QZSettings::power_sensor_as_bike, x & 1);
                    config.setValue(QZSettings::saris_trainer, x & 2);
                    configurations.push_back(config);
                }

            }})
        ->excluding(ftmsBikeConfigureExclusions);

    // FTMS Bike IConsole
    RegisterNewDeviceTestData(DeviceIndex::FTMSBikeIConsole)
        ->expectDevice<ftmsbike>()
        ->acceptDeviceName("ICONSOLE+", DeviceNameComparison::StartsWithIgnoreCase)
        ->configureSettingsWith(QZSettings::toorx_ftms)
        ->excluding(ftmsBikeConfigureExclusions);


    // FTMS Bike
    QStringList acceptableFTMSNames {
        "DHZ-", // JK fitness 577
        "MKSM", // MKSM3600036
        "YS_C1_", // Yesoul C1H
        "YS_G1_", // Yesoul S3
        "YS_G1MPLUS", // Yesoul G1M Plus
        "DS25-", // Bodytone DS25
        "SCHWINN 510T",
        "3G CARDIO ",
        "ZWIFT HUB",
        "FLXCY-", // Pro FlexBike
        "QB-WC01", // Nexgim QB-C01 smart bike
        "XBR55",
        "ECHO_BIKE_",
        "EW-JS-",
        "MERACH-MR667-",
        "DS60-",
        "SPAX-BK-",
        "YSV1",
        "VICTORY",
        "CECOTEC", // Cecotec DrumFit Indoor 10000 MagnoMotor Connected #2420
        "WATTBIKE",
        "ZYCLEZBIKE",
        "WAVEFIT-",
        "KETTLERBLE",
        "JAS_C3",
        "SCH_190U",
        "RAVE WHITE",
        "DOMYOS-BIKING-",
        "DU30-",
        "BIKZU_",
        "WLT8828",
        "VANRYSEL-HT",
        "HARISON-X15",
        "FEIVON V2",
        "FELVON V2",
        "ZUMO",
        "JUSTO",
        "T2 ",
        "VFSPINBIKE",
        "XS08-",
        "B94",
        "STAGES BIKE",
        "SUITO",
        "D2RIDE",
        "DIRETO X",
        "MERACH-667-",
        "MRK-S26S-",
        "MRK-S26C-",
        "MRK-S28-",
        "MRK-S38-",
        "SMB1",
        "UBIKE FTMS",
        "INRIDE"
    };
    RegisterNewDeviceTestData(DeviceIndex::FTMSBike)
        ->expectDevice<ftmsbike>()
        ->acceptDeviceNames(acceptableFTMSNames, DeviceNameComparison::StartsWithIgnoreCase)
        ->acceptDeviceName("DI", DeviceNameComparison::StartsWithIgnoreCase, 2) // Elite smart trainer #1682)
        ->acceptDeviceName("YSV", DeviceNameComparison::StartsWithIgnoreCase, 9) // YSV100783
        ->acceptDeviceName("URSB", DeviceNameComparison::StartsWithIgnoreCase, 7) // URSB005
        ->acceptDeviceName("DBF", DeviceNameComparison::StartsWithIgnoreCase, 6) // DBF135
        ->acceptDeviceName("KSU", DeviceNameComparison::StartsWithIgnoreCase, 7) // KSU1102
        ->acceptDeviceName("VOLT", DeviceNameComparison::StartsWithIgnoreCase, 4)
        ->acceptDeviceName("XQ0201118141", DeviceNameComparison::IgnoreCase)
        ->acceptDeviceName("F","ARROW",DeviceNameComparison::IgnoreCase) // FI9110 Arrow, https://www.fitnessdigital.it/bicicletta-smart-bike-ion-fitness-arrow-connect/p/10022863/ IO Fitness Arrow
        ->acceptDeviceName("ICSE", DeviceNameComparison::StartsWithIgnoreCase, 4)
        ->acceptDeviceName("FLX", DeviceNameComparison::StartsWithIgnoreCase, 10)
        ->acceptDeviceName("CSRB", DeviceNameComparison::StartsWithIgnoreCase, 11)


        // Starts with DT- and is 14+ characters long.
        // TODO: update once addDeviceName can generate valid and invalid names for variable length patterns

        ->acceptDeviceName("DT-0123456789A", DeviceNameComparison::IgnoreCase) // Sole SB700
        ->acceptDeviceName("DT-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", DeviceNameComparison::IgnoreCase) // Sole SB700
        ->rejectDeviceName("DT-0123456789", DeviceNameComparison::IgnoreCase) // too short for Sole SB700
        ->rejectDeviceName("DBF13", DeviceNameComparison::IgnoreCase) // too short for DBF135
        ->rejectDeviceName("DBF1355", DeviceNameComparison::IgnoreCase) // too long for DBF135
        ->rejectDeviceName("XQ020111814", DeviceNameComparison::IgnoreCase)
        ->rejectDeviceName("XQ02ABC18141", DeviceNameComparison::IgnoreCase)
        ->rejectDeviceName("XQ02011181411", DeviceNameComparison::IgnoreCase)

        ->excluding(ftmsBikeConfigureExclusions);

    // FTMS Accessory
    QString ftmsAccessoryName = "accessory";
    RegisterNewDeviceTestData(DeviceIndex::FTMSAccessory)
        ->expectDevice<ftmsbike>()        
        ->acceptDeviceName(ftmsAccessoryName, DeviceNameComparison::StartsWithIgnoreCase)
        ->configureSettingsWith(
            [ftmsAccessoryName](DeviceDiscoveryInfo& info, bool enable)->void
            {
                info.setValue(QZSettings::ss2k_peloton, enable);
                info.setValue(QZSettings::ftms_accessory_name, enable ? ftmsAccessoryName : "NOT " + ftmsAccessoryName );
            })
        ->excluding(ftmsBikeConfigureExclusions);


    // FTMS "BIKE-"
    RegisterNewDeviceTestData(DeviceIndex::FTMSBike3)
        ->expectDevice<ftmsbike>()
        ->acceptDeviceName("BIKE-", DeviceNameComparison::StartsWithIgnoreCase)
        ->excluding(ftmsBikeConfigureExclusions)
        ->configureSettingsWith([](const DeviceDiscoveryInfo& info, bool enable, std::vector<DeviceDiscoveryInfo>& configurations) -> void {
            if(!enable)
                return;

            DeviceDiscoveryInfo config(info);

            // distinguish from npecablebike
            config.setValue(QZSettings::flywheel_life_fitness_ic8, true);
            configurations.push_back(config);
        });

    // FTMS Bike 2
    RegisterNewDeviceTestData(DeviceIndex::FTMSBike2)
        ->expectDevice<ftmsbike>()
        ->acceptDeviceNames({"GLT",
                             "SPORT01-"}, // Labgrey Magnetic Exercise Bike https://www.amazon.co.uk/dp/B0CXMF1NPY?_encoding=UTF8&psc=1&ref=cm_sw_r_cp_ud_dp_PE420HA7RD7WJBZPN075&ref_=cm_sw_r_cp_ud_dp_PE420HA7RD7WJBZPN075&social_share=cm_sw_r_cp_ud_dp_PE420HA7RD7WJBZPN075&skipTwisterOG=1,
                            DeviceNameComparison::StartsWithIgnoreCase)
        ->excluding(ftmsBikeConfigureExclusions)
        ->configureSettingsWith(QBluetoothUuid((quint16)0x1826));

    // Power (Stages) Bike
    QString powerSensorName = "WattsItCalled";
    RegisterNewDeviceTestData(DeviceIndex::StagesPowerBike)
        ->expectDevice<stagesbike>()
        ->acceptDeviceName(powerSensorName+"Suffix", DeviceNameComparison::Exact) // needs a non-trivial name, but could be anything
        ->configureSettingsWith([powerSensorName](const DeviceDiscoveryInfo& info, bool enable, std::vector<DeviceDiscoveryInfo>& configurations) -> void {
            DeviceDiscoveryInfo config(info);

            if(enable) {
                config.setValue(QZSettings::power_sensor_as_bike, true);
                config.setValue(QZSettings::power_sensor_name, powerSensorName);
                configurations.push_back(config);
            } else {
                // enabled but wrong name
                config.setValue(QZSettings::power_sensor_as_bike, true);
                config.setValue(QZSettings::power_sensor_name, "NOT "+ powerSensorName);
                configurations.push_back(config);

                // disabled but acceptable name
                config.setValue(QZSettings::power_sensor_as_bike, false);
                config.setValue(QZSettings::power_sensor_name, powerSensorName);
                configurations.push_back(config);

                // disabled and wrong name
                config.setValue(QZSettings::power_sensor_as_bike, false);
                config.setValue(QZSettings::power_sensor_name, "NOT "+powerSensorName);
                configurations.push_back(config);
            }
        });

    // StrydeRun Power Sensor
    RegisterNewDeviceTestData(DeviceIndex::StrydeRunTreadmill_PowerSensor)
        ->expectDevice<strydrunpowersensor>()        
        ->acceptDeviceName("", DeviceNameComparison::StartsWith,1) // accept any name
        ->configureSettingsWith(
            [](const DeviceDiscoveryInfo &info, bool enable, std::vector<DeviceDiscoveryInfo> &configurations) -> void
            {
                DeviceDiscoveryInfo config(info);
                QString name = config.DeviceInfo()->name();
                if(enable) {
                    // power_as_treadmill enabled and powerSensorName in settings matches device name
                    config.setValue(QZSettings::power_sensor_as_treadmill, true);
                    config.setValue(QZSettings::power_sensor_name, name);
                    configurations.push_back(config);
                } else {
                    // enabled but powerSensorName in settings does not match device name
                    config.setValue(QZSettings::power_sensor_as_treadmill, true);
                    config.setValue(QZSettings::power_sensor_name, "NOT " + name);
                    configurations.push_back(config);

                    // disabled with non-matching name
                    config.setValue(QZSettings::power_sensor_as_treadmill, false);
                    config.setValue(QZSettings::power_sensor_name, "NOT " + name);
                    configurations.push_back(config);

                    // disabled with matching name
                    config.setValue(QZSettings::power_sensor_as_treadmill, false);
                    config.setValue(QZSettings::power_sensor_name, name);
                    configurations.push_back(config);
                }
            });

    RegisterNewDeviceTestData(DeviceIndex::StrydeRunTreadmill_PowerSensor2)
        ->expectDevice<strydrunpowersensor>()
        ->acceptDeviceNames({"TREADMILL", "S10"}, DeviceNameComparison::StartsWithIgnoreCase)
        ->configureSettingsWith(QBluetoothUuid((quint16)0x1814));

    auto trxAppGateUSBEllipticalSettingsApplicator =
        [](const DeviceDiscoveryInfo &info, bool enable, std::vector<DeviceDiscoveryInfo> &configurations) -> void
    {
        DeviceDiscoveryInfo config(info);
        if(enable) {
            config.setValue(QZSettings::ftms_bike, QZSettings::default_ftms_bike);
            configurations.push_back(config);
            config.setValue(QZSettings::ftms_bike, "X"+QZSettings::default_ftms_bike+"X");
            configurations.push_back(config);
        } else {
            config.setValue(QZSettings::ftms_bike, "PLACEHOLDER");
            configurations.push_back(config);
        }
    };


    // TODO: revisit
    // Zwift Runpod
    QString zwiftRunPodPowerSensorName = "WattsItCalled";
    RegisterNewDeviceTestData(DeviceIndex::ZwiftRunpod)
        ->expectDevice<strydrunpowersensor>()        
        ->acceptDeviceName("ZWIFT RUNPOD", DeviceNameComparison::StartsWithIgnoreCase)
        ->configureSettingsWith(
            [zwiftRunPodPowerSensorName](const DeviceDiscoveryInfo &info, bool enable, std::vector<DeviceDiscoveryInfo> &configurations) -> void
            {
                DeviceDiscoveryInfo config(info);

                if(enable) {
                    /* Avoid the config that enables the StrydeRunPowerSensorTestData device
                    // power_as_treadmill enabled and powerSensorName in settings matches device name
                    config.setValue(QZSettings::power_as_treadmill, true);
                    config.setValue(QZSettings::powerSensorName, powerSensorName);
                    configurations.push_back(config);
                    */

                    /*
                     * In order for the search to occur, the power sensor name must start with "Disabled", or
                     * power_as_bike or power_as_treadmill must be true.
                    */

                    config.setValue(QZSettings::power_sensor_as_treadmill, true);
                    config.setValue(QZSettings::power_sensor_name, "NOT " + zwiftRunPodPowerSensorName);
                    configurations.push_back(config);

                    config.setValue(QZSettings::power_sensor_as_treadmill, false);
                    config.setValue(QZSettings::power_sensor_name, "Disabled");
                    configurations.push_back(config);

                } else {
                    // disable the search
                    config.setValue(QZSettings::power_sensor_as_treadmill, false);
                    config.setValue(QZSettings::power_sensor_name, zwiftRunPodPowerSensorName);
                    config.setValue(QZSettings::power_sensor_as_bike, false);
                    configurations.push_back(config);
                }
            });


    isInitialized = true;

    // Debug log the type ids
    for(auto deviceTestData : testData) {
        qDebug() << "Device: " << deviceTestData->Name() << " expected device type id: " << deviceTestData->ExpectedDeviceType();
    }

    // Validate the test data
    for(auto deviceTestData : testData) {

        try {
            auto exclusions = deviceTestData->Exclusions();
        } catch(std::domain_error) {
            qDebug() << "Device: " << deviceTestData->Name() << " specifies at least 1 exclusion for which no test data was found.";
        }

    }
}
