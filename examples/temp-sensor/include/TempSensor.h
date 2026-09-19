#pragma once
#include <Arduino.h>
#include <board.h>

#define DS18B20_PIN PIN_ONE_WIRE
#define TEMP_RESOLUTION_BITS 12
#define TEMP_READ_INTERVAL_MS 5000

struct TempSensorStatus {
    bool connected = false;
    float tempC = NAN;
    uint32_t lastReadMs = 0;
    char address[17] = {};
};

void setupTempSensor();
void tickTempSensor();
void rescanTempSensor();
TempSensorStatus tempSensorStatus();
