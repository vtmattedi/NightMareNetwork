#pragma once
/*----------------------------------------------------------*/
///
///@file NightMareTCPCore.h -
/// Some common functions to NightMare TCP Server and Client
/// Author: Vitor Mattedi Carvalho
/// Date: 29/09/2023
/// Version: 1.1
///          Created.
/*----------------------------------------------------------*/

#define DefaultFastChar 0xFE
#define DefaultTransmissionChar '!'
#define KEEP_ALIVE_MESSAGE "***keep alive***"
#define KEEP_ALIVE_RESPONSE "***ack***"
#define USE_KEEP_ALIVE_BY_DEFAULT true
#define KEEP_ALIVE_DEFAULT_INTERVAL 60000
#define DEFAULT_TIMEOUT 180000 //3 mins
#define DEFAULT_PORT 4100
#include "Arduino.h"
typedef enum
{
  // not fully implemented
  ThreeCharHeaders = 2,
  // String is sent as {length:message} and when new data arrives, the data is read looking for the size until it reaches a colon then it outputs the msg of that size;
  SizeColon = 1,
  // String is sent and received as is.
  AllAvailable = 0
} TransmissionMode;

// Common functions

/// @brief Prepares the msg to be sent based on the transmission mode.
/// @param msg The message to be transmitted.
/// @param mode The mode to be used.
/// @return The String properly formatted.
String PrepareMsg(String msg, TransmissionMode mode);


