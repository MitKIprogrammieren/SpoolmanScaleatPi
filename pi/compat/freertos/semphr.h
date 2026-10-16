#pragma once
#include "freertos/FreeRTOS.h"

typedef void* SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
#define xSemaphoreTakeRecursive xSemaphoreTake
#define xSemaphoreGiveRecursive xSemaphoreGive
void vSemaphoreDelete(SemaphoreHandle_t s);
