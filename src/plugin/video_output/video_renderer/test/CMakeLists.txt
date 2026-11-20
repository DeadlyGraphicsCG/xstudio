include(CTest)

find_package(OpenColorIO CONFIG)

SET(LINK_DEPS
	OpenColorIO::OpenColorIO
)

create_tests("${LINK_DEPS}")

# The logic under test is plain C++ (no actors), so we build its sources
# straight into the test instead of loading the plugin.
target_sources(video_render_export_test
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../src/export_profiles.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../src/grade_sidecar.cpp
)
target_include_directories(video_render_export_test
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(video_render_export_test
	PRIVATE
		VIDEO_RENDER_PREFS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../src/video_render_prefs.json"
)
