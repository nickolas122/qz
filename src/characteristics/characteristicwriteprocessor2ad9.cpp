#include "characteristicwriteprocessor2ad9.h"
#include "devices/ftmsbike/ftmsbike.h"
#include <QSettings>
#include <QtMath>

CharacteristicWriteProcessor2AD9::CharacteristicWriteProcessor2AD9(double bikeResistanceGain,
                                                                   int8_t bikeResistanceOffset, bluetoothdevice *bike,
                                                                   CharacteristicNotifier2AD9 *notifier,
                                                                   QObject *parent)
    : CharacteristicWriteProcessor(bikeResistanceGain, bikeResistanceOffset, bike, parent), notifier(notifier) {}

int CharacteristicWriteProcessor2AD9::writeProcess(quint16 uuid, const QByteArray &data, QByteArray &reply) {
    if (!Bike) {
        // The DIRCON endpoint outlives the bike, so a client can write while nothing
        // is attached. Refuse rather than dereference a device that is not there.
        qDebug() << "CharacteristicWriteProcessor2AD9: write with no device attached, ignoring";
        return CP_INVALID;
    }
    if (data.size()) {
        BLUETOOTH_TYPE dt = Bike->deviceType();
        if (dt == BIKE) {
            QSettings settings;
            bool force_resistance =
                settings.value(QZSettings::virtualbike_forceresistance, QZSettings::default_virtualbike_forceresistance)
                    .toBool();
            bool erg_mode = settings.value(QZSettings::zwift_erg, QZSettings::default_zwift_erg).toBool();
            char cmd = data.at(0);
            emit ftmsCharacteristicChanged(QLowEnergyCharacteristic(), data);
            if (cmd == FTMS_SET_TARGET_RESISTANCE_LEVEL) {

                // Set Target Resistance
                resistance_t uresistance = data.at(1);
                uresistance = uresistance / 10;
                if (force_resistance && !erg_mode) {
                    Bike->changeResistance(uresistance);
                }
                qDebug() << QStringLiteral("new requested resistance ") + QString::number(uresistance) +
                                QStringLiteral(" enabled ") + QString::number(force_resistance);
                reply.append((quint8)FTMS_RESPONSE_CODE);
                reply.append((quint8)FTMS_SET_TARGET_RESISTANCE_LEVEL);
                reply.append((quint8)FTMS_SUCCESS);
            } else if (cmd == FTMS_SET_INDOOR_BIKE_SIMULATION_PARAMS) // simulation parameter

            {
                qDebug() << QStringLiteral("indoor bike simulation parameters");
                reply.append((quint8)FTMS_RESPONSE_CODE);
                reply.append((quint8)FTMS_SET_INDOOR_BIKE_SIMULATION_PARAMS);
                reply.append((quint8)FTMS_SUCCESS);

                int16_t iresistance = (((uint8_t)data.at(3)) + (data.at(4) << 8));
                uint8_t crr = data.at(5);
                uint8_t cw = data.at(6);
                changeSlope(iresistance, crr, cw);
            } else if (cmd == FTMS_SET_TARGET_POWER) // erg mode

            {
                qDebug() << QStringLiteral("erg mode");
                reply.append((quint8)FTMS_RESPONSE_CODE);
                reply.append((quint8)FTMS_SET_TARGET_POWER);
                reply.append((quint8)FTMS_SUCCESS);

                uint16_t power = (((uint8_t)data.at(1)) + (data.at(2) << 8));
                changePower(power);
            } else if (cmd == FTMS_START_RESUME) {
                qDebug() << QStringLiteral("start simulation!");

                reply.append((quint8)FTMS_RESPONSE_CODE);
                reply.append((quint8)FTMS_START_RESUME);
                reply.append((quint8)FTMS_SUCCESS);
            } else if (cmd == FTMS_STOP_PAUSE) {
                qDebug() << QStringLiteral("stop/pause simulation! ignoring it");

                reply.append((quint8)FTMS_RESPONSE_CODE);
                reply.append((quint8)FTMS_STOP_PAUSE);
                reply.append((quint8)FTMS_SUCCESS);
            } else if (cmd == FTMS_REQUEST_CONTROL) {
                qDebug() << QStringLiteral("control requested");

                reply.append((quint8)FTMS_RESPONSE_CODE);
                reply.append((char)FTMS_REQUEST_CONTROL);
                reply.append((quint8)FTMS_SUCCESS);
            } else {
                qDebug() << QStringLiteral("not supported");

                reply.append((quint8)FTMS_RESPONSE_CODE);
                reply.append((quint8)cmd);
                reply.append((quint8)FTMS_NOT_SUPPORTED);
            }
        }
        if (notifier) {
            notifier->answer = reply;
        }
        return CP_OK;
    } else
        return CP_INVALID;
}
