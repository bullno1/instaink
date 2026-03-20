idf_component_register(
	SRCS
		"main.c"
		"draw.c"
		"txt.c"
	REQUIRES "ii_hal" "clay"
	EMBED_FILES
		"assets/CourierPrime-Bold.ttf"
)
