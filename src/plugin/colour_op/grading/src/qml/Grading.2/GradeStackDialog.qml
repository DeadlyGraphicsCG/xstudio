// SPDX-License-Identifier: Apache-2.0

// Colour profiles: save the grade layers of the shot on screen as a named
// profile, then apply (or sync) it to many shots at once. Profile layers sit
// below or above each shot's own layers and never replace them.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

import xStudio 1.0
import xstudio.qml.models 1.0

XsWindow {
    id: dialog
    title: "Colour Profiles"
    width: 560
    height: 600
    minimumWidth: 480
    minimumHeight: 480

    property real rowHeight: XsStyleSheet.widgetStdHeight + 4
    property string selectedPath: ""
    property var selectedProfile: {
        for (var i = 0; i < profiles.length; ++i) {
            if (profiles[i].path == selectedPath) return profiles[i]
        }
        return undefined
    }

    XsModuleData {
        id: stack_attrs
        modelDataName: "grade_stack_settings"
    }
    XsAttributeValue {
        id: __action
        attributeTitle: "grade_stack_action"
        model: stack_attrs
    }
    XsAttributeValue {
        id: __profiles
        attributeTitle: "grade_stack_profiles"
        model: stack_attrs
    }
    XsAttributeValue {
        id: __status
        attributeTitle: "grade_stack_status"
        model: stack_attrs
    }
    XsAttributeValue {
        id: __position
        attributeTitle: "grade_stack_position"
        model: stack_attrs
    }
    XsAttributeValue {
        id: __can_undo
        attributeTitle: "grade_stack_can_undo"
        model: stack_attrs
    }
    XsAttributeValue {
        id: __folder
        attributeTitle: "grade_stack_default_folder"
        model: stack_attrs
    }

    property var profiles: {
        try {
            return __profiles.value ? JSON.parse(__profiles.value) : []
        } catch (e) {
            return []
        }
    }
    property var status: {
        try {
            return __status.value ? JSON.parse(__status.value) : undefined
        } catch (e) {
            return undefined
        }
    }
    property bool placeAbove: __position.value == "Above"

    onVisibleChanged: if (visible) send({action: "refresh"})

    function send(cmd) {
        __action.value = JSON.stringify(cmd)
    }

    function localPath(url) {
        if (url === false || url === undefined) return ""
        return helpers.pathFromURL(url)
    }

    function saveNew() {
        dialogHelpers.textInputDialog(
            function(name, button) {
                if (button == "Save" && name != "") {
                    send({action: "save", name: name, position: placeAbove ? "above" : "below"})
                }
            },
            "Save Colour Profile",
            "Save all grade layers of the shot on screen as a profile.\nSaving again with the same name updates that profile.",
            selectedProfile ? selectedProfile.name : "Show Look",
            ["Cancel", "Save"])
    }

    function updateSelected() {
        if (!selectedProfile) return
        dialogHelpers.multiChoiceDialog(
            function(button) {
                if (button == "Update") send({action: "save", path: selectedPath})
            },
            "Update Colour Profile",
            "Replace the layers of '" + selectedProfile.name + "' with the grade layers of the shot on screen?\n" +
            "Shots it was applied to pick up the change the next time you apply it.",
            ["Cancel", "Update"],
            undefined)
    }

    function apply(scope) {
        if (!selectedProfile) return
        send({action: "apply", path: selectedPath, scope: scope, position: placeAbove ? "above" : "below"})
    }

    function removeFrom(scope) {
        if (!selectedProfile) return
        send({action: "remove", path: selectedPath, scope: scope})
    }

    function importProfile() {
        dialogHelpers.showFileDialog(
            function(path, folder) {
                var p = localPath(path)
                if (p != "") send({action: "import", source: p})
            },
            file_functions.defaultSessionFolder(),
            "Import Colour Profile",
            "json",
            ["Colour profiles (*.xsgrade.json *.json)"],
            true,
            false)
    }

    function exportProfile() {
        if (!selectedProfile) return
        dialogHelpers.showFileDialog(
            function(path, folder) {
                var p = localPath(path)
                if (p == "") return
                if (!p.endsWith(".json")) p += ".xsgrade.json"
                send({action: "export", path: selectedPath, target: p})
            },
            file_functions.defaultSessionFolder(),
            "Export Colour Profile",
            "json",
            ["Colour profiles (*.xsgrade.json)"],
            false,
            false)
    }

    function exportCdl() {
        if (!selectedProfile) return
        dialogHelpers.showFileDialog(
            function(path, folder) {
                var p = localPath(path)
                if (p == "") return
                if (!p.endsWith(".ccc") && !p.endsWith(".cdl") && !p.endsWith(".cc")) p += ".ccc"
                send({action: "export_cdl", path: selectedPath, target: p})
            },
            file_functions.defaultSessionFolder(),
            "Export Colour Profile as ASC CDL",
            "ccc",
            ["CCC files (*.ccc)", "CDL files (*.cdl)", "CC files (*.cc)"],
            false,
            false)
    }

    function deleteSelected() {
        if (!selectedProfile) return
        dialogHelpers.multiChoiceDialog(
            function(button) {
                if (button == "Delete") {
                    send({action: "delete", path: selectedPath})
                    selectedPath = ""
                }
            },
            "Delete Colour Profile",
            "Delete '" + selectedProfile.name + "'? The file is moved to the 'deleted' folder inside the profile folder.\n" +
            "Layers already applied to shots stay where they are.",
            ["Cancel", "Delete"],
            undefined)
    }

    component SectionLabel: XsText {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font.bold: true
        color: XsStyleSheet.hintColor
    }

    component ActionButton: XsPrimaryButton {
        Layout.fillWidth: true
        Layout.preferredHeight: dialog.rowHeight
        imgSrc: ""
        font.pixelSize: XsStyleSheet.fontSize
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        XsText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            text: "A colour profile is a saved stack of grade layers. Apply it to many shots at once; it sits below or above each shot's own layers and never changes them. Applying again updates the profile's layers instead of adding copies."
        }

        SectionLabel { text: "Profiles" }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            color: XsStyleSheet.panelBgColor
            border.color: XsStyleSheet.widgetBgNormalColor

            XsText {
                anchors.centerIn: parent
                visible: profiles.length == 0
                text: "No profiles yet. Grade a shot, then press 'Save shot layers as new profile'."
                color: XsStyleSheet.hintColor
            }

            ListView {
                id: profileList
                anchors.fill: parent
                anchors.margins: 2
                clip: true
                spacing: 1
                model: profiles

                ScrollBar.vertical: XsScrollBar {
                    visible: profileList.height < profileList.contentHeight
                }

                delegate: XsPrimaryButton {
                    width: profileList.width
                    height: dialog.rowHeight * 1.6
                    imgSrc: ""
                    text: ""
                    isActive: modelData.path == dialog.selectedPath
                    isActiveIndicatorAtLeft: true
                    onClicked: dialog.selectedPath = modelData.path

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 6
                        spacing: 0
                        XsText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            font.bold: true
                            text: modelData.name
                            elide: Text.ElideRight
                        }
                        XsText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            color: XsStyleSheet.hintColor
                            elide: Text.ElideRight
                            text: modelData.layers + (modelData.layers == 1 ? " layer: " : " layers: ") +
                                  modelData.layer_names.join(", ") +
                                  (modelData.description ? "  -  " + modelData.description : "")
                        }
                    }
                }
            }
        }

        SectionLabel { text: "Shot on screen" }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                text: "Save shot layers as new profile..."
                onClicked: saveNew()
            }
            ActionButton {
                text: "Update selected profile"
                enabled: selectedProfile !== undefined
                onClicked: updateSelected()
            }
        }

        SectionLabel { text: "Place profile layers" }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                text: "Below shot layers (applied first)"
                isActive: !placeAbove
                onClicked: __position.value = "Below"
            }
            ActionButton {
                text: "Above shot layers (applied last)"
                isActive: placeAbove
                onClicked: __position.value = "Above"
            }
        }

        SectionLabel { text: "Apply or sync selected profile to" }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                text: "Selected media"
                enabled: selectedProfile !== undefined
                onClicked: apply("selected")
            }
            ActionButton {
                text: "Whole playlist / sequence"
                enabled: selectedProfile !== undefined
                onClicked: apply("all")
            }
        }

        SectionLabel { text: "Take selected profile off" }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                text: "Selected media"
                enabled: selectedProfile !== undefined
                onClicked: removeFrom("selected")
            }
            ActionButton {
                text: "Whole playlist / sequence"
                enabled: selectedProfile !== undefined
                onClicked: removeFrom("all")
            }
            ActionButton {
                text: "Undo last apply / take off"
                enabled: __can_undo.value == true
                onClicked: send({action: "undo"})
            }
        }

        SectionLabel { text: "Files" }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                text: "Import..."
                onClicked: importProfile()
            }
            ActionButton {
                text: "Export..."
                enabled: selectedProfile !== undefined
                onClicked: exportProfile()
            }
            ActionButton {
                text: "Export ASC CDL..."
                enabled: selectedProfile !== undefined
                onClicked: exportCdl()
            }
            ActionButton {
                text: "Delete"
                enabled: selectedProfile !== undefined
                onClicked: deleteSelected()
            }
        }

        XsText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.Wrap
            visible: status !== undefined
            text: status ? status.message : ""
            color: status && !status.ok ? "#ff6060" : XsStyleSheet.primaryTextColor
        }

        XsText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideMiddle
            color: XsStyleSheet.hintColor
            text: "Profile folder: " + (__folder.value ? __folder.value : "")
        }
    }
}
