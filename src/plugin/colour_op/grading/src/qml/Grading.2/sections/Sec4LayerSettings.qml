// SPDX-License-Identifier: Apache-2.0

import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import Grading 2.0

// Settings of the selected grade layer (working space) and the channel check
// buttons that show a single colour channel in the viewer.
Item{ id: settingsDiv

    property real labelWidth: 95

    RowLayout {
        anchors.fill: parent
        spacing: buttonSpacing

        XsText {
            Layout.preferredWidth: labelWidth
            Layout.fillHeight: true
            text: "Working space"
            horizontalAlignment: Text.AlignRight
            rightPadding: 6
            opacity: workingSpaceCombo.enabled ? 1.0 : 0.5
        }

        // The colour space the selected layer is applied in. The list comes
        // from the OCIO config of the media on screen.
        XsComboBox { id: workingSpaceCombo
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Layout.maximumWidth: 260
            Layout.fillHeight: true

            enabled: hasActiveGrade() && attrs.media_colour_managed
            model: attrs.media_colour_managed && attrs.colour_space_options ? attrs.colour_space_options : ["raw"]

            property string layerSpace: attrs.media_colour_managed && attrs.colour_space ? attrs.colour_space : "raw"

            function syncIndex() {
                currentIndex = model ? model.indexOf(layerSpace) : -1
            }
            onLayerSpaceChanged: syncIndex()
            onModelChanged: syncIndex()
            Component.onCompleted: syncIndex()

            // Only user picks change the layer, never model updates
            onActivated: (index) => {
                var picked = model[index]
                if (picked && picked != "raw" && picked != attrs.colour_space) {
                    attrs.colour_space = picked
                }
            }

            XsToolTip {
                text: "Colour space this layer is graded in (from the OCIO config). " +
                      "The image is converted to it before the layer and back after."
                visible: workingSpaceCombo.hovered && !workingSpaceCombo.popup.visible
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 4
            Layout.fillHeight: true
        }

        XsText {
            Layout.fillHeight: true
            text: "Check"
            rightPadding: 4
        }

        // Channel check: show one channel in the viewer, click again for RGB.
        // Uses the viewer channel mode, so the viewer toolbar and the R / G / B
        // viewer hotkeys stay in sync with these buttons.
        Repeater {
            model: [
                {"label": "R", "channel": "Red", "colour": "red", "hotkey": "Toggle Red Channel"},
                {"label": "G", "channel": "Green", "colour": "green", "hotkey": "Toggle Green Channel"},
                {"label": "B", "channel": "Blue", "colour": "blue", "hotkey": "Toggle Blue Channel"}
            ]

            XsPrimaryButton {
                Layout.preferredWidth: btnWidth
                Layout.fillHeight: true
                text: modelData.label
                font.bold: true
                isActive: attrs.viewer_channel == modelData.channel
                activeIndicator.color: isActive ? modelData.colour : "transparent"
                toolTip: "Show only the " + modelData.channel.toLowerCase() +
                         " channel in the viewer (click again to see all channels)"
                hotkeyNameForTooltip: modelData.hotkey
                onClicked: {
                    attrs.grading_action = "Toggle Channel " + modelData.channel
                }
            }
        }
    }
}
