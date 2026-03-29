#ifndef MOVEQUEUE_H
#define MOVEQUEUE_H

#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <queue>
#include <memory>


namespace movequeue {

static const char *TAG = "queue";

class MoveQueue {
  private:
    std::queue<std::shared_ptr<void>> _queue;
    SemaphoreHandle_t _semaphore;
    SemaphoreHandle_t _mutex;

  public:
    MoveQueue(/* args */)
        : _semaphore(xSemaphoreCreateCounting(5, 0)),
          _mutex(xSemaphoreCreateMutex()) {

          };
    ~MoveQueue() = default;


    template <typename T>
    void create_msg(const std::shared_ptr<T> &value) {
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "failed to get mutex");
            return;
        }
        _queue.push(value);
        xSemaphoreGive(_mutex);
        if (xSemaphoreGive(_semaphore) != pdTRUE) {
            ESP_LOGE(TAG, "Cant return semaphore");
            return;
        }
    }
    template <typename T>
    std::shared_ptr<T> receive_msg() {

        if (!_semaphore) {
            return nullptr;
        }
        if (xSemaphoreTake(_semaphore, portMAX_DELAY) != pdTRUE) {
            return nullptr;
        }
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
            xSemaphoreGive(_semaphore);
            return nullptr;
        }
        // queue....
        //ESP_LOGE(TAG, "Message received");
        std::shared_ptr<void> tmp_ptr = _queue.front();
        _queue.pop();
        xSemaphoreGive(_mutex);
        return std::static_pointer_cast<T>(tmp_ptr);
    }
};

} // namespace movequeue

#endif