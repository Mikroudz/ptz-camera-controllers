#ifndef JOYSTICREADER_H
#define JOYSTICREADER_H
#include <Arduino.h>

#include "AS5600.h"
#include "Wire.h"
#include "PTZDevice.h"
#include "UI.h"

class JoystickReader
{
private:
    DeviceHandler *ptz_device;
    UI *ui;
    TwoWire wire;
    AS5600 tilt_sensor;
    AS5600 pan_sensor;
    uint16_t tilt_avg{0};
    uint16_t pan_avg{0};
    uint16_t zoom_avg{0};
    uint16_t speed_avg{0};
    bool is_zooming_joystick{false};
    bool is_zooming_magnetic{false};

public:
    JoystickReader(DeviceHandler *device_handler, UI *p_ui);
    ~JoystickReader();
    void read();
    void run();
    void analogUpdateRead();
};

#endif