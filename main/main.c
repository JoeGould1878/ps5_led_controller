#include <stdio.h>
#include "esp_log.h"
#include "gamepad_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#define CROSS_LED_GPIO 25
#define CIRCLE_LED_GPIO 26
#define SQUARE_LED_GPIO 27
#define TRIANGLE_LED_GPIO 32
static void gpio_init(void){
    gpio_reset_pin(CROSS_LED_GPIO);
    gpio_set_direction(CROSS_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_reset_pin(CIRCLE_LED_GPIO);
    gpio_set_direction(CIRCLE_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_reset_pin(SQUARE_LED_GPIO);
    gpio_set_direction(SQUARE_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_reset_pin(TRIANGLE_LED_GPIO);
    gpio_set_direction(TRIANGLE_LED_GPIO, GPIO_MODE_OUTPUT);
}
void app_main(void)
{
    ESP_LOGI("APP", "Starting PS5 LED Controller (Event Driven)");
    //Initialize NVS (Needed for Bluepad32 to store pairing information)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    //Initialize GPIOs for LED control
    gpio_init();
    //Initialise the gamepad interface, and register the callbacks
    gamepad_interface_init();
}