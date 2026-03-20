cmake_minimum_required(VERSION 3.16)

if("$ENV{IDF_TARGET}" STREQUAL "linux" OR "${IDF_TARGET}" STREQUAL "linux")
	list(PREPEND EXTRA_COMPONENT_DIRS "components/sim")
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all   # treat all UB as fatal, don't continue
        -g3
    )
    add_link_options(-fsanitize=address,undefined)
else()
	list(PREPEND EXTRA_COMPONENT_DIRS "components/device")
endif()

list(APPEND EXTRA_COMPONENT_DIRS "components/common")

set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(instaink)
