#include <Arduino.h>
#include <unity.h>
#include <NightMare/Resources/ResourceManager.h>

using namespace NightMare;

class TestTransport final : public Transport {
public:
    bool connected() const override { return false; }
    bool publish(const String&, const String&, bool = false) override { return false; }
};

static TestTransport transport;
static ResourceManager resources("consumer", transport);
static NetValue<int32_t> remoteTemperature("temperature");
static NetValue<int32_t> localTemperature("localTemperature");
static NetAction<void> remoteAction("restart");

static void test_remote_freshness_transitions() {
    TEST_ASSERT_TRUE(resources.mirror(remoteTemperature, "sensor"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::UNKNOWN),
                            static_cast<uint8_t>(resources.freshness(remoteTemperature, 30000, 1000)));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, resources.ageMs(remoteTemperature, 1000));

    // Zero is a valid reception timestamp, not the UNKNOWN marker.
    resources.receive("sensor", "temperature", Operation::VALUE_STATE, "23", 0);
    TEST_ASSERT_TRUE(remoteTemperature.hasValue());
    TEST_ASSERT_EQUAL_INT32(23, remoteTemperature.get());
    TEST_ASSERT_EQUAL_UINT32(5000, resources.ageMs(remoteTemperature, 5000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::FRESH),
                            static_cast<uint8_t>(resources.freshness(remoteTemperature, 30000, 5000)));

    // A repeated, unchanged state is still a fresh observation.
    resources.receive("sensor", "temperature", Operation::VALUE_STATE, "23", 10000);
    TEST_ASSERT_EQUAL_UINT32(1000, resources.ageMs(remoteTemperature, 11000));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::FRESH),
                            static_cast<uint8_t>(resources.freshness(remoteTemperature, 30000, 40000)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::STALE),
                            static_cast<uint8_t>(resources.freshness(remoteTemperature, 30000, 40001)));
    TEST_ASSERT_EQUAL_INT32(23, remoteTemperature.get());

    // Invalid states cannot restore freshness or replace the last known value.
    resources.receive("sensor", "temperature", Operation::VALUE_STATE, "invalid", 40002);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::STALE),
                            static_cast<uint8_t>(resources.freshness(remoteTemperature, 30000, 40002)));
    TEST_ASSERT_EQUAL_INT32(23, remoteTemperature.get());

    resources.receive("sensor", "temperature", Operation::VALUE_STATE, "24", 50000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::FRESH),
                            static_cast<uint8_t>(resources.freshness(remoteTemperature, 30000, 50000)));
    TEST_ASSERT_EQUAL_INT32(24, remoteTemperature.get());
}

static void test_local_and_other_resources_are_unknown() {
    TEST_ASSERT_TRUE(resources.add(localTemperature));
    resources.set(localTemperature, int32_t(42));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::UNKNOWN),
                            static_cast<uint8_t>(resources.freshness(localTemperature, 30000, 50000)));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, resources.ageMs(localTemperature, 50000));
    TEST_ASSERT_TRUE(resources.mirror(remoteAction, "sensor"));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResourceFreshness::UNKNOWN),
                            static_cast<uint8_t>(resources.freshness(remoteAction, 30000, 50000)));
}

void setup() {
    delay(1000);
    UNITY_BEGIN();
    RUN_TEST(test_remote_freshness_transitions);
    RUN_TEST(test_local_and_other_resources_are_unknown);
    UNITY_END();
}

void loop() {}
