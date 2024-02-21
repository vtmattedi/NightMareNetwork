/*----------------------------------------------------------*/
///
///@file ServicesCore.h -
/// Basics for implemented Services
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.1
///         Created.
/*----------------------------------------------------------*/


#include <Arduino.h>
#include <ArduinoJson.h>
#include <core/ServerVariables.h>

#ifndef NIGHTMARESERVICES
#define NIGHTMARESERVICES

#ifndef HOUR
///One Hour in Seconds xD
#define HOUR 3600
#endif


/// @brief ID for different `ServerVariables<>` to call the same function
/// and have different formats.
enum HWID
{
    ACTEMP_ID,
    HWSLEEP_ID, 
    SWSLEEP_ID, 
    ACTARGET_ID,
    DOOR_ID,
    SLEEPIN_ID,
    LIGHTSTATE_ID,
    RESTORE_ID,
    HEXCOLOR_ID,
    BRIGHTNESS_ID
};

/// @brief Function to recieve a topic and payload and transmit to the user's MQTT or TCP network, must be configured by the user. 
/// @param Topic The topic.
/// @param Payload The payload.
extern void Send_to_MQTT(String, String);

/// @brief This function inserts the hostname in front of the Topic then call `Send_to_MQTT(Topic, Payload)`
/// with the new topic and the payload passed to it I.E. `FormatSend("/state","1", "MyPc");` calls `Send_to_MQTT("MyPc/state", "1");` 
/// @param Topic The topic without the hostname.
/// @param Payload The payload to be passed through.
/// @param Hostname The hostname to be inserted.
extern void FormatSend(String,String,String);
#endif