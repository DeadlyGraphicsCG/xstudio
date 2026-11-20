include(CTest)

SET(LINK_DEPS
	CAF::core
	xstudio::colour_pipeline::grading
)

create_tests("${LINK_DEPS}")

# The tests use the plugin's own (private) headers
foreach(TEST_NAME grading_data_test grading_working_space_test)
	target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
endforeach()
