if("${IDF_TARGET}" STREQUAL "esp32s3")
	idf_component_register(
		SRCS "src/papers3.c"
		INCLUDE_DIRS "include"
		REQUIRES vroland__epdiy papers3
	)
elseif("${IDF_TARGET}" STREQUAL "linux")
	idf_component_register(
		SRCS "src/linux.c"
		INCLUDE_DIRS "include"
	)
	target_link_libraries(${COMPONENT_LIB} PRIVATE
        GL
        dl
        X11
        Xi
        Xcursor
        m
    )
endif()
