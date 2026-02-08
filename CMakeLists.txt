# Initialize Build Environment
cmake_minimum_required(VERSION 3.18.4)

project(steg-png
	DESCRIPTION "Embed and obfuscate data in Portable Network Graphics."
	HOMEPAGE_URL "https://github.com/brandon1024/steg-png"
	LANGUAGES C
)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -ggdb")

# Load Project Dependencies
find_package(ZLIB REQUIRED)

# Configure Build Targets
set(STEG_PNG_BUILD_DEFINITIONS -D_DEFAULT_SOURCE)

set(STEG_PNG_SOURCE_FILES
	src/builtin src/main.c src/md5.c src/parse-options.c src/png-chunk-processor.c
	src/str-array.c src/strbuf.c src/utils.c src/builtin/embed.c src/builtin/extract.c
	src/builtin/inspect.c
)
add_executable(steg-png ${STEG_PNG_SOURCE_FILES})
target_compile_definitions(steg-png PUBLIC ${STEG_PNG_BUILD_DEFINITIONS})
target_include_directories(steg-png PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(steg-png PROPERTIES DEBUG_POSTFIX ".debug")

# Link Dependencies
target_link_libraries(steg-png ZLIB::ZLIB)

# Configure Install Targets
install(TARGETS steg-png RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Include Test Subdirectory
enable_testing()
add_subdirectory(${PROJECT_SOURCE_DIR}/test)
