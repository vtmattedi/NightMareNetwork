#pragma once
#include <Arduino.h>

// Example board/pin selection. Exactly one environment defines one BOARD_* flag.
#if (defined(BOARD_C3_V1) + defined(BOARD_ESP32_V1)) != 1
#error "Select exactly one board revision through platformio.ini."
#endif

#if defined(BOARD_C3_V1)

#define BOARD_NAME "ESP32-C3 SuperMini rev1"
#define PIN_ONE_WIRE 10

#elif defined(BOARD_ESP32_V1)

#define BOARD_NAME "ESP32 DevKit v1 rev1"
#define PIN_ONE_WIRE 4

#endif
