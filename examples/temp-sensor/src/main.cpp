#include <Arduino.h>
#include <NightMare.h>
#include <TempSensor.h>

static ManagedSensor<float> temperature("temperature");
static ManagedSensor<bool> connected("connected");
static ManagedSensor<std::string> sensorAddress("sensor_address");
static ManagedAction rescan("rescan");

static uint32_t publishedReadMs = 0;
static std::string publishedAddress;
static bool publishedConnected = false;

static ActionResult onRescan(ManagedAction &, const std::string &payload)
{
    if (payload.length() != 0)
        return {false, "rescan takes no arguments"};

    rescanTempSensor();
    connected.setValue(false);
    return {true, "OK"};
}

static void pollSensor()
{
    tickTempSensor();
    const TempSensorStatus status = tempSensorStatus();

    if (!publishedConnected ||
        connected.getValue() != status.connected)
    {
        publishedConnected = connected.setValue(status.connected);
    }

    if (status.connected && status.address[0] != '\0')
    {
        const std::string address(status.address);
        if (address != publishedAddress)
        {
            publishedAddress = address;
            sensorAddress.setValue(address);
        }
    }

    if (status.connected &&
        !isnan(status.tempC) &&
        status.lastReadMs != 0 &&
        status.lastReadMs != publishedReadMs)
    {
        publishedReadMs = status.lastReadMs;
        temperature.setValue(status.tempC);
    }
}

static void preferLocalBroker(bool firstConnection)
{
    if (firstConnection)
    {
        NightMare::Mqtt_enable(NightMare::MqttProfile::LOCAL);
        NightMare::SelectConnection(NightMare::ConnectionType::MQTT);
    }
}

void setup()
{
    Serial.begin(115200);

    setupTempSensor();

    rescan.onInvoke = onRescan;

    connected.setValue(false);

    // The DS18B20 driver is non-blocking; service its state machine regularly.
    gScheduler.timer("app.temp.poll", pollSensor, 100);

    WiFi_onConnected(preferLocalBroker);

    startNightMareESP();
}

void loop()
{
    tickNightMareESP();
}
