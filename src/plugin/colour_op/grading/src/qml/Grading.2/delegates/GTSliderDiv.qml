// SPDX-License-Identifier: Apache-2.0

import QtQuick

import QtQuick.Layouts

import xstudio.qml.bookmarks 1.0


import xStudio 1.0
import xstudio.qml.models 1.0
import Grading 2.0

Item{

    property string titleText: ""

    // Optional per channel [R, G, B] trims shown under the master value: the
    // 'values' of an XsAttributeFullData (see GTAttributes), or null.
    property var rgbData: null
    readonly property bool hasRgb: rgbData != null && rgbData.value !== undefined && rgbData.value.length == 3

    // Pixels of mouse movement to scrub the whole range of a value
    property real scrubPixels: 800

    function setChannel(channel, newValue) {
        if (!hasRgb || isNaN(newValue)) return
        var v = rgbData.value
        v[channel] = Math.min(rgbData.float_scrub_max[channel], Math.max(rgbData.float_scrub_min[channel], newValue))
        rgbData.value = v
    }

    function resetChannels() {
        if (!hasRgb) return
        rgbData.value = rgbData.default_value
    }

    readonly property bool rgbIsDefault: {
        if (!hasRgb) return true
        for (var i = 0; i < 3; ++i) {
            if (rgbData.value[i] != rgbData.default_value[i]) return false
        }
        return true
    }

    ColumnLayout{ id: col
        anchors.fill: parent
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.margins: 2
        spacing: 1

        RowLayout{ id: titleSlider
            Layout.fillWidth: true
            Layout.preferredHeight: XsStyleSheet.secondaryButtonStdWidth + 2
            Layout.maximumHeight: XsStyleSheet.secondaryButtonStdWidth + 2
            spacing: 0

            Item{
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            XsText { id: textDiv
                Layout.preferredWidth: textWidth
                Layout.fillHeight: true
                text: titleText
                elide: Text.ElideRight
                font.pixelSize: XsStyleSheet.fontSize*1.2
                font.bold: true

                MouseArea{ id: textMA
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked:{
                        resetButton.clicked()
                    }
                    onPressed: resetButton.down = true
                    onReleased: resetButton.down = undefined
                }
            }
            Item{
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: 4
                Layout.fillHeight: true
            }
            XsSecondaryButton {  id: resetButton
                Layout.minimumWidth: XsStyleSheet.secondaryButtonStdWidth
                Layout.preferredWidth: XsStyleSheet.secondaryButtonStdWidth
                Layout.fillHeight: true
                imgSrc: "qrc:/icons/rotate-ccw.svg"
                imageDiv.sourceSize.width: 14
                imageDiv.sourceSize.height: 14
                forcedHover: textMA.containsMouse

                onClicked: {
                    value = default_value
                    resetChannels()
                }
            }
            XsIcon {
                source: "qrc:/icons/warning.svg"
                sourceSize.width: 18
                sourceSize.height: 18
                visible: (titleText == "Cont." && (value != 1.0 || !rgbIsDefault)) ||
                         (titleText == "Sat." && !rgbIsDefault)
                MouseArea{
                    id: mm
                    anchors.fill: parent
                    hoverEnabled: true
                    propagateComposedEvents: true
                }
                XsToolTip {
                    text: titleText == "Cont." ?
                        "Contrast will be ignored for CDL export" :
                        "Per channel saturation will be ignored for CDL export"
                    visible: mm.containsMouse
                }
            }
            Item{
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        Item{
            Layout.fillWidth: true
            Layout.minimumHeight: 2
        }

        Item{ id: controlDiv
            Layout.fillWidth: true
            Layout.fillHeight: true

            GTSlider{ id: slider
                height: parent.height
                anchors.horizontalCenter: parent.horizontalCenter

                backend_value: value
                onSetValue: (newVal)=> {
                    var _value = (typeof value == "number") ? value : value[index]
                    _value = newVal
                    if(typeof value == "number") value = _value
                    else value[index] = _value
                }
            }
        }

        Item{ id: valuesDiv
            Layout.fillWidth: true
            Layout.minimumHeight: XsStyleSheet.widgetStdHeight

            GTValueEditor{
                width: 45
                height: XsStyleSheet.widgetStdHeight
                valueText: value.toFixed(3)
                indicatorColor: "transparent"
                anchors.horizontalCenter: parent.horizontalCenter

                scrubEnabled: true
                scrubValue: value
                scrubMin: float_scrub_min
                scrubMax: float_scrub_max
                scrubStep: (float_scrub_max - float_scrub_min) / scrubPixels
                toolTipText: "Drag left / right to change (Shift: fine, Ctrl: coarse). Double-click to reset."

                onScrubbed: (newValue) => { value = newValue }
                onResetRequested: { value = default_value }
                onEdited:{
                    var newValue = parseFloat(currentText)
                    if (!isNaN(newValue)) value = newValue
                }
            }
        }

        // Per channel trims (R, G, B) on top of the master value
        Repeater {
            model: hasRgb ? 3 : 0

            Item{
                Layout.fillWidth: true
                Layout.minimumHeight: XsStyleSheet.widgetStdHeight
                Layout.preferredHeight: XsStyleSheet.widgetStdHeight

                property real channelValue: hasRgb ? rgbData.value[index] : 0.0

                GTValueEditor{
                    width: 45
                    height: XsStyleSheet.widgetStdHeight
                    anchors.horizontalCenter: parent.horizontalCenter
                    valueText: channelValue.toFixed(3)
                    indicatorColor: index==0?"red":index==1?"green":"blue"

                    scrubEnabled: true
                    scrubValue: channelValue
                    scrubMin: rgbData.float_scrub_min[index]
                    scrubMax: rgbData.float_scrub_max[index]
                    scrubStep: (rgbData.float_scrub_max[index] - rgbData.float_scrub_min[index]) / scrubPixels
                    toolTipText: (index==0?"Red":index==1?"Green":"Blue") +
                        (titleText == "Exp." ? " exposure offset in stops, added to the master value." :
                                               " amount, multiplied with the master value.") +
                        " Drag to change, double-click to reset."

                    onScrubbed: (newValue) => setChannel(index, newValue)
                    onResetRequested: setChannel(index, rgbData.default_value[index])
                    onEdited: setChannel(index, parseFloat(currentText))
                }
            }
        }

    }

}