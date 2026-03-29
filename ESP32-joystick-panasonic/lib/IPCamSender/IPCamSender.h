#ifndef IPCAMSENDER_H
#define IPCAMSENDER_H

#include "FreeRTOS.h"
#include <Arduino.h>
#include <string>

#include "esp_http_client.h"
#include "queue.h"
#include "AsyncUDP.h"

#include "PTZDevice.h"

class PanasonicSender : public PTZDevice
{
private:
    QueueHandle_t command_queue;
    QueueHandle_t pantilt_queue;
    QueueHandle_t zoom_queue;

    TaskHandle_t command_handle;
    TaskHandle_t pantilt_handle;
    TaskHandle_t zoom_handle;
    IPAddress _ip_raw;
    std::string _camera_ip;
    uint16_t _port;
    esp_http_client_handle_t http_clients[4];
    ControlData _last_control;
    static void task_wrapper(void *pvParameters);
    bool is_blocking_messages = false;

public:
    PanasonicSender();
    ~PanasonicSender();

    void change_camera_ip(IPAddress ip, uint16_t port);
    void init();
    void send_data(const ControlData &data);
    void save_preset(uint8_t key, uint8_t preset_speed);

    bool move_pantilt(uint8_t p_speed, uint8_t t_speed);
    bool recall_preset(uint8_t preset_num);
    bool save_location_preset(uint8_t preset_num, uint8_t speed);
    bool zoom(uint8_t speed);
    void detach_camera();
    void run_preset(uint8_t preset);

    IPAddress get_camera_ip();

    void add_to_queue(QueueHandle_t queue, const char *input, bool override = false);
    void send_get(const char *msg, int client_id);
    void run_command_queue(QueueHandle_t queue, int client_id, int cooldown);
    void switch_camera();
    void create_command(char *buffer, size_t size, const char *msg);
};

#endif