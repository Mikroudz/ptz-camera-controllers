#include "ViscaSender.h"
#include "esp_log.h"

constexpr uint8_t TYPE_COMMAND[]{0x01, 0x00};

static const char *TAG = "sender";

#define VISCA_BROADCAST_PORT 52380

const char *visca_inquiry_network_broadcast{"\x002ENQ:network\x0ff"};

ViscaSender::ViscaSender() : command_queue(xQueueCreate(10, sizeof(ViscaCommand_t))), seq_num{0}
{
}

ViscaSender::~ViscaSender()
{
}

void ViscaSender::_broadcast_inquiry_timer_task(TimerHandle_t xTimer)
{
    AsyncUDP *udp = (AsyncUDP *)pvTimerGetTimerID(xTimer);
    ESP_LOGE(TAG, "Sending inquiry");
    udp->writeTo((uint8_t *)visca_inquiry_network_broadcast, sizeof(visca_inquiry_network_broadcast), IPAddress(192, 168, 1, 255), VISCA_BROADCAST_PORT);
}

void ViscaSender::start_listen_broadcast()
{
    if (udp.listen(VISCA_BROADCAST_PORT))
    {
        udp.onPacket([this](AsyncUDPPacket packet)
                     { ESP_LOG_BUFFER_CHAR_LEVEL(TAG, packet.data(), packet.length(),
                                                 ESP_LOG_ERROR); });
        // alive task
        auto alive_task_timer_handle = xTimerCreate(
            "InquiryTask",
            pdMS_TO_TICKS(5000),
            pdTRUE,
            &this->udp,
            &ViscaSender::_broadcast_inquiry_timer_task);

        xTimerStart(alive_task_timer_handle, 0);
        ESP_LOGE(TAG, "Started visca inquiry service");
    }
    else
    {
        ESP_LOGE(TAG, "Cannot start inquiry service!");
    }
}

void ViscaSender::_write(const uint8_t *data, size_t len)
{
    udp.writeTo(data, len, _camera_ip, _port);
}
void ViscaSender::init()
{
    xTaskCreate(task_wrapper, "command_task", configMINIMAL_STACK_SIZE * 4, this, tskIDLE_PRIORITY + 1, &command_handle);
}
void ViscaSender::change_camera_ip(IPAddress ip, uint16_t port)
{
    _camera_ip = ip;
    _port = port;
}

#define zero_to_127(in) (in > 127 ? in - 127 : 127 - in)

void ViscaSender::send_data(const ControlData &data)
{
    if (is_blocking_messages)
        return;
    if (_last_control.pan != data.pan || _last_control.tilt != data.tilt)
    {
        uint8_t speed = map(data.speed, 0, 255, 1, 32);

        uint8_t pan = min((uint8_t)map(zero_to_127(data.pan), 0, 127, 1, 32), speed);
        uint8_t tilt = min((uint8_t)map(zero_to_127(data.tilt), 0, 127, 1, 32), speed);

        move_pantilt(pan, tilt, data.pan > 127, data.tilt < 127);

        _last_control.pan = data.pan;
        _last_control.tilt = data.tilt;
        _last_control.speed = data.speed;
    }
    if (_last_control.zoom != data.zoom)
    {
        uint8_t speed_zoom = zero_to_127(data.zoom);
        if (speed_zoom == 0)
            zoom_stop();
        else
            zoom(map(speed_zoom, 0, 127, 0, 7), data.zoom < 127);
        _last_control.zoom = data.zoom;
    }
}

void ViscaSender::detach_camera()
{
    is_blocking_messages = true;
    xQueueReset(command_queue);
    move_pantilt(1, 1, true, true);
    zoom_stop();
    vTaskDelay(pdTICKS_TO_MS(100));
    is_blocking_messages = false;
}

void ViscaSender::send_queue()
{
    ViscaCommand_t event_received;

    while (true)
    {
        // are we waiting for a reply? ->wait until completed or timeout
        // Wait for an event on the queue.  Block indefinitely (or use a timeout).
        if (xQueueReceive(command_queue, &event_received, portMAX_DELAY) == pdPASS)
        {
            uint8_t data[24]{};
            // add command type to beginning
            memcpy(&data[0], TYPE_COMMAND, 2);
            uint16_t payload_len = event_received.len + 8; // 8 is header length
            // Payload length
            data[2] = (payload_len >> 8) & 0xFF;
            data[3] = payload_len & 0xFF;
            // sequence number
            data[4] = (seq_num >> 24) & 0xFF;
            data[5] = (seq_num >> 16) & 0xFF;
            data[6] = (seq_num >> 8) & 0xFF;
            data[7] = seq_num & 0xFF;
            // copy payload
            memcpy(&data[8], event_received.data, event_received.len);
            // send it
            _write(data, payload_len);
            seq_num++;
        }
    }
}

const uint8_t PAYLOAD_MOVE[]{0x81, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xff};

// PANTILT
const uint8_t MOVE_UP[]{0x03, 0x01};
const uint8_t MOVE_DOWN[]{0x03, 0x02};
const uint8_t MOVE_LEFT[]{0x01, 0x03};
const uint8_t MOVE_RIGHT[]{0x02, 0x03};
const uint8_t MOVE_STOP[]{0x03, 0x03};
const uint8_t MOVE_UPLEFT[]{0x01, 0x01};
const uint8_t MOVE_UPRIGHT[]{0x02, 0x01};
const uint8_t MOVE_DOWNLEFT[]{0x01, 0x02};
const uint8_t MOVE_DOWNRIGHT[]{0x02, 0x02};

// ZOOM
const uint8_t PAYLOAD_ZOOM[]{0x81, 0x01, 0x04, 0x07, 0x00, 0xff};
const uint8_t ZOOM_IN_VARIABLE_SPEED = 0x20;
const uint8_t ZOOM_OUT_VARIABLE_SPEED = 0x30;
const uint8_t ZOOM_STOP = 0x00;

// PRESETS
const uint8_t PRESET_RECALL[]{0x81, 0x01, 0x04, 0x3f, 0x02, 0x00, 0xff};
const uint8_t PRESET_RESET[]{0x81, 0x01, 0x04, 0x3f, 0x00, 0x00, 0xff};
const uint8_t PRESET_SET[]{0x81, 0x01, 0x04, 0x3f, 0x01, 0x00, 0xff};
const uint8_t PRESET_SPEED[]{0x81, 0x01, 0x7E, 0x01, 0x0b, 0x00, 0x00, 0xff};

bool ViscaSender::zoom(uint8_t speed, bool dir)
{
    ViscaCommand_t data;
    memcpy(data.data, PAYLOAD_ZOOM, sizeof(PAYLOAD_ZOOM) / sizeof(*PAYLOAD_ZOOM));
    data.len = sizeof(PAYLOAD_ZOOM) / sizeof(*PAYLOAD_ZOOM);
    data.data[4] = dir ? ZOOM_IN_VARIABLE_SPEED : ZOOM_OUT_VARIABLE_SPEED;
    data.data[4] = data.data[4] ^ (speed & 0x7);
    return _add_to_queue(data);
}

bool ViscaSender::zoom_stop()
{
    ViscaCommand_t data;
    // stop command is 0x00 so no need to set it.
    memcpy(data.data, PAYLOAD_ZOOM, sizeof(PAYLOAD_ZOOM) / sizeof(*PAYLOAD_ZOOM));
    data.len = sizeof(PAYLOAD_ZOOM) / sizeof(*PAYLOAD_ZOOM);
    return _add_to_queue(data);
}

bool ViscaSender::move_pantilt(uint8_t p_speed, uint8_t t_speed, bool p_dir, bool t_dir)
{
    ViscaCommand_t data;
    memcpy(data.data, PAYLOAD_MOVE, 9);
    data.len = 9;
    data.data[4] = p_speed;
    data.data[5] = t_speed;
    if (p_speed > 1 && t_speed > 1)
    {

        if (p_dir && t_dir)
        {
            memcpy(&data.data[6], MOVE_UPRIGHT, 2);
        }
        else if (!p_dir && t_dir)
        {
            memcpy(&data.data[6], MOVE_UPLEFT, 2);
        }
        else if (p_dir && !t_dir)
        {
            memcpy(&data.data[6], MOVE_DOWNRIGHT, 2);
        }
        else if (!p_dir && !t_dir)
        {
            memcpy(&data.data[6], MOVE_DOWNLEFT, 2);
        }
    }
    else if (t_speed > 1)
    {
        if (t_dir)
            memcpy(&data.data[6], MOVE_UP, 2);
        else
            memcpy(&data.data[6], MOVE_DOWN, 2);
    }
    else if (p_speed > 1)
    {
        if (p_dir)
            memcpy(&data.data[6], MOVE_RIGHT, 2);
        else
            memcpy(&data.data[6], MOVE_LEFT, 2);
    }
    else
    {
        memcpy(&data.data[6], MOVE_STOP, 2);
    }
    return _add_to_queue(data);
}

void ViscaSender::save_preset(uint8_t preset_num, uint8_t speed)
{
    ViscaCommand_t set_preset;
    memcpy(set_preset.data, PRESET_SET, sizeof(PRESET_SET) / sizeof(*PRESET_SET));
    set_preset.len = sizeof(PRESET_SET) / sizeof(*PRESET_SET);
    set_preset.data[5] = preset_num; // preset num 0 to 63

    ViscaCommand_t speed_preset;
    memcpy(speed_preset.data, PRESET_SPEED, sizeof(PRESET_SPEED) / sizeof(*PRESET_SPEED));
    speed_preset.len = sizeof(PRESET_SPEED) / sizeof(*PRESET_SPEED);
    speed_preset.data[5] = preset_num;              // preset num 0 to 63
    speed_preset.data[6] = map(speed, 0, 8, 1, 32); // preset num 1 to 32

    _add_to_queue(set_preset);
    _add_to_queue(speed_preset);
}

void ViscaSender::run_preset(uint8_t preset)
{
    ViscaCommand_t data;
    memcpy(data.data, PRESET_RECALL, 7);
    data.len = 7;
    data.data[5] = preset; // preset num 0 to 63
    _add_to_queue(data);
}

bool ViscaSender::_add_to_queue(const ViscaCommand_t &data)
{
    if (command_queue == nullptr)
    {
        return false;
    }

    BaseType_t result = xQueueSendToBack(command_queue, &data, pdMS_TO_TICKS(10));
    return result == pdPASS;
}

void ViscaSender::task_wrapper(void *pvParameters)
{
    ViscaSender *instance = static_cast<ViscaSender *>(pvParameters);
    instance->send_queue();
}