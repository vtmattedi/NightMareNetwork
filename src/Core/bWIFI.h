#pragma once
#ifndef NIGHTMARE_CORE_BWIFI_H
#define NIGHTMARE_CORE_BWIFI_H
#include <Modules.config.h>
#ifdef COMPILE_WIFI_MODULE

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
typedef void (*WiFiConnectedCallback)(bool firstConnection);
void onWiFiConnected(WiFiConnectedCallback callback);
void WiFi_Connect(const char *ssid, const char *password, int timeoutMs = 0, void *waitCallback(unsigned int) = nullptr);
bool WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect = true);
void WiFi_Disconnect();

#endif
#endif