#include "buttons.h"
#ifndef COMPILE_BUTTONS_MODULE
#define COMPILE_SERIAL
#ifndef BTNS_TAG
#define BTNS_TAG "\x1b[33m[Buttons]\x1b[0m"
#endif
#ifdef COMPILE_SERIAL
#define BTNS_TAGF(fmt, ...) Serial.printf("%s%s" fmt "\n", MILLIS_LOG, BTNS_TAG, ##__VA_ARGS__)
#else
#define BTNS_TAGF(fmt, ...)
#endif

struct ButtonTaskParams
{
    uint8_t pin;
    void (*handler)(ButtonEvent event);
    uint32_t longPressDurationMs;
    unsigned long debounceDelayMs;
    unsigned long doubleClickWindowMs;
    TickType_t pollingDelay;
};

void buttonTask(void *pvParameters)
{
    ButtonTaskParams *params = (ButtonTaskParams *)pvParameters;
    uint8_t pin = params->pin;
    void (*handler)(ButtonEvent) = params->handler;
    uint32_t longPressDurationMs = params->longPressDurationMs;
    unsigned long debounceDelayMs = params->debounceDelayMs;
    unsigned long doubleClickWindowMs = params->doubleClickWindowMs;
    TickType_t pollingDelay = params->pollingDelay;
    delete params;

    pinMode(pin, INPUT_PULLUP);
    bool lastReadingPressed = digitalRead(pin) == LOW;
    bool stablePressed = lastReadingPressed;
    unsigned long lastDebounceTime = millis();
    unsigned long pressStartTime = stablePressed ? millis() : 0;
    unsigned long pendingClickTime = 0;
    bool longPressDetected = false;
    bool waitingForSecondClick = false;

    auto emitEvent = [handler](ButtonEvent event)
    {
        if (handler != nullptr)
        {
            handler(event);
        }
    };

    for (;;)
    {
        unsigned long now = millis();
        bool currentReadingPressed = digitalRead(pin) == LOW;

        if (currentReadingPressed != lastReadingPressed)
        {
            lastDebounceTime = now;
            lastReadingPressed = currentReadingPressed;
        }

        if ((now - lastDebounceTime) >= debounceDelayMs && currentReadingPressed != stablePressed)
        {
            stablePressed = currentReadingPressed;

            if (stablePressed)
            {
                pressStartTime = now;
                longPressDetected = false;
                emitEvent(BUTTON_EVENT_PRESSED);
            }
            else
            {
                emitEvent(BUTTON_EVENT_RELEASED);

                if (longPressDetected)
                {
                    emitEvent(BUTTON_EVENT_LONGPRESS_RELEASED);
                    longPressDetected = false;
                    waitingForSecondClick = false;
                }
                else if (waitingForSecondClick && (now - pendingClickTime) <= doubleClickWindowMs)
                {
                    emitEvent(BUTTON_EVENT_DOUBLE_CLICKED);
                    waitingForSecondClick = false;
                }
                else
                {
                    waitingForSecondClick = true;
                    pendingClickTime = now;
                }
            }
        }

        if (stablePressed && !longPressDetected && (now - pressStartTime) >= longPressDurationMs)
        {
            longPressDetected = true;
            emitEvent(BUTTON_EVENT_LONGPRESS);
        }

        if (waitingForSecondClick && !stablePressed && (now - pendingClickTime) > doubleClickWindowMs)
        {
            emitEvent(BUTTON_EVENT_CLICKED);
            waitingForSecondClick = false;
        }

        vTaskDelay(pdMS_TO_TICKS(pollingDelay));
    }
}

ButtonTaskHandle_t createButtonOnPin(
    uint8_t pin,
    void (*handler)(ButtonEvent event),
    uint32_t longPressDurationMs,
    unsigned long debounceDelayMs,
    unsigned long doubleClickWindowMs,
    TickType_t pollingDelay)
{
    ButtonTaskParams *params = new ButtonTaskParams{pin, handler, longPressDurationMs, debounceDelayMs, doubleClickWindowMs, pollingDelay};
    TaskHandle_t taskHandle;
    char taskName[16];
    snprintf(taskName, sizeof(taskName), "Button%u", static_cast<unsigned int>(pin));
    BaseType_t result = xTaskCreate(buttonTask, taskName, 2048, params, 1, &taskHandle);
    if (result != pdPASS)
    {
        BTNS_TAGF("Failed to create button task for pin %d", pin);
        delete params; // Clean up if task creation failed
        return NULL;
    }
    return taskHandle;
}

const char *getButtonEventName(ButtonEvent event)
{
    switch (event)
    {
    case BUTTON_EVENT_NONE:
        return "NONE";
    case BUTTON_EVENT_PRESSED:
        return "PRESSED";
    case BUTTON_EVENT_RELEASED:
        return "RELEASED";
    case BUTTON_EVENT_CLICKED:
        return "CLICKED";
    case BUTTON_EVENT_DOUBLE_CLICKED:
        return "DOUBLE_CLICKED";
    case BUTTON_EVENT_LONGPRESS:
        return "LONGPRESS";
    case BUTTON_EVENT_LONGPRESS_RELEASED:
        return "LONGPRESS_RELEASED";
    }
    return "UNKNOWN";
}
#endif