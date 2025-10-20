/*----------------------------------------------------------*/
///
///@file NightMareTCPServer.h -
/// Implementation of the clients for the NightMare TCP Server
/// Author: Vitor Mattedi Carvalho
/// Date: 29/09/2023
/// Version: 1.1
///         Created.
/*----------------------------------------------------------*/

#ifndef NIGHTMARETCPSERVER_H
#define NIGHTMARETCPSERVER_H
#include "NightMareTCPCore.h"
#ifdef COMPILE_NIGHTMARE_TCP_SERVER
#include "Arduino.h"
#include "NightMareTCPServerClient.h"
#include <Xtra/NightMareTypes.h>
#ifdef TCP_USE_NIGHTMARE_COMMANDS
#include <Xtra/NightMareCommands.h>
#endif

#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#endif

#ifndef MAX_CLIENTS
#define MAX_CLIENTS 10
#endif



// NightMare Server class
class NightMareTCPServer
{
  typedef std::function<String(String, int)> TServerMessageHandler;
  typedef std::function<void()> TFastHandler;

private:
  WiFiServer _wifiServer;
  int _port;
  TFastHandler _fast_callback;
  TServerMessageHandler _message_callback;
  bool _debug;
  bool _EnableNightMareCommands;
  String NightMareCommands_Server(String msg, byte index);
public:
  // the timeout in ms of client inactivity
  uint _timeout;
  // flag that disables timing out clients
  bool _disable_timeout;
  /*NightMare TCP Server
   *@param port the port of the server [DEFAULT_PORT]
   *@param debug prints debug info if enabled [false]
   */
  NightMareTCPServer(int port = DEFAULT_PORT, bool debug = false);
  // Sets the function to handle server messages. It must return a String Response.
  NightMareTCPServer &setMessageHandler(TServerMessageHandler fn);
  // Sets the handler for the fast callback. it is imediatly triggred when the fast_char is read in the Stream;
  NightMareTCPServer &setFastHandler(TFastHandler fn);
  // Array with the clients of the server
  NightMareTCPServerClient clients[MAX_CLIENTS];
  // Starts the Server.
  void begin();
  // Sends the msg to all connected clients
  void broadcast(String msg);
  // looks for data from the clients and trigger the proper callbacks in case of data
  void handleServer();
  // sets the timeout of the server (-1) will toggle it.
  void setTimeout(int timeout = -1);
  // Sends the message to the client at the client_index
  bool sendToIndex(String msg, byte client_index);
  // Sends the message to the client with ip matching client_IP
  bool sendToIP(String msg, String client_IP);
  // Sends the message to the client with ID matching client_ID
  bool sendToID(String msg, String client_ID);
};

#endif
#endif