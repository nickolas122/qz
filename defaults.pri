QT += gui bluetooth widgets xml positioning quick networkauth websockets texttospeech location multimedia sql
QTPLUGIN += qavfmediaplayer
QT+= charts

unix:android: QT += androidextras gui-private

android: include(android_openssl/openssl.pri)

INCLUDEPATH += $$PWD/src/qmdnsengine/src/include

ANDROID_PACKAGE_SOURCE_DIR = $$PWD/src/android

ANDROID_ABIS = armeabi-v7a arm64-v8a x86 x86_64

#QMAKE_CXXFLAGS += -Werror=suggest-override

# protobuf and abseil, for zwift_messages.pb.cc, on MSVC + Qt 6. This lives here
# rather than in src/qdomyos-zwift.pri and tst/qdomyos-zwift-tests.pro because both
# link the same objects and both had their own copy of the list. They disagreed,
# and the disagreement was invisible until it corrupted std::string.
#
# The build variant is not a free choice. vcpkg's release libraries are built with
# the release CRT and _ITERATOR_DEBUG_LEVEL 0, its debug ones with the debug CRT and
# level 2, and MSVC stamps both into every object as a detect_mismatch pragma, so a
# debug build linking the release half fails at LNK2038.
#
# The old answer was to force _ITERATOR_DEBUG_LEVEL=0 everywhere and link the release
# half from a debug build. That is safe on Qt 5, where QByteArray::toStdString() is
# inline and no std::string ever crosses the Qt DLL boundary. Qt 6 exports it from
# Qt6Core instead, and then the override is silent memory corruption: our std::string
# is 32 bytes and the debug DLL's is 40, so QString::toStdString() returns an empty
# string - reading _Mysize out of the SSO buffer's zero padding - and the DLL writes
# _Myres 8 bytes past the caller's object. It surfaced as gtest aborting on an empty
# parameterized test name, which is a long way from the cause.
#
# So: match the CRT rather than override it. VCPKG is the installed triplet root,
# passed on the qmake command line; without it the names still resolve from whatever
# -L the caller supplied.
win32:!mingw:greaterThan(QT_MAJOR_VERSION, 5) {
    !isEmpty(VCPKG) {
        INCLUDEPATH += $$VCPKG/include
        CONFIG(debug, debug|release): LIBS += -L$$VCPKG/debug/lib
        else: LIBS += -L$$VCPKG/lib
    }
    # vcpkg suffixes the debug import libraries with d, except abseil_dll.
    CONFIG(debug, debug|release) {
        LIBS += -llibprotobufd -llibprotocd -llibprotobuf-lited -labseil_dll
    } else {
        LIBS += -llibprotobuf -llibprotoc -llibprotobuf-lite -labseil_dll
    }
    LIBS += -ldbghelp
}
