idf_component_register(SRCS "gamepad_interface.c"
                    INCLUDE_DIRS "."
                    REQUIRES bluepad32 nvs_flash bluepad32 btstack esp_driver_gpio)