#pragma once
#include <Arduino.h>

/* Board revision registry -- the pattern from docs/architecture.md, section 5.5.
 * Append only; exactly one revision selected with -D BOARD_<CHIP>_V<N>;
 * pins are macros so unselected revisions cost nothing. */

#if (defined(BOARD_C3_V1) + defined(BOARD_ESP32_V1)) != 1
#error "Select exactly one board revision, e.g. -D BOARD_C3_V1 in platformio.ini."
#endif

#if defined(BOARD_C3_V1)
#define BOARD_NAME "ESP32-C3 SuperMini rev1"
#define PIN_ONE_WIRE 10 // DS18B20 data; needs a 4k7 pull-up to 3V3
#define BOARD_GPIO_MAX 21
#define BOARD_GPIO_IS_RESERVED(g) ((g) >= 11 && (g) <= 19) // flash + USB console

#elif defined(BOARD_ESP32_V1)
#define BOARD_NAME "ESP32 DevKit v1 rev1"
#define PIN_ONE_WIRE 4 // not 6-11: those are the SPI flash on the classic ESP32
#define BOARD_GPIO_MAX 39
#define BOARD_GPIO_IS_RESERVED(g) ((g) >= 6 && (g) <= 11)
#endif
