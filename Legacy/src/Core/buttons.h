#pragma once
#ifndef NIGHTMARE_CORE_BUTTONS_H
#define NIGHTMARE_CORE_BUTTONS_H
#include <Modules.config.h>
#include <LOGS.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

typedef enum
{
    BUTTON_EVENT_NONE,
    BUTTON_EVENT_PRESSED,
    BUTTON_EVENT_RELEASED,
    BUTTON_EVENT_CLICKED,
    BUTTON_EVENT_DOUBLE_CLICKED,
    BUTTON_EVENT_LONGPRESS,
    BUTTON_EVENT_LONGPRESS_RELEASED,
    BUTTON_EVENT_ANY
} ButtonEvent;
typedef TaskHandle_t ButtonTaskHandle_t;
const char *getButtonEventName(ButtonEvent event);
ButtonTaskHandle_t createButtonOnPin(
    uint8_t pin,
    void (*handler)(ButtonEvent event),
    uint32_t longPressDurationMs = 1000,
    unsigned long debounceDelayMs = 30,
    unsigned long doubleClickWindowMs = 350,
    TickType_t pollingDelay = 10); // must match the definition in buttons.cpp (TickType_t), else the mangled names differ -> linker error

#endif