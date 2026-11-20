include(CTest)

SET(LINK_DEPS
	CAF::core
	xstudio::colour_pipeline::grading
)

create_tests("${LINK_DEPS}")

# The tests use the plugin's own headers (grade_stack.h, grading_data.h ...)
file(GLOB GRADING_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
foreach(GRADING_TEST ${GRADING_TEST_SOURCES})
	get_filename_component(GRADING_TEST_NAME ${GRADING_TEST} NAME_WE)
	target_include_directories(${GRADING_TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
endforeach()
