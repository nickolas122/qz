import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3

// The new UI's entry point, reached only when ui_next is on. Three destinations, Ride
// first and default, no drawer: what survived section 7 does not justify one.
// See STRIP-SPEC.md section 9.3.
ApplicationWindow {
    id: window
    visible: true
    width: 480
    height: 800
    title: "QZ"

    Material.theme: Material.Dark
    Material.accent: Material.Cyan

    // Everything is sized off this so the same tree works on a phone, a tablet and a
    // resizable desktop window. The tablet is the stricter case and sets the floor.
    readonly property real unit: Math.max(12, Math.min(width, height) / 40)

    header: TabBar {
        id: tabBar
        currentIndex: pages.currentIndex
        onCurrentIndexChanged: pages.currentIndex = currentIndex

        TabButton { text: qsTr("Ride") }
        TabButton { text: qsTr("Setup") }
        TabButton { text: qsTr("Settings") }
    }

    SwipeView {
        id: pages
        anchors.fill: parent
        currentIndex: tabBar.currentIndex
        onCurrentIndexChanged: tabBar.currentIndex = currentIndex

        RideScreen {}
        SetupScreen {}
        SettingsScreen {}
    }
}
