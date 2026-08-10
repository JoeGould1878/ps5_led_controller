idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash gamepad_interface nvs_flash
                       INCLUDE_DIRS ".")
