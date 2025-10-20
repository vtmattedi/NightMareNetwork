#include "NightMareTCPServerClient.h"
#ifdef COMPILE_NIGHTMARE_TCP_SERVER
NightMareTCPServerClient::NightMareTCPServerClient()
{
  // The actual TCP Object
  WiFiClient *client = NULL;
  // Last Time the client sent anything
  int clientsTimeout = 0;
  // Client Status {If a message Z;* is received then it is stored here}
  String clientsStatus = "";
  // Client ID
  String clientsID = "";
  // If Clients
  bool clientsRequestUpdates = false;
  // The Time Stamp for the Status String
  int clientsStatusTime = 0;
  // Set the Nightmare TCP transmission mode. ThreeCharHeaderis not supported here
  TransmissionMode transmissionMode = TransmissionMode::AllAvailable;
  _fastChar = DefaultFastChar;
  _trasmissionChar = DefaultTransmissionChar;
}

void NightMareTCPServerClient::send(String msg)
{
  if (client != NULL)
  {
    client->write(PrepareMsg(msg, transmissionMode).c_str());
    client->flush();
  }
}

void NightMareTCPServerClient::fastSend()
{
  if (client != NULL)
  {
    client->write(_fastChar);
  }
}

void NightMareTCPServerClient::reset()
{
  client = NULL;
  clientsTimeout = 0;
  clientsStatus = "";
  clientsID = "";
  clientsRequestUpdates = false;
  clientsStatusTime = 0;
  transmissionMode = TransmissionMode::AllAvailable;
}


String NightMareTCPServerClient::getIP()
{
  if (client != NULL)
  {
    return client->remoteIP().toString();
  }
  else
  {
    return "";
  }
}

#endif