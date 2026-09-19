#include <Arduino.h>
#include <ArduinoJson.h>
#include <NightMare.h>
#include <creds.h>

using namespace NightMare;

static Esp32Device device;
static ResourceMetadata brightnessMetadata = {"Brightness", "percent", nullptr, "light", "0", "100"};
static NetValue<uint8_t> brightness("brightness", NetAccess::READ_WRITE, &brightnessMetadata);
static NetAction<void> toggle("toggle", ActionResponse::ACK);
static NetEvent<void> buttonPressed("buttonPressed");

struct ColorArgs { uint8_t red, green, blue; };
namespace NightMare {
template<> struct NetCodec<ColorArgs> {
    static String encode(const ColorArgs& color) {
        return "[" + String(color.red) + "," + String(color.green) + "," + String(color.blue) + "]";
    }
    static bool decode(const String& text, ColorArgs& color) {
        DynamicJsonDocument doc(128);
        if (deserializeJson(doc, text) || !doc.is<JsonArray>() || doc.size() != 3) return false;
        String red = doc[0].as<String>(), green = doc[1].as<String>(), blue = doc[2].as<String>();
        return NetCodec<uint8_t>::decode(red, color.red) &&
               NetCodec<uint8_t>::decode(green, color.green) &&
               NetCodec<uint8_t>::decode(blue, color.blue);
    }
};
}

static const ResourceMetadata::Field colorFields[] = {
    {"red", NetValueType::UINT8}, {"green", NetValueType::UINT8}, {"blue", NetValueType::UINT8}
};
static ResourceMetadata colorMetadata = {"Set color", nullptr, nullptr, "light", nullptr, nullptr, colorFields, 3};
static NetAction<ColorArgs> setColor("setColor", ActionResponse::ACK, &colorMetadata);
static NetValue<uint32_t> color("color");

static ActionStatus setBrightness(void*, NetResource&, const String& requested) {
    uint8_t value;
    if (!NetCodec<uint8_t>::decode(requested, value) || value > 100)
        return ActionStatus::INVALID_ARGUMENT;
    device.resources().set(brightness, value);
    return ActionStatus::OK;
}

static ActionStatus toggleLight(void*, NetResource&, const String& arguments, String&) {
    if (arguments.length()) return ActionStatus::INVALID_ARGUMENT;
    device.resources().set(brightness, static_cast<uint8_t>(brightness.get() ? 0 : 100));
    return ActionStatus::OK;
}

static ActionStatus applyColor(void*, NetResource& resource, const String& arguments, String&) {
    ColorArgs next{};
    if (!static_cast<NetAction<ColorArgs>&>(resource).parse(arguments, next))
        return ActionStatus::INVALID_ARGUMENT;
    uint32_t rgb = (static_cast<uint32_t>(next.red) << 16) |
                   (static_cast<uint32_t>(next.green) << 8) | next.blue;
    device.resources().set(color, rgb);
    return ActionStatus::OK;
}

void setup() {
    Serial.begin(115200);
    Esp32DeviceOptions options;
    options.wifiSsid = DEFAULT_SSID;
    options.wifiPassword = DEFAULT_PASSWORD;
    options.mqttUri = MQTT_URI;
    options.mqttUser = MQTT_USER;
    options.mqttPassword = MQTT_PASSWD;
    options.firmwareVersion = "1.0.0";
    options.serialConsole = &Serial;
    if (!device.begin(options)) { Serial.println("NightMare startup failed"); return; }

    auto& resources = device.resources();
    resources.add(brightness);
    resources.add(toggle);
    resources.add(buttonPressed);
    resources.add(color);
    resources.add(setColor);
    resources.onWrite(brightness, setBrightness);
    resources.onAction(toggle, toggleLight);
    resources.onAction(setColor, applyColor);
    resources.set(brightness, static_cast<uint8_t>(0));
    // The facade already registers and updates uptime, heap, chip and boot Values.
    resources.invoke(setColor, ColorArgs{255, 120, 0});
}

void loop() {
    device.tick();
    // A button handler can call device.resources().emit(buttonPressed).
}
