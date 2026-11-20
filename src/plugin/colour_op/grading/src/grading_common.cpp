// SPDX-License-Identifier: Apache-2.0

#include "xstudio/media_reader/image_buffer.hpp"
#include "xstudio/utility/helpers.hpp"

#include "grading.h"
#include "grading_mask_render_data.h"
#include "grading_mask_gl_renderer.h"
#include "grading_colour_op.hpp"
#include "grading_common.h"
#include "grade_stack.h"

using namespace xstudio;
using namespace xstudio::ui::viewport;


bookmark::BookmarkAndAnnotations xstudio::ui::viewport::get_active_grade_bookmarks(
    const xstudio::media_reader::ImageBufPtr &image) {

    bookmark::BookmarkAndAnnotations grade_bookmarks;

    // Make sure the bookmarks are applied in a consistent order.
    // The order matters when applying multiple CDLs.
    // Layers from a colour profile can sit below or above the shot's own
    // layers, otherwise layers keep their created time order.
    bookmark::BookmarkAndAnnotations all_bookmarks = image.bookmarks();
    std::stable_sort(all_bookmarks.begin(), all_bookmarks.end(), [](auto &a, auto &b) {
        return grade_stack::layer_less(a->detail_, b->detail_);
    });

    for (auto &bookmark : all_bookmarks) {
        if (bookmark->detail_.user_type_.value_or("") == "Grading") {
            auto grading_data = dynamic_cast<const GradingData *>(bookmark->annotation_.get());
            if (grading_data) {
                grade_bookmarks.push_back(bookmark);
            }
        }
    }

    return grade_bookmarks;
}

std::vector<GradingInfo>
xstudio::ui::viewport::get_active_grades(const xstudio::media_reader::ImageBufPtr &image) {

    std::vector<GradingInfo> active_grades;

    // Make sure the bookmarks are applied in a consistent order.
    // The order matters when applying multiple CDLs.
    auto bookmarks = image.bookmarks();
    std::stable_sort(bookmarks.begin(), bookmarks.end(), [](auto &a, auto &b) {
        return grade_stack::layer_less(a->detail_, b->detail_);
    });

    for (auto &bookmark : bookmarks) {
        if (bookmark->detail_.user_type_.value_or("") == "Grading") {
            auto grading_data = dynamic_cast<const GradingData *>(bookmark->annotation_.get());
            if (grading_data) {
                auto json_data    = bookmark->detail_.user_data_.value_or(utility::JsonStore());
                bool grade_active = json_data.get_or("grade_active", true);
                active_grades.push_back({
                    grading_data,
                    grade_active,
                });
            }
        }
    }

    // Currently edited grade take precedence over image's bookmark, this improves
    // interactivity when drawing a mask. Simply update the vector in-place or append
    // if the grade was not saved in a bookmark yet.
    utility::BlindDataObjectPtr blind_data =
        image.plugin_blind_data(utility::Uuid(colour_pipeline::GradingTool::PLUGIN_UUID));
    if (blind_data) {
        auto render_data = dynamic_cast<const GradingMaskRenderData *>(blind_data.get());

        if (render_data) {
            auto blind_uuid = render_data->interaction_grading_data_.bookmark_uuid_;

            if (!active_grades.empty()) {
                for (auto &grade_info : active_grades) {
                    if (grade_info.data->bookmark_uuid_ == blind_uuid) {
                        grade_info.data = &(render_data->interaction_grading_data_);
                        break;
                    }
                }
            } else {
                active_grades.push_back({
                    &(render_data->interaction_grading_data_),
                    true,
                });
            }
        }
    }

    return active_grades;
}