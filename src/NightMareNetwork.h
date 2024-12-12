
/*----------------------------------------------------------*/
///
///@file NightMareNetwork.h -
/// Outter Header file with core functionality for 
/// the NightMare Network.
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.2
///         Added the time sync function.
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