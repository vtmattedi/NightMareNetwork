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
#ifdef COMPILE_TIMESYNC
#include <Arduino.h>
#include <TimeLib.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Core/Configs.h>
#define API_URL "http://worldtimeapi.org/api/timezone/America/Bahia.txt"

#ifdef COMPILE_SCHEDULER
#include <Xtra/Scheduler.h>
#endif

bool autoSyncTime();
void manualSyncTime(unsigned long timestamp);
void onTimeSync(void (*callback)(void));



#endif