#include <Arduino.h>
#include "UI.h"

#define KEY_MATRIX_COL_1 GPIO_NUM_7
#define KEY_MATRIX_COL_2 GPIO_NUM_21
#define KEY_MATRIX_COL_3 GPIO_NUM_47

#define KEY_MATRIX_ROW_1 GPIO_NUM_40
#define KEY_MATRIX_ROW_2 GPIO_NUM_41
#define KEY_MATRIX_ROW_3 GPIO_NUM_42
#define KEY_MATRIX_ROW_4 GPIO_NUM_39

void UI::gpio_isr_handler(TimerHandle_t xTimer)
{
    KeypadState *keypad_state = (KeypadState *)pvTimerGetTimerID(xTimer);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static const gpio_num_t row_gpio_num[]{KEY_MATRIX_ROW_1, KEY_MATRIX_ROW_2, KEY_MATRIX_ROW_3, KEY_MATRIX_ROW_4};

    for (int row = 0; row < 4; row++)
    {

        gpio_set_direction(row_gpio_num[row], GPIO_MODE_OUTPUT);
        gpio_set_level(row_gpio_num[row], 0);

        uint8_t current_state = 0;
        if (gpio_get_level(KEY_MATRIX_COL_1) == 0)
            current_state |= 1; // Bit 0
        if (gpio_get_level(KEY_MATRIX_COL_2) == 0)
            current_state |= (1 << 1); // Bit 1
        if (gpio_get_level(KEY_MATRIX_COL_3) == 0)
            current_state |= (1 << 2); // Bit 2
        if (current_state != keypad_state->last_state[row])
        {
            if (current_state != 0)
            {
                // Find which key is pressed.
                for (int col = 0; col < 3; col++)
                {
                    if ((keypad_state->last_state[row] & (1 << col)) == 0 && (current_state & (1 << col)) != 0)
                    {
                        uint8_t key_numb = row * 4 + col;

                        if (xQueueSendFromISR(keypad_state->queue, &key_numb, &xHigherPriorityTaskWoken) != pdTRUE)
                        {
                            // Queue is full.  Handle error (e.g., log, increment an overflow counter).
                            // ESP_LOGE(TAG, "Failed to send to queue from ISR!");
                            // Consider a counter for lost messages.
                        }
                        // ESP_LOGE(TAG, "Key pressed: Row %d, Column %d", row+1, col + 1);
                        // ESP_LOGE(TAG, "Current: %d", current_state);
                    }
                }
            }
            keypad_state->last_state[row] = current_state;
        }

        // Set the row back to input with pull-up
        gpio_set_level(row_gpio_num[row], 1);

        // gpio_set_direction(row_gpio_num[row], GPIO_MODE_INPUT);
    }
}

void UI::init_keyboard()
{

    gpio_config_t io_conf = {};

    // Configure horizontal pins
    io_conf.pin_bit_mask = (1ULL << KEY_MATRIX_ROW_1) | (1ULL << KEY_MATRIX_ROW_2) |
                           (1ULL << KEY_MATRIX_ROW_3) | (1ULL << KEY_MATRIX_ROW_4);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE; // Trigger on LOW level
    gpio_config(&io_conf);

    // Configure vertical pins
    io_conf.pin_bit_mask = (1ULL << KEY_MATRIX_COL_1) | (1ULL << KEY_MATRIX_COL_2) |
                           (1ULL << KEY_MATRIX_COL_3);
    io_conf.mode = GPIO_MODE_INPUT;          // You will change this when scanning.
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Good to keep consistency.
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_POSEDGE; // Trigger on HIGH level
    gpio_config(&io_conf);

    /*KeypadState *keypad_state = (KeypadState *)malloc(sizeof(KeypadState));
    if (keypad_state == NULL)
    {
        return;
    }
    memset(keypad_state, 0, sizeof(KeypadState)); // Initialize the memory*/

    keypad_state.queue = xQueueCreate(10, sizeof(uint8_t));
    auto keypad_timer_handle = xTimerCreate(
        "KeypadTimer",
        pdMS_TO_TICKS(20),
        pdTRUE,
        &keypad_state,
        &UI::gpio_isr_handler);

    xTimerStart(keypad_timer_handle, 0);
}