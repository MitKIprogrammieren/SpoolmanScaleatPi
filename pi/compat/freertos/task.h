#pragma once
#include "freertos/FreeRTOS.h"

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack_bytes,
                                   void* arg, UBaseType_t prio, TaskHandle_t* out, BaseType_t core);
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack_bytes, void* arg,
                       UBaseType_t prio, TaskHandle_t* out);
// vTaskDelete(NULL) ends the calling thread, as in FreeRTOS: it does not return.
[[noreturn]] void vTaskDeleteSelf();
void vTaskDeleteOther(TaskHandle_t t);
#define vTaskDelete(h) ((h) == nullptr ? vTaskDeleteSelf() : vTaskDeleteOther(h))
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount();
TaskHandle_t xTaskGetCurrentTaskHandle();
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t core);
#define xTaskGetCurrentTaskHandleForCPU xTaskGetCurrentTaskHandleForCore
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t);
const char* pcTaskGetName(TaskHandle_t t);
BaseType_t xPortGetCoreID();
