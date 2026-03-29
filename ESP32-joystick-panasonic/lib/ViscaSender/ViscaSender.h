#ifndef VISCASENDER_H
#define VISCASENDER_H

#include "FreeRTOS.h"
#include <Arduino.h>

#include "queue.h"
#include "AsyncUDP.h"
#include "PTZDevice.h"

typedef struct
{
    uint8_t data[16];
    uint8_t len;
} ViscaCommand_t;

typedef struct
{
    uint8_t mac_addr[6];
    IPAddress ip;
} ClientInfo_t;

class ViscaSender : public PTZDevice
{
private:
    AsyncUDP udp;
    QueueHandle_t command_queue;
    uint32_t seq_num;
    IPAddress _camera_ip;
    uint16_t _port;
    ClientInfo_t cameras[5]{};
    uint8_t _selected_camera;
    uint8_t cam_count;
    ControlData _last_control;
    bool is_blocking_messages{false};
    TaskHandle_t command_handle;

public:
    ViscaSender();
    ~ViscaSender();

    void init();
    void change_camera_ip(IPAddress ip, uint16_t port = 80);
    void send_data(const ControlData &data);
    void save_preset(uint8_t key, uint8_t preset_speed);
    void detach_camera();
    void run_preset(uint8_t preset);

    bool move_pantilt(uint8_t p_speed, uint8_t t_speed, bool p_dir, bool t_dir);
    bool zoom(uint8_t speed, bool dir);
    bool zoom_stop();

    void send_queue();

    void start_listen_broadcast();

protected:
    void _write(const uint8_t *data, size_t len);
    bool _add_to_queue(const ViscaCommand_t &data);
    static void _broadcast_inquiry_timer_task(TimerHandle_t xTimer);
    static void task_wrapper(void *pvParameters);
};

#endif