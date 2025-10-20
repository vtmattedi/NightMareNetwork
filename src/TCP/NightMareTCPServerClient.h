/*----------------------------------------------------------*/
///
///@file NightMareTCPServerClient.h -
/// Implementation of the clients for the NightMare TCP Server
/// Author: Vitor Mattedi Carvalho
/// Date: 29/09/2023
/// Version: 1.1
///         Created.
/*----------------------------------------------------------*/

#ifndef NIGHTMARETCPSERVERCLIENT_H
#define NIGHTMARETCPSERVERCLIENT_H
#include "Arduino.h"
#include "NightMareTCPCore.h"
#ifdef COMPILE_NIGHTMARE_TCP_SERVER
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#endif
// base Client for server
class NightMareTCPServerClient
{
private:

public:
  NightMareTCPServerClient();
  // The actual TCP Object
  WiFiClient *client;
  // Last Time the client sent anything
  uint32_t clientsTimeout;
  // Client Status {If a message Z;* is received then it is stored here}
  String clientsStatus;
  // Client ID
  String clientsID;
  // If Clients
  bool clientsRequestUpdates;
  // The Time Stamp for the Status String
  int clientsStatusTime;
  // Set the Nightmare TCP transmission mode. ThreeCharHeaderis not supported here
  TransmissionMode transmissionMode;
  // Sends The msg formatted according to the transmission mode
  void send(String msg);
  // Resets The internal variables
  void fastSend();
  // Sends the fast_char to the Server if client exists
  void reset();
  String getIP();
  char _fastChar;
  char _trasmissionChar;
};

#endif

#endif