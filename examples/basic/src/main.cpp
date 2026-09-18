#include <Arduino.h>
#include <NightMareNetwork.h>

// Anything the library's built-in resolver does not answer lands here.
// Command and subcommand arrive uppercased; a command's own parameters start
// at args[1] because args[0] is the subcommand.
NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
    NightMareResults res;
    if (message.command == "HELLO")
    {
        res.result = true;
        res.response = "Hello from " + String(getDeviceName());
        return res;
    }
    if (message.command == "ECHO")
    {
        res.result = true;
        res.response = message.args[0] + " " + message.args[1];
        return res;
    }
    res.result = false;
    res.response = "Unknown command. Available: [HELLO, ECHO].";
    return res;
}

// Runs on the WiFi task. MQTT is started here rather than in setup() because
// there is no network to connect to until now.
void onWifiConnected(bool firstConnection)
{
    Serial.println("WiFi connected");
    if (firstConnection)
        MQTT_Init(REMOTE_MQTT);
}

void setup()
{
    Config.begin(); // first: the device name and every module's settings come from it
    Serial.begin(115200);
    Serial.println(getDeviceName());
    Serial.println("Starting NightMare Network...");

    setCommandResolver(localHandleNightMareCommand); // before WiFi, so an early command has somewhere to go
    WiFi_onConnected(onWifiConnected);
    WiFi_Auto();

    // The library already publishes <Device>/telemetry every 15 s; a device
    // adds its own timers for its own work.
    Timers.create("heartbeat", 60, []()
                  { Serial.printf("up %lu s, heap %u\n", millis() / 1000, ESP.getFreeHeap()); });
}

void loop()
{
    Timers.run();
    scheduler.run();
    NightMareCommand_SerialResolver(&Serial, '\n');
}
