set(DEPS
	"spi_flash"
)

if("$ENV{IDF_TARGET}" STREQUAL "linux" OR "${IDF_TARGET}" STREQUAL "linux")
else()
	list(APPEND DEPS "papers3")
endif()

idf_component_register(
	SRCS "instaink.c"
	REQUIRES ${DEPS}
)
