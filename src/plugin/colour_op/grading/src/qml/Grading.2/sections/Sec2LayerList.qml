// SPDX-License-Identifier: Apache-2.0

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0
import Grading 2.0

Item{ id: listDiv

    property alias bookmarkList: bookmarkList

    Rectangle{
        anchors.fill: parent
        color: panelColor

        XsListView { id: bookmarkList
            width: parent.width - x*2
            height: parent.height - y
            x: itemSpacing
            y: itemSpacing
            model: bookmarkFilterModel
            spacing: itemSpacing

            ScrollBar.vertical: XsScrollBar {
                visible: bookmarkList.height < bookmarkList.contentHeight
            }

            property var curr_bookmark_id: helpers.QUuidFromUuidString(attrs.grading_bookmark)

            // Starts editing the name of the selected layer (context menu "Rename...")
            function renameSelectedLayer() {
                for (var i = 0; i < count; ++i) {
                    var item = itemAtIndex(i)
                    if (item && item.isSelected) {
                        positionViewAtIndex(i, ListView.Contain)
                        item.startRename()
                        return
                    }
                }
            }

            delegate: XsPrimaryButton { id: bookmark
                width: bookmarkList.width
                height: btnHeight * 1.1

                isActive: isSelected
                isActiveIndicatorAtLeft: true
                activeIndicator.width: (1*3) * 3

                property var uuid: uuidRole
        
                readonly property bool isSelected: uuid == bookmarkList.curr_bookmark_id
                property bool isHovered: hovered

                // Layer renaming (double-click on the layer, or "Rename..." in the menu)
                property bool isRenaming: false

                function startRename() {
                    isRenaming = true
                }

                function finishRename(accept) {
                    if (!isRenaming) return
                    isRenaming = false
                    var newName = nameEditor.text.trim()
                    if (accept && newName != "" && newName != nameDiv.text) {
                        attrs.grading_action = "Rename Layer|" + uuidRole + "|" + newName
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onClicked: (mouse) => {
                        if (mouse.button == Qt.LeftButton && attrs.grading_bookmark != uuidRole){
                            if (currentPlayhead.mediaFrame < startFrameRole || currentPlayhead.mediaFrame > endFrameRole) {
                                // jump to frame of grade
                                currentPlayhead.logicalFrame = currentPlayhead.logicalFrame + (startFrameRole-currentPlayhead.mediaFrame)
                            }
                            attrs.grading_bookmark = uuidRole
                        } else if(mouse.button == Qt.LeftButton){
                            // deselect the grade
                            attrs.grading_bookmark = helpers.makeQUuid()
                        }
                        else if(mouse.button == Qt.RightButton){
                            if(moreMenu.visible) moreMenu.visible = false
                            else{
                                moreMenu.x = x + width
                                moreMenu.y = y + height
                                moreMenu.visible = true
                            }
                        }
                    }

                    onDoubleClicked: (mouse) => {
                        if (mouse.button != Qt.LeftButton) return
                        // the first click of the double-click may have
                        // deselected the layer, select it again
                        if (attrs.grading_bookmark != uuidRole) {
                            attrs.grading_bookmark = uuidRole
                        }
                        bookmark.startRename()
                    }
                }

                RowLayout{
                    spacing: 0
                    anchors.fill: parent

                    XsText{ id: nameDiv
                        Layout.fillWidth: true
                        Layout.minimumWidth: 50
                        Layout.preferredWidth: 100
                        Layout.fillHeight: true

                        text: layerUIName()
                        color: bookmark.isRenaming ? "transparent" : XsStyleSheet.primaryTextColor
                        font.weight: isSelected? Font.Bold : Font.Normal
                        horizontalAlignment: Text.AlignLeft
                        leftPadding: bookmark.activeIndicator.width + 5
                        elide: Text.ElideRight

                        XsTextField { id: nameEditor
                            anchors.fill: parent
                            anchors.leftMargin: bookmark.activeIndicator.width + 2
                            anchors.topMargin: 2
                            anchors.bottomMargin: 2
                            visible: bookmark.isRenaming
                            horizontalAlignment: TextInput.AlignLeft
                            leftPadding: 3
                            bgColor: XsStyleSheet.widgetBgNormalColor
                            borderColor: XsStyleSheet.accentColor
                            maximumLength: 128

                            onVisibleChanged: {
                                if (visible) {
                                    text = nameDiv.text
                                    forceActiveFocus()
                                    selectAll()
                                }
                            }
                            onAccepted: bookmark.finishRename(true)
                            onActiveFocusChanged: {
                                // clicking elsewhere keeps the new name
                                if (!activeFocus) bookmark.finishRename(true)
                            }
                            Keys.onEscapePressed: bookmark.finishRename(false)
                        }

                        function layerUIName() {
                            var name = "";
                            if (userDataRole.layer_name) {
                                name = userDataRole.layer_name
                                // if (startFrameRole == endFrameRole) {
                                //     name += " - Frame " + startFrameRole
                                // }
                            } else {
                                name = "Grade Layer " + (index+1)
                            }

                            return name
                        }
                    }
                    Item{ id: maskDiv
                        Layout.preferredWidth: !maskBtn.visible? 0 : height * 1.2
                        Layout.fillHeight: true

                        XsSecondaryButton{ id: maskBtn
                            width: parent.width - 1
                            height: parent.height - 4
                            anchors.verticalCenter: parent.verticalCenter

                            isActive: false
                            visible: userDataRole.mask_active
                            imgSrc: "qrc:/grading_icons/mask_domino.svg"
                            text: "Mask Active"
                            scale: 0.95
                            imageSrcSize: 20
                            hoverEnabled: false
                            enabled: false
                            onlyVisualyEnabled: true
                        }
                    }
                    Item{ id: rangeDiv
                        Layout.preferredWidth: height * 1.2
                        Layout.fillHeight: true

                        XsPrimaryButton{ id: rangeBtn
                            width: parent.width - 1
                            height: parent.height - 4
                            anchors.verticalCenter: parent.verticalCenter

                            property bool isFullClip: startFrameRole != -1 && startFrameRole != endFrameRole

                            isActiveViaIndicator: false
                            isActive: false
                            // enabled: hasActiveGrade()
                            imgSrc: isFullClip? "qrc:/grading_icons/all_inclusive.svg" : "qrc:/grading_icons/step_into.svg"
                            text: isFullClip? "Full Clip" : "Single Frame"
                            scale: 0.95

                            onClicked: {                                
                                if(!isFullClip){
                                    
                                    attrs.grading_action = "Set Bookmark Full Range|" + uuidRole

                                } else {

                                    attrs.grading_action = "Set Bookmark One Frame|" + uuidRole + "|" + currentPlayhead.mediaFrame
                                }
                            }

                        }
                    }
                    Item{ id: visibilityDiv
                        Layout.preferredWidth: height * 1.2
                        Layout.fillHeight: true

                        XsPrimaryButton{ id: visibilityBtn
                            width: parent.width - 1
                            height: parent.height - 4
                            anchors.verticalCenter: parent.verticalCenter

                            isActive: !userDataRole.grade_active
                            imgSrc: userDataRole.grade_active? "qrc:/icons/visibility.svg" : "qrc:/icons/visibility_off.svg"
                            isActiveViaIndicator: false
                            text: "Visibility"
                            scale: 0.95

                            onClicked: {
                                var tmp = userDataRole
                                tmp.grade_active = !tmp.grade_active
                                userDataRole = tmp
                            }
                        }
                    }
                }
            }
        }
    }
}