/*----------------------------------------------------------
* Modules.config.h - Configuration file for the NightMare Network
* Author: Vitor Mattedi Carvalho
* Date: 16-10-2025
* Version: 1.0
*         Created.
*         Added support for multiple modules
* This File should be included by all modules
* This file should only containg preprocessor defines
* This should be used to control what modules are compiled
*----------------------------------------------------------*/

#pragma once
#ifndef NIGHTMARE_MODULES_CONFIG_H
#define NIGHTMARE_MODULES_CONFIG_H

// #define DEVICE_NAME "NightMare Device" //Default device name, used in MQTT and other places
// #define COMPILE_SERIAL //Uncomment this line to enable serial debug output (Global)
#pragma region "Core Modules"
//Core Modules 
#define COMPILE_MQTT
#define COMPILE_LVGL //Compiles LVGL helpers code
#define COMPILE_SERVERVARIABLES // Compile the ServerVariables template class
/*------- ServerVariables Templates to be compiled---------*/
#ifdef COMPILE_SERVERVARIABLES
/*------- ServerVariables Templates to be compiled---------*/
// #define  USE_BYTE_TEMPLATE //Compile Byte Template
#define USE_INT_TEMPLATE // Compile int Template
// #define  USE_UINT16_TEMPLATE //Complie uin16_t template
#define USE_UINT32_TEMPLATE // Complie uin32_t template
#define USE_DOUBLE_TEMPLATE // Complie double template
#define USE_BOOL_TEMPLATE   // Complie bool template
// #define  USE_STRING_TEMPLATE //Complie String template
/*---------------------------------------*/
/*---Services Dependent on ServerVariables*/
#define COMPILE_SERVICES // Compile encapsulation for services AC, LIGHT, LIGHTCOLOR
#define COMPILE_LIGHTCONTROLLER // Compile the LightController service
#define COMPILE_ACCONTROLLER    // Compile the AcController service
#define COMPILE_LIGHTCOLORCONTROLLER // Compile the LightColorController service
#endif
/*----------------------------------------------------------*/
#define COMPILE_MISC // Compile miscellaneous functions
#define COMPILE_TIMERS // Compile the Timers handler
#define COMPILE_TIMESYNC // Compile the time sync function
#pragma endregion

#pragma region "Xtra Modules"
//Xtra Modules
#define COMPILE_SCHEDULER // Compile the scheduler module
#define COMPILE_COMMAND_RESOLVER // Compile the command resolver module
#ifdef COMPILE_COMMAND_RESOLVER
/*--- Command Resolver configs */
#define ENABLE_PREPROCESSING // Enable command pre processing
#define SCHEDULER_AWARE // Compile the command resolver module with scheduler support on the PREPROCESSING
#endif
#pragma endregion

#endif