// SPDX-License-Identifier: Apache-2.0

import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.bookmarks 1.0

Item { id: widget

    property string valueText: ""
    property alias currentText: textField.text
    property bool isHovered: scrubArea.containsMouse || textField.hovered
    property bool isPressed: false

    property color bgColorNormal: XsStyleSheet.widgetBgNormalColor
    property color bgColorPressed: XsStyleSheet.accentColor
    property color indicatorColor: "transparent"
    property real borderWidth: 1
    property color borderColorHovered: bgColorPressed
    property color borderColorNormal: "transparent"

    // Mouse scrubbing: click and drag left / right to change the value.
    // Hold Shift for fine changes, Ctrl for big changes. Double-click resets
    // the value (resetRequested), a single click (no drag) lets you type a
    // value. Set scrubEnabled and scrubValue to use it.
    property bool scrubEnabled: false
    property real scrubValue: 0.0
    property real scrubMin: -Number.MAX_VALUE
    property real scrubMax: Number.MAX_VALUE
    // Value change per pixel of mouse movement
    property real scrubStep: 0.01
    property real fineFactor: 0.1
    property real coarseFactor: 10.0
    readonly property bool isScrubbing: scrubArea.dragging
    property string toolTipText: ""

    signal edited()
    signal scrubbed(real newValue)
    signal resetRequested()

    function startEditing() {
        textField.forceActiveFocus()
        textField.selectAll()
    }

    XsGradientRectangle { id: bgDiv

        anchors.fill: parent
        border.color: widget.isScrubbing || widget.isHovered || textField.activeFocus ? borderColorHovered: borderColorNormal
        border.width: borderWidth

        opacity: enabled? 1.0 : 0.33
        flatColor: topColor
        topColor: isPressed? bgColorPressed: bgColorNormal
        bottomColor: isPressed? bgColorPressed: bgColorNormal

        Rectangle{ id: activeIndicator
            anchors.bottom: parent.bottom
            width: borderWidth*5
            height: parent.height
            color: indicatorColor
        }
    }

    XsTextField{ id: textField
        width: parent.width
        height: parent.height
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        text: valueText
        font.bold: true

        borderColor: "transparent"
        // validator: DoubleValidator {  //to fix support for GTSliderItem & GTWheelItem
        //     bottom: float_scrub_min[index]
        // }
        onEditingFinished: {
            edited()
            // give the mouse back to the scrub area
            focus = false
        }
    }

    // Sits above the text field. It is turned off while the user types in the
    // text field, so the text field gets the mouse for selecting text.
    MouseArea { id: scrubArea
        anchors.fill: parent
        enabled: !textField.activeFocus
        hoverEnabled: true
        preventStealing: true
        cursorShape: widget.scrubEnabled ? Qt.SizeHorCursor : Qt.IBeamCursor

        property real pressX: 0
        property real lastX: 0
        property real scrubbedValue: 0
        property bool dragging: false
        property bool ignoreRelease: false

        onPressed: (mouse) => {
            pressX = mouse.x
            lastX = mouse.x
            scrubbedValue = widget.scrubValue
            dragging = false
        }

        onPositionChanged: (mouse) => {
            if (!pressed || !widget.scrubEnabled) return

            // small movements are still a click
            if (!dragging) {
                if (Math.abs(mouse.x - pressX) < 3) return
                dragging = true
                editTimer.stop()
            }

            var factor = 1.0
            if (mouse.modifiers & Qt.ShiftModifier) factor = widget.fineFactor
            else if (mouse.modifiers & Qt.ControlModifier) factor = widget.coarseFactor

            scrubbedValue += (mouse.x - lastX) * widget.scrubStep * factor
            scrubbedValue = Math.min(widget.scrubMax, Math.max(widget.scrubMin, scrubbedValue))
            lastX = mouse.x
            widget.scrubbed(scrubbedValue)
        }

        onReleased: {
            if (ignoreRelease) {
                ignoreRelease = false
            } else if (!dragging) {
                // wait a moment: this click may be the first half of a
                // double-click (reset)
                editTimer.restart()
            }
            dragging = false
        }

        onDoubleClicked: {
            editTimer.stop()
            ignoreRelease = true
            widget.resetRequested()
        }
    }

    Timer { id: editTimer
        interval: Qt.styleHints.mouseDoubleClickInterval
        repeat: false
        onTriggered: widget.startEditing()
    }

    XsToolTip {
        text: widget.toolTipText
        visible: text != "" && scrubArea.containsMouse && !widget.isScrubbing
    }

}
