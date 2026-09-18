/*----------------------------------------------------------*/
///
///@file TimeSyncronization.h -
/// Implements function to sync the time using worldtimeapi.
/// Author: Vitor Mattedi Carvalho
/// Date: 12-12-2024
/// Version: 1.0
/*----------------------------------------------------------*/


#pragma once
#include <Modules.config.h>

// COMPILE_AUTOTIMESYNC is a sub-flag of COMPILE_TIMESYNC: it controls only the
// over-the-internet clock sync (autoSyncTime, and the automatic calls to it).
// Setting the clock by hand with manualSyncTime() belongs to COMPILE_TIMESYNC
// and is always available -- so a device that gets its time from the network,
// over MQTT on Control/request, keeps COMPILE_TIMESYNC and drops this one.
//
// Dropping it also drops the HTTPClient dependency and the several seconds the
// panel would otherwise spend on a blocking HTTP GET at every first connect.
#if defined(COMPILE_AUTOTIMESYNC) && !defined(COMPILE_TIMESYNC)
#error "COMPILE_AUTOTIMESYNC requires COMPILE_TIMESYNC. Define both, or neither."
#endif

#ifdef COMPILE_TIMESYNC
#include <Arduino.h>
#include <TimeLib.h>
#include <Core/Configs.h>
#include <ArduinoJson.h>
#include <creds.h> // supplied by the consuming project, not by this library

#ifdef COMPILE_AUTOTIMESYNC
#include <WiFi.h>
#include <HTTPClient.h>
#define API_URL "http://utctime.app/api/now/America/Bahia"
#endif

#ifdef COMPILE_SCHEDULER
#include <Xtra/Scheduler.h>
#endif

#ifdef COMPILE_AUTOTIMESYNC
/// @brief Sync the clock from an online time API over HTTP. Blocking.
bool autoSyncTime();
#endif

void manualSyncTime(unsigned long timestamp);
void onTimeSync(void (*callback)(void));



#endif