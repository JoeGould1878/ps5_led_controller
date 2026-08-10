#include <stdio.h> //NOTE: move NVS init to main.c, from gamepad_interface_init() (When you can be assed, still runs just cant call again)
#include <string.h>
#include "esp_log.h"
#include "gamepad_interface.h"
#include <uni.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include "driver/gpio.h"
#define CROSS_LED_GPIO 25
#define CIRCLE_LED_GPIO 26
#define SQUARE_LED_GPIO 27
#define TRIANGLE_LED_GPIO 32
static const char* TAG = "gamepad_interface";
static uint16_t current_button_state = 0; //global variable to store the current button state as a bitmask
//uses bluepad32 to interface with the gamepad and get button states
/*1. Callback functions for Bluepad32:
    -my_platform_init(int argc, const char** argv) Stub function for core initialisation
    my_platform_on_init_complete(void)
    - executes when BT radio is ready, must call uni_bt_enable_new_connections_unsafe(true)
    to allow esp32 to scan for controller
    my_platform_on_device_connected(uni_hid_device_t* dev)
        -Executes upon initial connection establishment, no action required, used for logging
    my_platform_on_device_disconnected(uni_hid_device_t* dev)
        -Executes upon disconnection, Should reset current_button_state =0; to ensure LEDs turn off when controller is disconnected
    my_platform_on_device_ready(uni_hid_device_t* dev)
        -Executes when HID handshake completes, must return UNI_ERROR_SUCCESS to allow Bluepad32 to continue processing the device,
         if not returned, the device will be disconnected
    my_platform_on_controller_data(uni_hid_device_t* dev, uni_controller_t* data)
        -The primary data handler, fires multiple times per second,
        Acessess ctl->gamepad.buttons struct member extract 16bit mask and assign it to current_button_state variable,
         which is used to control the LED state. Logging of button state & control of LEDS done here to reduce clutter in main 

2. Platform Configuration: above callbacks are packaged into struct that Bluepad32 can parse:
get_my_platform(void), static func that returns a pointer to a populated struct uni_platform 
with the above callbacks assigned to corresponding function pointers within this struct,
this struct is passed into uni_platform_set_custom(get_my_platform()) to register the callbacks with Bluepad32
3. BT stack requires permanent blocking loop to process incoming data, it must be isolated from the main application thread:
 -done with bt_task_runner(void*) that calls btstack_run_loop_execute(), it is created as a task in gamepad_interface_init() and never returns
 
 4. Public API implementation: 
    -gamepad_interface_init():
        Executes initialisation, calls btstack_init(), uni_platform_set_custom(get_my_platform()), uni_init(0, NULL)
         and must call xTaskCreate(bt_task_runner, "bt_task_runner", 4096, NULL, 5, NULL) to start the BT stack processing loop
    - gamepad_get_button_mask():
        simple getter function that returns the value of current_button_state variable, allowing main to read state safely,
        stored as bitmask, each bit represents a button on the controller, 1=pressed, 0=not pressed
        from LSB to MSB: X, Circle, Square, Triangle */
/*Following Block from Bluepad32 Examples/my_platform
 URL: https://github.com/ricardoquesada/bluepad32/blob/main/examples/esp32/main/my_platform.c 
 */
typedef struct my_platform_instance_s{
    uni_gamepad_seat_t gamepad_seat; //seat assignment for the controller
} my_platform_instance_t;
//
//Helpers
static my_platform_instance_t* get_my_platform_instance(uni_hid_device_t* dev){
    return (my_platform_instance_t*)&dev->platform_data[0];
}

static void trigger_event_on_gamepad(uni_hid_device_t* dev){
    my_platform_instance_t* ins= get_my_platform_instance(dev);
    if(dev->report_parser.play_dual_rumble != NULL){
        dev->report_parser.play_dual_rumble(dev, 0, 150, 128, 40);
    }
    if(dev->report_parser.set_player_leds != NULL){
        dev->report_parser.set_player_leds(dev, ins->gamepad_seat);
    }
    if(dev->report_parser.set_lightbar_color != NULL){
        uint8_t red = (ins->gamepad_seat & 0x01) ? 0xff : 0;
        uint8_t green = (ins->gamepad_seat & 0x02) ? 0xff : 0;
        uint8_t blue = (ins->gamepad_seat & 0x04) ? 0xff : 0;
        dev->report_parser.set_lightbar_color(dev, red, green, blue);
    }
}
//Platform Callbacks
static void my_platform_init(int argc, const char** argv){
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    ESP_LOGI(TAG, "my_platform_init() called");
}
static void my_platform_on_init_complete(void){
    ESP_LOGI(TAG, "my_platform_on_init_complete() called");
    //start scanning for controllers
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
    //print saved controllers to serial monitor
    uni_bt_list_keys_unsafe();
}
static void my_platform_on_device_connected(uni_hid_device_t* dev){
    ESP_LOGI(TAG, "my_platform_on_device_connected() called for device: %p \n", dev);
}
static void my_platform_on_device_disconnected(uni_hid_device_t* dev){
    ESP_LOGI(TAG, "my_platform_on_device_disconnected() called for device: %p \n", dev);
    current_button_state = 0; //reset button state to 0 when controller is disconnected
}
static uni_error_t my_platform_on_device_ready(uni_hid_device_t* dev){
    ESP_LOGI(TAG, "my_platform_on_device_ready() called for device: %p \n", dev);
    my_platform_instance_t* ins= get_my_platform_instance(dev);
    ins->gamepad_seat = GAMEPAD_SEAT_A; //assign seat A to the controller, this is required for Bluepad32 to function properly
    return UNI_ERROR_SUCCESS; //must return success to allow Bluepad32 to continue processing the device
}
static uni_error_t my_platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi){
    ESP_LOGI(TAG, "my_platform_on_device_discovered() called for device: %s \n", name);
    return UNI_ERROR_SUCCESS;
}
static void my_platform_on_oob_event(uni_platform_oob_event_t event, void* data){
    switch(event){
        case UNI_PLATFORM_OOB_GAMEPAD_SYSTEM_BUTTON: {
            uni_hid_device_t* dev = (uni_hid_device_t*)data;
            if(dev==NULL){
                ESP_LOGE(TAG, "my_platform_on_oob_event() called with NULL device pointer");
                return;
            }
            ESP_LOGI(TAG, "my_platform_on_oob_event() called for device: %p\n on event: %d\n", dev, event);

            my_platform_instance_t* ins= get_my_platform_instance(dev);
            ins->gamepad_seat = ins->gamepad_seat == GAMEPAD_SEAT_A ? GAMEPAD_SEAT_B : GAMEPAD_SEAT_A;

            trigger_event_on_gamepad(dev);
            break;
        }
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED: 
            ESP_LOGI(TAG, "Bluetooth enabled: %d\n", (bool)(data));
            break;
        default:
            ESP_LOGI(TAG, "my_platform_on_oob_event() unsupported event: 0x%04x\n", event);
            break;
    }
}

static const uni_property_t* my_platform_get_property(uni_property_idx_t idx){
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_controller_data(uni_hid_device_t* dev, uni_controller_t* ctl){
    //Remember states (FSM and LED memory)
    static uint16_t prev_button_state = 0;
    static bool is_toggle_mode = false; //toggle mode state, true=toggle mode on, false=toggle mode off, changed by R1
    static bool cross_state = false;
    static bool circle_state = false;
    static bool square_state = false;
    static bool triangle_state = false;
    //check if data is valid controller data
    if(ctl->klass == UNI_CONTROLLER_CLASS_GAMEPAD){
        //ignore analog stick noise
        if(prev_button_state == current_button_state){
            return;
        }
        //==================
        //FSM MODE SWITCHING LOGIC (R1 button)
        //==================
        //check for rising edge on R1 button to toggle between toggle mode and hold mode
        if((current_button_state & 0x0020) && !(prev_button_state & 0x0020)){
            is_toggle_mode = !is_toggle_mode; //toggle mode state
            if(is_toggle_mode){
                ESP_LOGI(TAG, "Toggle mode ON");
            } else {
                ESP_LOGI(TAG, "Toggle mode OFF");
            }
            //reset LEDS when switching modes
            gpio_set_level(CROSS_LED_GPIO, 0);
            gpio_set_level(CIRCLE_LED_GPIO, 0);
            gpio_set_level(SQUARE_LED_GPIO, 0);
            gpio_set_level(TRIANGLE_LED_GPIO, 0);
            cross_state = false;
            circle_state = false;
            square_state = false;
            triangle_state = false;
        }
        //==================
        //LED CONTROL LOGIC (State Machine)
        //==================
        if(is_toggle_mode){
            //MODE 1: Toggle mode, button press toggles LED state
            //Use Edge detection so it only toggles on button press, not when held down
            if((current_button_state & 0x0001) && !(prev_button_state & 0x0001)){ //X button
                cross_state = !cross_state; //flip cross bit
                gpio_set_level(CROSS_LED_GPIO, cross_state);
        }
        if((current_button_state & 0x0002) && !(prev_button_state & 0x0002)){ //Circle button
                circle_state = !circle_state; //flip circle bit
                gpio_set_level(CIRCLE_LED_GPIO, circle_state);
        }
        if((current_button_state & 0x0004) && !(prev_button_state & 0x0004)){ //Square button
                square_state = !square_state; //flip square bit
                gpio_set_level(SQUARE_LED_GPIO, square_state);  
        }
        if((current_button_state & 0x0008) && !(prev_button_state & 0x0008)){ //Triangle button
                triangle_state = !triangle_state; //flip triangle bit
                gpio_set_level(TRIANGLE_LED_GPIO, triangle_state);
        }
        } else {
            //MODE 2: Hold mode, LED is on only when button is held down
            gpio_set_level(CROSS_LED_GPIO, (current_button_state & 0x0001) ? 1 : 0);
            gpio_set_level(CIRCLE_LED_GPIO, (current_button_state & 0x0002) ? 1 : 0);
            gpio_set_level(SQUARE_LED_GPIO, (current_button_state & 0x0004) ? 1 : 0);
            gpio_set_level(TRIANGLE_LED_GPIO, (current_button_state & 0x0008) ? 1 : 0);
        } 
        //Save current button state for next iterations edge detection
        prev_button_state = current_button_state;
}
//Entry point for Bluepad32 to register the platform callbacks, returns a pointer to a populated struct uni_platform
static struct uni_platform* get_my_platform(void){
    static struct uni_platform platform = {
        .name = "PS5_Platform",
        .init = my_platform_init,
        .on_init_complete = my_platform_on_init_complete,
        .on_device_discovered = my_platform_on_device_discovered,
        .on_device_connected = my_platform_on_device_connected,
        .on_device_disconnected = my_platform_on_device_disconnected,
        .on_device_ready = my_platform_on_device_ready,
        .on_oob_event = my_platform_on_oob_event,
        .on_controller_data = my_platform_on_controller_data,
        .get_property = my_platform_get_property,
    };
    return &platform;
}

//END OF REFERENCED CODE
/* Static FreeRTOS Task function that calls btstack_run_loop_execute
because the platform needs to handle Bluetooth events in a separate task 
and this function never returns, so needs to be task to avoid CPU blocking */
static void bt_task_runner(void* arg){
    ARG_UNUSED(arg);
    ESP_LOGI(TAG, "bt_task_runner() started");
    btstack_run_loop_execute();
}
/*Public API function to initialise the gamepad interface, must be called before any other functions in this module,
 sets up the Bluepad32 platform and starts the BT stack processing loop in a separate task*/
 void gamepad_interface_init(void){
    //NVS init moved to main.c, from here, because it can only be called once per application, and this function is called multiple times during development
    //initialise BT memory and hardware
    btstack_init();
    //Set custom platform
    uni_platform_set_custom(get_my_platform());
    //Initialise bluepad32 core logi
    uni_init(0, NULL);
    ESP_LOGI(TAG, "gamepad_interface_init() completed");
    //Start BT event loop, this blocks indefinetly, listening for controller inputs
    //outputs are purely event driven, so no need for a polling loop in main.c
    btstack_run_loop_execute();
}
/*Public API function to get the current button state as a bitmask, each bit represents a button on the controller, 1=pressed, 0=not pressed
 from LSB to MSB: X, Circle, Square, Triangle */
uint16_t gamepad_get_button_mask(void){
    return current_button_state;
}