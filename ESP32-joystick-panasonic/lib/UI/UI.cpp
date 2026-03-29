#include "UI.h"
#include "keyboard.h"
#include "esp_log.h"

static const char *TAG = "ui";

enum KEY_MAPPING
{
    F1 = 12,
    F2 = 13,
    F3 = 14,
    F4 = 0,
    F5 = 1,
    F6 = 2,
    F7 = 4,
    F8 = 5,
    F9 = 6,
    F10 = 8,
    F11 = 9,
    F12 = 10,
};

static const uint8_t key_num_map[] = {F10, F1, F2, F3, F4, F5, F6, F7, F8, F9};

UI::UI(DeviceHandler *p_device) : device(p_device), display(0x3c, 9, 8), persistent(Preferences())
{
    current_scene = 0;
    _temp.reserve(256);
}

UI::~UI()
{
}

void UI::init()
{

    load_camera_configs();
    display.init();
    display.flipScreenVertically();
    display.drawString(0, 0, "Starting....");
    display.display();
    init_keyboard();
    xTaskCreate(task_wrapper, "ui_polling_task", configMINIMAL_STACK_SIZE * 4, (void *)this, tskIDLE_PRIORITY + 1, &polling_updates_task);
}

void UI::goto_page(uint8_t page_idx)
{
    xQueueSendToBack(keypad_state.queue, &page_idx, pdMS_TO_TICKS(10));
}

void UI::load_camera_configs()
{
    persistent.begin("ui", false);
    persistent.getBytes("config", &cam_configs, sizeof(cam_configs));
    persistent.end();
}

void UI::save_camera_configs()
{
    persistent.begin("ui", false);
    persistent.putBytes("config", &cam_configs, sizeof(cam_configs));
    persistent.end();
}

UIScreenMap UI::settings(uint8_t key, void *data)
{
    display.drawString(0, 0, "F1: Edit camera address");
    display.drawString(0, 16, "F2: Change connection type");
    display.drawString(0, 24, "F3: Create preset");
    display.drawString(0, 32, "F9: Back");

    switch (key)
    {
    case F1:
        return CHANGE_IP;
        break;
    case F2:
        return CHANGE_CONN_TYPE;
        break;
    case F3:
        return SET_PRESET;
        break;
    case F9:
        return FRONTPAGE; // cancel
        break;
    default:
        return SETTINGS;
        break;
    }
}

UIScreenMap UI::change_connection_type(uint8_t key, void *data)
{
    if (current_scene != last_scene)
    {
        if (cam_configs[selected_camera_idx].connection_type == SONY_VISCA || cam_configs[selected_camera_idx].connection_type == PANASONIC_IP)
        {
            editing_connection_type = cam_configs[selected_camera_idx].connection_type;
        }
        else
        {
            editing_connection_type = PANASONIC_IP;
        }
    }

    display.drawString(0, 0, "Change cam " + String(selected_camera_idx + 1) + " connection");

    display.drawString(0, 32, "F8: Save\nF9: Back");
    if (current_scene == last_scene)
    {
        switch (key)
        {
        case F1:
            editing_connection_type = PANASONIC_IP;
            break;
        case F2:
            editing_connection_type = SONY_VISCA;
            break;
        case F8: // save
            cam_configs[selected_camera_idx].connection_type = editing_connection_type;
            save_camera_configs();
            device->set_impl_index(editing_connection_type);
            display.clear();
            display.drawString(0, 16, "Connection type saved");
            display.display();
            vTaskDelay(pdTICKS_TO_MS(1000));
            return FRONTPAGE;
            break;
        case F9:
            return FRONTPAGE; // cancel
            break;
        default:
            return CHANGE_CONN_TYPE;
            break;
        }
    }
    if (editing_connection_type == SONY_VISCA)
    {
        display.drawString(0, 16, "F1:  Panasonic TCP/HTTP");
        display.drawString(0, 24, "F2: [Sony Visca UDP]");
    }
    else if (editing_connection_type == PANASONIC_IP)
    {
        display.drawString(0, 16, "F1: [Panasonic TCP/HTTP]");
        display.drawString(0, 24, "F2:  Sony Visca UDP");
    }
    return CHANGE_CONN_TYPE;
}

uint8_t UI::check_number_press(uint8_t key)
{
    if (current_scene == last_scene)
    {
        for (int i = 0; i < sizeof(key_num_map); i++)
        {
            if (key_num_map[i] == key)
            {
                return i;
            }
        }
    }
    return 255;
}

UIScreenMap UI::change_camera_ip(uint8_t key, void *data)
{
    // just switched to scene
    if (current_scene != last_scene)
    {
        editing_ip = cam_configs[selected_camera_idx].ip;
        editing_port = cam_configs[selected_camera_idx].port;
        current_menu_item_idx = 3; // last pos
        temp_input_val = 0;
    }
    display.drawString(0, 0, "Edit IP");
    display.drawString(0, 24, "F11 move left, F12 right");

    uint8_t num_key_press = check_number_press(key);

    if (num_key_press != 255)
    {

        temp_input_val *= 10;
        temp_input_val += num_key_press;
        ESP_LOGI(TAG, "press:%d, menu: %d", temp_input_val, current_menu_item_idx);
        ESP_LOGI(TAG, "%s", String(editing_ip));

        if (current_menu_item_idx < 4)
            editing_ip[current_menu_item_idx] = temp_input_val;
        else if (current_menu_item_idx == 4)
            editing_port = temp_input_val;
    }
    if (current_scene == last_scene)
    {
        switch (key)
        {
        case F9: // select
            if (current_menu_item_idx == 5)
                return FRONTPAGE; // Cancel
            else if (current_menu_item_idx == 6)
            {
                cam_configs[selected_camera_idx].ip = editing_ip;
                cam_configs[selected_camera_idx].port = editing_port;
                save_camera_configs();
                device->change_camera_ip(cam_configs[selected_camera_idx].ip, cam_configs[selected_camera_idx].port);
                return FRONTPAGE;
            }
            break;
        case F11:
            if (current_menu_item_idx > 0)
            {
                temp_input_val = 0;
                current_menu_item_idx--;
            }
            break;
        case F12:
            if (current_menu_item_idx < 7)
            {
                temp_input_val = 0;
                current_menu_item_idx++;
            }
            break;
        default:
            break;
        }
    }
    if (current_menu_item_idx == 5)
        display.drawString(0, 32, "Select: F9\n[Cancel] Save");
    else if (current_menu_item_idx == 6)
        display.drawString(0, 32, "Select: F9\n Cancel [Save]");
    else
        display.drawString(0, 48, " Cancel  Save");

    String ip_str = editing_ip.toString();
    int dotPositions[5];
    dotPositions[0] = -1; // Start before the first character
    dotPositions[1] = ip_str.indexOf('.');
    dotPositions[2] = ip_str.indexOf('.', dotPositions[1] + 1);
    dotPositions[3] = ip_str.indexOf('.', dotPositions[2] + 1);

    dotPositions[4] = ip_str.length();
    _temp.clear();
    for (int i = 0; i < 4; i++)
    {
        int start = dotPositions[i] + 1; // starting index
        int end = dotPositions[i + 1];   // ending index
        String part = ip_str.substring(start, end);
        if (i == current_menu_item_idx)
        {
            _temp += "[" + part + "]";
        }
        else
        {
            _temp += part;
        }
        if (i < 3)
        {
            _temp += "."; // Add a dot except after the last part
        }
    }
    _temp += ":";
    if (current_menu_item_idx == 4)
    {
        _temp += "[" + String(editing_port) + "]";
    }
    else
    {
        _temp += String(editing_port);
    }
    display.drawString(0, 12, _temp);
    return CHANGE_IP; // this menu
}

UIScreenMap UI::set_preset(uint8_t key, void *data)
{
    if (current_scene != last_scene)
    {
        preset_speed = 8;
    }
    display.drawString(0, 0, "Press F10-F12 to save\ncurrent position as a preset");
    display.drawString(0, 32, "F7 to cancel");
    uint8_t preset_loc = 255;
    if (current_scene == last_scene)
    {
        switch (key)
        {
        case F7:
            return FRONTPAGE; // Cancel
            break;
        case F2:
            if (preset_speed < 8)
            {
                preset_speed++;
            }
            break;
        case F8:
            if (preset_speed > 1)
            {
                preset_speed--;
            }
            break;
        case F10:
            preset_loc = 0;
            break;
        case F11:
            preset_loc = 1;
            break;
        case F12:
            preset_loc = 2;
            break;
        default:
            break;
        }
    }
    if (preset_loc != 255)
    {
        device->save_preset(preset_loc, preset_speed);
        display.clear();
        display.drawString(0, 16, "Location preset saved to F" + (preset_loc + 10));
        display.display();
        vTaskDelay(pdTICKS_TO_MS(1000));
        return FRONTPAGE;
    }
    String tmp = "Speed: ";
    tmp += preset_speed;
    display.drawString(0, 42, tmp);

    return SET_PRESET; // preset saver
}

void UI::drawVerticalProgress(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t progress)
{
    display.drawRect(x, y, width, height);
    uint8_t progY = map(progress, 0, 255, 0, height);
    display.fillRect(x + 1, y + (height - progY), width - 2, progY);
}

UIScreenMap UI::frontpage(uint8_t key, void *data)
{
    display.drawString(0, 0, "F9 settings\nF10-F12 presets");
    drawVerticalProgress(78, 0, 6, 32, control_data.speed);
    drawVerticalProgress(88, 0, 6, 32, control_data.pan);
    drawVerticalProgress(98, 0, 6, 32, control_data.tilt);
    drawVerticalProgress(108, 0, 6, 32, control_data.zoom);

    switch (key)
    {
    case F1:
        selected_camera_idx = 0;
        break;
    case F5: // F5
        selected_camera_idx = 4;
        break;
    case F6: // F6
        selected_camera_idx = 5;
        break;
    case F2: // F2
        selected_camera_idx = 1;
        break;
    case F3: // F3
        selected_camera_idx = 2;
        break;
    case F4: // F4
        selected_camera_idx = 3;
        break;
    case F9: // F8
        return SETTINGS;
        break;
    case F10: // preset 1
        device->run_preset(0);
        break;
    case F11: // preset 2
        device->run_preset(1);
        break;
    case F12: // preset 3
        device->run_preset(2);
        break;
    default:
        break;
    }
    for (int i = 0; i < 6; i++)
    {
        draw_cam_rect(i * 21, 43, i + 1, i == selected_camera_idx);
    }
    display.drawString(0, 31, "IP: " + IPAddress(cam_configs[selected_camera_idx].ip).toString() + ":" + String(cam_configs[selected_camera_idx].port));
    if (last_camera_idx != selected_camera_idx)
    {
        device->detach_camera();
        device->set_impl_index(cam_configs[selected_camera_idx].connection_type);
        device->change_camera_ip(cam_configs[selected_camera_idx].ip, cam_configs[selected_camera_idx].port);
        last_camera_idx = selected_camera_idx;
    }

    return FRONTPAGE;
}

void UI::update_wait_for_key()
{
    uint8_t key;
    if (xQueueReceive(keypad_state.queue, &key, portMAX_DELAY) == pdTRUE)
    {
        update(key);
        ESP_LOGE(TAG, "Key: %d", key);
    }

    delay(10);
}

void UI::update(uint8_t key)
{
    display.clear();
    // call current scene function
    current_scene = (this->*scenes[current_scene])(key, 0);
    // if scene changes call again
    if (last_scene != current_scene)
    {
        display.clear();
        // If scene changes, pass 255 as key (does nothing)
        (this->*scenes[current_scene])(255, 0);
        last_scene = current_scene;
    }
    display.display();
    // ESP_LOGE(TAG, "Key: %d", key);
}

// x-y is left top corner
void UI::draw_cam_rect(int16_t x, int16_t y, const uint8_t cam_id, const bool selected)
{
    if (selected)
    {
        display.fillRect(x, y, 21, 21);
        display.setColor(BLACK);
    }
    else
    {
        display.drawRect(x, y, 21, 21);
    }
    display.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
    display.drawString(x + 10, y + 6, "CAM");
    display.drawString(x + 10, y + 15, String(cam_id));

    display.setColor(WHITE);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
}

void UI::update_sticK_values(const ControlData &data)
{
    control_data = data;
}

void UI::run_polling()
{
    while (true)
    {
        if (control_data != last_control_data)
        {
            update(255);
            last_control_data = control_data;
        }
        vTaskDelay(pdTICKS_TO_MS(100));
    }
}

void UI::task_wrapper(void *pvParameters)
{
    UI *instance = static_cast<UI *>(pvParameters);
    instance->run_polling();
}