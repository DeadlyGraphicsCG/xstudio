// SPDX-License-Identifier: Apache-2.0

// Adds the "Colour Profiles..." entry to the grading panel's menu and owns
// the profile window (see GradeStackDialog.qml).

import QtQuick

import xStudio 1.0
import xstudio.qml.models 1.0

Item {
    id: root

    // name of the menu model to add our entries to
    property string menuModelName: ""

    function showProfiles() {
        if (!dialogLoader.item) {
            dialogLoader.sourceComponent = dialogComponent
        }
        dialogLoader.item.show()
        dialogLoader.item.raise()
        dialogLoader.item.requestActivate()
    }

    Loader {
        id: dialogLoader
    }

    Component {
        id: dialogComponent
        GradeStackDialog {}
    }

    XsMenuModelItem {
        text: "Colour Profiles..."
        menuPath: ""
        menuItemPosition: 11
        menuModelName: root.menuModelName
        onActivated: root.showProfiles()
    }
}
