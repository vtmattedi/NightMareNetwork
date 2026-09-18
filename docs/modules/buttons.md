---
title: Buttons
description: A debounced button on a GPIO, watched on its own task, reporting click, double-click and long-press events.
section: modules
order: 140
---

# Buttons — `Core/buttons.h`

One call turns a GPIO into a button that reports events, on a task of its
own, so `loop()` never polls it.

```cpp
typedef enum {
    BUTTON_EVENT_NONE, BUTTON_EVENT_PRESSED, BUTTON_EVENT_RELEASED,
    BUTTON_EVENT_CLICKED, BUTTON_EVENT_DOUBLE_CLICKED,
    BUTTON_EVENT_LONGPRESS, BUTTON_EVENT_LONGPRESS_RELEASED,
    BUTTON_EVENT_ANY
} ButtonEvent;

ButtonTaskHandle_t createButtonOnPin(uint8_t pin, void (*handler)(ButtonEvent),
                                     uint32_t longPressDurationMs = 1000,
                                     unsigned long debounceDelayMs = 30,
                                     unsigned long doubleClickWindowMs = 350,
                                     TickType_t pollingDelay = 10);
const char *getButtonEventName(ButtonEvent event);
```

The pin is read active-low with the internal pull-up — the way a dev board's
BOOT button and a bare switch to ground are wired. A `CLICKED` fires on
release when no second press followed within the double-click window; a
`DOUBLE_CLICKED` replaces it when one did; `LONGPRESS` fires while still held
once the duration passes.

```cpp
createButtonOnPin(PIN_BUTTON, [](ButtonEvent e) {
    if (e == BUTTON_EVENT_CLICKED) irSendByName("POWER");
});
```

The handler runs on the button's task, so it should do what a callback
should: set a flag, queue something, send an IR code through a queue — not
build JSON and publish it.

The last argument's type is deliberately `TickType_t`: the declaration and
the definition must agree exactly or the mangled names differ and the link
fails with an unexplained undefined reference. That comment is in the header
because it happened.
