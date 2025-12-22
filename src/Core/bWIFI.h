#pragma once
#ifndef NIGHTMARE_CORE_BWIFI_H
#define NIGHTMARE_CORE_BWIFI_H
#include <Modules.config.h>
#ifdef COMPILE_WIFI_MODULE

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#ifdef COMPILE_OTA
#include <Core/OTA.h>
#endif

#ifdef COMPILE_CONFIGS
#include <Core/Configs.h>
#include <Core/creds.h>
#if !defined(DEFAULT_SSID) || !defined(DEFAULT_PASSWORD)
#error "Please define DEFAULT_SSID and DEFAULT_PASSWORD in creds.h"
#endif

#ifdef COMPILE_TIMESYNC
#include <Core/TimeSyncronization.h>
#endif

#endif
typedef void (*WiFiConnectedCallback)(bool firstConnection);
void onWiFiConnected(WiFiConnectedCallback callback);
bool WiFi_Connect(const char *ssid, const char *password, int timeoutMs = 0, void *waitCallback(unsigned int) = nullptr);
bool WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect = true);
void WiFi_Disconnect();
void WiFi_Auto();
bool WiFi_ChangeCredentials(const String &ssid, const String &password);
const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType);
#endif
#endif