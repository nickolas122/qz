include(qdomyos-zwift.pri)

# MSVC writes <target>.lib beside any binary that exports a symbol, and this app
# exports several (SMTP_BUILD, qmdnsengine). That name and directory are exactly
# what qdomyos-zwift-lib.pro produces, so linking the app silently overwrote the
# static library with an import library, and the tests - built after both - linked
# against the wrong file and failed on 142 unresolved externals. Sending the import
# library somewhere harmless keeps the two apart; nothing consumes it. mingw does
# not emit one at all, which is why only the Qt 6/MSVC job ever saw this.
win32-msvc: QMAKE_LFLAGS += /IMPLIB:$$OUT_PWD/qdomyos-zwift-app.lib

QMAKE_IOS_DEPLOYMENT_TARGET = 12.0
QMAKE_DEVELOPMENT_TEAM = 6335M7T29D
QMAKE_CODE_SIGN_IDENTITY = "iPhone Developer"
QMAKE_CODE_SIGN_STYLE = Automatic

# What Windows reads off the .exe: the icon Explorer and the taskbar draw, and the
# strings under Properties > Details. qmake already generated the version resource
# from VERSION - that is the gitignored src/qdomyos-zwift_resource.rc, build output
# and not a file to edit - but with no RC_ICONS it named no icon, so the binary
# carried none and Windows drew the blank default. Setting these three strings beside
# it is also the only place the product name reaches a rider who never opens the app.
# Only the executable gets this: the static library shares the .pri, and an icon in a
# .a is nothing. The filename stays qdomyos-zwift.exe, which CI, the build script and
# the docs all reach for by name; the identity does not live in the filename.
win32 {
    RC_ICONS = $$PWD/icons/qz-lite.ico
    QMAKE_TARGET_PRODUCT = QZ-lite
    QMAKE_TARGET_DESCRIPTION = QZ-lite - bridge between the trainer and the training app
    QMAKE_TARGET_COMPANY = QZ-lite (a fork of QZ by Roberto Viola)
}
