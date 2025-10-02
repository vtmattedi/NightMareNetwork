#pragma Once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <./vendor/pubsubclient-2.8/src/PubSubClient.h>
#ifndef MQTT_CREDS_H
#error "Please create a MQTT_Creds.h file with the necessary definitions."
#endif
#ifndef DEVICE_NAME
#define DEVICE_NAME "UnknownDevice"
#endif
#define LOCAL_MQTT true
#define REMOTE_MQTT false

void MQTT_Init(bool local = false);
void MQTT_End();
bool MQTT_Safe_Connect();
bool MQTT_isLocal();
void MQTT_Loop();
void MQTT_change_to(bool local);
void MQTT_Send_Raw(String topic, String message);
void MQTT_Send(String topic, String message, bool insertOwner = true, bool retained = false);
void MQTT_onMessage(void (*cb)(String topic, String message));
void MQTT_onConnected(void (*cb)(void));
byte MQTT_Connected();