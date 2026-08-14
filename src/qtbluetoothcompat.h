#ifndef QTBLUETOOTHCOMPAT_H
#define QTBLUETOOTHCOMPAT_H

#include <QtGlobal>
#include <QtBluetooth/QLowEnergyController>
#include <QtBluetooth/QLowEnergyService>

/*
 * The Windows build is on Qt 6; android-build is still on Qt 5.15.0, and the
 * device layer is shared between them. Almost everything Qt 6 renamed in
 * QtBluetooth can be written in a form both accept - notably the UUID
 * enumerators, because C++11 lets an unscoped enum be qualified by its enum
 * name, so QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration
 * compiles against Qt 5's plain enum and Qt 6's enum class alike.
 *
 * The error signals are the exception. Qt 6 renamed error() to errorOccurred()
 * and removed the old name outright (qlowenergyservice.h:113,
 * qlowenergycontroller.h:113 in 6.8.2), while Qt 5.15.2 has only error(), which
 * additionally needs a static_cast because it is overloaded with the error()
 * getter. There is no spelling that satisfies both, so it is selected here once
 * instead of at each of the fifty-odd connect() sites.
 */

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

#define QZ_LE_SERVICE_ERROR_SIGNAL (&QLowEnergyService::errorOccurred)
#define QZ_LE_CONTROLLER_ERROR_SIGNAL (&QLowEnergyController::errorOccurred)

#else

#define QZ_LE_SERVICE_ERROR_SIGNAL                                                                                     \
    (static_cast<void (QLowEnergyService::*)(QLowEnergyService::ServiceError)>(&QLowEnergyService::error))
#define QZ_LE_CONTROLLER_ERROR_SIGNAL                                                                                  \
    (static_cast<void (QLowEnergyController::*)(QLowEnergyController::Error)>(&QLowEnergyController::error))

#endif

#endif // QTBLUETOOTHCOMPAT_H
