#ifndef GAMEPAD_INTERFACE_H
#define GAMEPAD_INTERFACE_H
#include <stdint.h>
/**
 * @brief Initialise the Bluepad32 Bluetooth stack and custom platform callbacks
* Spawns a FreeRTOS task to run the BT event loop.
 * @note This function must be called before any other functions in this module

 */
void gamepad_interface_init(void);

/**
 * @brief Get the current button state as a bitmask
 * @return uint16_t The button mask
 */
uint16_t gamepad_get_button_mask(void);

#endif // GAMEPAD_INTERFACE_H