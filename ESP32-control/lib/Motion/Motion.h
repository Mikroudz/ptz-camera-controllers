#ifndef MOTION_H
#define MOTION_H

#include "ESP_FlexyStepper.h"
#include "MoveQueue.h"
#include "ViscaListener.h"
#include "esp_log.h"
#include <TMCStepper.h>
#include <Preferences.h>

namespace motion {

static const char *TAG = "motion";

typedef struct {
    float pan_pos;
    float tilt_pos;
    uint8_t pan_speed;
    uint8_t tilt_speed;
    bool enabled;

} PositionPreset_t;

class Motion {
  private:
    movequeue::MoveQueue &queue;
    std::unique_ptr<ESP_FlexyStepper> st1;
    std::unique_ptr<ESP_FlexyStepper> st2;
    std::unique_ptr<ESP_FlexyStepper> st3; // zoom

    std::unique_ptr<TMC2208Stepper> tmc_driver;
    PositionPreset_t position_presets[64]{};
    TaskHandle_t stepper_task_handle = NULL;
    Preferences persistent;

  public:
    Motion(movequeue::MoveQueue &p_queue);
    ~Motion();
    void run();
    void setup();
    bool start_stepper_processes();
    void call_process();
    static void stepper_task(void *parameter);
    bool load_presets();

    void enable_tilt();
    void enable_pan();

    bool do_action(const visca::ViscaCommandBase &data);
    bool home_tilt();
    bool home_pan();


    void stop_tilt();
    void stop_pan();
    void disable_motors();
    void move_pan(uint8_t speed, bool dir);
    void move_tilt(uint8_t speed, bool dir);
    void move_zoom(uint8_t speed, bool dir);
    bool set_current_position_preset(uint8_t preset_num, uint8_t p_speed,
                                     uint8_t t_speed);
    bool recall_preset(uint8_t preset_num);
    bool set_preset_speed(uint8_t preset_num, uint8_t p_speed,
        uint8_t t_speed);

};

} // namespace motion

#endif
