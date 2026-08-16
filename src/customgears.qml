import QtQuick 2.7
import QtQuick.Layouts 1.3
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.0
import Qt.labs.settings 1.0

ScrollView {
    contentWidth: -1
    focus: true
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.fill: parent
    id: customGearSettingsWindow
    visible: true
    clip: true

    Settings {
        id: settings
        property bool gears_custom_table_enabled: false
        property string gears_custom_table: "1|1\n2|2\n3|3\n4|4\n5|5\n6|6\n7|7\n8|8\n9|9\n10|10\n11|11\n12|12\n13|13\n14|14\n15|15\n16|16\n17|17\n18|18\n19|19\n20|20\n21|21\n22|22\n23|23\n24|24"
        property int gears_neutral_gear: 0
    }

    ListModel {
        id: gearListModel
    }

    property var gearRows: []
    property int rowHeight: 55
    property int offsetControlHeight: 43
    property int maxGearCount: 24
    // A neutral gear turns the column from an offset added to the gear into the resistance
    // level that gear should reach, so the heading has to say which one the user is typing.
    property bool absoluteMode: settings.gears_neutral_gear >= 1

    Component.onCompleted: {
        loadGearRows()
    }

    function clampOffset(value) {
        if (isNaN(value)) {
            return 0
        }
        return Math.max(-100, Math.min(100, value))
    }

    function parseOffset(value) {
        return parseFloat(String(value).replace(",", "."))
    }

    function defaultGearRows(count) {
        var rows = []
        for (var i = 1; i <= count; i++) {
            rows.push({ gear: i, offset: i })
        }
        return rows
    }

    function loadGearRows() {
        var parsed = stringToGearRows(settings.gears_custom_table)
        gearRows = parsed.length >= 1 ? parsed : defaultGearRows(maxGearCount)
        updateGearListModel()
        saveGearRows()
    }

    // One row is one gear, so the number of rows is the number of gears the bike has. Rows
    // stop at the first missing gear: a table with a hole in it cannot describe a cassette,
    // and QZ would not let you shift past the hole anyway.
    function stringToGearRows(gearString) {
        if (!gearString) {
            return []
        }

        var rows = []
        var lines = gearString.split("\n")
        for (var i = 0; i < lines.length; i++) {
            var parts = lines[i].split("|")
            if (parts.length < 2) {
                continue
            }
            var gear = parseInt(parts[0])
            var offset = clampOffset(parseOffset(parts[1]))
            if (gear >= 1 && gear <= maxGearCount) {
                rows[gear - 1] = { gear: gear, offset: offset }
            }
        }

        var compactRows = []
        for (var j = 1; j <= maxGearCount && rows[j - 1]; j++) {
            compactRows.push(rows[j - 1])
        }
        return compactRows
    }

    function setGearCount(count) {
        if (isNaN(count)) {
            // Typing nothing must not wipe the cassette.
            return
        }
        count = Math.max(1, Math.min(maxGearCount, Math.round(count)))
        var rows = []
        for (var i = 1; i <= count; i++) {
            rows.push(gearRows[i - 1] ? gearRows[i - 1] : { gear: i, offset: i })
        }
        gearRows = rows
        if (settings.gears_neutral_gear > count) {
            settings.gears_neutral_gear = count
        }
        updateGearListModel()
        saveGearRows()
    }

    function gearRowsToString(rows) {
        return rows.map(function(row) {
            return row.gear + "|" + clampOffset(parseOffset(row.offset))
        }).join("\n")
    }

    function saveGearRows() {
        settings.gears_custom_table = gearRowsToString(gearRows)
    }

    function updateGearListModel() {
        if (!gearListModel) {
            return
        }
        gearListModel.clear()
        for (var i = 0; i < gearRows.length; i++) {
            gearListModel.append(gearRows[i])
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        IndicatorOnlySwitch {
            text: qsTr("Enable Custom Gear Table")
            spacing: 0
            bottomPadding: 0
            topPadding: 0
            rightPadding: 0
            leftPadding: 0
            clip: false
            checked: settings.gears_custom_table_enabled
            Layout.alignment: Qt.AlignLeft | Qt.AlignTop
            Layout.fillWidth: true
            onClicked: settings.gears_custom_table_enabled = checked
        }

        Label {
            text: absoluteMode
                  ? qsTr("The column below is the resistance level each gear should reach, and the neutral gear is the one that rides exactly what the training app asked for - a flat road stays flat in it. Away from neutral, each shift is also worth half a percent of simulated grade.")
                  : qsTr("Each gear uses the offset below instead of the raw gear value. QZ applies it automatically to resistance, inclination, or slope depending on the trainer path. Default is linear.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: Qt.application.font.pixelSize - 2
            color: Material.accent
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Gears")
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: gearCountField
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignHCenter
                text: gearRows.length
                inputMethodHints: Qt.ImhDigitsOnly
                function applyCount() {
                    setGearCount(parseInt(text))
                    text = gearRows.length
                }
                onAccepted: applyCount()
                onEditingFinished: applyCount()
            }

            Label {
                text: qsTr("Neutral gear (0 = off)")
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
            }

            TextField {
                id: neutralGearField
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignHCenter
                text: settings.gears_neutral_gear
                inputMethodHints: Qt.ImhDigitsOnly
                function applyNeutral() {
                    var value = parseInt(text)
                    if (isNaN(value)) {
                        value = 0
                    }
                    settings.gears_neutral_gear = Math.max(0, Math.min(gearRows.length, value))
                    text = settings.gears_neutral_gear
                }
                onAccepted: applyNeutral()
                onEditingFinished: applyNeutral()
            }
        }

        Button {
            text: qsTr("Reset to Linear Defaults")
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            onClicked: {
                gearRows = defaultGearRows(gearRows.length)
                updateGearListModel()
                saveGearRows()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#f0f0f0"
            border.width: 1
            border.color: "#cccccc"

            Row {
                anchors.fill: parent

                Rectangle {
                    width: parent.width / 2
                    height: parent.height
                    border.width: 1
                    border.color: "#cccccc"
                    color: "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Gear")
                        font.bold: true
                        color: "black"
                    }
                }

                Rectangle {
                    width: parent.width / 2
                    height: parent.height
                    border.width: 1
                    border.color: "#cccccc"
                    color: "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: absoluteMode ? qsTr("Resistance") : qsTr("Offset")
                        font.bold: true
                        color: "black"
                    }
                }
            }
        }

        ListView {
            id: gearTable
            Layout.fillWidth: true
            Layout.preferredHeight: gearRows.length * rowHeight
            clip: true
            model: gearListModel

            Component.onCompleted: {
                updateGearListModel()
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                width: gearTable.width
                height: rowHeight
                color: index % 2 === 0 ? "white" : "#fafafa"

                Row {
                    anchors.fill: parent

                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        border.width: 1
                        border.color: "#cccccc"
                        color: "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: gear
                            color: "black"
                        }
                    }

                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        border.width: 1
                        border.color: "#cccccc"
                        color: "transparent"

                        RowLayout {
                            anchors.centerIn: parent
                            width: parent.width * 0.92
                            height: offsetControlHeight
                            spacing: 4

                            Button {
                                text: "-"
                                Layout.preferredWidth: 34
                                Layout.fillHeight: true
                                onClicked: {
                                    gearRows[index].offset = clampOffset(parseOffset(gearRows[index].offset) - 0.5)
                                    offsetTextField.text = gearRows[index].offset
                                    saveGearRows()
                                }
                            }

                            TextField {
                                id: offsetTextField
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: offset
                                color: "black"
                                selectedTextColor: "white"
                                selectionColor: Material.accent
                                horizontalAlignment: Text.AlignHCenter
                                background: Rectangle {
                                    color: "white"
                                    border.color: "#cccccc"
                                    radius: 2
                                }
                                function applyOffset() {
                                    var newOffset = clampOffset(parseOffset(text))
                                    gearRows[index].offset = newOffset
                                    text = newOffset
                                    saveGearRows()
                                }
                                onAccepted: applyOffset()
                                onEditingFinished: applyOffset()
                            }

                            Button {
                                text: "+"
                                Layout.preferredWidth: 34
                                Layout.fillHeight: true
                                onClicked: {
                                    gearRows[index].offset = clampOffset(parseOffset(gearRows[index].offset) + 0.5)
                                    offsetTextField.text = gearRows[index].offset
                                    saveGearRows()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
