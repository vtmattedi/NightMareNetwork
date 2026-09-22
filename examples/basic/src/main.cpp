#include <Arduino.h>
#include <NightMare.h>

static ManagedSensor<uint32_t> uptimeSeconds("uptime_s");
static ManagedState<uint8_t> brightness("brightness");
static ManagedAction identify("identify");

static bool onBrightnessWrite(ManagedState<uint8_t> &, const uint8_t &requested)
{
    if (requested > 100)
        return false;

    Serial.printf("brightness -> %u%%\n", static_cast<unsigned>(requested));
    return true;
}

static ActionResult onIdentify(ManagedAction &, const String &payload)
{
    if (payload.length() != 0)
        return {false, "identify takes no arguments"};

    Serial.println("NightMare basic example");
    return {true, "OK"};
}

static void publishUptime()
{
    uptimeSeconds.setValue(millis() / 1000UL);
}

static void preferLocalBroker(bool firstConnection)
{
    if (firstConnection)
        MQTT_change_to(LOCAL_MQTT);
}

void setup()
{
    Serial.begin(115200);

    brightness.onWrite = onBrightnessWrite;
    identify.onInvoke = onIdentify;

    gResourcesManager.bindResource(&uptimeSeconds);
    gResourcesManager.bindResource(&brightness);
    gResourcesManager.bindResource(&identify);

    uptimeSeconds.setValue(0);
    brightness.setValue(0);

    // Application C++ jobs are MANAGED and cannot be removed by JOB CLEAR.
    gScheduler.everyMonotonic("app.uptime", publishUptime, 1000);

    // The standard lifecycle starts Remote MQTT on the first WiFi connection.
    // For this local development example, switch immediately to Local MQTT.
    WiFi_onConnected(preferLocalBroker);

    startNightMareESP();
}

void loop()
{
    tickNightMareESP();
}
