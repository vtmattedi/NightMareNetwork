/*----------------------------------------------------------*/
///
///@file NightMareTCPCClient.h -
/// NightMare TCP Client implementation
/// Author: Vitor Mattedi Carvalho
/// Date: 29/09/2023
/// Version: 1.1
/*----------------------------------------------------------*/
#ifndef NIGHTMARETCPCLIENT_H
#define NIGHTMARETCPCLIENT_H
#include "NightMareTCPCore.h"
#ifdef COMPILE_NIGHTMARE_TCP_CLIENT
#include "Arduino.h"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#endif



// Nightmare Client Object
class NightMareTCPClient
{
private:
  typedef std::function<void()> TFastHandler;
  typedef std::function<String(String)> TClientMessageHandler;
  /// @brief Message Handler.
  TClientMessageHandler _message_callback;
  /// @brief Fast Char Handler.
  TFastHandler _fast_callback;
  /// @brief char to trigger the Fast Char Handler.
  char _fastChar;
  /// @brief Debug messages.
  bool _debug;
  /// @brief Host IP.
  String _ip;
  /// @brief Host port.
  int _port;
  /// @brief Transmission Char used in Three Char Transmission Method/
  char _trasmissionChar;
  /// @brief Client label.
  String _clientID;
  /// @brief Client Name.
  String _clientName;
  /// @brief Client info.
  String _clientInfo;
  /// @brief Flag to send the keepAlive message.
  bool _keepAlive;
  /// @brief Interval to send the keepAlive message.
  int _keepAliveInterval;
  /// @brief Last time a keepAlive message was sent.
  uint32_t _keepAliveLastTime;
  /// @brief Handle NightMareCommands.
  /// @param msg The String with the message to be handled.
  /// @return the response of the message, ignored if "" or NULL.
  String NightMareCommands_Client(String msg);
  // Comunication protocol used, check available TransmissionModes;
  void newMessage(String message);
  TransmissionMode transmissionMode;
  uint32_t _lastMessageRecived;

public:
  /// @brief NightMare TCP Client Object.
  /// @param debug print out debug info?
  NightMareTCPClient(bool debug = false);
  /// @brief NightMare TCP Client Object.
  /// @param ID The Client ID.
  /// @param debug print out debug info?
  NightMareTCPClient(String ID, bool debug = false);
  // the client object with the actual tcp socket
  WiFiClient *client;
  // Sets the new message handler
  NightMareTCPClient &setMessageHandler(TClientMessageHandler fn);
  // Sets the handler for the fast callback. it is imediatly triggred when the fast_char is read in the Stream;
  NightMareTCPClient &setFastHandler(TFastHandler fn);
  // look for data available at the client and also reconnects if not connected
  void handleClient();
  // Sends the msg to the server if client exists. msg is passed through a prepare message function.
  void send(String msg);
  // Sets the debug flag
  void setdebug(bool newDebug);
  // Sets the fast_char to be used
  void setFastChar(char newFastchar);
  // Sends the fast_char to the Server if client exists
  void fastSend();
  // Sets the Client ID of he object for tracking among many.
  void setName(String newName);
  // Gets the Client ID of the object @returns Arduino String with object's label or "" if label was never set
  String Name();
  void setCllientInfo(String clientID, String info);
  /// @brief Attemps to connect to the server specified.
  /// @param ip String with the IP address of the host.
  /// @param port Port of the host.
  bool connect(String ip, int port = DEFAULT_PORT);
  /// @brief Attemps to connect to the server specified.
  /// @param ip The IP address of the host.
  /// @param port Port of the host.
  bool connect(IPAddress ip, int port = DEFAULT_PORT);
  /// @brief Attemps to reconnect to the server.
  void reconnect();
  /// @brief Sets the keepAlive Interval.
  /// @param newInterval The new Interval in ms, if <0 it will be disabled
  void setKeepAlive(int newInterval);
  /// @brief Wheater or not the client is connected
  /// @return True if client is connected, false otherwise.
  bool connected();
  void setMode(TransmissionMode newMode);
};

#endif

#endif