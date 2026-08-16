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