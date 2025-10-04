#include "MQTT.h"

/// @brief Processes incoming MQTT messages before dispatching to user callbacks.
/// @param topic The topic of the incoming message.
/// @param payload The payload of the incoming message.
/// @param length The length of the incoming message payload.
void processMqttMessage(char *topic, byte *payload, unsigned int length); // Forward declaration
/// @brief Callback function to handle incoming MQTT messages.
void (*handleMqttMessage)(String topic, String message) = NULL;
/// @brief MQTT client Instance.
PubSubClient *mqttClient;
/// @brief MQTT client for secure connections.
WiFiClientSecure *mqttWifiClientSecure;
/// @brief MQTT client for non-secure connections.
WiFiClient *mqttWifiClient;
void _internal_MQTT_Send(String topic, String message, bool retained);

/// @brief  Callback function to handle when MQTT is connected.
void (*handleMqttConnected)(void) = NULL; // Callback for when MQTT is connected
/// @brief  Callback function to handle when MQTT is disconnected.
/// @param bool Indicates if the disconnection was intentional (true) or unintentional (false).
void (*handleMqttDisconnected)(bool) = NULL; // Callback for when MQTT is disconnected

bool local_initialized = false;
bool last_state = false;
void MQTT_change_to(bool local)
{
    if (local_initialized == local)
    {
#ifdef COMPILE_SERIAL
        Serial.printf("MQTT client already initialized as %s!\n", local ? "local" : "remote");
#endif
        return;
    }
    MQTT_End();
    MQTT_Init(local);
    bool res = MQTT_Safe_Connect();
    if (!res)
    {
#ifdef COMPILE_SERIAL
        Serial.printf("MQTT client failed to connect as %s!\n", local ? "local" : "remote");
#endif
        MQTT_End();
        MQTT_Init(!local); // Reinitialize as the opposite type (old type)
        MQTT_Safe_Connect();
    }
}

void MQTT_onMessage(void (*cb)(String topic, String message))
{
    if (cb)
    {
        handleMqttMessage = cb;
    }
}
void MQTT_onConnected(void (*cb)(void))
{
    if (cb)
    {
        handleMqttConnected = cb;
    }
}
void MQTT_onDisconnected(void (*cb)(void))
{
    if (cb)
    {
        handleMqttDisconnected = cb;
    }
}
void MQTT_Init(bool local)
{
    if (mqttClient || mqttWifiClient || mqttWifiClientSecure)
    {
#ifdef COMPILE_SERIAL
        Serial.println("MQTT client already initialized!, explictly call MQTT_End() before re-initializing.");
#endif
        return;
    }
    local_initialized = local;
    if (local)
    {
        mqttWifiClient = new WiFiClient();
        mqttClient = new PubSubClient(*mqttWifiClient);
        mqttClient->setServer(LOCAL_MQTT_HOST, LOCAL_MQTT_PORT);

#ifdef COMPILE_SERIAL
        Serial.println("MQTT client initialized with WiFiClient.");
#endif
        mqttClient->setCallback(processMqttMessage);
    }
    else
    {
        mqttWifiClientSecure = new WiFiClientSecure();
        mqttWifiClientSecure->setCACert(root_ca);
        mqttClient = new PubSubClient(*mqttWifiClientSecure);
        mqttClient->setServer(REMOTE_MQTT_URL, REMOTE_MQTT_PORT);
        mqttClient->setCallback(processMqttMessage);
#ifdef COMPILE_SERIAL
        Serial.println("MQTT client initialized with WiFiClientSecure.");
#endif
    }
}

void MQTT_End()
{
    if (mqttClient)
    {
        mqttClient->disconnect();
        if (handleMqttDisconnected)
        {
            handleMqttDisconnected(true); // Call the disconnected handler if set
        }
        delete mqttClient;
        mqttClient = nullptr;
    }
    if (mqttWifiClientSecure)
    {
        delete mqttWifiClientSecure;
        mqttWifiClientSecure = nullptr;
    }
    if (mqttWifiClient)
    {
        delete mqttWifiClient;
        mqttWifiClient = nullptr;
    }
    local_initialized = false;
#ifdef COMPILE_SERIAL
    Serial.println("MQTT client ended.");
#endif
}

const char *mqttErrorFromCode(int code)
{
    switch (code)
    {
    case MQTT_CONNECTION_TIMEOUT:
        return "Connection timeout";
    case MQTT_CONNECTION_LOST:
        return "Connection lost";
    case MQTT_CONNECT_FAILED:
        return "Connect failed";
    case MQTT_DISCONNECTED:
        return "Disconnected";
    case MQTT_CONNECTED:
        return "Connected";
    case MQTT_CONNECT_BAD_PROTOCOL:
        return "Bad protocol";
    case MQTT_CONNECT_BAD_CLIENT_ID:
        return "Bad client ID";
    case MQTT_CONNECT_UNAVAILABLE:
        return "Unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS:
        return "Bad authentication";
    case MQTT_CONNECT_UNAUTHORIZED:
        return "Not authorized";
    default:
        return "Unknown error";
    }
}

bool MQTT_Safe_Connect()
{
    if (!mqttClient)
    {
#ifdef COMPILE_SERIAL
        Serial.println("MQTT client not initialized!");
#endif
        return false;
    }
    if (mqttClient->connected())
        return false;
    mqttClient->setCallback(processMqttMessage);
    String clientId = String(DEVICE_NAME) + "-" + String(esp_random(), HEX);
    bool connected = mqttClient->connect(clientId.c_str(), MQTT_USER, MQTT_PASSWD, DEVICE_NAME "/status", 1, true, "offline", true);
    if (connected)
    {
        mqttClient->publish(DEVICE_NAME "/status", "online", true);
#ifdef COMPILE_SERIAL
        Serial.println("MQTT connected successfully!");
        // Subscribe to the all topics // Monitor Other Devices So we can display their status
        Serial.printf("Subscribing to #\n");
#endif
        bool subscribed = mqttClient->subscribe("#");
#ifdef COMPILE_SERIAL
        Serial.printf("Subscription to #: %s\n", subscribed ? "Success" : "Failed");
#endif
        if (handleMqttConnected)
        {
            handleMqttConnected(); // Call the connected handler if set
        }
    }
    else
    {
#ifdef COMPILE_SERIAL
        Serial.printf("MQTT connection failed, rc=%d: <%s>\n", mqttClient->state(), mqttErrorFromCode(mqttClient->state()));
#endif
    }
    return connected;
}

void MQTT_Loop()
{
    if (!mqttClient)
    {
#ifdef COMPILE_SERIAL
        Serial.println("MQTT client not initialized!");
#endif
        return;
    }
    mqttClient->loop();
    bool state = mqttClient->connected();
    if (!state)
    {
        // if the state changed from connected to disconnected
        if (last_state && handleMqttDisconnected)
        {
            handleMqttDisconnected(false); // Call the disconnected handler if set
        }
        MQTT_Safe_Connect();
    }
    last_state = state;

    // int processedMessages = 0;
    // while (mqttHandler.hasMessages() && processedMessages < MAX_MSGS_PER_LOOP)
    // {
    //     MqttMessage msg = mqttHandler.consume();
    //     _internal_MQTT_Send(msg.topic, msg.payload, msg.retained);
    //     processedMessages++;
    // }
}

void InsertTopicOwner(String *topic)
{
    if (topic->startsWith("/"))
        *topic = topic->substring(1);
    *topic = DEVICE_NAME + String("/") + *topic;
}

void MQTT_Send(String topic, String message, bool insertOwner, bool retained)
{
    if (insertOwner)
        InsertTopicOwner(&topic);
    _internal_MQTT_Send(topic, message, retained);
    // mqttHandler.addMessage(topic, message, retained);
}

void _internal_MQTT_Send(String topic, String message, bool retained)
{
    if (!mqttClient)
    {
#ifdef COMPILE_SERIAL
        Serial.println("MQTT client not initialized!");
#endif
        return;
    }
    if (!mqttClient->connected())
    {
#ifdef COMPILE_SERIAL
        Serial.println("MQTT client not connected.");
#endif
        return;
    }

    bool result = mqttClient->publish(topic.c_str(), message.c_str(), retained);

#if defined(COMPILE_SERIAL) && defined(DEBUG_MQTT)
    Serial.printf(">>MQTT [res:%d][Sending: '%s' : '%s']\n", result, topic.c_str(), message.c_str());
#endif
}

void MQTT_Send_Raw(String topic, String message)
{
    MQTT_Send(topic, message, false);
}

void processMqttMessage(char *topic, byte *payload, unsigned int length)
{
    if (!handleMqttMessage)
    {
#ifdef COMPILE_SERIAL
        Serial.println("No MQTT message handler set!");
#endif
        return;
    }
    // Serial.printf("MQTT Message received on topic: %s, length: %d\n", topic, length);
    String topicStr = String(topic);
    String payloadStr = String((char *)payload, length);

    handleMqttMessage(topicStr, payloadStr);
}

bool MQTT_isLocal()
{
    return local_initialized;
}

/// @brief Gets the connection status of the MQTT client.
/// @return a unsigned short value indicating the connection status:
/// - 0: Not connected to any MQTT broker.
/// - 1: Connected to local MQTT broker.
/// - 2: Connected to remote MQTT broker.
/// - 255: MQTT client not initialized.
byte MQTT_Connected()
{
    if (!mqttClient)
    {
#ifdef COMPILE_SERIAL
        Serial.println("MQTT client not initialized!");
#endif
        return 0xff;
    }
    byte base = local_initialized ? 1 : 2; // Default base value

    return mqttClient->connected() * base; // Return 1 for local, 2 for remote
}