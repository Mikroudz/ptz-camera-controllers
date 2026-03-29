#include "Motion.h"
#include "MoveQueue.h"
#include "ViscaListener.h"
#include "esp_log.h"
#include <Arduino.h>
#include <WiFi.h>

static const char *TAG = "PTZ";

// visca command queue
movequeue::MoveQueue queue;

visca::ViscaListener visca_listener(queue);
motion::Motion motion_handler(queue);

static void viscaTask(void *pvParameters) {
    // configASSERT(((uint32_t)pvParameters) == 1);
    visca::ViscaListener *viscaHandler =
        static_cast<visca::ViscaListener *>(pvParameters);
    viscaHandler->start();
}

static void motionTask(void *pvParameters) {
    // configASSERT(((uint32_t)pvParameters) == 1);
    motion::Motion *motionHandler = static_cast<motion::Motion *>(pvParameters);
    // Waits for messages
    motionHandler->run();
}

void setup() {
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "Starting...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    ESP_LOGI(TAG, "Connecting to WiFi ..");

    while (WiFi.status() != WL_CONNECTED) {
        ESP_LOGI(TAG, "%d", WiFi.status());
        delay(1000);
    }

    motion_handler.setup();
    visca_listener.start();
    xTaskCreate(motionTask, "Motion", configMINIMAL_STACK_SIZE * 6,
                &motion_handler, tskIDLE_PRIORITY + 1, nullptr);
}

void loop() { delay(999999); }
