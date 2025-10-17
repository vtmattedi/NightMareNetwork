#pragma Once
#ifndef NIGHTMARE_CORE_MQTT_H
#define NIGHTMARE_CORE_MQTT_H
//* MQTT Wrapper for expressif ESP32 mqtt client
// * Supports local and remote MQTT brokers with TLS
// * Automatically prepends device name to topics
// * Handles connection, disconnection, and message callbacks
// Fixed crashes by not using PubSubClient 
#include <Modules.config.h>
#ifdef COMPILE_MQTT
#include <Arduino.h>
#define MQTT_SKIP_PUBLISH_IF_DISCONNECTED
#include "mqtt_client.h"
#include "creds.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#ifndef MQTT_CREDS_H
#error "Please create a creds.h file with the necessary definitions."
#endif
#ifndef DEVICE_NAME
#warning "DEVICE_NAME not defined, using default name."
#define DEVICE_NAME "NightMare Device"
#endif
#ifdef USING_DEFAULT_DEVICE_NAME
#warning "Using default DEVICE_NAME, please define a unique name in Modules.config.h"
#endif
// #define COMPILE_SERIAL
#define LOCAL_MQTT true
#define REMOTE_MQTT false

//Automatically include the command resolver if MQTT_PREPROCESS is defined
//This will make the MQTT client handle commands sent to the topic <DEVICE_NAME>/console/in
#ifdef MQTT_PREPROCESS
#include <Xtra/NightMareCommand.h>
#endif

void MQTT_Init(bool local = false);
void MQTT_Finish();
void MQTT_change_to(bool local);
bool MQTT_isLocal();
void MQTT_Send_Raw(String topic, String message);
void MQTT_Send(String topic, String message, bool insertOwner = true, bool retained = false);
void MQTT_onMessage(void (*cb)(String topic, String message));
void MQTT_onConnected(void (*cb)(void));
void MQTT_onDisconnected(void (*cb)(bool));
int8_t MQTT_Connected();
void Send_to_MQTT(String topic, String message);
int8_t MQTT_State();
#endif
#endif