#pragma once
#include <Gateway/NightMareGateway.h>
#include <Gateway/Broker/Message.h>
#include "mqtt_client.h"

// Keeps a copy of the broker settings for mqtt_init(): the client holds on to
// pointers into them (the CA certificate among them) for as long as it lives.
void mqtt_configure(const NightMareMqttConfig &config);

// Connects to the broker given to mqtt_configure(), subscribes to every topic
// ("#") and starts the client's own background task. Call once the station has
// an IP address. After mqtt_stop() it restarts the existing client.
void mqtt_init(void);

// Disconnects and stops the client, keeping it so mqtt_init() can start it
// again. Call from the gateway task, never from an MQTT event handler.
void mqtt_stop(void);

// Queues a message for the publisher task; never blocks. False if the queue
// is full or MQTT was never started. Messages still queued when the broker is
// offline are dropped, not held.
// Takes ownership so large payloads can be queued without another allocation.
bool mqtt_publish(NightMare::Message message);

// Synchronous publication used by mqtt_prepare_session() and mqtt_stop() to
// establish gateway-owned truth before subscriptions admit retained replay.
// Blocks (up to a few seconds) for a free slot in the broker's QoS1 window, so
// never call it from an MQTT event handler.
bool mqtt_publish_immediate(esp_mqtt_client_handle_t client,
                            const NightMare::Message &message);

// Diagnostics for a flapping link: when on, every MQTT disconnect also dumps
// the Wi-Fi driver's buffer and RX/TX statistics. Off by default, it is verbose.
void mqtt_setWifiDumpOnDisconnect(bool enabled);
bool mqtt_wifiDumpOnDisconnect(void);

// Diagnostics: publishes `count` QoS1 messages of `bytes` to <gateway id>/diag,
// one at a time, each waiting for its PUBACK, and logs the latency of each plus
// a summary on the "mqtttest" log module. Returns at once (a task does the work); false
// when MQTT is not connected, a test is already running, or arguments are zero.
bool mqtt_startDiagTest(uint32_t bytes, uint32_t count);

// True once the broker has accepted the connection.
bool mqtt_is_connected(void);

// Hook invoked whenever a message arrives on any subscribed topic. Runs in
// the esp-mqtt client task context. Implemented by the gateway.
void mqtt_onMessage(NightMare::Message message);

// Publishes the gateway's retained state and client snapshot. Called from the
// publisher task after each connect, before the transport subscribes to normal
// topics.
bool mqtt_prepare_session(esp_mqtt_client_handle_t client);
