#include <Arduino.h>
#include <NightMare.h>
#include <TempSensor.h>

static ManagedSensor<float> temperature("temperature");
static ManagedSensor<bool> connected("connected");
static ManagedSensor<String> sensorAddress("sensor_address");
static ManagedAction rescan("rescan");

static uint32_t publishedReadMs = 0;
static String publishedAddress;

static ActionResult onRescan(ManagedAction &, const String &payload)
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

    if (!connected.hasAuthoritativeValue() ||
        connected.getValue() != status.connected)
    {
        connected.setValue(status.connected);
    }

    if (status.connected && status.address[0] != '\0')
    {
        const String address(status.address);
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
        MQTT_change_to(LOCAL_MQTT);
}

void setup()
{
    Serial.begin(115200);

    setupTempSensor();

    rescan.onInvoke = onRescan;

    gResourcesManager.bindResource(&temperature);
    gResourcesManager.bindResource(&connected);
    gResourcesManager.bindResource(&sensorAddress);
    gResourcesManager.bindResource(&rescan);

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
