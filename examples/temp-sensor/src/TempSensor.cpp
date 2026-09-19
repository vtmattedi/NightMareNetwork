#include <TempSensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire oneWire;
static DallasTemperature sensors(&oneWire);
static DeviceAddress address;
static TempSensorStatus status;
static uint32_t nextReadMs = 0;
static uint32_t conversionStartedMs = 0;
static bool converting = false;
static uint8_t failures = 0;

void setupTempSensor() {
    oneWire.begin(DS18B20_PIN);
    sensors.setWaitForConversion(false);
    nextReadMs = 0;
}

void rescanTempSensor() {
    status.connected = false;
    status.tempC = NAN;
    status.address[0] = '\0';
    converting = false;
    failures = 0;
    nextReadMs = 0;
}

void tickTempSensor() {
    uint32_t nowMs = millis();
    if (converting) {
        if (static_cast<uint32_t>(nowMs - conversionStartedMs) <
            sensors.millisToWaitForConversion(TEMP_RESOLUTION_BITS)) return;
        converting = false;
        float reading = sensors.getTempC(address);
        if (reading == DEVICE_DISCONNECTED_C) {
            if (++failures >= 3) rescanTempSensor();
        } else {
            failures = 0;
            status.tempC = reading;
            status.lastReadMs = nowMs;
        }
        nextReadMs = nowMs + TEMP_READ_INTERVAL_MS;
        return;
    }
    if (static_cast<int32_t>(nowMs - nextReadMs) < 0) return;
    if (!status.connected) {
        sensors.begin();
        if (!sensors.getAddress(address, 0) ||
            !sensors.setResolution(address, TEMP_RESOLUTION_BITS)) {
            nextReadMs = nowMs + TEMP_READ_INTERVAL_MS;
            return;
        }
        status.connected = true;
        for (int i = 0; i < 8; ++i)
            snprintf(status.address + i * 2, 3, "%02X", address[i]);
    }
    if (!sensors.requestTemperaturesByAddress(address)) {
        if (++failures >= 3) rescanTempSensor();
        nextReadMs = nowMs + TEMP_READ_INTERVAL_MS;
        return;
    }
    conversionStartedMs = nowMs;
    converting = true;
}

TempSensorStatus tempSensorStatus() { return status; }
