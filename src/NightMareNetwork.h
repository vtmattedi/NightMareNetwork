
/*----------------------------------------------------------*/
///
///@file NightMareNetwork.h -
/// Outter Header file with core functionality for 
/// the NightMare Network.
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.3
///         Added the time sync function.
///         Added the MQTT functions.
///         Added the Scheduler & Command functions.
/*----------------------------------------------------------*/

#pragma once
#ifndef HOUR
#define HOUR 3600
#endif

#ifndef MINUTE
#define MINUTE 60
#endif

#include <Core/Misc.h>
#include <Core/Timers.h>
#include <Core/ServerVariables.h>
#include <Core/TimeSyncronization.h>
#include <Core/MQTT.h>
#include <Xtra/Scheduler.h>
#include <Xtra/NightMareCommand.h>
#include <TCP/NightMareTCP.h>
#include <Modules.config.h>