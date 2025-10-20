#include "NightMareTCPServer.h"

String NightMareTCPServer::NightMareCommands_Server(String msg, byte index)
{
  String response = "";
  if (msg == "Help" || msg == "help" || msg == "H" || msg == "h")
  {
    response += ("Welcome to NightMare Home Systems ©\nThis is a ESP32 Module and it can answer to the following commands:\n");
    response += ("Quick obs.: the character int [19] or char () is ignored when received for facilitating reasons.");
    response += ("'A' > Gets the current state of available variables\n'L' > Toggles the LIGH_RELAY state\n");
    response += ("'T;******;' > Sets the TIMEOUT value for the tcp server.[Replace '******' with a long.\n");
    response += ("'SOFTWARE_RESET' requests a software reset.");
  }
  else if (msg == "REQ_CLIENTS")
  {
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
      if (NULL != clients[i].client)
      {
        response += "Client [";
        response += i;
        response += "], IP: ";
        response += clients[i].client->remoteIP().toString();
        response += ", (self) ID: ";
        response += clients[i].clientsID;
        response += ", INFO: ";
        response += clients[i].clientsStatus[i];
        response += "\n";
      }
    }
  }
  else if (msg == "REQ_UPDATES")
  {
    clients[index].clientsRequestUpdates = !clients[index].clientsRequestUpdates;
    response = "Your REQ_UPDATES flag is now: ";
    if (clients[index].clientsRequestUpdates)
      response += true;
    else
      response += false;
  }
  else if (msg == "REQ_UPDATES_1")
  {
    clients[index].clientsRequestUpdates = true;
    response += "Your REQ_UPDATES flag is now: true";
  }
  else if (msg == "REQ_UPDATES_0")
  {
    clients[index].clientsRequestUpdates = false;
    response += "Your REQ_UPDATES flag is now: false";
  }
  else if (msg == "TMODE=0" || msg == "7:TMODE=0")
  {
    clients[index].transmissionMode = TransmissionMode::AllAvailable;
    response = "Transmission mode set to AllAvailable";
  }
  else if (msg == "TMODE=1" || msg == "7:TMODE=1")
  {
    clients[index].transmissionMode = TransmissionMode::SizeColon;
    response = "Transmission mode set to SizeColon";
  }
  else if (msg.equals(KEEP_ALIVE_MESSAGE))
  {
    response = KEEP_ALIVE_RESPONSE;
  }
  else
  {
    int indicator_index = msg.indexOf("ID::");
    if (indicator_index >= 0)
    {
      int endindex = msg.indexOf(";", indicator_index + 4);
      if (endindex > 0)
        clients[index].clientsID = msg.substring(indicator_index + 4, endindex);
      response = "M;ACK;";
    }
    indicator_index = msg.indexOf("Z::");
    if (indicator_index >= 0)
    {
      clients[index].clientsStatus = msg.substring(indicator_index + 3);
      ;
      for (int i = 0; i < MAX_CLIENTS; i++)
      {
        if (clients[i].clientsRequestUpdates)
        {
          clients[i].send(msg);
        }
      }
      response = "M;ACK;";
    }
  }

  return response;
}

void NightMareTCPServer::begin()
{
  _wifiServer.begin(_port);
  log_f(_debug, "Starting NightMare TCP Server at port: %d\n", _port);
}

void NightMareTCPServer::broadcast(String msg)
{
  for (size_t i = 0; i < MAX_CLIENTS; i++)
  {
    clients[i].send(msg);
  }
}

void NightMareTCPServer::handleServer()
{
  // polls for new clients
  WiFiClient newClient = _wifiServer.available();
  if (newClient)
  {
    log_f(_debug, "new client connected \n");
    // Find the first unused space
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
      if (NULL == clients[i].client)
      {
        clients[i].client = new WiFiClient(newClient);
        log_f(_debug, "client [ %s", newClient.remoteIP().toString().c_str());
        log_f(_debug, ":%d] allocated at (%d)\n", newClient.remotePort(), i);
        clients[i].clientsTimeout = millis();
        clients[i].send("REQ_ID");
        i = MAX_CLIENTS;
      }
      else if (i == MAX_CLIENTS - 1)
      {

        log_f(_debug, "Could not allocate client [ %s", newClient.remoteIP().toString().c_str());
        log_f(_debug, ":%d] all (%d) clients spaces are beeing used right now.\n", newClient.remotePort(), MAX_CLIENTS);
      }
    }
  }

  // Check whether each client has some data
  for (int i = 0; i < MAX_CLIENTS; ++i)
  {
    // If the client is in use, and has some data...
    if (NULL != clients[i].client && clients[i].client->available())
    {
      log_f("New Msg from [%s] mode [%d]\n", clients[i].client->remoteIP().toString().c_str(), clients[i].transmissionMode);
      String msg = "";
      clients[i].clientsTimeout = millis();
      int size = 0;
      int index = 0;
      bool sizeFound = false;
      while (NULL != clients[i].client && clients[i].client->available())
      {
        char newChar = clients[i].client->read();

        if (newChar == clients[i]._fastChar)
        {
          if (_fast_callback)
            _fast_callback();
          return;
        }

        if (newChar != (char)19)
        {
          if (sizeFound == 0 && newChar == ':' && clients[i].transmissionMode == TransmissionMode::SizeColon)
          {
            size = atoi(msg.c_str());
            sizeFound = true;
            msg = "";
            index = 0;
          }
          else if (sizeFound == 1 && index >= size && clients[i].transmissionMode == TransmissionMode::SizeColon)
          {
            if (_EnableNightMareCommands)
            {
              String rtr = NightMareCommands_Server(msg, i);
              if (rtr != "")
              {
                clients[i].send(rtr);
              }
            }

            if (msg != "" && _message_callback)
              clients[i].send(_message_callback(msg, i));
            sizeFound = false;
            msg = "";
            msg += newChar;
          }
          else
          {
            index++;
            //Check if the char is a printable char or extended ascii
            if (newChar >= 32 && newChar <= 126 || newChar >= 128 && newChar <= 255)
            {
              msg += newChar;
            }
          }
          //log_f("%c (%d) -> %s\n", newChar, newChar, msg.c_str());
        }
      }

      if (_EnableNightMareCommands)
      {
        String rtr = NightMareCommands_Server(msg, i);
        if (rtr != "")
        {
          clients[i].send(rtr);
        }
        else
        {
          if (_message_callback)
            clients[i].send(_message_callback(msg, i));
        }
      }
      else
      {
        if (_message_callback)
          clients[i].send(_message_callback(msg, i));
      }
    }
    // If a client disconnects, clear the memory for a new one
    if (NULL != clients[i].client && !clients[i].client->connected())
    {
      clients[i].client->stop();
      delete clients[i].client;
      clients[i].reset();
    }
    // check if any client have been idle and time it out
    else if (NULL != clients[i].client && clients[i].client->connected())
    {
      if ((millis() - clients[i].clientsTimeout >= _timeout) and !_disable_timeout)
      {
        clients[i].send("E;0x3F;Timedout;");
        clients[i].client->stop();
        delete clients[i].client;
        clients[i].reset();
      }
    }
  }
}

void NightMareTCPServer::setTimeout(int timeout)
{
  if (timeout < 0)
    _disable_timeout = !_disable_timeout;
  else if (timeout > 0)
  {
    _timeout = timeout;
  }
}

bool NightMareTCPServer::sendToIP(String msg, String ip)
{
  for (size_t i = 0; i < MAX_CLIENTS; i++)
  {
    if (clients[i].client != NULL && clients[i].client->remoteIP().toString() == ip)
    {
      clients[i].send(msg);
      log_f(_debug, "Sending '%s' to '%s' at index [%d]\n", msg.c_str(), ip.c_str(), i);
      return true;
    }
  }
  log_f(_debug, "Failed, no client wit IP: '%s' found\n", ip.c_str());
  return false;
}

bool NightMareTCPServer::sendToIndex(String msg, byte index)
{
  if (index >= MAX_CLIENTS)
  {

    log_f(_debug, "Failed. '%d' is outside of clients array which has size: %d\n", index, MAX_CLIENTS);
    return false;
  }
  if (clients[index].client == NULL)
  {

    log_f(_debug, "Failed, client at index '%d' is null\n", index);
    return false;
  }
  clients[index].send(msg);
  return true;
}

NightMareTCPServer &NightMareTCPServer::setMessageHandler(TServerMessageHandler fn)
{
  _message_callback = fn;
  return *this;
}

NightMareTCPServer &NightMareTCPServer::setFastHandler(TFastHandler fn)
{
  _fast_callback = fn;
  return *this;
}

NightMareTCPServer::NightMareTCPServer(int port, bool debug) : _message_callback(NULL), _fast_callback(NULL)
{
  _wifiServer = WiFiServer(_port);
  _disable_timeout = false;
  _timeout = DEFAULT_TIMEOUT;
  _EnableNightMareCommands = true;
  _port = port;
  _debug = debug;
}
