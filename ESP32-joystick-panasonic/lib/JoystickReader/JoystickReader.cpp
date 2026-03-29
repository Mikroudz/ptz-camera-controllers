#include "JoystickReader.h"

static const char *TAG = "joystick";

#define TILT_CENTER 1980
#define PAN_CENTER 1975
#define ZOOM_CENTER 1950

#define TILT_MAX 2650
#define PAN_MAX 2650
#define ZOOM_MAX 2950

#define TILT_MIN 1300
#define PAN_MIN 1300
#define ZOOM_MIN 1080

#define TILT_DEADZONE 100
#define PAN_DEADZONE 100
#define ZOOM_DEADZONE 200

#define TILT_POT_PIN 6
#define PAN_POT_PIN 5
#define ZOOM_POT_PIN 4
#define SPEED_POT_PIN 18

#define TILT_SENSOR_DIR_PIN 15

#define MAGNETIC_ZOOM_MIN 1050
#define MAGNETIC_ZOOM_MAX 1600
#define MAGNETIC_ZOOM_CENTER 1292
#define MAGNETIC_ZOOM_DEADZONE 70

inline bool get_direction(uint16_t val, uint16_t center)
{
    return val > center;
}

uint8_t map_to_speed(uint16_t p_val, uint16_t from_min, uint16_t from_max, uint16_t to_min, uint16_t to_max, uint16_t from_center, uint16_t deadzone, uint16_t to_center)
{
    uint16_t tmp = 0;
    uint16_t val = constrain(p_val, from_min, from_max);
    // detect deadzone
    if (abs(val - from_center) <= deadzone)
    {
        return to_center;
    }
    if (val < from_center)
    {
        tmp = map(val, from_min, from_center - deadzone, to_min, to_center);
    }
    else if (val > from_center)
    {
        tmp = map(val, from_center + deadzone, from_max, to_center, to_max);
    }

    return tmp;
}

JoystickReader::JoystickReader(DeviceHandler *device_handler, UI *p_ui) : ptz_device(device_handler), ui(p_ui), wire(1), tilt_sensor(), pan_sensor(&wire)
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    tilt_sensor.begin(TILT_SENSOR_DIR_PIN);
    // pan_sensor.begin(PAN_SENSOR_DIR_PIN);
    tilt_sensor.setDirection(AS5600_CLOCK_WISE);
    // pan_sensor.setDirection(AS5600_CLOCK_WISE);
    ESP_LOGD(TAG, "sensor connected %s:", tilt_sensor.isConnected() ? "true" : "false");
}

JoystickReader::~JoystickReader()
{
}

void JoystickReader::run()
{

    while (true)
    {
        // Note; this blocks for 100ms
        analogUpdateRead();

        read();
        // vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void JoystickReader::analogUpdateRead()
{
    tilt_avg = analogRead(TILT_POT_PIN);
    pan_avg = analogRead(PAN_POT_PIN);
    zoom_avg = analogRead(ZOOM_POT_PIN);
    speed_avg = analogRead(SPEED_POT_PIN);
    vTaskDelay(pdMS_TO_TICKS(25));
    speed_avg += analogRead(SPEED_POT_PIN);
    vTaskDelay(pdMS_TO_TICKS(25));
    speed_avg += analogRead(SPEED_POT_PIN);
    vTaskDelay(pdMS_TO_TICKS(25));
    speed_avg = (speed_avg + analogRead(SPEED_POT_PIN)) / 4;
    vTaskDelay(pdMS_TO_TICKS(25));
}

void JoystickReader::read()
{

    // uint16_t pan_raw = pan_sensor.rawAngle();
    uint16_t tilt_raw = tilt_sensor.rawAngle();

    // ESP_LOGD(TAG, "mag sensor: %d", tilt_raw);
    uint8_t zoom_magnetic_mapped = map_to_speed(tilt_raw, MAGNETIC_ZOOM_MIN, MAGNETIC_ZOOM_MAX, 0, 255, MAGNETIC_ZOOM_CENTER, MAGNETIC_ZOOM_DEADZONE, 127);

    uint8_t tilt_mapped = map_to_speed(tilt_avg, TILT_MIN, TILT_MAX, 0, 255, TILT_CENTER, TILT_DEADZONE, 127);
    uint8_t pan_mapped = map_to_speed(pan_avg, PAN_MIN, PAN_MAX, 0, 255, PAN_CENTER, PAN_DEADZONE, 127);
    uint8_t zoom_mapped = map_to_speed(zoom_avg, ZOOM_MIN, ZOOM_MAX, 0, 255, ZOOM_CENTER, ZOOM_DEADZONE, 127);
    uint8_t speed_mapped = map(min(speed_avg, (uint16_t)2048), 0, 2048, 0, 255);
    // ESP_LOGD(TAG, "speed_map:%d", speed_mapped);

    if (!is_zooming_magnetic && zoom_mapped != 127 && !is_zooming_joystick)
    {
        is_zooming_joystick = true;
    }
    else if (is_zooming_joystick && zoom_mapped == 127)
    {
        is_zooming_joystick = false;
    }

    if (!is_zooming_joystick && zoom_magnetic_mapped != 127 && !is_zooming_magnetic)
    {
        is_zooming_magnetic = true;
    }
    else if (is_zooming_magnetic && zoom_magnetic_mapped == 127)
    {
        is_zooming_magnetic = false;
    }

    if (is_zooming_magnetic)
    {
        zoom_mapped = zoom_magnetic_mapped;
    }
    // ESP_LOGD(TAG, "tilt:%d,pan:%d,zoom:%d", tilt_mapped, pan_mapped, zoom_mapped);
    ControlData data{
        .pan = pan_mapped,
        .tilt = tilt_mapped,
        .zoom = zoom_mapped,
        .speed = speed_mapped};

    ptz_device->send_data(data);
    ui->update_sticK_values(data);

    // ESP_LOGD(TAG, "speed:%d", speed_avg);
}
