idf_component_register(
	SRCS "src/papers3.c" "src/epd.c" "src/bmi270.c" "src/gt911.c" "src/touch_processor.c"
	INCLUDE_DIRS "include"
	REQUIRES vroland__epdiy
)
