#include <Arduino.h>
#include <NightMare.h>
#include <creds.h>
#include <TempSensor.h>

using namespace NightMare;

static constexpr char DEVICE_ID[] = "temperature-node";
static MqttTransport transport;
static NightMare::Network network(DEVICE_ID, transport);
static WifiStation wifi;
static Runtime runtime;
static bool mqttStarted = false;
static ResourceMetadata temperatureMetadata = {"Room temperature", "C"};
static NetValue<float> temperature("temperature", NetAccess::READ, &temperatureMetadata);
static NetValue<bool> connected("connected");
static NetValue<String> sensorAddress("sensorAddress");
static NetAction<void> rescan("rescan", ActionResponse::ACK);
static NetEvent<void> sensorLost("sensorLost");

static ActionStatus handleRescan(void*, NetResource&, const String& arguments, String&) {
    if (arguments.length()) return ActionStatus::INVALID_ARGUMENT;
    rescanTempSensor();
    return ActionStatus::OK;
}

static void pollSensor(void*) {
    tickTempSensor();
    TempSensorStatus status = tempSensorStatus();
    bool wasConnected = connected.get();
    network.resources().set(connected, status.connected);
    if (wasConnected && !status.connected) network.resources().emit(sensorLost);
    if (status.connected) {
        network.resources().set(sensorAddress, String(status.address));
        if (!isnan(status.tempC)) network.resources().set(temperature, status.tempC);
    }
}

static void pollWifi(void*) {
    static uint32_t lastMqttAttemptMs = 0;
    bool newlyConnected = wifi.tick();
    uint32_t nowMs = millis();
    if (wifi.connected() && !mqttStarted &&
        (newlyConnected || static_cast<uint32_t>(nowMs - lastMqttAttemptMs) >= 10000)) {
        lastMqttAttemptMs = nowMs;
        mqttStarted = transport.begin(MQTT_URI, MQTT_USER, MQTT_PASSWD);
    }
}

void setup() {
    Serial.begin(115200);
    setupTempSensor();
    transport.attach(network);
    auto& resources = network.resources();
    resources.add(temperature, {true, 60000});
    resources.add(connected);
    resources.add(sensorAddress);
    resources.add(rescan);
    resources.add(sensorLost);
    resources.onAction(rescan, handleRescan);
    runtime.add(pollWifi);
    runtime.add([](void*) { network.tick(); });
    runtime.add(pollSensor);
    wifi.begin(DEFAULT_SSID, DEFAULT_PASSWORD, DEVICE_ID);
}

void loop() { runtime.tick(); }
