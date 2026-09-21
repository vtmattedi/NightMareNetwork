#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_TIME_SYNC
/*----------------------------------------------------------*/
///
///@file TimeSyncronization.h -
/// Implements function to sync the time using worldtimeapi.
/// Author: Vitor Mattedi Carvalho
/// Date: 12-12-2024
/// Version: 1.0
/*----------------------------------------------------------*/


#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Core/StateStore.h>
#include <ArduinoJson.h>
#include <creds.h> // supplied by the consuming project, not by this library
#define API_URL "http://utctime.app/api/now/America/Bahia"

bool autoSyncTime();
void manualSyncTime(unsigned long timestamp);
void onTimeSync(void (*callback)(void));
#endif // NM_ENABLE_TIME_SYNC
