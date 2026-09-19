#include <Arduino.h>
#include <NightMare.h>
#include <creds.h>

using namespace NightMare;

static constexpr char DEVICE_ID[] = "example-device";
static MqttTransport transport;
static NightMare::Network network(DEVICE_ID, transport);
static WifiStation wifi;
static Runtime runtime;
static Console console(network.resources());
static bool mqttStarted = false;

static ResourceMetadata brightnessMetadata = {"Brightness", "percent", nullptr, "light", "0", "100"};
static NetValue<uint8_t> brightness("brightness", NetAccess::READ_WRITE, &brightnessMetadata);
static NetValue<uint32_t> uptime("uptime");
static NetValue<String> firmwareVersion("firmwareVersion");
static NetAction<void> toggle("toggle", ActionResponse::ACK);
static NetEvent<void> buttonPressed("buttonPressed");
struct ColorArgs { uint8_t red, green, blue; };
namespace NightMare {
template<> struct NetCodec<ColorArgs> {
    static String encode(const ColorArgs& color) {
        return "[" + String(color.red) + "," + String(color.green) + "," + String(color.blue) + "]";
    }
    static bool decode(const String& text, ColorArgs& color) {
        if (text.length() < 7 || text[0] != '[' || text[text.length() - 1] != ']') return false;
        String values = text.substring(1, text.length() - 1);
        int first = values.indexOf(',');
        int second = values.indexOf(',', first + 1);
        if (first <= 0 || second <= first + 1 || values.indexOf(',', second + 1) >= 0) return false;
        return NetCodec<uint8_t>::decode(values.substring(0, first), color.red) &&
               NetCodec<uint8_t>::decode(values.substring(first + 1, second), color.green) &&
               NetCodec<uint8_t>::decode(values.substring(second + 1), color.blue);
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
    network.resources().set(brightness, value);
    return ActionStatus::OK;
}

static ActionStatus toggleLight(void*, NetResource&, const String& arguments, String&) {
    if (arguments.length()) return ActionStatus::INVALID_ARGUMENT;
    network.resources().set(brightness, static_cast<uint8_t>(brightness.get() ? 0 : 100));
    return ActionStatus::OK;
}

static ActionStatus applyColor(void*, NetResource& resource, const String& arguments, String&) {
    ColorArgs next{};
    if (!static_cast<NetAction<ColorArgs>&>(resource).parse(arguments, next))
        return ActionStatus::INVALID_ARGUMENT;
    uint32_t rgb = (static_cast<uint32_t>(next.red) << 16) |
                   (static_cast<uint32_t>(next.green) << 8) | next.blue;
    network.resources().set(color, rgb);
    return ActionStatus::OK;
}

static void updateUptime(void*) {
    network.resources().set(uptime, static_cast<uint32_t>(millis() / 1000UL));
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
    transport.attach(network);
    auto& resources = network.resources();
    resources.add(brightness);
    resources.add(uptime, {false, 15000});
    resources.add(firmwareVersion);
    resources.add(toggle);
    resources.add(buttonPressed);
    resources.add(color);
    resources.add(setColor);
    resources.onWrite(brightness, setBrightness);
    resources.onAction(toggle, toggleLight);
    resources.onAction(setColor, applyColor);
    resources.set(brightness, static_cast<uint8_t>(0));
    resources.set(firmwareVersion, String("1.0.0"));
    resources.invoke(setColor, ColorArgs{255, 120, 0});

    runtime.add(pollWifi);
    runtime.add([](void*) { network.tick(); });
    runtime.add([](void*) { console.tick(Serial); });
    runtime.scheduler().every("uptime", 1000, updateUptime);
    wifi.begin(DEFAULT_SSID, DEFAULT_PASSWORD, DEVICE_ID);
}

void loop() {
    runtime.tick();
    // A button handler can call network.resources().emit(buttonPressed).
}
