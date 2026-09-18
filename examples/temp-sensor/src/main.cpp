#include <Arduino.h>
#include <NightMareNetwork.h>
#include <ArduinoJson.h>
#include <TempSensor.h>
#include <board.h>

// A one-sensor device, speaking the sensors protocol as documented:
//   <Device>/sensors  {"temperature": 23.44}    on change and every 60 s
//   `sensors`         the declaration the backend reads on discovery
//   SENSORS REPORT | SENSORS INFO | DS18 READ | DS18 STATUS | HELP

// ---- the sensor's two JSON writers ------------------------------------------
// One feeds the readings object, one feeds the declaration; the aggregators
// below call these, so adding a sensor is one more pair of writers.

static void tempSensorReport(JsonObject into)
{
    // NAN serialises as null, which the backend stores as "no reading" -- not zero.
    into["temperature"] = currentTemperature();
}

static void tempSensorInfo(JsonObject into)
{
    TempSensorStatus s = tempSensorStatus();
    JsonObject t = into.createNestedObject("temperature");
    // The five the backend reads. `id` must equal the key the reading is published under.
    t["id"] = "temperature";
    t["label"] = "Room temperature";
    t["unit"] = "\xC2\xB0" "C"; // "°C" as UTF-8 bytes, so the source file stays ASCII
    t["type"] = "float";
    t["disable"] = false;
    t["critical"] = false;
    // The rest is for people and for the Dashboard.
    t["hardware"] = "DS18B20";
    t["pin"] = DS18B20_PIN;
    t["connected"] = s.connected;
    t["address"] = s.address;
    t["parasite"] = s.parasite;
    t["value"] = s.tempC;
    if (s.lastReadMs)
        t["age_ms"] = millis() - s.lastReadMs;
}

// ---- aggregators ------------------------------------------------------------

static String sensorsReportJson()
{
    DynamicJsonDocument doc(256);
    JsonObject root = doc.to<JsonObject>();
    tempSensorReport(root);
    String out;
    serializeJson(doc, out);
    return out;
}

static String sensorsDeclarationJson()
{
    DynamicJsonDocument doc(512);
    JsonObject root = doc.to<JsonObject>();
    tempSensorInfo(root);
    String out;
    serializeJson(doc, out);
    return out;
}

static void publishSensors()
{
    MQTT_Send("/sensors", sensorsReportJson());
}

static void publishInfo()
{
    DynamicJsonDocument doc(768);
    doc["device"] = getDeviceName();
    doc["board"] = BOARD_NAME;
    JsonObject sensors = doc.createNestedObject("sensors");
    tempSensorInfo(sensors);
    String out;
    serializeJson(doc, out);
    MQTT_Send("/info", out);
}

// Publish on change: a reading that moved by a step worth seeing, or a sensor
// that appeared or vanished. The 60 s heartbeat covers the rest.
static void watchForChange()
{
    static float last = NAN;
    static bool lastConnected = false;
    TempSensorStatus s = tempSensorStatus();
    bool moved = (isnan(last) != isnan(s.tempC)) || (!isnan(s.tempC) && fabsf(s.tempC - last) >= 0.25f);
    if (moved || s.connected != lastConnected)
    {
        last = s.tempC;
        lastConnected = s.connected;
        publishSensors();
    }
}

// ---- commands ---------------------------------------------------------------

NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
    NightMareResults res;
    res.result = false;
    res.response = "not implemented";

    if (message.command == "SENSORS")
    {
        // Bare `sensors` -- no subcommand -- is the declaration the backend asks for on
        // discovery. The subcommands are the human-facing views of the same data.
        if (message.subcommand == "")
        {
            res.result = true;
            res.response = sensorsDeclarationJson();
        }
        else if (message.subcommand == "REPORT")
        {
            res.result = true;
            res.response = sensorsReportJson();
        }
        else if (message.subcommand == "INFO")
        {
            res.result = true;
            res.response = sensorsDeclarationJson();
        }
        else
        {
            res.response = "Unknown SENSORS subcommand available: [REPORT, INFO].";
        }
    }
    else if (message.command == "DS18")
    {
        // Both read what the task last published; neither touches the bus.
        TempSensorStatus s = tempSensorStatus();
        if (message.subcommand == "READ")
        {
            res.result = !isnan(s.tempC);
            if (res.result)
                res.response = String(s.tempC, 2);
            else if (s.connected)
                res.response = "Sensor found, first conversion still running.";
            else
                res.response = "No DS18B20 found on GPIO" + String(DS18B20_PIN) + ".";
        }
        else if (message.subcommand == "STATUS")
        {
            DynamicJsonDocument doc(256);
            doc["connected"] = s.connected;
            doc["pin"] = DS18B20_PIN;
            doc["address"] = s.address;
            doc["parasite"] = s.parasite;
            doc["temperature"] = s.tempC; // null while NAN
            if (s.lastReadMs)
                doc["age_ms"] = millis() - s.lastReadMs;
            String json;
            serializeJson(doc, json);
            res.response = json;
            res.result = true;
        }
        else
        {
            res.response = "Unknown DS18 subcommand available: [READ, STATUS].";
        }
    }
    else if (message.command == "HELP")
    {
        res.result = true;
        res.response = "Available commands: SENSORS [REPORT|INFO], DS18 [READ|STATUS], HELP.";
    }
    else
    {
        res.response = "Unknown command available: [SENSORS, DS18, HELP].";
    }
    return res;
}

// ---- lifecycle --------------------------------------------------------------

void onWifiConnected(bool firstConnection)
{
    if (firstConnection)
        MQTT_Init(REMOTE_MQTT);
}

void setup()
{
    Config.begin();
    Serial.begin(115200);
    Serial.printf("%s on %s\n", getDeviceName(), BOARD_NAME);

    setCommandResolver(localHandleNightMareCommand);
    WiFi_onConnected(onWifiConnected);
    WiFi_Auto();

    setupTempSensor();

    // The declaration on every connect, so a consumer that just came up has it without asking.
    MQTT_onConnected(publishInfo);
    Timers.create("sensors_heartbeat", 60, publishSensors);
    Timers.create("sensors_watch", 5, watchForChange);
}

void loop()
{
    Timers.run();
    scheduler.run();
    NightMareCommand_SerialResolver(&Serial, '\n');
}
