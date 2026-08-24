#include <stdexcept>

#include "devicediscoveryinfo.h"
#include "qzsettings.h"

static QMap<QString, QVariant> trackedSettings;

void InitializeTrackedSettings()
{
    if(trackedSettings.size())
        return;

    trackedSettings.insert(QZSettings::cadence_sensor_as_bike, QZSettings::default_cadence_sensor_as_bike);
    trackedSettings.insert(QZSettings::cadence_sensor_name, QZSettings::default_cadence_sensor_name);
    trackedSettings.insert(QZSettings::domyosbike_notfmts, QZSettings::default_domyosbike_notfmts);
    trackedSettings.insert(QZSettings::filter_device, QZSettings::default_filter_device);
    trackedSettings.insert(QZSettings::flywheel_life_fitness_ic8, QZSettings::default_flywheel_life_fitness_ic8);
    trackedSettings.insert(QZSettings::ftms_bike, QZSettings::default_ftms_bike);
    trackedSettings.insert(QZSettings::hammer_racer_s, QZSettings::default_hammer_racer_s);
    trackedSettings.insert(QZSettings::power_sensor_name, QZSettings::default_power_sensor_name);
    trackedSettings.insert(QZSettings::saris_trainer, QZSettings::default_saris_trainer);
    trackedSettings.insert(QZSettings::toorx_ftms, QZSettings::default_toorx_ftms);
};

static void AssertKeyIsTracked(const QString& key) {

    if(!trackedSettings.contains(key))
        throw std::invalid_argument("Setting not tracked: "+key.toLatin1());
}

DeviceDiscoveryInfo::DeviceDiscoveryInfo(bool loadDefaults){
    InitializeTrackedSettings();

    if(loadDefaults)
        this->loadDefaultValues();
}

DeviceDiscoveryInfo::DeviceDiscoveryInfo(const QBluetoothDeviceInfo &deviceInfo, bool loadDefaults) : DeviceDiscoveryInfo(loadDefaults) {
    this->bluetoothDeviceInfo = deviceInfo;
}

DeviceDiscoveryInfo::DeviceDiscoveryInfo(const DeviceDiscoveryInfo &other, const QBluetoothDeviceInfo &deviceInfo)
    : DeviceDiscoveryInfo(other) {
    this->bluetoothDeviceInfo = deviceInfo;
}

 QBluetoothDeviceInfo *DeviceDiscoveryInfo::DeviceInfo()  { return &this->bluetoothDeviceInfo; }

 const QString DeviceDiscoveryInfo::DeviceName() const { return this->bluetoothDeviceInfo.name(); }

void DeviceDiscoveryInfo::includeBluetoothService(const QBluetoothUuid &serviceUuid, bool include) {
    if(include)
        this->addBluetoothService(serviceUuid);
    else
        this->removeBluetoothService(serviceUuid);
}

void DeviceDiscoveryInfo::addBluetoothService(const QBluetoothUuid &serviceUuid) {
    auto services = bluetoothDeviceInfo.serviceUuids();

    if(!services.contains(serviceUuid)) {
        services.push_back(serviceUuid);
        bluetoothDeviceInfo.setServiceUuids(services.toVector());
    }
}

void DeviceDiscoveryInfo::removeBluetoothService(const QBluetoothUuid &serviceUuid) {
    auto services = bluetoothDeviceInfo.serviceUuids();
    services.removeAll(serviceUuid);

    bluetoothDeviceInfo.setServiceUuids(services.toVector());
}

void DeviceDiscoveryInfo::setValues(QSettings &settings, bool clear) const {
    if(clear) settings.clear();

    for(const QString& key : trackedSettings.keys()) {
        settings.setValue(key, this->Value(key));
    }

}

void DeviceDiscoveryInfo::getValues(QSettings &settings){

    for(const QString& key : trackedSettings.keys())  {
        this->setValue(key, settings.value(key, trackedSettings[key]));
    }


}

void DeviceDiscoveryInfo::loadDefaultValues() {
    // generate some settings with some random ids
    QSettings settings("21f0b218-895f-11ed-a1eb-0242ac120002", "21f0b218-895f-11ed-a1eb-0242ac120002");

    // clear the settings in case they exist from some other call
    settings.clear();

    this->getValues(settings);

    // clear the settings
    settings.clear();
}

QVariant DeviceDiscoveryInfo::Value(const QString &key, const QVariant &defaultValue) const {
    AssertKeyIsTracked(key);

    if(values.contains(key))
        return values[key];
    return defaultValue;
}

QVariant DeviceDiscoveryInfo::Value(const QString &key) const {
    AssertKeyIsTracked(key);

    return values[key];
}

void DeviceDiscoveryInfo::setValue(const QString &key, const QVariant &value)
{
    AssertKeyIsTracked(key);
    values[key] = value;
}

