file(GLOB_RECURSE TEST_LIST FOLLOW_SYMLINKS ${PROJECT_SOURCE_DIR}/test/*.sh)

find_program(BASH_INTERPRETER bash)

file(COPY ${PROJECT_SOURCE_DIR}/test/resources DESTINATION ${PROJECT_BINARY_DIR}/test)

foreach(test_executable ${TEST_LIST})
	get_filename_component(FILENAME ${test_executable} NAME)
	configure_file(
			"${test_executable}"
			"${PROJECT_BINARY_DIR}/test/${FILENAME}"
			COPYONLY
	)

	add_test(NAME "${FILENAME}" COMMAND ${BASH_INTERPRETER} ${PROJECT_BINARY_DIR}/test/${FILENAME})
endforeach()
