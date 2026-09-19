#include <Arduino.h>
#include <unity.h>
#include <NightMare.h>

using namespace NightMare;

class MemoryBackend final : public SettingsPersistence {
public:
    String keys[8], values[8];
    size_t count = 0;
    bool load(SettingsStore& store) override {
        for (size_t i = 0; i < count; ++i)
            if (!store.setString(keys[i].c_str(), values[i], SettingsAccess::Internal)) return false;
        return true;
    }
    bool save(const SettingsStore& store) override {
        count = 0;
        store.visit([](void* context, const String& key, const String& value) {
            auto* self = static_cast<MemoryBackend*>(context);
            if (self->count < 8) {
                self->keys[self->count] = key;
                self->values[self->count++] = value;
            }
        }, this, SettingsAccess::Internal);
        return true;
    }
};

class MockTransport final : public Transport {
public:
    bool online = false;
    String lastTopic, lastPayload;
    uint32_t publications = 0;
    bool connected() const override { return online; }
    bool publish(const String& topic, const String& payload, bool = false) override {
        if (!online) return false;
        lastTopic = topic;
        lastPayload = payload;
        ++publications;
        return true;
    }
};

static void settingsAndIdentity() {
    MemoryBackend backend;
    SettingsStore first("config", StorageMode::Persistent, &backend);
    TEST_ASSERT_TRUE(first.begin());
    TEST_ASSERT_TRUE(first.setBool("enabled", true));
    TEST_ASSERT_TRUE(first.setInt("count", -7));
    TEST_ASSERT_TRUE(first.setFloat("gain", 1.25f));
    TEST_ASSERT_TRUE(first.setString("label", "Bedroom AC"));
    TEST_ASSERT_TRUE(first.getBool("enabled"));
    TEST_ASSERT_EQUAL_INT32(-7, first.getInt("count"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, first.getFloat("gain"));
    TEST_ASSERT_EQUAL_STRING("Bedroom AC", first.getString("label").c_str());
    TEST_ASSERT_TRUE(first.setString("_internal", "secret", SettingsAccess::Internal));
    TEST_ASSERT_FALSE(first.exists("_internal"));
    TEST_ASSERT_FALSE(first.remove("_internal"));
    TEST_ASSERT_TRUE(first.clear());
    TEST_ASSERT_TRUE(first.exists("_internal", SettingsAccess::Internal));
    TEST_ASSERT_EQUAL_UINT32(0, first.count());
    TEST_ASSERT_TRUE(first.setString("label", "Bedroom AC"));
    DeviceIdentity identity;
    TEST_ASSERT_TRUE(identity.begin(first, "abcdef"));
    String stable = identity.id();
    TEST_ASSERT_TRUE(identity.setLabel("Bedroom AC"));
    TEST_ASSERT_EQUAL_STRING(stable.c_str(), identity.id().c_str());
    TEST_ASSERT_TRUE(identity.adoptId("new-device-id"));
    TEST_ASSERT_EQUAL_STRING(stable.c_str(), identity.id().c_str());
    SettingsStore second("config", StorageMode::Persistent, &backend);
    TEST_ASSERT_TRUE(second.begin());
    TEST_ASSERT_EQUAL_STRING("Bedroom AC", second.getString("label").c_str());
    TEST_ASSERT_EQUAL_STRING("new-device-id",
        second.getString("_device_id", "", SettingsAccess::Internal).c_str());
    TEST_ASSERT_TRUE(second.clear(SettingsAccess::Internal));
    TEST_ASSERT_EQUAL_UINT32(0, second.count(SettingsAccess::Internal));
}

static void parserCases() {
    ParsedCommand parsed;
    String error;
    TEST_ASSERT_TRUE(CommandParser::parse("config   set label \"Bedroom \\\"AC\\\"\"", parsed, error));
    TEST_ASSERT_EQUAL_UINT8(4, parsed.count);
    TEST_ASSERT_EQUAL_STRING("Bedroom \"AC\"", parsed.words[3].c_str());
    TEST_ASSERT_TRUE(CommandParser::parse("config set label \"\"", parsed, error));
    TEST_ASSERT_EQUAL_UINT8(4, parsed.count);
    TEST_ASSERT_EQUAL_STRING("", parsed.words[3].c_str());
    TEST_ASSERT_FALSE(CommandParser::parse("config set broken \\", parsed, error));
    TEST_ASSERT_EQUAL_STRING("unterminated escape", error.c_str());
    String longLine;
    for (int i = 0; i < 257; ++i) longLine += 'a';
    TEST_ASSERT_FALSE(CommandParser::parse(longLine, parsed, error));
}

static ActionStatus captureAction(void* context, NetResource&, const String& payload, String& result) {
    *static_cast<String*>(context) = payload;
    result = "done";
    return ActionStatus::OK;
}

static void commandsAndFreshness() {
    MockTransport transport;
    ResourceManager resources("local", transport);
    SettingsStore settings("runtime", StorageMode::Memory);
    TEST_ASSERT_TRUE(settings.begin());
    TEST_ASSERT_TRUE(settings.setString("_hidden", "x", SettingsAccess::Internal));
    NetAction<String> rename("rename", ActionResponse::RESULT);
    TEST_ASSERT_TRUE(resources.add(rename));
    String received;
    resources.onAction(rename, captureAction, &received);
    CommandRouter router(resources);
    router.attachSettings(settings);
    auto result = router.execute("> rename \"Bedroom AC\"");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("~Bedroom AC", received.c_str());
    TEST_ASSERT_EQUAL_STRING("done", result.text.c_str());
    result = router.execute("rename \"Second Room\"");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("~Second Room", received.c_str());
    static const ResourceMetadata::Field fields[] = {
        {"red", NetValueType::UINT8}, {"green", NetValueType::UINT8}, {"blue", NetValueType::UINT8}
    };
    ResourceMetadata metadata;
    metadata.fields = fields;
    metadata.fieldCount = 3;
    NetResource color("setColor", NetResourceKind::ACTION, NetValueType::STRUCT,
                      NetAccess::READ, &metadata);
    TEST_ASSERT_TRUE(resources.add(color));
    resources.onAction(color, captureAction, &received);
    result = router.execute("> setColor 255 120 0");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("[255,120,0]", received.c_str());
    result = router.execute("> setColor 999 120 0");
    TEST_ASSERT_FALSE(result.ok);
    result = router.execute("config get _hidden");
    TEST_ASSERT_FALSE(result.ok);
    result = router.execute("config list");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(result.text.indexOf("_hidden") < 0);
    result = router.execute("config set _hidden x --admin");
    TEST_ASSERT_FALSE(result.ok);
    result = router.execute("config list", {CommandSource::Application, SettingsAccess::Internal});
    TEST_ASSERT_TRUE(result.text.indexOf("_hidden") >= 0);
    result = router.execute("config get _hidden", {CommandSource::Application, SettingsAccess::Internal});
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_EQUAL_STRING("x", result.text.c_str());
    result = router.execute("config set label \"Bedroom AC\"");
    TEST_ASSERT_TRUE(result.ok);
    result = router.execute("config get label");
    TEST_ASSERT_EQUAL_STRING("Bedroom AC", result.text.c_str());
    result = router.execute("config exists label");
    TEST_ASSERT_EQUAL_STRING("true", result.text.c_str());
    result = router.execute("config flag enabled true");
    TEST_ASSERT_TRUE(result.ok);
    result = router.execute("config flag enabled");
    TEST_ASSERT_EQUAL_STRING("true", result.text.c_str());
    result = router.execute("config remove label");
    TEST_ASSERT_TRUE(result.ok);
    result = router.execute("config clear");
    TEST_ASSERT_TRUE(result.ok);
    TEST_ASSERT_TRUE(settings.exists("_hidden", SettingsAccess::Internal));

    NetValue<int32_t> remote("temperature");
    TEST_ASSERT_TRUE(resources.mirror(remote, "sensor"));
    TEST_ASSERT_EQUAL(ResourceFreshness::UNKNOWN, resources.freshness(remote, 30000, 1000));
    resources.receive("sensor", "temperature", Operation::VALUE_STATE, "23", 1000);
    TEST_ASSERT_EQUAL(ResourceFreshness::FRESH, resources.freshness(remote, 30000, 6000));
    resources.receive("sensor", "temperature", Operation::VALUE_STATE, "23", 20000);
    TEST_ASSERT_EQUAL_UINT32(1000, resources.ageMs(remote, 21000));
    TEST_ASSERT_EQUAL(ResourceFreshness::STALE, resources.freshness(remote, 30000, 51000));
    TEST_ASSERT_EQUAL_INT32(23, remote.get());
    resources.receive("sensor", "temperature", Operation::VALUE_STATE, "24", 52000);
    TEST_ASSERT_EQUAL(ResourceFreshness::FRESH, resources.freshness(remote, 30000, 53000));
    TEST_ASSERT_EQUAL_INT32(24, remote.get());
    NetValue<int32_t> local("localValue");
    TEST_ASSERT_TRUE(resources.add(local));
    TEST_ASSERT_EQUAL(ResourceFreshness::UNKNOWN, resources.freshness(local, 30000, 53000));
}

static void consoleNetworkReply() {
    MockTransport transport;
    transport.online = true;
    NightMare::Network network("local", transport);
    Console console(network.resources());
    network.attachConsole(console);
    network.onMessage("local/console/in", "unknown command");
    network.tick(1000);
    TEST_ASSERT_EQUAL_STRING("local/console/out", transport.lastTopic.c_str());
    TEST_ASSERT_TRUE(transport.lastPayload.startsWith("error:"));
    network.onMessage("local/console/in", "help");
    network.tick(1100);
    TEST_ASSERT_EQUAL_STRING("local/console/out", transport.lastTopic.c_str());
    TEST_ASSERT_TRUE(transport.lastPayload.indexOf("> [owner/]action") >= 0);
}

void setup() {
    Serial.begin(115200);
    UNITY_BEGIN();
    RUN_TEST(settingsAndIdentity);
    RUN_TEST(parserCases);
    RUN_TEST(commandsAndFreshness);
    RUN_TEST(consoleNetworkReply);
    UNITY_END();
}

void loop() {}
