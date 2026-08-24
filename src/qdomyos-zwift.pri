include(../defaults.pri)
QT += bluetooth widgets positioning quick websockets texttospeech location multimedia
QTPLUGIN += qavfmediaplayer
QT+= charts core-private sql concurrent

qtHaveModule(httpserver) {
    QT += httpserver
    DEFINES += Q_HTTPSERVER
    SOURCES += webserverinfosender.cpp
    HEADERS += webserverinfosender.h

    # android and iOS are using ChartJS
    unix:android: {
        QT+= webview
        DEFINES += CHARTJS
    }
    ios: {
        QT+= webview
        DEFINES += CHARTJS
    }
#	 win32: {
#	     DEFINES += CHARTJS
#		}
}

CONFIG += c++17 console app_bundle optimize_full ltcg

CONFIG += qmltypes

#win32: CONFIG += webengine
#unix:!android: CONFIG += webengine

# Qt 5 only, and deliberately so - see the long note in ../defaults.pri, which
# carries the Qt 6 half. The msvc2019 job copies vcpkg's release libraries into src/
# and builds debug, which only links because the level is forced to match; on Qt 5
# that is safe, because QByteArray::toStdString() is inline there.
# The define keeps its original win32 scope, mingw included, where the macro is
# inert - matching tst/qdomyos-zwift-tests.pro, so the two halves of the test link
# cannot end up on opposite sides. Only the version gate is new.
win32:lessThan(QT_MAJOR_VERSION, 6): DEFINES += _ITERATOR_DEBUG_LEVEL=0
win32:!mingw:lessThan(QT_MAJOR_VERSION, 6): LIBS += -llibprotobuf -llibprotoc -labseil_dll -llibprotobuf-lite -ldbghelp -L$$PWD
# BluetoothRemoveDevice(), for dropping a lapsed pairing - see windowsblebond.cpp.
win32:LIBS += -lbthprops

QML_IMPORT_NAME = org.cagnulein.qdomyoszwift
QML_IMPORT_MAJOR_VERSION = 1
# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Additional import path used to resolve QML modules just for Qt Quick Designer
QML_DESIGNER_IMPORT_PATH =

# Every flag here is GCC's, and win32 is not only mingw: on the MSVC build link.exe
# is handed -static-libstdc++ and -l..., which it cannot parse. The bundled OpenSSL
# in windows_openssl/ is a mingw import-library pair for the same reason. Qt's own
# TLS backend loads OpenSSL at runtime, so an MSVC build needs neither.
mingw {
    QMAKE_LFLAGS_DEBUG += -static-libstdc++ -static-libgcc -llibcrypto-1_1-x64 -llibssl-1_1-x64 -L$$PWD/../windows_openssl
    QMAKE_LFLAGS_RELEASE += -static-libstdc++ -static-libgcc -llibcrypto-1_1-x64 -llibssl-1_1-x64 -L$$PWD/../windows_openssl
}

# -s (strip) and -fno-sized-deallocation are likewise GCC/clang spellings.
gcc {
    QMAKE_LFLAGS_RELEASE += -s
    QMAKE_CXXFLAGS += -fno-sized-deallocation
}
mingw: QMAKE_CXXFLAGS += -Wa,-mbig-obj
msvc {
   win32:QMAKE_CXXFLAGS_DEBUG += /RTC1
}
unix:android: {
    CONFIG -= optimize_size
    QMAKE_CFLAGS_OPTIMIZE_FULL -= -Oz
    QMAKE_CFLAGS_OPTIMIZE_FULL += -O3
}
macx: CONFIG += debug
win32: CONFIG += debug
macx: CONFIG += static
macx {
    QMAKE_INFO_PLIST = macx/Info.plist
}
INCLUDEPATH += qmdnsengine/src/include

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS IO_UNDER_QT SMTP_BUILD NOMINMAX

# Stamp the commit into the binary so a running build can identify itself in its
# own log. Answering "am I running the new binary?" by grepping ASCII out of the
# .exe cost more than one debugging round. -C $$PWD because shadow builds run
# qmake from outside the work tree, and CI checks out detached HEAD, which
# rev-parse resolves correctly anyway.
win32 {
    QZ_GIT_SHA = $$system(git -C \"$$PWD\" rev-parse --short HEAD 2>NUL)
} else {
    QZ_GIT_SHA = $$system(git -C \"$$PWD\" rev-parse --short HEAD 2>/dev/null)
}
isEmpty(QZ_GIT_SHA): QZ_GIT_SHA = unknown
DEFINES += QZ_GIT_SHA=\\\"$$QZ_GIT_SHA\\\"


# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0


# include(../qtzeroconf/qtzeroconf.pri)

SOURCES += \
    $$PWD/characteristics/characteristicnotifier0002.cpp \
    $$PWD/characteristics/characteristicnotifier0004.cpp \
    $$PWD/characteristics/characteristicwriteprocessor0003.cpp \
    $$PWD/androidqlog.cpp \
    $$PWD/devices/coresensor/coresensor.cpp \
    $$PWD/devices/elitesquarecontroller/elitesquarecontroller.cpp \
    $$PWD/devices/cycplusbc2controller/cycplusbc2controller.cpp \
    $$PWD/devices/sramAXSController/sramAXSController.cpp \
    $$PWD/devices/thinkridercontroller/thinkridercontroller.cpp \
    $$PWD/logwriter.cpp \
    $$PWD/filesearcher.cpp \
devices/eliteariafan/eliteariafan.cpp \
virtualdevices/virtualdevice.cpp \
androidactivityresultreceiver.cpp \
androidadblog.cpp \
handleurl.cpp \
localipaddress.cpp \
windowsblebond.cpp \
gamepadcontroller.cpp \
rtssosd.cpp \
devices/wahookickrheadwind/wahookickrheadwind.cpp \
zwift_play/zwiftclickremote.cpp \
characteristics/characteristicnotifier2a53.cpp \
characteristics/characteristicnotifier2a5b.cpp \
characteristics/characteristicnotifier2acc.cpp \
characteristics/characteristicnotifier2acd.cpp \
characteristics/characteristicnotifier2ad9.cpp \
characteristics/characteristicwriteprocessor.cpp \
characteristics/characteristicwriteprocessore005.cpp \
qmdnsengine/src/src/abstractserver.cpp \
qmdnsengine/src/src/bitmap.cpp \
qmdnsengine/src/src/browser.cpp \
qmdnsengine/src/src/cache.cpp \
qmdnsengine/src/src/dns.cpp \
qmdnsengine/src/src/hostname.cpp \
qmdnsengine/src/src/mdns.cpp \
qmdnsengine/src/src/message.cpp \
qmdnsengine/src/src/prober.cpp \
qmdnsengine/src/src/provider.cpp \
qmdnsengine/src/src/query.cpp \
qmdnsengine/src/src/record.cpp \
qmdnsengine/src/src/resolver.cpp \
qmdnsengine/src/src/server.cpp \
qmdnsengine/src/src/service.cpp \
devices/bike.cpp \
devices/bluetooth.cpp \
devices/bluetoothdevice.cpp \
characteristics/characteristicnotifier2a37.cpp \
characteristics/characteristicnotifier2a63.cpp \
characteristics/characteristicnotifier2ad2.cpp \
characteristics/characteristicwriteprocessor2ad9.cpp \
devices/cscbike/cscbike.cpp \
devices/dircon/dirconmanager.cpp \
devices/dircon/dirconpacket.cpp \
devices/dircon/dirconprocessor.cpp \
devices/eliterizer/eliterizer.cpp \
devices/elitesterzosmart/elitesterzosmart.cpp \
filedownloader.cpp \
devices/fitmetria_fanfit/fitmetria_fanfit.cpp \
devices/ftmsbike/ftmsbike.cpp \
devices/heartratebelt/heartratebelt.cpp \
mywhooshlink.cpp \
keepawakehelper.cpp \
main.cpp \
metric.cpp \
qznotify.cpp \
qzpaths.cpp \
qzsettings.cpp \
screencapture.cpp \
sessionline.cpp \
signalhandler.cpp \
simplecrypt.cpp \
devices/simulatedbike/ridescenario.cpp \
devices/simulatedbike/simulatedbike.cpp \
devices/stagesbike/stagesbike.cpp \
templateinfosender.cpp \
templateinfosenderbuilder.cpp \
virtualdevices/virtualbike.cpp \
scanrecordresult.cpp \
ui/ridestate.cpp
   
macx: SOURCES += macos/lockscreen.mm

#zwift api
msvc {
    SOURCES += zwift-api/zwift_messages.pb.cc
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

INCLUDEPATH += devices/

HEADERS += \
    $$PWD/EventHandler.h \
    $$PWD/characteristics/characteristicnotifier0002.h \
    $$PWD/characteristics/characteristicnotifier0004.h \
    $$PWD/characteristics/characteristicwriteprocessor0003.h \
    $$PWD/devices/coresensor/coresensor.h \
    $$PWD/devices/elitesquarecontroller/elitesquarecontroller.h \
    $$PWD/devices/cycplusbc2controller/cycplusbc2controller.h \
    $$PWD/devices/sramAXSController/sramAXSController.h \
    $$PWD/devices/thinkridercontroller/thinkridercontroller.h \
    $$PWD/ergtable.h \
    $$PWD/inclinationresistancetable.h \
    $$PWD/logwriter.h \
    $$PWD/filesearcher.h \
    $$PWD/wheelcircumference.h \
devices/eliteariafan/eliteariafan.h \
zwift-api/PlayerStateWrapper.h \
zwift-api/zwift_client_auth.h \
zwift_play/abstractZapDevice.h \
zwift_play/zapBleUuids.h \
zwift_play/zapConstants.h \
zwift_play/zwiftPlayDevice.h \
zwift_play/zwiftclickremote.h \
virtualdevices/virtualdevice.h \
androidactivityresultreceiver.h \
androidadblog.h \
devices/discoveryoptions.h \
handleurl.h \
localipaddress.h \
windowsblebond.h \
gamepadcontroller.h \
rtssosd.h \
devices/wahookickrheadwind/wahookickrheadwind.h \
characteristics/characteristicnotifier2a53.h \
characteristics/characteristicnotifier2a5b.h \
characteristics/characteristicnotifier2acc.h \
characteristics/characteristicnotifier2acd.h \
characteristics/characteristicnotifier2ad9.h \
characteristics/characteristicwriteprocessore005.h \
definitions.h \
qmdnsengine/src/include/qmdnsengine/abstractserver.h \
qmdnsengine/src/include/qmdnsengine/bitmap.h \
qmdnsengine/src/include/qmdnsengine/browser.h \
qmdnsengine/src/include/qmdnsengine/cache.h \
qmdnsengine/src/include/qmdnsengine/dns.h \
qmdnsengine/src/include/qmdnsengine/hostname.h \
qmdnsengine/src/include/qmdnsengine/mdns.h \
qmdnsengine/src/include/qmdnsengine/message.h \
qmdnsengine/src/include/qmdnsengine/prober.h \
qmdnsengine/src/include/qmdnsengine/provider.h \
qmdnsengine/src/include/qmdnsengine/query.h \
qmdnsengine/src/include/qmdnsengine/record.h \
qmdnsengine/src/include/qmdnsengine/resolver.h \
qmdnsengine/src/include/qmdnsengine/server.h \
qmdnsengine/src/include/qmdnsengine/service.h \
qmdnsengine/src/src/bitmap_p.h \
qmdnsengine/src/src/browser_p.h \
qmdnsengine/src/src/cache_p.h \
qmdnsengine/src/src/hostname_p.h \
qmdnsengine/src/src/message_p.h \
qmdnsengine/src/src/prober_p.h \
qmdnsengine/src/src/provider_p.h \
qmdnsengine/src/src/query_p.h \
qmdnsengine/src/src/record_p.h \
qmdnsengine/src/src/resolver_p.h \
qmdnsengine/src/src/server_p.h \
qmdnsengine/src/src/service_p.h \
devices/bike.h \
devices/bluetooth.h \
devices/bluetoothdevice.h \
characteristics/characteristicnotifier.h \
characteristics/characteristicnotifier2a37.h \
characteristics/characteristicnotifier2a63.h \
characteristics/characteristicnotifier2ad2.h \
characteristics/characteristicwriteprocessor.h \
characteristics/characteristicwriteprocessor2ad9.h \
devices/cscbike/cscbike.h \
devices/dircon/dirconmanager.h \
devices/dircon/dirconpacket.h \
devices/dircon/dirconprocessor.h \
devices/eliterizer/eliterizer.h \
devices/elitesterzosmart/elitesterzosmart.h \
filedownloader.h \
devices/fitmetria_fanfit/fitmetria_fanfit.h \
devices/ftmsbike/ftmsbike.h \
devices/ftmsbike/ftmscontrolpointhandshake.h \
devices/ftmsbike/resistanceslewlimiter.h \
devices/ftmsbike/speedracex_defaults.h \
devices/heartratebelt/heartratebelt.h \
mywhooshlink.h \
ios/lockscreen.h \
keepawakehelper.h \
macos/lockscreen.h \
ios/M3iIOS-Interface.h \
material.h \
metric.h \
qdebugfixup.h \
qmdnsengine_export.h \
qznotify.h \
qzpaths.h \
qzsettings.h \
qzforkversion.h \
screencapture.h \
sessionline.h \
signalhandler.h \
simplecrypt.h \
devices/simulatedbike/ridescenario.h \
devices/simulatedbike/simulatedbike.h \
devices/stagesbike/stagesbike.h \
templateinfosender.h \
templateinfosenderbuilder.h \
virtualdevices/virtualbike.h \
scanrecordresult.h \
ui/ridestate.h


exists(secret.h): HEADERS += secret.h


# Translation files - 30 most used languages worldwide
CONFIG += lrelease
LRELEASE_DIR = $$PWD/translations

TRANSLATIONS += \
    $$PWD/translations/qdomyos-zwift_it.ts \
    $$PWD/translations/qdomyos-zwift_de.ts \
    $$PWD/translations/qdomyos-zwift_fr.ts \
    $$PWD/translations/qdomyos-zwift_es.ts \
    $$PWD/translations/qdomyos-zwift_pt.ts \
    $$PWD/translations/qdomyos-zwift_pt_BR.ts \
    $$PWD/translations/qdomyos-zwift_ru.ts \
    $$PWD/translations/qdomyos-zwift_zh_CN.ts \
    $$PWD/translations/qdomyos-zwift_zh_TW.ts \
    $$PWD/translations/qdomyos-zwift_ja.ts \
    $$PWD/translations/qdomyos-zwift_ko.ts \
    $$PWD/translations/qdomyos-zwift_ar.ts \
    $$PWD/translations/qdomyos-zwift_hi.ts \
    $$PWD/translations/qdomyos-zwift_tr.ts \
    $$PWD/translations/qdomyos-zwift_vi.ts \
    $$PWD/translations/qdomyos-zwift_pl.ts \
    $$PWD/translations/qdomyos-zwift_uk.ts \
    $$PWD/translations/qdomyos-zwift_nl.ts \
    $$PWD/translations/qdomyos-zwift_th.ts \
    $$PWD/translations/qdomyos-zwift_id.ts \
    $$PWD/translations/qdomyos-zwift_ro.ts \
    $$PWD/translations/qdomyos-zwift_cs.ts \
    $$PWD/translations/qdomyos-zwift_el.ts \
    $$PWD/translations/qdomyos-zwift_sv.ts \
    $$PWD/translations/qdomyos-zwift_hu.ts \
    $$PWD/translations/qdomyos-zwift_fi.ts \
    $$PWD/translations/qdomyos-zwift_no.ts \
    $$PWD/translations/qdomyos-zwift_da.ts \
    $$PWD/translations/qdomyos-zwift_he.ts \
    $$PWD/translations/qdomyos-zwift_ca.ts

# Qt compiles .ts to .qm files before building the resource file.
# .qm files are ignored by git and embedded through translations.qrc.

RESOURCES += \
   icons.qrc \
	translations.qrc

# The .qml sources are written for Qt 5, because that is what Android, iOS and
# every other shipping target build with. Qt 5.15 rejects a library import that
# carries no version, and Qt 6 does not offer the versions Qt 5 asks for -
# QtMultimedia 5.15, QtCharts 2.2 and QtQuick.Dialogs 1.0 are gone, and
# QtGraphicalEffects entirely so. QML has no preprocessor, so the Qt 6 variant is
# generated: same resource paths, rewritten imports, Qt 5 untouched.
greaterThan(QT_MAJOR_VERSION, 5) {
    QML6_QRC = $$OUT_PWD/qml6.qrc
    QML6_CMD = python $$shell_quote($$shell_path($$PWD/../tools/qt6-qml-imports.py)) \
        --qrc $$shell_quote($$shell_path($$PWD/qml.qrc)) \
        --out-dir $$shell_quote($$shell_path($$OUT_PWD/qml6)) \
        --out-qrc $$shell_quote($$shell_path($$QML6_QRC))

    # Once now, because qmake has to read the generated .qrc to work out what rcc
    # depends on, and once per build, so editing a .qml is enough on its own.
    # Failing loudly here beats letting qmake carry on and rcc complain about a
    # .qrc that was never written - the usual cause is python not being on PATH.
    !system($$QML6_CMD) {
        error("Could not generate the Qt 6 QML resources. Is python on PATH? Command: $$QML6_CMD")
    }
    RESOURCES += $$QML6_QRC

    qml6imports.target = qml6-imports
    qml6imports.commands = $$QML6_CMD
    QMAKE_EXTRA_TARGETS += qml6imports
    PRE_TARGETDEPS += qml6-imports
} else {
    RESOURCES += qml.qrc
}

DISTFILES += \
    $$PWD/android/libs/android_antlib_4-16-0.aar \
    $$PWD/android/libs/ciq-companion-app-sdk-2.0.3.aar \
    $$PWD/android/libs/zaplibrary-debug.aar \
    $$PWD/android/res/xml/device_filter.xml \
    $$PWD/android/src/BikeChannelController.java \
    $$PWD/android/src/BleAdvertiser.java \
   $$PWD/android/src/CSafeRowerUSBHID.java \
    $$PWD/android/src/ContentHelper.java \
    $$PWD/android/src/CustomQtActivity.java \
    $$PWD/android/src/Garmin.java \
   $$PWD/android/src/HidBridge.java \
    $$PWD/android/src/IQMessageReceiverWrapper.java \
    $$PWD/android/src/LocationHelper.java \
    $$PWD/android/src/MediaButtonReceiver.java \
    $$PWD/android/src/MediaProjection.java \
    $$PWD/android/src/MulticastLockHelper.java \
    $$PWD/android/src/NetworkAddressHelper.java \
    $$PWD/android/src/NotificationUtils.java \
    $$PWD/android/src/QLog.java \
    $$PWD/android/src/ScreenCaptureService.java \
    $$PWD/android/src/Shortcuts.java \
    $$PWD/android/src/WearableController.java \
    $$PWD/android/src/WearableMessageListenerService.java \
    $$PWD/android/src/ZapClickLayer.java \
    $$PWD/android/src/ZwiftAPI.java \
    $$PWD/android/src/ZwiftHubBike.java \
    $$PWD/android/src/main/proto/zwift_hub.proto \
    $$PWD/android/src/main/proto/zwift_messages.proto \
    .clang-format \
   AppxManifest.xml \
   android/AndroidManifest.xml \
	android/build.gradle \
	android/gradle/wrapper/gradle-wrapper.jar \
	android/gradle/wrapper/gradle-wrapper.properties \
	android/gradlew \
	android/gradlew.bat \
	android/res/values/libs.xml \
	android/src/Ant.java \
	android/src/ChannelService.java \
   android/src/ForegroundService.java \
   android/src/NotificationClient.java \
   android/src/QZAdbRemote.java \
        android/src/ScanRecordResult.java \
        android/src/NativeScanCallback.java \
   android/src/HeartChannelController.java \
	android/src/MyActivity.java \
	android/src/PowerChannelController.java \
	android/src/SpeedChannelController.java \
   android/src/SDMChannelController.java \
    android/src/Usbserial.java \
   android/src/com/cgutman/adblib/AdbBase64.java \
   android/src/com/cgutman/adblib/AdbConnection.java \
   android/src/com/cgutman/adblib/AdbCrypto.java \
   android/src/com/cgutman/adblib/AdbProtocol.java \
   android/src/com/cgutman/adblib/AdbStream.java \
   android/src/com/cgutman/adblib/package-info.java \
   android/src/com/cgutman/androidremotedebugger/AdbUtils.java \
   android/src/com/cgutman/androidremotedebugger/adblib/AndroidBase64.java \
   android/src/com/cgutman/androidremotedebugger/console/CommandHistory.java \
   android/src/com/cgutman/androidremotedebugger/console/ConsoleBuffer.java \
   android/src/com/cgutman/androidremotedebugger/devconn/DeviceConnection.java \
   android/src/com/cgutman/androidremotedebugger/devconn/DeviceConnectionListener.java \
   android/src/com/cgutman/androidremotedebugger/service/ShellListener.java \
   android/src/com/cgutman/androidremotedebugger/service/ShellService.java \
   android/src/com/cgutman/androidremotedebugger/ui/Dialog.java \
   android/src/com/cgutman/androidremotedebugger/ui/SpinnerDialog.java \
	android/src/com/dsi/ant/channel/PredefinedNetwork.java \
    android/gradle.properties \
	android/src/org/qtproject/qt/android/purchasing/Security.java \
	android/src/org/qtproject/qt/android/purchasing/InAppPurchase.java \
	android/src/org/qtproject/qt/android/purchasing/Base64.java \
	android/src/org/qtproject/qt/android/purchasing/Base64DecoderException.java \
	ios/AppDelegate.swift \
	ios/BLEPeripheralManager.swift \
	ios/LiveActivityManager.swift

win32: DISTFILES += \
   $$PWD/adb/AdbWinApi.dll \
	$$PWD/adb/AdbWinUsbApi.dll \
	$$PWD/adb/adb.exe \


ios {
    ios_icon.files = $$files($$PWD/icons/ios/*.png)
	 QMAKE_BUNDLE_DATA += ios_icon
}

ios {
    OBJECTIVE_SOURCES += ios/lockscreen.mm \
    ios/ios_eliteariafan.mm \
    ios/ios_app_delegate.mm \
    ios/ios_liveactivity.mm \
         ios/M3iNS.mm \

    SOURCES += ios/M3iNSQT.cpp

    OBJECTIVE_HEADERS += ios/M3iNS.h \
    ios/ios_liveactivity.h

    QMAKE_INFO_PLIST = ios/Info.plist
	 QMAKE_ASSET_CATALOGS = $$PWD/ios/Images.xcassets
	 QMAKE_ASSET_CATALOGS_APP_ICON = "AppIcon"
	 QMAKE_ASSET_CATALOGS_BUILD_PATH = $$PWD/ios/ 

    TARGET = qdomyoszwift
	 QMAKE_TARGET_BUNDLE_PREFIX = org.cagnulein
    
    # iOS Code Signing Configuration - handled manually in Xcode project
    
    DEFINES+=_Nullable_result=_Nullable NS_FORMAT_ARGUMENT\\(A\\)=
}

HEADERS += \
    androidstatusbar.h \
    fontmanager.h

SOURCES += \
    androidstatusbar.cpp \
    fontmanager.cpp

include($$PWD/purchasing/purchasing.pri)
INCLUDEPATH += purchasing/qmltypes
INCLUDEPATH += purchasing/inapp

WINRT_MANIFEST = AppxManifest.xml

VERSION = 2.21.6
