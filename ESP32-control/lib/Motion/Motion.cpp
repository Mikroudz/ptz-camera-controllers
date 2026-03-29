#include "Motion.h"
#include "Arduino.h"
#include "speed_lookup.h"

#define PAN_TILT_EN_PIN 4 // Enable
#ifdef ESP32_S3
#define ST1_DIR_PIN 12 // Direction
#endif
#ifdef WT32_ETH01
#define ST1_DIR_PIN IO32 // Direction
#endif
#define ST1_STEP_PIN 2 // Step
#define ST1_ENDSTOP 39 // Endstop

#define ST2_DIR_PIN 15  // Direction
#define ST2_STEP_PIN 33 // Step
#define ST2_ENDSTOP 36  // Endstop

#define ST3_DIR_PIN 12 // Direction
#define ST3_STEP_PIN 5 // Step

#define SERIAL_PORT Serial2 // TMC2208/TMC2224 HardwareSerial port

#define ZOOM_SERIAL_PORT Serial1 // zoom tmc uart

#define R_SENSE 0.11f // SilentStepStick series use 0.11

#define MICROSTEPS 16
#define MOTOR_STEPS_PER_REV 200 * MICROSTEPS

namespace motion {

const int DISTANCE_TO_TRAVEL_IN_STEPS = 2000;
const int SPEED_REV_IN_S_MAX = 1;
const int SPEED_REV_IN_S_MIN = 0.01;

const float TILT_RANGE_MAX_STEPS = 0.9 * MOTOR_STEPS_PER_REV;
const float PAN_RANGE_MAX_STEPS = 2 * MOTOR_STEPS_PER_REV;

const int ACCELERATION_IN_STEPS_PER_SECOND = 800;
const int DECELERATION_IN_STEPS_PER_SECOND = 800;

float calculate_log_speed(uint8_t speed, const float s_min, const float s_max,
                          uint8_t in_min, uint8_t in_max,
                          const float base = 1.8) {

    if (speed < in_min || speed > in_max) {
        speed = constrain(speed, in_min, in_max);
    }

    float normalized_input =
        static_cast<float>(speed - in_min) / (in_max - in_min);
    float log_value = log(normalized_input * (base - 1) + 1) / log(base);
    return log_value * (s_max - s_min) + s_min;
}

float get_lut_speed(uint8_t speed, uint8_t steps) {
    if (steps == 32) {
        return rpm_lookup_32[speed - 1];
    } else if (steps == 8) {
        return rpm_lookup_8[speed];
    }
    return calculate_log_speed(speed, 0, 1, 0, steps);
}

void set_zoom_params_uart() {
    ZOOM_SERIAL_PORT.begin(115200, SERIAL_8N1, -1, 14);
    TMC2208Stepper tmc_driver(&ZOOM_SERIAL_PORT, R_SENSE);
    tmc_driver.begin(); // UART: Init SW UART (if selected) with default 115200
    // baudrate
    tmc_driver.pdn_disable(1);
    tmc_driver.mstep_reg_select(true);
    tmc_driver.multistep_filt(true);
    tmc_driver.dedge(false);
    tmc_driver.I_scale_analog(0);
    tmc_driver.rms_current(300, 0.5);  // Set motor RMS current
    tmc_driver.microsteps(MICROSTEPS); // Set microsteps to 1/16th

    tmc_driver.intpol(true);
    tmc_driver.pwm_autoscale(true); // Needed for stealthChop
    tmc_driver.TPWMTHRS(0);
    tmc_driver.en_spreadCycle(false); // Toggle spreadCycle on TMC2208/2209/2224
    tmc_driver.freewheel(0);
    // tmc_driver->TPWMTHRS()
    tmc_driver.toff(5); // Enables driver in software
}

Motion::Motion(movequeue::MoveQueue &p_queue)
    : queue(p_queue), st1(std::make_unique<ESP_FlexyStepper>()),
      st2(std::make_unique<ESP_FlexyStepper>()),
      st3(std::make_unique<ESP_FlexyStepper>()),
      tmc_driver(std::make_unique<TMC2208Stepper>(&SERIAL_PORT, R_SENSE)) {
    // prevent startup weirdness
    pinMode(PAN_TILT_EN_PIN, OUTPUT);
    disable_motors();
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);
}

Motion::~Motion() {}

// seems to be better calling setup from arduino setup to let the system
// initialize itself
void Motion::setup() {
    load_presets();
    SERIAL_PORT.begin(115200, SERIAL_8N1, -1, 17);
    delay(200);
    /*while (!SERIAL_PORT) {
        delay(10);
    }*/
    if (st1 == nullptr || st2 == nullptr) {
        ESP_LOGE(TAG, "Stepper controller not initialized!");
    }
    st1->connectToPins(ST1_STEP_PIN, ST1_DIR_PIN);
    st2->connectToPins(ST2_STEP_PIN, ST2_DIR_PIN);
    st3->connectToPins(ST3_STEP_PIN, ST3_DIR_PIN);

    digitalWrite(ST1_DIR_PIN, LOW);
    digitalWrite(ST1_DIR_PIN, HIGH);

    pinMode(ST1_ENDSTOP, INPUT_PULLUP);
    pinMode(ST2_ENDSTOP, INPUT_PULLUP);

    st1->setStepsPerRevolution(MOTOR_STEPS_PER_REV);
    st2->setStepsPerRevolution(MOTOR_STEPS_PER_REV);
    st3->setStepsPerRevolution(MOTOR_STEPS_PER_REV);

    st1->setSpeedInRevolutionsPerSecond(1.);
    st2->setSpeedInRevolutionsPerSecond(1.);
    st3->setSpeedInRevolutionsPerSecond(1.);

    st1->setAccelerationInRevolutionsPerSecondPerSecond(4.);
    st1->setDecelerationInRevolutionsPerSecondPerSecond(4.);
    st2->setAccelerationInRevolutionsPerSecondPerSecond(4.);
    st2->setDecelerationInRevolutionsPerSecondPerSecond(4.);
    st3->setDecelerationInRevolutionsPerSecondPerSecond(4.);
    st3->setAccelerationInRevolutionsPerSecondPerSecond(4.);

    ESP_LOGE(TAG, "Step params set");

    tmc_driver->begin(); // UART: Init SW UART (if selected) with default 115200
    // baudrate
    tmc_driver->pdn_disable(1);
    tmc_driver->mstep_reg_select(true);
    tmc_driver->multistep_filt(true);
    tmc_driver->dedge(false);
    tmc_driver->I_scale_analog(0);
    tmc_driver->rms_current(1000, 0.5); // Set motor RMS current
    tmc_driver->microsteps(MICROSTEPS); // Set microsteps to 1/16th

    tmc_driver->intpol(true);
    tmc_driver->pwm_autoscale(true); // Needed for stealthChop
    tmc_driver->TPWMTHRS(0);
    tmc_driver->en_spreadCycle(
        false); // Toggle spreadCycle on TMC2208/2209/2224
    tmc_driver->freewheel(0);
    // tmc_driver->TPWMTHRS()
    tmc_driver->toff(5); // Enables driver in software
    set_zoom_params_uart();
    delay(200);

    enable_pan();
    delay(200); // might cause overload to psu if started at same time
    enable_tilt();

    delay(500);
    ESP_LOGE(TAG, "Homing...");
    home_tilt();
    home_pan();
    ESP_LOGE(TAG, "All homed!");
    start_stepper_processes();
}

bool Motion::load_presets() {
    persistent.begin("presets", false);
    uint8_t buf[sizeof(PositionPreset_t)];
    for (int i = 0; i < 3; i++) {
        if (persistent.isKey("pos-" + i)) {
            const size_t read_bytes =
                persistent.getBytes("pos-" + i, &buf, sizeof(buf));
            if (read_bytes == sizeof(buf)) {
                memcpy(&position_presets[i], buf, sizeof(buf));
            }
        }
    }
    persistent.end();
    return true;
}

bool Motion::start_stepper_processes() {

    disableCore1WDT();

    xTaskCreatePinnedToCore(
        Motion::stepper_task, /* Task function. */
        "MotionProcess",      /* String with name of task (by default max 16
                                characters long) */
        2000,                 /* Stack size in bytes. */
        this,                 /* Parameter passed as input of the task */
        1, /* Priority of the task, 1 seems to work just fine for us */
        &this->stepper_task_handle, /* Task handle. */
        1 /* the cpu core to use, 1 is where usually the Arduino
                      Framework code (setup and loop function) are running, core
                      0 by default runs the Wifi Stack */
    );

    configASSERT(this->stepper_task_handle);
    ESP_LOGE(TAG, "Started stepper service!");

    return true;
}

void Motion::call_process() {
    st1->processMovement();
    st2->processMovement();
    st3->processMovement();
}

void Motion::stepper_task(void *parameter) {
    Motion *motionRef = static_cast<Motion *>(parameter);
    for (;;) {
        motionRef->call_process();
    }
}

bool Motion::home_tilt() {

    bool st1_home_ok{false};
    // check if endstop active (outside of the camera range)
    if (digitalRead(ST1_ENDSTOP) == HIGH) {
        // If we are active, find first position where it is not active
        ESP_LOGE(TAG, "Switch active, finding default...");
        const float home_moves[] = {0.3, -0.6, 0.9, -1.2, 1.8, -4};
        bool limit_switch_found{false};
        for (int i = 0; i < 6; i++) {
            st1->setTargetPositionRelativeInSteps(MOTOR_STEPS_PER_REV *
                                                  home_moves[i]);
            // ESP_LOGE(TAG, "Seeking tilt swich... in dir %d", dir);

            while (!st1->processMovement()) {
                // if inside camera range
                if (digitalRead(ST1_ENDSTOP) == LOW) {
                    limit_switch_found = true;
                    break;
                }
            }
            if (limit_switch_found) {
                // set home pos
                st1->setCurrentPositionInSteps(
                    home_moves[i] > 0 ? 0 : MOTOR_STEPS_PER_REV);
                st1_home_ok = true;
                ESP_LOGE(TAG, "Switch found in active state!");
                break;
            }
        }
    } else { // when inside camera range
        // if not active, we can do regular homing
        // Flexystepper homing reads switches incorrectly so we do own homing
        st1->setTargetPositionRelativeInSteps(-MOTOR_STEPS_PER_REV);
        while (!st1->processMovement()) {
            // if inside camera range
            if (digitalRead(ST1_ENDSTOP) == HIGH) {
                st1->setCurrentPositionInSteps(0);
                st1_home_ok = true;
                break;
            }
        }
    }
    // move to center
    st1->setTargetPositionInRevolutions(0.5);
    while (!st1->processMovement()) {
    }

    return st1_home_ok;
}

bool Motion::home_pan() {
    // Check if already active
    if (digitalRead(ST2_ENDSTOP) == HIGH) {
        st2->setCurrentPositionInSteps(0);
        return true;
    } else {
        const int revs[] = {2, -4};
        for (int i = 0; i < 2; i++) {
            st2->setTargetPositionRelativeInSteps(MOTOR_STEPS_PER_REV *
                                                  revs[i]);
            while (!st2->processMovement()) {
                // if inside camera range
                if (digitalRead(ST2_ENDSTOP) == HIGH) {
                    st2->setTargetPositionInRevolutions(0);
                    st2->setCurrentPositionInSteps(0);
                    return true;
                }
            }
        }
    }
    // move to
    return true;
}

void Motion::enable_tilt() { digitalWrite(PAN_TILT_EN_PIN, LOW); }

void Motion::enable_pan() { digitalWrite(PAN_TILT_EN_PIN, LOW); }

void Motion::disable_motors() {
    digitalWrite(PAN_TILT_EN_PIN, HIGH);
    digitalWrite(PAN_TILT_EN_PIN, HIGH);
}

void Motion::move_tilt(uint8_t speed, bool dir) {
    st1->setSpeedInRevolutionsPerSecond(get_lut_speed(speed, 32));
    const long current_pos = st1->getCurrentPositionInSteps();
    if (dir && current_pos < TILT_RANGE_MAX_STEPS) {
        st1->setTargetPositionInSteps(TILT_RANGE_MAX_STEPS);
    } else if (!dir && current_pos > 0) {
        st1->setTargetPositionInSteps(0);
    }
}

void Motion::move_pan(uint8_t speed, bool dir) {
    st2->setSpeedInRevolutionsPerSecond(get_lut_speed(speed, 32));
    const long current_pos = st2->getCurrentPositionInSteps();
    if (dir && current_pos < PAN_RANGE_MAX_STEPS) {
        st2->setTargetPositionInSteps(PAN_RANGE_MAX_STEPS);
    } else if (!dir && current_pos > 0) {
        st2->setTargetPositionInSteps(0);
    }
}

void Motion::move_zoom(uint8_t speed, bool dir) {
    st3->setSpeedInRevolutionsPerSecond(get_lut_speed(speed, 8));
    st3->setTargetPositionInSteps(MOTOR_STEPS_PER_REV * (dir ? -2 : 2));
}

void Motion::stop_pan() {
    if (st2->getDirectionOfMotion() != 0) {
        st2->setTargetPositionToStop();
    }
}

void Motion::stop_tilt() {
    if (st1->getDirectionOfMotion() != 0) {
        st1->setTargetPositionToStop();
    }
}

bool Motion::set_current_position_preset(uint8_t preset_num, uint8_t p_speed,
                                         uint8_t t_speed) {
    PositionPreset_t pos_preset;
    pos_preset.tilt_pos = st1->getCurrentPositionInRevolutions();
    pos_preset.pan_pos = st2->getCurrentPositionInRevolutions();
    pos_preset.pan_speed = p_speed;
    pos_preset.tilt_speed = t_speed;
    pos_preset.enabled = true;
    // save into persistent mem
    persistent.begin("presets", false);
    persistent.putBytes("pos-" + preset_num, &pos_preset, sizeof(pos_preset));
    persistent.end();

    position_presets[preset_num] = pos_preset;
    return true;
}

bool Motion::set_preset_speed(uint8_t preset_num, uint8_t p_speed,
                              uint8_t t_speed) {
    if (position_presets[preset_num].enabled) {
        position_presets[preset_num].pan_speed = p_speed;
        position_presets[preset_num].tilt_speed = t_speed;
        // need to update the persistent speeds
        persistent.begin("presets", false);
        persistent.putBytes("pos-" + preset_num, &position_presets[preset_num],
                            sizeof(PositionPreset_t));
        persistent.end();
    } else {
        return false;
    }
    return true;
}

bool Motion::recall_preset(uint8_t preset_num) {
    if (position_presets[preset_num].enabled) {
        st1->setSpeedInRevolutionsPerSecond(
            get_lut_speed(position_presets[preset_num].tilt_speed, 32));
        st2->setSpeedInRevolutionsPerSecond(
            get_lut_speed(position_presets[preset_num].pan_speed, 32));
        st1->setTargetPositionInRevolutions(
            position_presets[preset_num].tilt_pos);
        st2->setTargetPositionInRevolutions(
            position_presets[preset_num].pan_pos);
    }
    return true;
}

bool Motion::do_action(const visca::ViscaCommandBase &data) {
    const auto type = data.get_action();
    /*ESP_LOGE(TAG, "Speed: %.2f, %.2f",
             calculate_log_speed(data.tilt_speed(), SPEED_REV_IN_S_MIN,
                                 SPEED_REV_IN_S_MAX, 1, 32),
             calculate_log_speed(data.pan_speed(), SPEED_REV_IN_S_MIN,
                                 SPEED_REV_IN_S_MAX, 1, 32));*/
    switch (type) {
    case visca::ActionType::MOVE_UP:
        stop_pan();
        move_tilt(data.tilt_speed(), true);
        break;
    case visca::ActionType::MOVE_DOWN:
        stop_pan();
        move_tilt(data.tilt_speed(), false);
        break;
    case visca::ActionType::MOVE_LEFT:
        stop_tilt();
        move_pan(data.pan_speed(), false);
        break;
    case visca::ActionType::MOVE_RIGHT:
        stop_tilt();
        move_pan(data.pan_speed(), true);
        break;
    case visca::ActionType::MOVE_DOWNLEFT:
        move_tilt(data.tilt_speed(), false);
        move_pan(data.pan_speed(), false);
        break;
    case visca::ActionType::MOVE_DOWNRIGHT:
        move_tilt(data.tilt_speed(), false);
        move_pan(data.pan_speed(), true);

        break;
    case visca::ActionType::MOVE_UPLEFT:
        move_tilt(data.tilt_speed(), true);
        move_pan(data.pan_speed(), false);

        break;
    case visca::ActionType::MOVE_UPRIGHT:
        move_tilt(data.tilt_speed(), true);
        move_pan(data.pan_speed(), true);

        break;
    case visca::ActionType::MOVE_STOP:
        stop_tilt();
        stop_pan();
        break;
    case visca::ActionType::RECALL_PRESET:
        recall_preset(data.get_preset());
        break;
    case visca::ActionType::SET_PRESET:
        set_current_position_preset(data.get_preset(), 5, 5);
        break;
    case visca::ActionType::PRESET_SPEED:
        set_preset_speed(data.get_preset(), data.pan_speed(),
                         data.tilt_speed());
        break;
    case visca::ActionType::ZOOM_IN:
        move_zoom(data.zoom_speed(), false);
        break;
    case visca::ActionType::ZOOM_OUT:
        move_zoom(data.zoom_speed(), true);
        break;
    case visca::ActionType::ZOOM_STOP:
        st3->setTargetPositionToStop();
        break;
    default:
        return false;
        break;
    }
    return true;
}

void Motion::run() {

    for (;;) {
        std::shared_ptr<visca::ViscaCommandBase> data =
            queue.receive_msg<visca::ViscaCommandBase>();
        ESP_LOGE(TAG, "Received %d", data->get_action());
        do_action(*data);
    }
}

} // namespace motion