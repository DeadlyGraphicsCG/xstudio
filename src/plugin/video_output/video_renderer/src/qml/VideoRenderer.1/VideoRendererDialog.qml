// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs

import xstudio.qml.models 1.0
import xStudio 1.0
import "."

XsWindow {

	id: dialog
	minimumWidth: 520
	minimumHeight: 900
    width: 620
    title: clipMode ? "Export Clip with Grade" : (gradeExport ? "Export with Grade" : "Render Video")

    flags: Qt.platform.os === "windows" ? Qt.Window : Qt.Dialog

    // The "Export with Grade ..." menu items open this dialog with
    // gradeExport set: it starts from the last used export profile. With
    // clipMode set, the selected clips in the media list are rendered one by
    // one (one file each) instead of whole playlists or timelines.
    property bool gradeExport: false
    property bool clipMode: false

    property var mediaSelection: clipMode ? mediaSelectionModel.selectedIndexes : []
    property var numSelected: clipMode ? mediaSelection.length : sessionSelectionModel.selectedIndexes.length
    property var currentItem: 0
    property var currentMedia: clipMode && numSelected > currentItem ? mediaSelection[currentItem] : undefined
    property var itemToRender: clipMode ? playlistOfMedia(currentMedia) : (numSelected ? sessionSelectionModel.selectedIndexes[currentItem] : undefined)

    property var selectedContainerType: clipMode ? (currentMedia ? "Clip" : "") : (itemToRender ? theSessionData.get(itemToRender, "typeRole") : "")
    property var selectedContainerName: clipMode ? (currentMedia ? theSessionData.get(currentMedia, "nameRole") : "") : (itemToRender ? theSessionData.get(itemToRender, "nameRole") : "")
    property var selectedContainerUuid: itemToRender ? theSessionData.get(itemToRender, "actorUuidRole") : ""

    // the playlist that holds a clip (clips in subsets and timelines are
    // also in their playlist)
    function playlistOfMedia(mediaIndex) {
        if (!mediaIndex || !mediaIndex.valid) return undefined
        var container = mediaIndex.parent.parent
        if (!container || !container.valid) return undefined
        if (theSessionData.get(container, "typeRole") == "Playlist") return container
        return container.parent.parent
    }

    // "shot010.####.exr" -> "shot010", used for the {media_name} token
    function cleanMediaName(n) {
        if (n == undefined) return ""
        n = ("" + n).replace(/\.[A-Za-z0-9]+$/, "")
        n = n.replace(/[._]?(#+|@+|%0?\d*d|\{:0?\d*d\})$/, "")
        return n
    }

    property var frame_based_formats: ['jpg', 'jpeg', 'tiff', 'png', 'bmp', 'tif']

    onItemToRenderChanged: {
        if (itemToRender && itemToRender.valid) {
            var renderTargetId = theSessionData.get(itemToRender, "actorUuidRole")
            if (renderTargetId) {
                xstudio_callback(["update_ocio_choices", renderTargetId])
                // the OCIO menus are reset for the new item: put the
                // profile's display and view back once they are
                var p = profileByName(currentProfileName)
                if (p && (p.ocio_display || p.ocio_view)) {
                    pendingDisplay = p.ocio_display ? p.ocio_display : ""
                    pendingView = p.ocio_view ? p.ocio_view : ""
                    ocio_retry.restart()
                }
            }

            if (theSessionData.get(itemToRender, "typeRole") == "Timeline") {
                render_range = theSessionData.getTimelineFullRangeAndLoopRangeAndFPS(itemToRender);
                if (render_range.length == 4) {
                    inPoint.text = ""+0
                    outPoint.text = ""+render_range[0]
                    fps_best_guess = render_range[3]
                }
            } else {

                inPoint.text = "--"
                outPoint.text = "--"
                getSourceFPSAndStartFrame(true)

            }

            framerates_match = checkIfFrameRatesMatch()

        }
    }

    function getSourceFPSAndStartFrame() {

        // get the first media item in the container (or the clip itself)
        let media_index = clipMode ? currentMedia : theSessionData.searchRecursive("Media", "typeRole", itemToRender);
        if (!media_index) return
        if (media_index.valid) {
            let imageSourceId = theSessionData.get(media_index, "imageActorUuidRole")
            let image_source_idx = theSessionData.searchRecursive(imageSourceId, "actorUuidRole", media_index);                
            if (image_source_idx.valid) {
                let sf = theSessionData.get(image_source_idx, "timecodeAsFramesRole")
                let fps = theSessionData.get(image_source_idx, "rateFPSRole")
                if (sf != undefined && fps != undefined) {
                    start_frame = sf
                    fps_best_guess = fps
                }
            }
        }

    }

    property var framerates_match: true

    function checkIfFrameRatesMatch() {

        // ugly! If the timeline frame rate, or the frame rate of the media 
        // in the playlist to be rendered doesn't match the user selected frame rate, 
        // then we want to show the 'Re-speed' checkbox

        let user_fps = parseFloat(frame_rate.currentText)
        if (clipMode) {
            return fps_best_guess == undefined || Math.abs(user_fps - fps_best_guess) <= 0.01
        } else if (theSessionData.get(itemToRender, "typeRole") == "Timeline") {
            if (Math.abs(user_fps - fps_best_guess) > 0.01) {
                return false
            } else {
                return true
            }
        } else {
            var fps_per_media = theSessionData.getAllContainerMediaFPS(itemToRender)
            for (let i = 0; i < fps_per_media.length; ++i) {
                let fps = fps_per_media[i];
                if (Math.abs(user_fps - fps) > 0.01) return false
            }
        }
        return true
    }

    onCurrentMediaChanged: {
        if (!clipMode || !currentMedia) return
        getSourceFPSAndStartFrame()
        framerates_match = checkIfFrameRatesMatch()
        updateOutputPreview()
    }

    property int start_frame: 0
    property var render_range
    property bool apply_render_range: selectedContainerType == "Timeline"


    property var fps_best_guess

    onFps_best_guessChanged: {
        if (frame_rates == undefined) return
        var frateIdx = -1
        for (var fridx = 0; fridx < frame_rates.length; ++fridx) {
            if (Math.abs(parseFloat(frame_rates[fridx])-fps_best_guess) < 0.001) {
                frateIdx = fridx
                break
            }
        }
        if (frateIdx != -1) {
            frame_rate.currentIndex = frateIdx
        }
    }

    onSelectedContainerUuidChanged: {

        // Here we want to auto-set the frame rate to match the timeline frame
        // rate or the rate of the media in the playlist (or the firs bit of
        // media in the playlist)

        if (itemToRender == undefined || !itemToRender.valid) return;
        if (clipMode) {

            // the clip's own frame rate, see onCurrentMediaChanged

        } else if (theSessionData.get(itemToRender, "typeRole") == "Timeline") {

            // use call to getTimelineFullRangeAndLoopRangeAndFPS made above to
            // retrieve the timeline frame rate.

        } else {

            // Otherwise we want to auto-set the FrameRate to match the rate of
            // the first bit of media in the selected container.

            // get the model index for the Media List under the playlist
            var media_list_idx = theSessionData.index(0, 0, itemToRender)
            var n_media = theSessionData.rowCount(media_list_idx)
            for (var i = 0; i < n_media; ++i) {
                var media_item_idx = theSessionData.index(i, 0, media_list_idx)
                // now get the index for the object
                var actoruuid = theSessionData.get(media_item_idx, "imageActorUuidRole")
                let image_source = theSessionData.searchRecursive(actoruuid, "actorUuidRole", media_item_idx)
                if (image_source.valid) {
                    var _fps = theSessionData.get(image_source, "rateFPSRole")
                    if (_fps) {
                        // set the best guess, triggering the combo-box selection
                        fps_best_guess = _fps;
                        break;
                    }
                }
            }
        }

        // we also want to turn on audio output if any of the media has
        // audio ... actually, this doesn't work

        /*var has_audio = false
        for (var i = 0; i < n_media; ++i) {
            var media_item_idx = theSessionData.index(i, 0, media_list_idx)
            // now get the index for the object
            var actoruuid = theSessionData.get(media_item_idx, "audioActorUuidRole")
            if (!actoruuid.isNull) {
                has_audio = true
                break
            }
        }
        render_audio.checked = has_audio*/

    }

    // access 'attribute' data exposed by our C++ plugin
    XsModuleData {
        id: video_render_attrs
        modelDataName: "video_render_attrs"
    }
    property alias video_render_attrs: video_render_attrs

    XsAttributeValue {
        id: __render_queue_visible
        attributeTitle: "render_queue_visible"
        model: video_render_attrs
    }
    property alias render_queue_visible: __render_queue_visible.value

    XsAttributeValue {
        id: __ffmpeg_stdout
        attributeTitle: "ffmpeg_stdout"
        model: video_render_attrs
    }
    property alias ffmpeg_stdout: __ffmpeg_stdout.value

    XsAttributeValue {
        id: __frame_rate_attr
        attributeTitle: "frame_rate"
        model: video_render_attrs
    }
    property alias frame_rate_attr: __frame_rate_attr.value

    XsAttributeValue {
        id: __resolution_options
        attributeTitle: "resolution_options"
        model: video_render_attrs
    }
    property alias resolution_options: __resolution_options.value
    property var resOptWatcher: __resolution_options.value
    property var resolution_names: []
    onResOptWatcherChanged: {
        // resolution_options is an array. Each element is a 2 element array, the name
        // of the resoluition and whether it can be deleted (defaults can't
        // be deleted from the list)
        var opts = []
        for (var i = 1; i < resolution_options.length; ++i) {
            opts.push(resolution_options[i][0])
        }
        resolution_names = opts

        // pick the last res that the user chose
        var idx = resolution_names.indexOf(resolution_options[0])
        if (idx != -1) {
            output_resolution.currentIndex = idx
        }
    }

    XsAttributeValue {
        id: __render_audio
        attributeTitle: "render_audio"
        model: video_render_attrs
    }
    property alias render_audio: __render_audio.value

    XsAttributeValue {
        id: __load_on_completion
        attributeTitle: "load_on_completion"
        model: video_render_attrs
    }
    property alias load_on_completion: __load_on_completion.value

    XsAttributeValue {
        id: __include_annotations
        attributeTitle: "include_annotations"
        model: video_render_attrs
    }
    property alias include_annotations: __include_annotations.value

    XsAttributeValue {
        id: __frame_rates
        attributeTitle: "frame_rates"
        model: video_render_attrs
    }
    property alias frame_rates: __frame_rates.value

    // ---------------------------------------------------------------------
    // Grade and export profile settings (see video_render_plugin.cpp)
    // ---------------------------------------------------------------------

    XsAttributeValue {
        id: __bake_grade
        attributeTitle: "bake_grade"
        model: video_render_attrs
    }
    property alias bake_grade: __bake_grade.value

    XsAttributeValue {
        id: __sidecar_cdl
        attributeTitle: "sidecar_cdl"
        model: video_render_attrs
    }
    property alias sidecar_cdl: __sidecar_cdl.value

    XsAttributeValue {
        id: __sidecar_lut
        attributeTitle: "sidecar_lut"
        model: video_render_attrs
    }
    property alias sidecar_lut: __sidecar_lut.value

    XsAttributeValue {
        id: __export_profiles
        attributeTitle: "export_profiles"
        model: video_render_attrs
    }
    property alias export_profiles: __export_profiles.value

    XsAttributeValue {
        id: __last_export_profile
        attributeTitle: "last_export_profile"
        model: video_render_attrs
    }
    property alias last_export_profile: __last_export_profile.value

    XsAttributeValue {
        id: __export_profiles_message
        attributeTitle: "export_profiles_message"
        model: video_render_attrs
    }
    property alias export_profiles_message: __export_profiles_message.value

    XsAttributeValue {
        id: __output_path_preview
        attributeTitle: "output_path_preview"
        model: video_render_attrs
    }
    property alias output_path_preview: __output_path_preview.value

    property var profile_names: {
        var names = []
        if (export_profiles) {
            for (var i = 0; i < export_profiles.length; ++i) {
                names.push(export_profiles[i].name)
            }
        }
        return names
    }

    property string currentProfileName: ""
    property bool profileApplied: false

    property bool ready: false
    property bool initialProfileDone: false

    // "Export with Grade": start from the last used profile (or the first
    // one). The attribute data can arrive a little after the dialog is made,
    // so this is tried again when the profile list changes.
    function applyInitialProfile() {
        if (!ready || !gradeExport || initialProfileDone || !profile_names.length) return
        initialProfileDone = true
        var idx = profile_names.indexOf(last_export_profile)
        if (idx == -1) idx = 0
        Qt.callLater(applyProfileByName, profile_names[idx])
    }

    Component.onCompleted: {
        ready = true
        if (gradeExport) bake_grade = true
        applyInitialProfile()
        updateOutputPreview()
    }

    onProfile_namesChanged: {
        applyInitialProfile()
        // keep the combo box on the chosen profile when the list changes
        if (ready) profile_combo.currentIndex = profile_names.indexOf(currentProfileName)
    }

    function profileByName(name) {
        if (!export_profiles) return undefined
        for (var i = 0; i < export_profiles.length; ++i) {
            if (export_profiles[i].name == name) return export_profiles[i]
        }
        return undefined
    }

    function frameBasedExt(ext) {
        return frame_based_formats.indexOf(("" + ext).toLowerCase()) >= 0
    }

    function folderOf(path) {
        var i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return i >= 0 ? path.substring(0, i) : ""
    }

    // same rule as export_profiles::profile_output_path in C++
    function profileOutputPath(p) {
        var folder = p.output_folder
        if (!folder) folder = folderOf(outputFile.text)
        if (!folder) folder = last_render_folder ? last_render_folder : ""
        if (!folder) {
            var d = "" + file_functions.defaultSessionFolder()
            folder = d.startsWith("file:") ? helpers.pathFromURL(d) : d
        }
        var name = p.naming_template ? p.naming_template : "{media_name}_{version}"
        var ext = p.container ? ("" + p.container).replace(/^\./, "").toLowerCase() : "mov"
        if (name.toLowerCase().endsWith("." + ext)) name = name.substring(0, name.length - ext.length - 1)
        if (frameBasedExt(ext) && name.indexOf("#") == -1) name = name + ".####"
        var result = folder
        if (result != "" && !result.endsWith("/") && !result.endsWith("\\")) result = result + "/"
        return result + name + "." + ext
    }

    // OCIO display/view of a profile. The menus are filled in a moment after
    // the item to render changes, so we try again when they do.
    property string pendingDisplay: ""
    property string pendingView: ""
    function applyPendingOcio() {
        if (pendingDisplay != "") {
            var di = ocio_display.indexOfValue(pendingDisplay)
            if (di != -1) {
                if (ocio_display.currentIndex != di) ocio_display.currentIndex = di
                pendingDisplay = ""
            }
        }
        if (pendingView != "" && pendingDisplay == "") {
            var vi = ocio_view.indexOfValue(pendingView)
            if (vi != -1) {
                if (ocio_view.currentIndex != vi) ocio_view.currentIndex = vi
                pendingView = ""
            }
        }
    }
    Timer {
        id: ocio_retry
        interval: 300
        repeat: true
        property int tries: 0
        onTriggered: {
            applyPendingOcio()
            tries++
            if ((pendingDisplay == "" && pendingView == "") || tries > 20) {
                stop()
                tries = 0
            }
        }
    }

    function applyProfileByName(name) {
        var p = profileByName(name)
        if (p == undefined) return
        currentProfileName = name
        profileApplied = true
        profile_combo.currentIndex = profile_names.indexOf(name)

        vid_codec_opts.set_preset_name(p.video_preset)

        if (p.resolution) {
            var ri = resolution_names.indexOf(p.resolution)
            if (ri != -1) {
                output_resolution.currentIndex = ri
            } else if (/([0-9]+)x([0-9]+)/.exec(p.resolution)) {
                var v = resolution_options
                v.push([p.resolution, true])
                resolution_options = v
                output_resolution.currentIndex = v.length-2
            }
        }

        if (p.frame_rate) {
            var fi = frame_rates.indexOf(p.frame_rate)
            if (fi != -1) frame_rate.currentIndex = fi
        }

        bake_grade = p.bake_grade
        sidecar_cdl = p.sidecar_cdl
        sidecar_lut = p.sidecar_lut
        include_annotations = p.include_annotations
        render_audio = p.render_audio

        pendingDisplay = p.ocio_display ? p.ocio_display : ""
        pendingView = p.ocio_view ? p.ocio_view : ""
        applyPendingOcio()
        if (pendingDisplay != "" || pendingView != "") ocio_retry.restart()

        outputFile.text = profileOutputPath(p)
        xstudio_callback(["select_export_profile", {"name": name}])
    }

    // the dialog's current settings as an export profile
    function currentSettingsAsProfile(name) {
        var path = outputFile.text
        var folder = folderOf(path)
        var fname = folder != "" ? path.substring(folder.length + 1) : path
        var ext = "mov"
        var m = fname.match(/^(.*)\.([^\.]+)$/)
        if (m) {
            fname = m[1]
            ext = m[2].toLowerCase()
        }
        if (frameBasedExt(ext)) fname = fname.replace(/[._]?#+$/, "")
        return {
            "name": name,
            "video_preset": vid_codec_opts.get_preset_name(),
            "container": ext,
            "resolution": output_resolution.currentText,
            "frame_rate": frame_rate.currentText,
            "ocio_display": ocio_display.currentText,
            "ocio_view": ocio_view.currentText,
            "bake_grade": bake_grade == true,
            "sidecar_cdl": sidecar_cdl == true,
            "sidecar_lut": sidecar_lut == true,
            "output_folder": folder,
            "naming_template": fname != "" ? fname : "{media_name}_{version}",
            "include_annotations": include_annotations == true,
            "render_audio": render_audio == true
        }
    }

    // values for the naming template tokens of the current item
    function namingTokens() {
        var t = {}
        if (clipMode) {
            t["media_name"] = cleanMediaName(selectedContainerName)
            t["playlist"] = itemToRender ? theSessionData.get(itemToRender, "nameRole") : ""
            // clip frame range from its image source
            if (currentMedia) {
                let imageSourceId = theSessionData.get(currentMedia, "imageActorUuidRole")
                let src = theSessionData.searchRecursive(imageSourceId, "actorUuidRole", currentMedia)
                if (src.valid) {
                    let sf = theSessionData.get(src, "timecodeAsFramesRole")
                    let dur = theSessionData.get(src, "durationFrameRole")
                    if (sf != undefined && dur != undefined && dur > 0) {
                        t["frame_range"] = sf + "-" + (sf + dur - 1)
                    }
                }
            }
        } else {
            t["media_name"] = selectedContainerName
            t["playlist"] = selectedContainerName
            if (apply_render_range && inPoint.text != "--" && outPoint.text != "--") {
                t["frame_range"] = inPoint.text + "-" + outPoint.text
            }
        }
        t["profile"] = currentProfileName
        return t
    }

    property int previewRequest: 0
    property string outputPreview: ""
    function updateOutputPreview() {
        if (!ready) return
        if (outputFile.text.indexOf("{") == -1) {
            outputPreview = ""
            return
        }
        previewRequest = previewRequest + 1
        var res = /([0-9]+)x([0-9]+)/.exec(output_resolution.currentText)
        xstudio_callback(["preview_output_path", {
            "request": previewRequest,
            "path": outputFile.text,
            "media_name": selectedContainerName,
            "width": res ? parseInt(res[1]) : 0,
            "height": res ? parseInt(res[2]) : 0,
            "ocio_display": ocio_display.currentText,
            "ocio_view": ocio_view.currentText,
            "bake_grade": bake_grade == true,
            "tokens": namingTokens()
        }])
    }
    onOutput_path_previewChanged: {
        if (output_path_preview && output_path_preview.request == previewRequest) {
            outputPreview = output_path_preview.path
        }
    }
    onSelectedContainerNameChanged: updateOutputPreview()
    onBake_gradeChanged: updateOutputPreview()
    onCurrentProfileNameChanged: updateOutputPreview()

    // export options sent with the render (see create_render_job in C++)
    function exportOptions() {
        var opts = {
            "bake_grade": bake_grade == true,
            "sidecar_cdl": bake_grade != true && sidecar_cdl == true,
            "sidecar_lut": bake_grade != true && sidecar_lut == true,
            "profile": currentProfileName,
            "tokens": namingTokens()
        }
        if (clipMode && currentMedia) {
            opts["media_filter"] = [("" + theSessionData.get(currentMedia, "actorUuidRole")).replace(/[{}]/g, "")]
        }
        return opts
    }

    property string nameEditMode: ""
    function startNameEdit(mode) {
        nameEditMode = mode
        profile_name_field.text = mode == "rename" ? currentProfileName : ""
        profile_name_field.forceActiveFocus()
    }
    function finishNameEdit() {
        var name = profile_name_field.text.trim()
        if (name == "") return
        if (nameEditMode == "rename") {
            xstudio_callback(["rename_export_profile", {"old_name": currentProfileName, "new_name": name}])
            // the plugin refuses names that are taken, and tells us why
            if (profile_names.indexOf(name) == -1) currentProfileName = name
        } else {
            saveProfile(name)
        }
        nameEditMode = ""
    }
    function saveProfile(name) {
        xstudio_callback(["save_export_profile", currentSettingsAsProfile(name)])
        currentProfileName = name
        profileApplied = true
    }
    function deleteProfile() {
        xstudio_callback(["delete_export_profile", {"name": currentProfileName}])
        currentProfileName = ""
    }

    property bool isFrameBased: false

    property string last_render_folder
    property string file_extension

    property var of: outputFile.text
    onOfChanged: {
        const re = /(.+\.)([^\.]+)$/
        const ext = of.match(re)        
        if (ext) {
            isFrameBased = (frame_based_formats.indexOf(ext[2].toLowerCase()) >= 0)
            file_extension = ext[2].toUpperCase()
        }
    }

    onIsFrameBasedChanged: {
        if (isFrameBased) {
            const re3 = /(.+)(\#+)(.*)(\..+)$/
            const with_hashes = path.match(re3)
            if (with_hashes) {
                outputAudioFile.text = with_hashes[1] + with_hashes[3] + ".aiff"
            }
        }
    }

    function choose_output(fileUrl, undefined, func) {
        
        var path = helpers.pathFromURL(fileUrl)
        const re = /(.+\.)(.+)$/
        const ext = path.match(re)
        if (frame_based_formats.indexOf(ext[2].toLowerCase()) >= 0) {
            const re2 = /(.+\.)([0-9]+)\.(.+)$/
            const re3 = /.+\.(\#+).*\.(.+)$/
            const with_frame_num = path.match(re2)
            const with_hashes = path.match(re3)
            if (with_frame_num) {
                // must be a more elegant way to do this!
                var hashes = ""
                for (var i = 0; i < with_frame_num[2].length; ++i) {
                    hashes = hashes+"#"
                }
                path = path.replace(with_frame_num[2], hashes)
                outputAudioFile.text = with_frame_num[1] + "aiff"
            } else if (!with_hashes) {
                path = ext[1] + "####." + ext[2]
                outputAudioFile.text = ext[1] + "aiff"
            } else if (with_hashes) {
                outputAudioFile.text = ext[1] + "aiff"
            }
        }
        
        outputFile.text = path

        // Without this, on MacOS, the dialog dissapears behind
        // the main window when the file browser is closed
        dialog.hide()
        dialog.show()
        dialog.raise()
        dialog.raise()
    }

    function choose_audio(fileUrl, undefined, func) {

        outputAudioFile.text = helpers.pathFromURL(fileUrl)

        // Without this, on MacOS, the dialog dissapears behind
        // the main window when the file browser is closed
        dialog.hide()
        dialog.show()
        dialog.raise()
        dialog.raise()
    }

    property var widgetHeight: 24

    ColumnLayout {

        width: parent.width

        GridLayout {

            Layout.fillWidth: true
            Layout.margins: 20
            columnSpacing: 12
            rowSpacing: 8
            columns: 2

            VidRenderDlgDivider {
                Layout.preferredHeight: widgetHeight
                Layout.fillWidth: true
                Layout.columnSpan: 2
                label: "Export Profile"
            }

            XsText {
                text: "Profile"
                Layout.alignment: Qt.AlignRight
            }

            RowLayout {
                Layout.fillWidth: true

                XsComboBox {
                    id: profile_combo
                    model: profile_names
                    Layout.minimumWidth: 160
                    Layout.fillWidth: true
                    Layout.preferredHeight: widgetHeight
                    displayText: currentIndex == -1 ? "(no profile)" : currentText
                    onActivated: (index) => {
                        applyProfileByName(profile_names[index])
                    }
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 50
                    text: "Save"
                    enabled: currentProfileName != ""
                    onClicked: saveProfile(currentProfileName)
                    toolTip: "Save the settings below into this profile"
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 76
                    text: "Save As ..."
                    onClicked: startNameEdit("save_as")
                    toolTip: "Save the settings below as a new profile"
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 70
                    text: "Rename ..."
                    enabled: currentProfileName != ""
                    onClicked: startNameEdit("rename")
                    toolTip: "Give this profile a new name"
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 56
                    text: "Delete"
                    enabled: currentProfileName != ""
                    onClicked: deleteProfile()
                    toolTip: "Delete this profile"
                }
            }

            XsText {
                text: nameEditMode == "rename" ? "New Name" : "Profile Name"
                Layout.alignment: Qt.AlignRight
                visible: nameEditMode != ""
            }

            RowLayout {
                Layout.fillWidth: true
                visible: nameEditMode != ""

                XsTextField {
                    id: profile_name_field
                    Layout.fillWidth: true
                    Layout.preferredHeight: widgetHeight
                    placeholderText: "Name for the export profile"
                    onAccepted: finishNameEdit()
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 50
                    text: "OK"
                    enabled: profile_name_field.text.trim() != ""
                    onClicked: finishNameEdit()
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 60
                    text: "Cancel"
                    onClicked: nameEditMode = ""
                }
            }

            XsText {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: export_profiles_message != undefined && export_profiles_message != ""
                text: export_profiles_message ? export_profiles_message : ""
                color: "orange"
                horizontalAlignment: Text.AlignHCenter
            }

            VidRenderDlgDivider {
                Layout.preferredHeight: widgetHeight
                Layout.fillWidth: true
                Layout.columnSpan: 2
                label: "Output"
            }

            XsText {
                text: "Item to Render"
                Layout.alignment: Qt.AlignRight
            }

            XsTextCopyable {
                horizontalAlignment: Text.AlignLeft
                text: selectedContainerName + " (" + selectedContainerType + ")"
                font.weight: Font.Bold
                Layout.fillWidth: true
            }

            XsText {
                text: isFrameBased ? "Output Frames" : "Output File"
                Layout.alignment: Qt.AlignRight
            }

            RowLayout {
                Layout.fillWidth: true
                XsTextField{

                    id: outputFile
                    Layout.fillWidth: true
                    Layout.preferredHeight: widgetHeight
                    clip: true
                    placeholderText: "Enter or browse for ouput file"
                    background: Rectangle{
                        color: outputFile.activeFocus? Qt.darker(XsStyleSheet.accentColor, 1.5): outputFile.hovered? Qt.lighter(XsStyleSheet.panelBgColor, 2):Qt.lighter(XsStyleSheet.panelBgColor, 1.5)
                        border.width: outputFile.hovered || outputFile.active? 1:0
                        border.color: XsStyleSheet.accentColor
                        opacity: 0.7
                    }
                    //text: "file:///user_data/.tmp/test.mp4"
                    onTextChanged: updateOutputPreview()
                    XsToolTip {
                        text: "The file name can use tokens that are filled in when you render: {media_name} {playlist} {date} {time} {year} {version} {frame_range} {profile} {grade} {width} {height} {display} {view}. {version} becomes v001, v002 ... picking the first one that is not on disk yet."
                        visible: outputFile.hovered
                        maxWidth: dialog.width*2/3
                    }
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    text: "Browse ..."
                    onClicked: {
                        dialogHelpers.showFileDialog(
                            choose_output,
                            last_render_folder ? last_render_folder : file_functions.defaultSessionFolder(),
                            "Export Render",
                            ".mov",
                            ["All Files (*.*)", "Media Files (*.mov *.mp4 *.mkv *.webm)"],
                            false,
                            false,
                            undefined
                        )
                    }
                }
            }

            Item {
                visible: outputPreview != ""
                Layout.preferredHeight: 1
                Layout.preferredWidth: 1
            }

            XsText {
                id: output_preview_text
                visible: outputPreview != ""
                text: "Will write: " + outputPreview
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                opacity: 0.7
            }

            XsText {
                text: "Output Audio File"
                Layout.alignment: Qt.AlignRight
                visible: isFrameBased
                enabled: render_audio
            }

            RowLayout {
                Layout.fillWidth: true
                visible: isFrameBased
                enabled: render_audio
                XsTextField{

                    id: outputAudioFile
                    Layout.fillWidth: true
                    Layout.preferredHeight: widgetHeight
                    clip: true
                    placeholderText: "Enter or browse for ouput audio"
                    background: Rectangle{
                        color: outputFile.activeFocus? Qt.darker(XsStyleSheet.accentColor, 1.5): outputFile.hovered? Qt.lighter(XsStyleSheet.panelBgColor, 2):Qt.lighter(XsStyleSheet.panelBgColor, 1.5)
                        border.width: outputFile.hovered || outputFile.active? 1:0
                        border.color: XsStyleSheet.accentColor
                        opacity: 0.7
                    }
                    //text: "file:///user_data/.tmp/test.mp4"
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    text: "Browse ..."
                    onClicked: {
                        dialogHelpers.showFileDialog(
                            choose_audio,
                            last_render_folder ? last_render_folder : file_functions.defaultSessionFolder(),
                            "Export Audio",
                            ".aiff",
                            ["All Files (*.*)", "Audio Files (*.aiff *.wav *.ogg *.mp3 )"],
                            false,
                            false,
                            undefined
                        )
                    }
                }
            }

            XsText {
                text: "First Frame Number"
                Layout.alignment: Qt.AlignRight
                visible: isFrameBased
            }

            RowLayout {

                visible: isFrameBased

                XsTextField{
                    Layout.preferredWidth: 50
                    Layout.preferredHeight: widgetHeight
                    id: firstFrame
                    validator: IntValidator
                    property var guess: start_frame
                    onGuessChanged: text = "" + guess
                }

            }

            XsText {
                text: "Render Range"
                Layout.alignment: Qt.AlignRight
                enabled: apply_render_range
            }

            RowLayout {

                XsTextField{
                    Layout.preferredWidth: 50
                    Layout.preferredHeight: widgetHeight
                    id: inPoint
                    enabled: apply_render_range
                    validator: IntValidator
                }

                XsText {
                    text: "to"
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    enabled: apply_render_range
                }

                XsTextField{
                    Layout.preferredWidth: 50
                    Layout.preferredHeight: widgetHeight
                    id: outPoint
                    enabled: apply_render_range
                    validator: IntValidator
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 70
                    Layout.leftMargin: 10
                    text: "Use Full"
                    enabled: apply_render_range
                    onClicked: {
                        if (theSessionData.get(itemToRender, "typeRole") == "Timeline") {
                            render_range = theSessionData.getTimelineFullRangeAndLoopRangeAndFPS(itemToRender);
                        }
                        if (render_range.length == 4) {
                            inPoint.text = ""+0
                            outPoint.text = ""+render_range[0]
                        }
                    }
                    toolTip: "Set render range to full timeline range"
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.preferredWidth: 70
                    text: "Use Loop"
                    enabled: apply_render_range
                    onClicked: {
                        if (theSessionData.get(itemToRender, "typeRole") == "Timeline") {
                            render_range = theSessionData.getTimelineFullRangeAndLoopRangeAndFPS(itemToRender);
                        }
                        if (render_range.length == 4) {
                            inPoint.text = ""+render_range[1]
                            outPoint.text = ""+render_range[2]
                        }
                    }
                    toolTip: "Set render range to current playhead loop range"
                }

            }


            XsText {
                text: "Load on Completion"
                Layout.alignment: Qt.AlignRight
            }

            XsCheckBox {

                Layout.alignment: Qt.AlignLeft
                Layout.preferredHeight: widgetHeight
                checked: load_on_completion
                onClicked: {
                    load_on_completion = !load_on_completion
                }
            }

            XsText {
                text: "Include Annotations"
                Layout.alignment: Qt.AlignRight
            }

            XsCheckBox {

                Layout.alignment: Qt.AlignLeft
                Layout.preferredHeight: widgetHeight
                checked: include_annotations == true
                onClicked: {
                    include_annotations = !include_annotations
                }
            }

            VidRenderDlgDivider {
                Layout.preferredHeight: widgetHeight
                Layout.topMargin: 20
                Layout.fillWidth: true
                Layout.columnSpan: 2
                label: "Video Options"
            }

            XsText {
                text: "Output Resolution"
                Layout.alignment: Qt.AlignRight
            }

            XsComboBoxEditable{

                id: output_resolution
                model: resolution_names
                Layout.alignment: Qt.AlignLeft
                Layout.minimumWidth: 200
                Layout.preferredHeight: widgetHeight
                onAccepted: {
                    // note, accepted() is emitted rather often as we have linked
                    // it to onEditingFinished on XsComboBox.
                    const regex = /([0-9]+)x([0-9]+)/;
                    var resmatch = regex.exec(editText)
                    if (resmatch && resmatch.length >= 3) {
                        for (var i = 0; i < resolution_names.length; ++i) {
                            var rf = regex.exec(resolution_names[i])
                            if (rf.length >= 3 && rf[1] == resmatch[1] && rf[2] == resmatch[2]) {
                                // already have this res
                                return
                            }
                        }
                        var v = resolution_options
                        v.push([editText, true])
                        resolution_options = v
                        currentIndex = v.length-2
                    }
                }

                function get_resolution() {

                    // store the selected res in the first element, so we can
                    // 'remember' it
                     var v = resolution_options
                     v[0] = currentText
                     resolution_options = v

                    const regex = /([0-9]+)x([0-9]+)/;
                    var resmatch = regex.exec(currentText)
                    if (resmatch && resmatch.length >= 3) {
                        return ["ivec2", 1, parseInt(resmatch[1]), parseInt(resmatch[2])]
                    }
                    return ["ivec2", 1, 1290, 345]
                }

                onClearButtonPressed: (clearedIndex)=> {
                    if (resolution_options[clearedIndex+1][1]) {
                        var v = resolution_options
                        v.splice(clearedIndex+1,1)
                        resolution_options = v
                        currentIndex = 0
                    }
                }

            }

            XsText {
                text: "Frame Rate"
                Layout.alignment: Qt.AlignRight
                enabled: !isFrameBased
            }

            XsComboBoxEditable{

                id: frame_rate
                model: frame_rates
                Layout.alignment: Qt.AlignLeft
                Layout.minimumWidth: 200
                Layout.preferredHeight: widgetHeight
                enabled: !isFrameBased
                function get_rate() {
                    frame_rate_attr = currentText
                    return parseFloat(currentText)
                }
                onModelChanged: {
                    currentIndex = model.indexOf(frame_rate_attr)
                }
                onCurrentTextChanged: {
                    framerates_match = checkIfFrameRatesMatch()
                }
                onAccepted: {
                    var v = parseFloat(editText)
                    if (v && !isNaN(v)) {
                        for (var i = 0; i < frame_rates.length; ++i) {
                            var vv = parseFloat(frame_rates[i])
                            if (vv == v) {
                                // already have this rate
                                return
                            }
                        }
                        frame_rate_attr = ""+v
                        var frs = frame_rates
                        frs.push(editText)
                        frame_rates = frs
                        currentIndex = frs.length-1
                    }
                }
            }

            XsText {
                text: "Re-speed"
                Layout.alignment: Qt.AlignRight
                visible: !framerates_match
            }

            XsCheckBox {
                id: re_speed
                Layout.alignment: Qt.AlignLeft
                Layout.preferredHeight: widgetHeight
                checked: true
                visible: !framerates_match
                XsToolTip {
                    text: "Check if you want 1:1 mapping of FRAMES from the source media/timeline and the output video. If you have media that is 24fps, for example, and your render frame rate is 48fps then you would check this option if you wanted the result to play at double speed. Not checking it means that the frames will be doubled to maintain the original real-time duration of the media. Note that the re-speed option can result in re-pitching of audio in the output render."
                    visible: re_speed.hovered
                    maxWidth: dialog.width*2/3
                }
            }

            XsText {
                text: "Add Timecode"
                Layout.alignment: Qt.AlignRight
                enabled: !isFrameBased
            }

            RowLayout {
                
                XsCheckBox {
                    id: useTimecode
                    checked: true
                    enabled: !isFrameBased
                }

                XsTextField{
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: widgetHeight
                    id: timecode
                    text: "00:00:00:00"
                    enabled: !isFrameBased && useTimecode.checked
                    font.family: "monospace"
                    onTextChanged: {
                        const re = /[0-9]+\:[0-9]+\:[0-9]+\:[0-9]+$/
                        const m = text.match(re)        
                        if (!m) {
                            background.color = "red"
                        } else {
                            background.color = "transparent"
                        }
                    }
                }

                XsPrimaryButton {
                    Layout.preferredHeight: widgetHeight
                    Layout.leftMargin: 10
                    Layout.preferredWidth: 30
                    text: "Set"
                    onClicked: {
                        timecode.text = currentPlayhead.timecode
                    }
                    toolTip: "Set the timecode from the current on-screen frame."
                    enabled: !isFrameBased && useTimecode.checked
                }

            }

            XsText {
                text: "Video & Audio Encoding Preset"
                Layout.alignment: Qt.AlignRight
                enabled: !isFrameBased
            }

            CodecOptionsBox {

                Layout.alignment: Qt.AlignLeft
                Layout.preferredHeight: widgetHeight
                attr_name: "video_codec_options"
                default_attr_name: "video_codec_default_options"
                id: vid_codec_opts
                enabled: !isFrameBased
            }

            XsText {
                text: "Render Audio"
                Layout.alignment: Qt.AlignRight
            }

            XsCheckBox {

                Layout.alignment: Qt.AlignLeft
                Layout.preferredHeight: widgetHeight
                id: ra
                checked: render_audio
                onClicked: {
                    render_audio = !render_audio
                }
            }

            VidRenderDlgDivider {
                Layout.preferredHeight: widgetHeight
                Layout.topMargin: 20
                Layout.fillWidth: true
                Layout.columnSpan: 2
                label: "Colour Settings"
            }

            XsText {
                text: "OCIO Display"
                Layout.alignment: Qt.AlignRight
            }

            XsAttrComboBox {
                id: ocio_display
                Layout.alignment: Qt.AlignLeft
                Layout.preferredHeight: widgetHeight
                Layout.minimumWidth: 200
                attr_title: "Display"
                attr_model_name: "video_renderer_ocio_opts"
            }

            XsText {
                text: "OCIO View"
                Layout.alignment: Qt.AlignRight
            }

            XsAttrComboBox {
                id: ocio_view
                Layout.alignment: Qt.AlignLeft
                Layout.preferredHeight: widgetHeight
                Layout.minimumWidth: 200
                attr_title: "View"
                attr_model_name: "video_renderer_ocio_opts"
            }

            VidRenderDlgDivider {
                Layout.preferredHeight: widgetHeight
                Layout.topMargin: 20
                Layout.fillWidth: true
                Layout.columnSpan: 2
                label: "Grade"
            }

            XsText {
                text: "Burn In Grade"
                Layout.alignment: Qt.AlignRight
            }

            RowLayout {
                Layout.fillWidth: true

                XsCheckBox {
                    id: bake_grade_box
                    Layout.preferredHeight: widgetHeight
                    checked: bake_grade == true
                    onClicked: {
                        bake_grade = !bake_grade
                    }
                    XsToolTip {
                        text: "On: the xSTUDIO grade layers are applied to the rendered pixels, even if grades are bypassed in the viewer. Off: the render has no xSTUDIO grade, and you can write the grade to files next to it instead."
                        visible: bake_grade_box.hovered
                        maxWidth: dialog.width*2/3
                    }
                }

                XsText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: bake_grade == true ? "The grade is part of the rendered pixels" : "The render is left without the grade"
                    opacity: 0.7
                }
            }

            XsText {
                text: "Grade Files"
                Layout.alignment: Qt.AlignRight
                enabled: bake_grade != true
            }

            RowLayout {
                id: grade_files_row
                Layout.fillWidth: true
                enabled: bake_grade != true

                XsCheckBox {
                    id: sidecar_cdl_box
                    Layout.preferredHeight: widgetHeight
                    checked: sidecar_cdl == true
                    onClicked: {
                        sidecar_cdl = !sidecar_cdl
                    }
                    XsToolTip {
                        text: "When the render has finished, write the grade next to it as an ASC CDL collection (.ccc) with the same name. One correction per grade layer; exposure is folded into the slope. Contrast and masks can't be stored in a CDL."
                        visible: sidecar_cdl_box.hovered
                        maxWidth: dialog.width*2/3
                    }
                }

                XsText {
                    text: "CDL (.ccc)"
                }

                XsCheckBox {
                    id: sidecar_lut_box
                    Layout.leftMargin: 20
                    Layout.preferredHeight: widgetHeight
                    checked: sidecar_lut == true
                    onClicked: {
                        sidecar_lut = !sidecar_lut
                    }
                    XsToolTip {
                        text: "When the render has finished, write the whole grade of each clip next to it as a 3D LUT (.cube). The LUT works on images in the compositing_log colour space of the clip's OCIO config. Masks are left out."
                        visible: sidecar_lut_box.hovered
                        maxWidth: dialog.width*2/3
                    }
                }

                XsText {
                    text: "LUT (.cube)"
                }

                Item {
                    Layout.fillWidth: true
                }
            }

        }

    }

    RowLayout {

        spacing: 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10

        XsSimpleButton {
            id: helpBtn
            Layout.alignment: Qt.AlignLeft
            text: qsTr("Help")
            width: XsStyleSheet.primaryButtonStdWidth*2
            onClicked: {
                var url = studio.docsSectionUrl("docs/user_docs/workflow/video_output.html")
                if (url == "") {
                    dialogHelpers.errorDialogFunc("Error", "Unable to resolve location of user docs.")
                } else if (!helpers.openURL(url)) {
                    dialogHelpers.errorDialogFunc("Error", "Failed to launch webbrowser, this is the link, please manually visit this.<BR><BR>"+url+"<BR><BR>")
                }
            }
            XsToolTip {
                text: "Check for the help page in your open web browser after clicking this button."
                visible: helpBtn.hovered
            }
        }

        Item {
            Layout.fillWidth: true
        }

        XsSimpleButton {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Cancel")
            width: XsStyleSheet.primaryButtonStdWidth*2
            onClicked: {
                dialog.hide()
                dialog.destroy()
            }
        }

        XsSimpleButton {
            Layout.alignment: Qt.AlignRight
            Layout.leftMargin: visible ? 5 : 0
            text: qsTr("Skip")
            width: visible ? XsStyleSheet.primaryButtonStdWidth*2 : 0
            visible: numSelected-currentItem > 1
            onClicked: {
                currentItem = currentItem+1
                if (currentItem == numSelected) {
                    dialog.hide()
                    dialog.destroy()
                }
            }
        }

        XsSimpleButton {
            id: render_all_btn
            Layout.alignment: Qt.AlignRight
            Layout.leftMargin: visible ? 5 : 0
            text: qsTr("Render All (%1)").arg(numSelected-currentItem)
            width: visible ? XsStyleSheet.primaryButtonStdWidth*3 : 0
            visible: clipMode && numSelected-currentItem > 1
            // without {media_name} every clip would write the same file
            enabled: outputFile.text.indexOf("{media_name}") != -1
            onClicked: {
                while (currentItem < numSelected) {
                    submitCurrentItem()
                    currentItem = currentItem+1
                }
                dialog.hide()
                dialog.destroy()
            }
            XsToolTip {
                text: render_all_btn.enabled ? "Render each selected clip to its own file." : "Put {media_name} in the output file name so each clip gets its own file."
                visible: render_all_btn.hovered
                maxWidth: dialog.width*2/3
            }
        }

        XsSimpleButton {
            Layout.alignment: Qt.AlignRight
            Layout.leftMargin: 5
            text: qsTr("Render")
            width: XsStyleSheet.primaryButtonStdWidth*2
            enabled: outputFile.text != ""
            onClicked: {

                submitCurrentItem()
                currentItem = currentItem+1
                if (currentItem == numSelected) {
                    dialog.hide()
                    dialog.destroy()
                }

            }
        }
    }

    function submitCurrentItem() {

        var parentPlaylistUuiid = theSessionData.get(itemToRender, "actorUuidRole")
        var renderName = selectedContainerName
        if (["Subset", "Timeline", "ContactSheet"].includes(selectedContainerType)) {
            parentPlaylistUuiid = theSessionData.get(itemToRender.parent.parent, "actorUuidRole")
            var parentPlaylistName = theSessionData.get(itemToRender.parent.parent, "nameRole")
            renderName = parentPlaylistName + " / " + renderName
        } else if (clipMode) {
            renderName = theSessionData.get(itemToRender, "nameRole") + " / " + renderName
        }

        renderName = renderName + " ("+ selectedContainerType + ")"

        let tc = ""
        if (isFrameBased) {
            tc = firstFrame.text
        } else if (useTimecode.checked) {
            tc = timecode.text
        }

        var cb_args = [
                renderName,
                parentPlaylistUuiid,
                selectedContainerUuid,
                outputFile.text,
                (isFrameBased && render_audio) ? outputAudioFile.text : "",
                output_resolution.get_resolution(),
                inPoint.text != "--" ? parseInt(inPoint.text) : -1,
                outPoint.text != "--" ? parseInt(outPoint.text) : -1,
                frame_rate.get_rate(),
                framerates_match ? false : re_speed.checked,
                vid_codec_opts.get_codec_options()[0],
                vid_codec_opts.get_bit_depth(),
                render_audio ? vid_codec_opts.get_codec_options()[1] : "",
                isFrameBased ? file_extension : vid_codec_opts.get_preset_name(),
                ocio_display.currentText,
                ocio_view.currentText,
                load_on_completion,
                tc,
                exportOptions()
            ]

        render_queue_visible = true

        xstudio_callback(cb_args)
    }

}