#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, TaskHandle_t *handle);
