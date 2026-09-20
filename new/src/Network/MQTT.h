#pragma once

#include <Arduino.h>

// Project-facing MQTT conveniences. The ESP client and credentials stay private.
// The broker choice is unrelated to resource ownership.
constexpr bool LOCAL_MQTT = true;
constexpr bool REMOTE_MQTT = false;

void MQTT_Init(bool localBroker = REMOTE_MQTT);
void MQTT_End();
// Stops the client and control task asynchronously; call MQTT_Init after it finishes.
void MQTT_Finish();
void MQTT_change_to(bool localBroker);
bool MQTT_isLocal();
bool MQTT_Connected();
int8_t MQTT_State();
String MQTTStateJson();

// MQTT_Publish returns whether the ESP client accepted the complete message.
// insertOwner prefixes the current device name; false uses the exact topic.
// An empty retained payload clears a retained topic.
bool MQTT_Publish(const String &topic, const String &message,
                  bool insertOwner = true, bool retained = false);
void MQTT_Send(String topic, String message, bool insertOwner = true, bool retained = false);
void MQTT_Send_Raw(String topic, String message);
bool MQTT_Queue_Async_Message(String topic, String message,
                              bool insertOwner = false, bool retained = false);

// Optional project hooks. Automatic console, time and resource routing runs first.
// The default message hook receives only this device's topics without its prefix.
void MQTT_onMessage(void (*cb)(String topic, String message), bool onlyDeviceMessages = true);
void MQTT_onConnected(void (*cb)(void));
void MQTT_onDisconnected(void (*cb)(bool localBroker));
