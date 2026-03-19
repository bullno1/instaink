if("${IDF_TARGET}" STREQUAL "esp32s3")
	idf_component_register(
		SRCS "src/papers3.c"
		INCLUDE_DIRS "include"
		REQUIRES vroland__epdiy papers3
	)
endif()
