#include <Arduino.h>
#include "esp_log.h"
#include "JoystickReader.h"
#include "IPCamSender.h"
#include "ViscaSender.h"

#include "PTZDevice.h"

#include <WiFi.h>
#include "UI.h"

static const char *TAG = "main";

PanasonicSender panasonic_ptz;
ViscaSender visca_sender;

std::array<PTZDevice *, 3> devices = {&panasonic_ptz, &visca_sender};

DeviceHandler device(devices);

UI ui(&device);

JoystickReader joystick(&device, &ui);

static void joystickTask(void *pvParameters)
{
    // configASSERT(((uint32_t)pvParameters) == 1);
    JoystickReader *joysticHandler = static_cast<JoystickReader *>(pvParameters);

    joysticHandler->run();
}

void setup()
{
    delay(1000);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "Starting...");
    ui.init();

    device.set_impl(&panasonic_ptz);
    panasonic_ptz.init();
    visca_sender.init();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "Connecting to WiFi ..");

    while (WiFi.status() != WL_CONNECTED)
    {
        ESP_LOGI(TAG, "%d", WiFi.status());
        delay(1000);
    }
    xTaskCreate(joystickTask, "JoysticTask", configMINIMAL_STACK_SIZE * 4,
                &joystick, tskIDLE_PRIORITY + 1, nullptr);

    ui.goto_page(0);
}

void loop()
{
    ui.update_wait_for_key();
}
