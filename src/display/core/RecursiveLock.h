#ifndef RECURSIVELOCK_H
#define RECURSIVELOCK_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class RecursiveLockGuard {
  public:
    explicit RecursiveLockGuard(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY) : mutex(mutex) {
        if (this->mutex != nullptr) {
            locked = xSemaphoreTakeRecursive(this->mutex, timeout) == pdTRUE;
        }
    }

    ~RecursiveLockGuard() {
        if (locked && mutex != nullptr) {
            xSemaphoreGiveRecursive(mutex);
        }
    }

    RecursiveLockGuard(const RecursiveLockGuard &) = delete;
    RecursiveLockGuard &operator=(const RecursiveLockGuard &) = delete;

    explicit operator bool() const { return locked; }

  private:
    SemaphoreHandle_t mutex = nullptr;
    bool locked = false;
};

#endif // RECURSIVELOCK_H
