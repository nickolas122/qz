GOOGLETEST_DIR = $$PWD/googletest

include(../defaults.pri)
include(gtest_dependency.pri)

TEMPLATE = app

CONFIG += console c++11
CONFIG -= app_bundle
CONFIG += thread
CONFIG += androidextras

# The library these tests link forces _ITERATOR_DEBUG_LEVEL=0 on Qt 5
# (src/qdomyos-zwift.pri), and MSVC refuses to link objects that disagree about it -
# a debug build defaults to 2, so gtest-all.obj and every object in
# qdomyos-zwift.lib would come out on opposite sides. Harmless under mingw, where
# the macro means nothing.
#
# Qt 6 does not force it, for the reason spelled out in ../defaults.pri, so this
# must not either: the two halves of the link have to keep agreeing.
win32:lessThan(QT_MAJOR_VERSION, 6): DEFINES += _ITERATOR_DEBUG_LEVEL=0

SOURCES += \
        Devices/bluetoothdevicetestdata.cpp \
        Devices/bluetoothdevicetestdatabuilder.cpp \
        Devices/bluetoothdevicetestsuite.cpp \
        Devices/bluetoothsignalreceiver.cpp \
        Devices/devicediscoveryinfo.cpp \
        Devices/deviceindex.cpp \
        Devices/devicenamepatterngroup.cpp \
        Devices/devicetestdataindex.cpp \
        Erg/ergtabletestsuite.cpp \
        GarminConnect/garminconnecttestsuite.cpp \
        TrainingProgram/trainprogramtestsuite.cpp \
        ToolTests/qfittestsuite.cpp \
        ToolTests/testsettingstestsuite.cpp \
        ToolTests/testtrainingloadtestsuite.cpp \
        ToolTests/zwiftworkouttestsuite.cpp \
        Tools/testsettings.cpp \
        Tools/typeidgenerator.cpp \
        Devices/TestZwiftRideController.cpp \
        Devices/TestResistanceSlewLimiter.cpp \
        Devices/TestFtmsControlPointHandshake.cpp \
        Devices/TestServiceSubscriptionPlan.cpp \
        Erg/TestErgTableSelection.cpp \
        Erg/TestErgAutoMode.cpp \
        main.cpp

# Avoid the "File too big" error building in Windows. This has happened when a template class is used with Google Test / typed tests
# to produce a large number of classes.
#
# Both spellings are needed: -Wa,-mbig-obj is passed to GNU as and cl rejects it
# outright, /bigobj is the MSVC equivalent. This used to be a bare win32: scope,
# which handed the GCC flag to cl and stopped the build at moc_predefs.h.
mingw: QMAKE_CXXFLAGS += -Wa,-mbig-obj
msvc: QMAKE_CXXFLAGS += /bigobj

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../src/release/ -lqdomyos-zwift
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../src/debug/ -lqdomyos-zwift
else:unix: LIBS += -L$$OUT_PWD/../src/ -lqdomyos-zwift

# The static library the tests link carries webserverinfosender.o and
# windowsblebond.o, so the test binary needs the same two dependencies the app
# has: Qt's HttpServer module, and bthprops for BluetoothRemoveDevice(). Without
# them the link fails on Windows with undefined references to QHttpServer::* and
# BluetoothRemoveDevice. It goes unnoticed in CI because the only job that builds
# the tests is linux-x86-build, where neither applies.
qtHaveModule(httpserver): QT += httpserver
win32:LIBS += -lbthprops
# Under msvc the library also carries zwift_messages.pb.obj and trainprogram's use
# of it, so the test binary needs the same protobuf set the app links. On Qt 6 that
# set comes from ../defaults.pri, which both projects include precisely so this list
# cannot drift from the app's again. The -L comes from the build's vcpkg path.
win32:!mingw:lessThan(QT_MAJOR_VERSION, 6): LIBS += -llibprotobuf -llibprotoc -labseil_dll -llibprotobuf-lite -ldbghelp

INCLUDEPATH += $$PWD/../src $$PWD/../src/devices $$PWD/../src/fit-sdk
DEPENDPATH += $$PWD/../src $$PWD/../src/devices

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../src/release/libqdomyos-zwift.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../src/debug/libqdomyos-zwift.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../src/release/qdomyos-zwift.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../src/debug/qdomyos-zwift.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../src/libqdomyos-zwift.a

HEADERS += \
    Devices/bluetoothdevicetestdata.h \
    Devices/bluetoothdevicetestdatabuilder.h \
    Devices/bluetoothdevicetestsuite.h \
    Devices/bluetoothsignalreceiver.h \
    Devices/devicediscoveryinfo.h \
    Devices/deviceindex.h \
    Devices/devicenamepatterngroup.h \
    Devices/devicetestdataindex.h \
    Devices/TestResistanceSlewLimiter.h \
    Devices/TestFtmsControlPointHandshake.h \
    Devices/TestServiceSubscriptionPlan.h \
    Erg/ergtabletestsuite.h \
    Erg/TestErgTableSelection.h \
    Erg/TestErgAutoMode.h \
    GarminConnect/garminconnecttestsuite.h \
    TrainingProgram/trainprogramtestsuite.h \
    ToolTests/qfittestsuite.h \
    ToolTests/testsettingstestsuite.h \
    ToolTests/testtrainingloadtestsuite.h \
    ToolTests/zwiftworkouttestsuite.h \
    Tools/devicetypeid.h \
    Tools/testsettings.h \
    Tools/typeidgenerator.h
