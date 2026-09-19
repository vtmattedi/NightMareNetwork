/*
  NightMareTcp.h - TCP client for microcontrollers using Arduino Framework
  this Library is a TCP server/client built around the WiFi.h lib
  used mainly neat code used across different projects and also standarize
  and sync changes along all future codes using nightmare TCP Comunication.

  Author: Vitor Mattedi Carvalho
  Date: 29/03/2023
  Version: 1.0
  Date: 29/09/2023
  Version: 1.1
    --- Changed the internal structure to the library by dividing into Client Server and Core.
    --- You can now add Server and Client separately also fixed some bugs in Client and Added keepAlive funcionalities.
*/

#ifndef NIGHTMARETCP_H
#define NIGHTMARETCP_H
#include "NightMareTCPClient.h"
#include "NightMareTCPServer.h"
#endif