#include <Gateway/NightMareGateway.h>
#if NM_ENABLE_GATEWAY
#include <Gateway/GatewayResources.h>

#include <Core/NetResources.h>
#include <Core/PlatformClock.h>
#include <Gateway/GatewayStats.h>
#include <Gateway/HardwareTrace.h>
#if NM_GATEWAY_ESPNOW
#include <Gateway/EspNow/espBroker.h>
#endif
#if NM_GATEWAY_MQTT
#include <Gateway/EspMqtt/espMqtt.h>
#endif

#ifndef NM_GATEWAY_STATUS_INTERVAL_MS
#define NM_GATEWAY_STATUS_INTERVAL_MS 5000UL
#endif

namespace
{
ManagedSensor<int> clients("gateway:clients");
ManagedSensor<bool> uplink("gateway:uplink");
ManagedSensor<int> rxDropped("gateway:rx_dropped");
ManagedSensor<int> msgsFromLocal("gateway:msgs_from_local");
ManagedSensor<int> msgsFromRemote("gateway:msgs_from_remote");
ManagedState<bool> enabled("gateway:enabled");
ManagedState<bool> espNowPaused("gateway:espnow_paused");
ManagedState<bool> hardwareTrace("gateway:hardware_trace");

bool writeEnabled(ManagedState<bool> &, const bool &requested)
{
    NightMareGateway_enable(requested);
    return true;
}

bool writeEspNowPaused(ManagedState<bool> &, const bool &requested)
{
#if NM_GATEWAY_ESPNOW
    NightMareGateway_pauseEspNow(requested);
    return true;
#else
    (void)requested;
    return false;
#endif
}

bool writeHardwareTrace(ManagedState<bool> &, const bool &requested)
{
    NightMareGateway_setHardwareTrace(requested);
    return true;
}

struct HandlerInstaller
{
    HandlerInstaller()
    {
        enabled.onWrite = writeEnabled;
        espNowPaused.onWrite = writeEspNowPaused;
        hardwareTrace.onWrite = writeHardwareTrace;
    }
};
HandlerInstaller handlerInstaller;

int counter(const std::atomic<uint32_t> &value)
{
    return static_cast<int>(value.load(std::memory_order_relaxed) & 0x7FFFFFFFu);
}

template <typename T, typename R>
void update(R &resource, const T &value)
{
    if (resource.getValue() != value || !resource.available())
        resource.setValue(value);
}
}

namespace GatewayResources
{
void tick()
{
    // States follow the gateway whoever changed it: these, a GATEWAY command,
    // or the application.
    update(enabled, NightMareGateway_enabled());
#if NM_GATEWAY_ESPNOW
    update(espNowPaused, NightMareGateway_espNowPaused());
#endif
    update(hardwareTrace, NightMareGateway_hardwareTrace());

    static bool sampled = false;
    static uint32_t lastMs = 0;
    const uint32_t now = NightMare::Platform::monotonicMillis();
    if (sampled && now - lastMs < NM_GATEWAY_STATUS_INTERVAL_MS)
        return;
    sampled = true;
    lastMs = now;

#if NM_GATEWAY_ESPNOW
    update(clients, static_cast<int>(espBroker_deviceCount()));
#else
    update(clients, 0);
#endif
#if NM_GATEWAY_MQTT
    update(uplink, mqtt_is_connected());
#else
    update(uplink, false);
#endif
    update(rxDropped, counter(g_gatewayStats.espnowRxDropped));
    update(msgsFromLocal, counter(g_gatewayStats.msgsFromLocal));
    update(msgsFromRemote, counter(g_gatewayStats.msgsFromRemote));
}
}

#endif // NM_ENABLE_GATEWAY
