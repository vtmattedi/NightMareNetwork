#pragma once
#include <Arduino.h>
#include <board.h>

/// 1-Wire data pin for the DS18B20. The bus needs an external 4.7k pull-up to 3.3V: the ESP32
/// internal one (~45k) is too weak for it. Most 3-pin breakout modules already carry the resistor;
/// bare TO-92 parts and waterproof probes do not.
#define DS18B20_PIN PIN_ONE_WIRE
/// 9-12. 12 bits gives 0.0625 C steps and takes 750 ms to convert; each bit less halves both.
#define TEMP_RESOLUTION_BITS 12
/// Time from the start of one reading to the start of the next.
#define TEMP_READ_INTERVAL_MS 5000
/// Consecutive failed reads before the sensor is treated as gone and the bus is searched again.
/// A single CRC error is not worth reporting the temperature as unknown.
#define TEMP_MAX_FAILURES 3

/// Only bus I/O and the odd log line run on this task -- no JSON or MQTT.
#define TEMP_TASK_STACK 3072
/// Above the Arduino loop task (1), far below WiFi (18+).
#define TEMP_TASK_PRIORITY 1

/// @brief Everything the task knows about the sensor, as of one instant.
struct TempSensorStatus
{
    bool connected;      ///< Found on the bus and not yet written off by TEMP_MAX_FAILURES.
    bool parasite;       ///< Powered from the data line instead of VDD.
    float tempC;         ///< Same as currentTemperature(): NAN when there is no reading.
    uint32_t lastReadMs; ///< millis() of the last good reading, 0 if there has never been one.
    char address[17];    ///< ROM code in hex; empty while not connected.
};

/// @brief Starts the sampling task and returns immediately. The sensor is found and read on the
/// task; a missing one is retried every TEMP_READ_INTERVAL_MS, so it can be plugged in later.
void setupTempSensor();
/// @brief Latest reading in Celsius, or NAN when there is none (not found yet, or lost).
/// Never blocks: it returns what the task last read.
float currentTemperature();
/// @brief Snapshot of the sensor state for diagnostics. Copied under one lock, so the fields never
/// mix values from before and after one of the task's updates.
TempSensorStatus tempSensorStatus();
