#include "IPCamSender.h"
#include "esp_log.h"

static const char *TAG = "sender";

static const char *PANASONIC_CGI_PATH = "/cgi-bin/aw_ptz?cmd=%23";

#define PANASONIC_MIN 1
#define PANASONIC_MAX 99
#define PANASONIC_STOP 50

typedef struct
{
    QueueHandle_t queue;
    int client_id;
    int cooldown;
    PanasonicSender *ref;
} TaskParams_t;

TaskParams_t command_param = {nullptr, 0, 0, nullptr};
TaskParams_t pantilt_param = {nullptr, 0, 0, nullptr};
TaskParams_t zoom_param = {nullptr, 0, 0, nullptr};

void pad_int(char *buffer, size_t size, uint16_t val, uint8_t pad = 2)
{
    std::snprintf(buffer, size, "%0*d", pad, val);
}

PanasonicSender::PanasonicSender()
{
}

PanasonicSender::~PanasonicSender()
{
}

void PanasonicSender::init()
{

    esp_http_client_config_t config = {
        .url = "http://lev.lan",
        .method = HTTP_METHOD_GET,
        .disable_auto_redirect = true,
        .is_async = true,

    };
    for (int i = 0; i < 4; i++)
    {
        http_clients[i] = esp_http_client_init(&config);
        if (http_clients[i] == NULL)
        {
            ESP_LOGE(TAG, "Failed to initialize HTTP client %d", i);
        }
    }
    command_queue = xQueueCreate(10, sizeof(char *));
    command_param.queue = command_queue;
    command_param.ref = this;
    pantilt_queue = xQueueCreate(1, sizeof(char *));
    pantilt_param.queue = pantilt_queue;
    pantilt_param.client_id = 1;
    pantilt_param.ref = this;
    pantilt_param.cooldown = 150;
    zoom_queue = xQueueCreate(1, sizeof(char *));
    zoom_param.queue = zoom_queue;
    zoom_param.client_id = 2;
    zoom_param.ref = this;
    zoom_param.cooldown = 150;

    xTaskCreate(task_wrapper, "command_task", configMINIMAL_STACK_SIZE * 4, (void *)&command_param, tskIDLE_PRIORITY + 1, &command_handle);
    xTaskCreate(task_wrapper, "zoom_task", configMINIMAL_STACK_SIZE * 4, (void *)&zoom_param, tskIDLE_PRIORITY + 1, &zoom_handle);
    xTaskCreate(task_wrapper, "pantilt_task", configMINIMAL_STACK_SIZE * 4, (void *)&pantilt_param, tskIDLE_PRIORITY + 1, &pantilt_handle);
}

void PanasonicSender::create_command(char *buffer, size_t size, const char *msg)
{
    snprintf(buffer, size,
             "http://%s:%d%s%s&res=1",
             _camera_ip.c_str(),
             _port,
             PANASONIC_CGI_PATH,
             msg);
}

void PanasonicSender::send_data(const ControlData &data)
{
    if (is_blocking_messages)
        return;
    if (_last_control.pan != data.pan || _last_control.tilt != data.tilt)
    {
        move_pantilt(map(data.pan, 0, 254, PANASONIC_MIN, PANASONIC_MAX), map(data.tilt, 0, 254, PANASONIC_MIN, PANASONIC_MAX));
        _last_control.pan = data.pan;
        _last_control.tilt = data.tilt;
        // return;
    }
    if (_last_control.zoom != data.zoom)
    {
        zoom(map(data.zoom, 0, 254, PANASONIC_MIN, PANASONIC_MAX));
        _last_control.zoom = data.zoom;
        // return;
    }
}

// TODO: maybe use send_data with presets also? Would it be doable?
void PanasonicSender::save_preset(uint8_t key, uint8_t preset_speed)
{
    if (is_blocking_messages)
        return;
    char url_buffer[256];

    char temp[20] = "M";
    pad_int(temp + 1, sizeof(temp) - 1, key);
    create_command(url_buffer, sizeof(url_buffer), temp);
    add_to_queue(command_queue, url_buffer);

    temp[0] = '\0';
    strcpy(temp, "UPVS");

    url_buffer[0] = '\0';
    pad_int(temp + 4, sizeof(temp) - 4, map(preset_speed, 0, 8, 250, 999), 3);
    create_command(url_buffer, sizeof(url_buffer), temp);
    add_to_queue(command_queue, url_buffer);
}

void PanasonicSender::run_preset(uint8_t preset)
{
    if (is_blocking_messages)
        return;
    char url_buffer[256];
    char temp[20] = "R";
    pad_int(temp + 1, sizeof(temp) - 1, preset);
    create_command(url_buffer, sizeof(url_buffer), temp);
    add_to_queue(command_queue, url_buffer);
}

void PanasonicSender::change_camera_ip(IPAddress ip, uint16_t port)
{
    _ip_raw = ip;
    _camera_ip = _ip_raw.toString().c_str();
    _port = port;
}

void PanasonicSender::switch_camera()
{
    xQueueReset(command_queue);
    xQueueReset(pantilt_queue);
    xQueueReset(zoom_queue);
    if (_last_control.zoom != PANASONIC_STOP)
    {
        zoom(PANASONIC_STOP);
    }
    if (_last_control.tilt != PANASONIC_STOP || _last_control.pan != PANASONIC_STOP)
    {
        move_pantilt(PANASONIC_STOP, PANASONIC_STOP);
    }
}

void PanasonicSender::detach_camera()
{
    is_blocking_messages = true;
    switch_camera();
    vTaskDelay(pdTICKS_TO_MS(200));
    is_blocking_messages = false;
}

void PanasonicSender::run_command_queue(QueueHandle_t queue, int client_id, int cooldown)
{
    char *event_data;
    while (true)
    {
        // are we waiting for a reply? ->wait until completed or timeout
        // Wait for an event on the queue.  Block indefinitely (or use a timeout).
        if (xQueueReceive(queue, &event_data, portMAX_DELAY) == pdPASS)
        {
            send_get(event_data, client_id);
            if (cooldown > 0)
            {
                vTaskDelay(cooldown / portTICK_PERIOD_MS);
            }
            // ESP_LOGI(TAG, "heap: %d", esp_get_free_heap_size());
            vPortFree((void *)event_data);
        }
    }
}

void PanasonicSender::send_get(const char *msg, int client_id)
{

    if (client_id < 0 || client_id >= 3 || http_clients[client_id] == NULL)
    {
        ESP_LOGE(TAG, "Invalid client ID or client not initialized");
        return;
    }
    esp_http_client_handle_t client = http_clients[client_id];
    esp_http_client_set_url(client, msg);
    esp_err_t err;
    while (1)
    {
        err = esp_http_client_perform(client);

        if (err != ESP_ERR_HTTP_EAGAIN)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (err == ESP_OK)
    {
        // ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
        //          esp_http_client_get_status_code(client),
        //          esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_close(client);
}

void PanasonicSender::add_to_queue(QueueHandle_t queue, const char *input, bool override)
{
    BaseType_t res = pdFAIL;
    size_t len = strlen(input) + 1;
    char *pBuffer = (char *)pvPortMalloc(len);
    if (pBuffer != NULL)
    {
        strcpy(pBuffer, input);
    }
    if (override)
    {
        char *old = nullptr;
        xQueuePeek(queue, &old, 0);
        if (old != nullptr)
            vPortFree(old);
        res = xQueueOverwrite(queue, &pBuffer);
    }
    else
    {
        res = xQueueSendToBack(queue, &pBuffer, pdMS_TO_TICKS(10));
    }
    if (res != pdTRUE)
    {
        ESP_LOGE(TAG, "Cannot insert into queue");
        vPortFree(pBuffer);
    }
}

bool PanasonicSender::zoom(uint8_t speed)
{
    char url_buffer[256];

    char temp[20] = "Z";
    pad_int(temp + 1, sizeof(temp) - 1, speed);
    create_command(url_buffer, sizeof(url_buffer), temp);
    add_to_queue(zoom_queue, url_buffer, true);
    return true;
}

bool PanasonicSender::move_pantilt(uint8_t p_speed, uint8_t t_speed)
{

    char temp[20] = "PTS";
    pad_int(temp + 3, sizeof(temp) - 3, p_speed);
    pad_int(temp + 5, sizeof(temp) - 5, t_speed);
    char url_buffer[256];

    create_command(url_buffer, sizeof(url_buffer), temp);

    add_to_queue(pantilt_queue, url_buffer, true);
    return true;
}

bool PanasonicSender::save_location_preset(uint8_t preset_num, uint8_t speed)
{

    return true;
}

bool PanasonicSender::recall_preset(uint8_t preset_num)
{
    return true;
}

void PanasonicSender::task_wrapper(void *pvParameters)
{
    TaskParams_t *params = static_cast<TaskParams_t *>(pvParameters);
    PanasonicSender *instance = static_cast<PanasonicSender *>(params->ref);
    instance->run_command_queue(params->queue, params->client_id, params->cooldown);
}