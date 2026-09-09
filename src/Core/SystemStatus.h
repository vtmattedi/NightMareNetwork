/*----------------------------------------------------------*/
///
///@file SystemStatus.h -
/// Aggregated system status report for NightMare Network.
///
/// This module sits at the TOP of the dependency stack: its implementation reads from WiFi, MQTT,
/// HTTP, Configs and the async command system. The header deliberately declares nothing but the
/// function itself, so including it never drags those modules along and no include cycle can form.
/// Every heavy include lives in SystemStatus.cpp, which nothing includes.
///
/// This is why the report does not belong in Misc: Misc.h is a leaf that Timers.h,
/// NightMareCommand.h and NightMareNetwork.h all depend on, and a leaf must not reach upward.
/*----------------------------------------------------------*/

#pragma once
#include <Modules.config.h>
#ifdef COMPILE_SYSTEMSTATUS
#include <Arduino.h>

/// @brief Standardized system status report in JSON format.
/// Fields for modules that are not compiled in are reported as disabled rather than omitted, so
/// the shape of the JSON stays the same across builds.
/// @return A String containing the system status in JSON format.
String getSystemStatus();

#endif
