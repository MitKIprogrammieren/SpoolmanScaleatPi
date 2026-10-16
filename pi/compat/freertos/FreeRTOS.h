// FreeRTOS subset on std::thread / std::mutex for the Pi build.
#pragma once
#include <cstdint>
#include <cstddef>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu
#define portTICK_PERIOD_MS 1
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define tskIDLE_PRIORITY 0
#define tskNO_AFFINITY (-1)
#define configMAX_PRIORITIES 25

struct portMUX_TYPE_s { void* m; };
typedef struct portMUX_TYPE_s portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {nullptr}
void portENTER_CRITICAL(portMUX_TYPE* m);
void portEXIT_CRITICAL(portMUX_TYPE* m);
#define portENTER_CRITICAL_ISR(m) portENTER_CRITICAL(m)
#define portEXIT_CRITICAL_ISR(m) portEXIT_CRITICAL(m)
#define taskENTER_CRITICAL(m) portENTER_CRITICAL(m)
#define taskEXIT_CRITICAL(m) portEXIT_CRITICAL(m)

#include "freertos/task.h"
