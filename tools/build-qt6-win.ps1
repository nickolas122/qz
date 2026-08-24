<#
.SYNOPSIS
    Build QZ locally with MSVC 2022 + Qt 6.8.2, the same toolchain as CI's
    `window-qt6-build` job.

.DESCRIPTION
    This is the only toolchain whose Bluetooth backend is WinRT rather than Win32,
    which is why it cannot be substituted with the mingw/Qt 5 build when the thing
    under investigation is a connection. See docs/fork/BUILDING-ON-WINDOWS.md.

    The build is a SHADOW build by default. The mingw tree lives in src/debug and an
    in-tree MSVC qmake would overwrite its Makefiles and mix .obj into a directory
    full of .o. Keeping them apart means both toolchains stay usable without a clean.

.PARAMETER BuildDir
    Shadow build directory. Default C:\qz-qt6.

.PARAMETER Vcpkg
    vcpkg installed triplet root. Default C:\qz-deps\vcpkg\installed\x64-windows.

.PARAMETER Qt
    Qt 6 prefix. Default C:\Qt\6.8.2\msvc2022_64.

.PARAMETER Clean
    Delete BuildDir before configuring. Needed after a .pro/.pri change that moves
    files, because qmake's dependency lists go stale rather than wrong.

.PARAMETER Tests
    Run tst\debug\qdomyos-zwift-tests.exe after building.

.PARAMETER Deploy
    Run windeployqt and stage a runnable tree under BuildDir\output.

.PARAMETER DeployTo
    Additionally copy the fresh .exe over an existing install. Only valid while the
    Qt DLL set is unchanged - the build banner QZ logs on launch is the check.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\build-qt6-win.ps1 -Tests

.EXAMPLE
    # iterate: rebuild and drop the exe into the existing deployed tree
    powershell -ExecutionPolicy Bypass -File tools\build-qt6-win.ps1 -DeployTo C:\QZ\lite-version
#>
[CmdletBinding()]
param(
    [string] $BuildDir = "C:\qz-qt6",
    [string] $Vcpkg    = "C:\qz-deps\vcpkg\installed\x64-windows",
    [string] $Qt       = "C:\Qt\6.8.2\msvc2022_64",
    [switch] $Clean,
    [switch] $Tests,
    [switch] $Deploy,
    [string] $DeployTo
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

function Step($msg) { Write-Host "`n=== $msg" -ForegroundColor Cyan }
function Fail($msg) { Write-Host "FAILED: $msg" -ForegroundColor Red; exit 1 }

# --- preconditions, checked up front ------------------------------------------
# Each of these produces a failure a long way from its cause if left to the build:
# a missing Qt surfaces as an unknown qmake, a missing vcpkg as LNK2019 on a
# protobuf symbol several minutes in, and a missing python as an rcc complaint
# about a .qrc that qmake was supposed to have generated.

if (-not (Test-Path "$Qt\bin\qmake.exe")) { Fail "no qmake at $Qt\bin - is Qt 6.8.2 msvc2022_64 installed?" }

$btConfig = "$Qt\include\QtBluetooth\6.8.2\QtBluetooth\private\qtbluetooth-config_p.h"
if (Test-Path $btConfig) {
    if (-not (Select-String -Path $btConfig -Pattern "define QT_FEATURE_winrt_bt 1" -Quiet)) {
        Fail "this Qt has the dummy Bluetooth backend (QT_FEATURE_winrt_bt is not 1). A build from it cannot talk to a bike."
    }
} else {
    Write-Warning "could not verify QT_FEATURE_winrt_bt - $btConfig is missing"
}

if (-not (Test-Path "$Vcpkg\include\google\protobuf")) {
    Fail "no protobuf headers under $Vcpkg - see docs/fork/BUILDING-ON-WINDOWS.md, 'Building locally with MSVC and Qt 6'"
}

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Fail "python is not on PATH. qmake runs tools/qt6-qml-imports.py to generate the Qt 6 QML resources and cannot proceed without it."
}

# --- the MSVC environment ------------------------------------------------------
# vcvars64.bat only exists as a batch script, so the only way to get its variables
# into this session is to run it in cmd and read `set` back out.

$vcvars = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsRoot) { $vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat" }
}
if (-not $vcvars -or -not (Test-Path $vcvars)) {
    $vcvars = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
}
if (-not (Test-Path $vcvars)) { Fail "vcvars64.bat not found - install VS 2022 Build Tools with the Desktop C++ workload" }

Step "MSVC environment from $vcvars"
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item ("env:" + $matches[1]) $matches[2] }
}
if (-not (Get-Command nmake -ErrorAction SilentlyContinue)) { Fail "nmake still not on PATH after vcvars64" }

# Qt ahead of everything, so qmake/lrelease/windeployqt all come from the same prefix.
$env:PATH = "$Qt\bin;" + $env:PATH

# --- configure -----------------------------------------------------------------

if ($Clean -and (Test-Path $BuildDir)) {
    Step "removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Force $BuildDir | Out-Null }

Step "lrelease"
& lrelease "$repo\src\qdomyos-zwift.pri"
if ($LASTEXITCODE -ne 0) { Fail "lrelease: $LASTEXITCODE" }

Set-Location $BuildDir

Step "qmake (shadow build in $BuildDir)"
# VCPKG rather than a raw -L: defaults.pri has to pick the debug or release half to
# match the CRT, and only it knows which side of debug|release this build is on.
# Linking the release half into a debug build is not a link error on Qt 6, it is
# silent std::string corruption - defaults.pri:20-38 has the full account.
& qmake "$repo\qdomyos-zwift.pro" "VCPKG=$Vcpkg"
if ($LASTEXITCODE -ne 0) { Fail "qmake: $LASTEXITCODE" }

# --- build ---------------------------------------------------------------------

Step "nmake debug"
# `debug`, not the default target: qmake writes debug and release makefiles for
# every subproject, and left alone nmake builds src in debug and then tries tst in
# release, against a library that was never built.
& nmake debug
if ($LASTEXITCODE -ne 0) { Fail "nmake: $LASTEXITCODE" }

$exe = "$BuildDir\src\debug\qdomyos-zwift.exe"
if (-not (Test-Path $exe)) { Fail "nmake reported success but $exe does not exist" }
Write-Host "built $exe" -ForegroundColor Green

# --- tests ---------------------------------------------------------------------

if ($Tests) {
    Step "tests"
    # A debug build links vcpkg's debug protobuf and abseil, and those DLLs live in
    # debug\bin. Without this the exe dies before main() with 0xC0000135, which
    # surfaces as an exit code that looks like a test failure.
    $saved = $env:PATH
    $env:PATH = "$Vcpkg\debug\bin;$Vcpkg\bin;" + $env:PATH
    & "$BuildDir\tst\debug\qdomyos-zwift-tests.exe"
    $rc = $LASTEXITCODE
    $env:PATH = $saved
    if ($rc -ne 0) { Fail "tests: $rc" }
}

# --- deploy --------------------------------------------------------------------

if ($Deploy) {
    Step "windeployqt"
    $out = "$BuildDir\src\debug\output"
    if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }
    Copy-Item $exe $out -Force
    Set-Location $out
    & windeployqt --qmldir "$repo" qdomyos-zwift.exe
    if ($LASTEXITCODE -ne 0) { Fail "windeployqt: $LASTEXITCODE" }

    # windeployqt exits 0 when it cannot find a QML module: it notes the import and
    # moves on. That is how a green build once shipped a binary whose first act was
    # to fail to load its UI.
    foreach ($m in @("QtQuick\Controls", "QtQuick\Controls\Material", "QtQuick\Layouts", "Qt\labs\settings")) {
        if (-not (Test-Path "qml\$m")) { Fail "windeployqt did not deploy qml\$m - the binary cannot load its UI" }
    }

    # Both halves, debug last so it wins the names they share.
    Copy-Item "$Vcpkg\bin\*.dll" -Destination . -Force
    Copy-Item "$Vcpkg\debug\bin\*.dll" -Destination . -Force
    Write-Host "deployed to $out" -ForegroundColor Green
}

if ($DeployTo) {
    if (-not (Test-Path "$DeployTo\qdomyos-zwift.exe")) {
        Fail "$DeployTo has no qdomyos-zwift.exe to replace - use -Deploy for a fresh tree first"
    }
    Step "copying exe into $DeployTo"
    Copy-Item $exe $DeployTo -Force
    Write-Host "replaced $DeployTo\qdomyos-zwift.exe" -ForegroundColor Green
}

Set-Location $repo
Write-Host "`ndone." -ForegroundColor Green
