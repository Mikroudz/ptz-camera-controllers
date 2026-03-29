#ifndef UI_H
#define UI_H
#include "SH1106Wire.h"
#include <Preferences.h>
#include "PTZDevice.h"

enum UIScreenMap
{
    FRONTPAGE = 0,
    SET_PRESET = 1,
    CHANGE_IP = 2,
    SETTINGS = 3,
    CHANGE_CONN_TYPE = 4,
};

template <typename T>
using UiSceneFunction = UIScreenMap (T::*)(uint8_t key, void *data);

typedef struct
{
    uint8_t last_state[4];
    QueueHandle_t queue;
} KeypadState;

enum ConnectionType
{
    PANASONIC_IP = 0,
    SONY_VISCA,
};

struct CameraConfig
{
    uint32_t ip;
    uint16_t port;
    ConnectionType connection_type{PANASONIC_IP};
};

class UI
{
private:
    DeviceHandler *device;
    SH1106Wire display;
    TaskHandle_t polling_updates_task;

    KeypadState keypad_state;
    uint8_t current_scene;
    uint8_t last_scene;

    UiSceneFunction<UI> scenes[5] = {&UI::frontpage, &UI::set_preset, &UI::change_camera_ip, &UI::settings, &UI::change_connection_type};
    Preferences persistent;

    uint8_t current_menu_item_idx{0};
    uint8_t last_menu_item_idx{255};
    uint16_t temp_input_val;
    ConnectionType editing_connection_type;

    // ip menu
    uint8_t ip_number_pos;
    IPAddress editing_ip;
    uint16_t editing_port;
    // preset
    uint8_t preset_speed;
    // Camera stuff
    uint8_t selected_camera_idx{0};
    uint8_t last_camera_idx{255};

    CameraConfig cam_configs[6];
    String _temp;

    ControlData control_data;
    ControlData last_control_data;
    static void task_wrapper(void *pvParameters);
    void run_polling();

public:
    UI(DeviceHandler *p_device);
    ~UI();
    void update(uint8_t key);
    void update_wait_for_key();

    void init();
    void goto_page(uint8_t page_idx);
    void save_camera_configs();
    void load_camera_configs();

    void update_sticK_values(const ControlData &data);

protected:
    UIScreenMap frontpage(uint8_t key, void *data);
    UIScreenMap set_preset(uint8_t key, void *data);
    UIScreenMap settings(uint8_t key, void *data);
    UIScreenMap change_connection_type(uint8_t key, void *data);
    static void gpio_isr_handler(TimerHandle_t xTimer);
    void init_keyboard();
    UIScreenMap change_camera_ip(uint8_t key, void *data);
    uint8_t check_number_press(uint8_t key);
    void draw_cam_rect(int16_t x, int16_t y, const uint8_t cam_id, const bool selected);
    void drawVerticalProgress(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t progress);
};

#endif