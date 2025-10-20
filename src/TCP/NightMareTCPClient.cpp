#include "NightMareTCPClient.h"

#ifndef USE_KEEP_ALIVE_BY_DEFAULT
#define USE_KEEP_ALIVE_BY_DEFAULT false
#endif

String NightMareTCPClient::NightMareCommands_Client(String msg)
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
    else if (msg == "TMODE=0" || msg == "7:TMODE=0")
    {
        transmissionMode = TransmissionMode::AllAvailable;
        response = "Transmission mode set to AllAvailable";
    }
    else if (msg == "TMODE=1" || msg == "7:TMODE=1")
    {
        transmissionMode = TransmissionMode::SizeColon;
        response = "Transmission mode set to SizeColon";
    }
    else if (msg == "REQ_ID" || msg == "6:REQ_ID")
    {
        response = "ID::" + _clientID + ";" + "name::" + _clientName + ";info::" + _clientInfo + ";";
    }
    else if (msg == "ID_TAKEN")
    {
        String newId = "-0x";
        auto r = random(16, 255);
        if (r < 0x10)
        {
            newId += "0";
        }
        _clientID += newId;
        response = "ID::" + _clientID + ";";
    }
    else
    {
        // Handle Broadcast message
        int indicator_index = msg.indexOf("Z::");
        if (indicator_index >= 0)
        {
            if (_message_callback)
            {
                _message_callback(msg.substring(indicator_index + 3));
            }
            response = "M;ACK;";
        }
    }

    return response;
}

NightMareTCPClient::NightMareTCPClient(bool debug) : _message_callback(NULL), _fast_callback(NULL)
{

    if (USE_KEEP_ALIVE_BY_DEFAULT)
    {
        _keepAliveInterval = KEEP_ALIVE_DEFAULT_INTERVAL;
        _keepAlive = true;
        _keepAliveLastTime = millis();
    }
    else
    {
        _keepAlive = false;
    }

    client = new WiFiClient();
    _fastChar = DefaultFastChar;
    _debug = debug;
    _trasmissionChar = DefaultTransmissionChar;
    _clientName = "not-set";
    setCllientInfo("no-id", "no-info");
}

NightMareTCPClient::NightMareTCPClient(String Name, bool debug) : _message_callback(NULL), _fast_callback(NULL)
{
    if (USE_KEEP_ALIVE_BY_DEFAULT)
    {
        _keepAliveInterval = KEEP_ALIVE_DEFAULT_INTERVAL;
        _keepAlive = true;
        _keepAliveLastTime = millis();
    }
    else
    {
        _keepAlive = false;
    }
    _clientName = Name;
    client = new WiFiClient();
    _fastChar = DefaultFastChar;
    _debug = debug;
    _trasmissionChar = DefaultTransmissionChar;
    setCllientInfo("no-id", "no-info");
}

void NightMareTCPClient::handleClient()
{
    if (!client)
    {
        return;
    }

    if (millis() - _keepAliveLastTime > _keepAliveInterval && _keepAlive && client->connected())
    {
        // no need to call _keepAliveLastTime = millis(); since send takes care of it.
        log_f(_debug, "***Sending keepAlive***\n");
        send(KEEP_ALIVE_MESSAGE);
    }

    if (millis() - _lastMessageRecived > _keepAliveInterval * 2 && _keepAlive && client->connected() )
    {
        log_f(_debug, "***No response from server, disconnected***\n");
        client->stop();
    }
    

    if (client->available() > 0)
    {
        _lastMessageRecived = millis();
        String msg = "";
        int size = 0;
        int index = 0;
        bool sizeFound = false;
        while (client->available() > 0)
        {
            char newChar = client->read();
            if (newChar == _fastChar && _fast_callback)
            {
                _fast_callback();
                return;
            }

            if (newChar != (char)19)
            {
                if (!sizeFound && newChar == ':' && transmissionMode == TransmissionMode::SizeColon)
                {
                    size = atoi(msg.c_str());
                    sizeFound = true;
                    msg = "";
                    index = 0;
                }
                else if (sizeFound && index >= size && transmissionMode == TransmissionMode::SizeColon)
                {
                    newMessage(msg);
                    sizeFound = false;
                    msg = "";
                    msg += newChar;
                }
                else
                {
                    index++;
                    msg += newChar;
                }
            }
        }

        String nightmare = NightMareCommands_Client(msg);
        if (!nightmare.equals(""))
        {
            send(nightmare);
        }
        else
        {
            if (!msg.equals(KEEP_ALIVE_RESPONSE))
            {
                if (_message_callback)
                    _message_callback(msg);
            }
        }
    }
}

void NightMareTCPClient::send(String msg)
{
    if (client != NULL)
    {
        client->write(PrepareMsg(msg, transmissionMode).c_str());
        //client->flush();
        _keepAliveLastTime = millis();
    }
}

void NightMareTCPClient::setName(String newName)
{
    _clientID = newName;
}

String NightMareTCPClient::Name()
{
    if (_clientID)
        return _clientID;
    return "";
}

void NightMareTCPClient::fastSend()
{
    if (client != NULL)
        client->write(_fastChar);
}

void NightMareTCPClient::setdebug(bool newDebug)
{
    _debug = newDebug;
}

void NightMareTCPClient::setFastChar(char newFastChar)
{
    _fastChar = newFastChar;
}

bool NightMareTCPClient::connect(String ip, int port)
{
    _lastMessageRecived = millis();
    _ip = ip;
    _port = port;
    return client->connect(_ip.c_str(), _port);
}

bool NightMareTCPClient::connect(IPAddress ip, int port)
{
    _lastMessageRecived = millis();
    _ip = ip.toString();
    _port = port;
   return client->connect(ip, _port);
}

void NightMareTCPClient::reconnect()
{
    if (!client->connected())
        client->connect(_ip.c_str(), _port);
}

void NightMareTCPClient::setKeepAlive(int newInterval)
{
    if (newInterval < 0)
        _keepAlive = false;
    else
        _keepAliveInterval = newInterval;
}

NightMareTCPClient &NightMareTCPClient::setMessageHandler(TClientMessageHandler fn)
{
    _message_callback = fn;
    return *this;
}

NightMareTCPClient &NightMareTCPClient::setFastHandler(TFastHandler fn)
{
    _fast_callback = fn;
    return *this;
}

void NightMareTCPClient::setCllientInfo(String clientID, String info)
{
    _clientID = clientID;
    _clientInfo = info;
}
bool NightMareTCPClient::connected()
{
    return client->connected();
}

void NightMareTCPClient::setMode(TransmissionMode newMode)
{
    transmissionMode = newMode;
    if (newMode == SizeColon)
    {
        send("TMODE=1");
    }
    else if (newMode == AllAvailable)
    {
        send("TMODE=0");
    }
}

void NightMareTCPClient::newMessage(String message)
{
    String nightmare = NightMareCommands_Client(message);
    if (!nightmare.equals(""))
    {
        send(nightmare);
    }
    else
    {
        if (!message.equals(KEEP_ALIVE_RESPONSE))
        {
            if (_message_callback)
                _message_callback(message);
        }
    }
}