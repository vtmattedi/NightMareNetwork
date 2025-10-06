#pragma Once
//* MQTT Wrapper for expressif ESP32 mqtt client
// * Supports local and remote MQTT brokers with TLS
// * Automatically prepends device name to topics
// * Handles connection, disconnection, and message callbacks
// Fixed crashes by not using PubSubClient 
#include <Arduino.h>
#include "mqtt_client.h"
#include "creds.h"
#ifndef MQTT_CREDS_H
#error "Please create a creds.h file with the necessary definitions."
#endif
#ifndef DEVICE_NAME
#define DEVICE_NAME "NightMare Device"
#endif
// #define COMPILE_SERIAL
#define LOCAL_MQTT true
#define REMOTE_MQTT false

void MQTT_Init(bool local = false);
void MQTT_End();
bool MQTT_isLocal();
void MQTT_change_to(bool local);
void MQTT_Send_Raw(String topic, String message);
void MQTT_Send(String topic, String message, bool insertOwner = true, bool retained = false);
void MQTT_onMessage(void (*cb)(String topic, String message));
void MQTT_onConnected(void (*cb)(void));
int8_t MQTT_Connected();