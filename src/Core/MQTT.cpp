#include "MQTT.h"

/// @brief Callback function to handle incoming MQTT messages.
void (*handleMqttMessage)(String topic, String message) = NULL;

void (*handleMqttConnected)(void) = NULL;
void (*handleMqttDisconnected)(bool) = NULL;
static bool mqtt_connected = false;
/// @brief ESP-MQTT client handle
esp_mqtt_client_handle_t mqttClient = NULL;
static int8_t mqtt_state = -1;         //  3 = connecting -1 = not initialized, 0 = disconnected, 1 = local connected, 2 = remote connected
static bool local_initialized = false; // True if initialized as local, false if remote
#ifdef COMPILE_SERIAL
const char *CLIENT[2] = {
    "\x1b[93;1m[Local]\x1b[0m",
    "\x1b[94;1m[Remote]\x1b[0m"};
#endif

#define printc Serial.print(local_initialized ? CLIENT[0] : CLIENT[1])
#define client_str (local_initialized ? CLIENT[0] : CLIENT[1])

#ifdef COMPILE_SERIAL
#define printf(...) \
    printc;         \
    Serial.printf(__VA_ARGS__);
#else
#define printf(...)
#endif
/// @brief MQTT event handler.
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        mqtt_state = local_initialized ? 1 : 2;
        printf(" MQTT connected successfully!\n");
        // Subscribe to all topics
        int rc = esp_mqtt_client_subscribe(mqttClient, "#", 0);
        printf("Subscription to #: %s (rc=%d)\n", rc >= 0 ? "Success" : "Failed", rc);
        // Send initial messages
        MQTT_Send("/status", "online", true, true);
        if (handleMqttConnected)
        {
            handleMqttConnected(); // Call the connected handler if set
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
    {
        mqtt_state = 0;
        printf("MQTT disconnected!");
        if (handleMqttDisconnected)
        {
            handleMqttDisconnected(local_initialized);
        }
        break;
    }

    case MQTT_EVENT_SUBSCRIBED:
        printf("MQTT_EVENT_SUBSCRIBED, msg_id=%d\n", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        printf("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d\n", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        printf("MQTT_EVENT_PUBLISHED, msg_id=%d\n", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
    {
        if (handleMqttMessage && event->data_len > 0 && event->topic_len > 0)
        {
            String topicStr = String(event->topic, event->topic_len);
            String payloadStr = String(event->data, event->data_len);
            printf(">>[%d][%s]:%s\n", event->msg_id, topicStr.c_str(), payloadStr.c_str());
            handleMqttMessage(topicStr, payloadStr);
        }
        break;
    }
    case MQTT_EVENT_ERROR:
 printf("MQTT_EVENT_ERROR\n");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            printf("Last error code reported from esp-tls: 0x%x\n", event->error_handle->esp_tls_last_esp_err);
            printf("Last tls stack error number: 0x%x\n", event->error_handle->esp_tls_stack_err);
            printf("Last captured errno : %d (%s)\n", event->error_handle->esp_transport_sock_errno,
                          strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            printf("Connection refused error: 0x%x\n", event->error_handle->connect_return_code);
        }

        break;

    default:
        printf("Other event id:%d\n", event->event_id);
        break;
    }
}
//
/// @brief Sets the callback function to be called when a new MQTT message is received.
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

void MQTT_onDisconnected(void (*cb)(bool))
{
    if (cb)
    {
        handleMqttDisconnected = cb;
    }
}

/// @brief Initializes the MQTT client.
/// @param local If true, initializes as local; if false, initializes as remote.
void MQTT_Init(bool local)
{
    local_initialized = local;

    esp_mqtt_client_config_t mqtt_cfg = {};
    String clientId = String(DEVICE_NAME) + "-" + String(esp_random(), HEX);

    // Allocate memory for strings that need to persist
    static char uri_buffer[128];
    static char last_will_topic[64] = DEVICE_NAME "/status\0";
    static char last_will_message[16] = "offline\0";
    static char client_id_buffer[64];
    snprintf(client_id_buffer, sizeof(client_id_buffer), "%s-%x", DEVICE_NAME, esp_random());
    mqtt_cfg.username = MQTT_USER;
    mqtt_cfg.password = MQTT_PASSWD;
    mqtt_cfg.client_id = client_id_buffer;
    mqtt_cfg.lwt_topic = last_will_topic;
    mqtt_cfg.lwt_msg = last_will_message;
    mqtt_cfg.lwt_qos = 0;
    mqtt_cfg.lwt_retain = 1;

    if (local)
    {
        // Local connection (non-secure)
        snprintf(uri_buffer, sizeof(uri_buffer), "mqtt://%s:%d", LOCAL_MQTT_HOST, LOCAL_MQTT_PORT);
        mqtt_cfg.uri = uri_buffer;
        printf("MQTT client initialized for local connection.\n");
    }
    else
    {
        // Remote connection (secure)
        snprintf(uri_buffer, sizeof(uri_buffer), "mqtts://%s:%d", REMOTE_MQTT_URL, REMOTE_MQTT_PORT);
        mqtt_cfg.uri = uri_buffer;
        mqtt_cfg.cert_pem = root_ca; // Set to NULL to disable server cert verification
        printf("MQTT client initialized for remote connection.\n");
    }

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    if (mqttClient)
    {
        esp_mqtt_client_register_event(mqttClient, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(mqttClient);
        mqtt_state = 3; // Connecting
    }
    else
    {
        printf("Failed to initialize MQTT client!\n");
    }
}
/// @brief Ends the MQTT client connection and cleans up resources.
void MQTT_End()
{
    if (mqttClient)
    {
        esp_mqtt_client_stop(mqttClient);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = NULL;
        printf("MQTT client disconnected and resources cleaned up.\n");
    }
    mqtt_state = -1; // Not initialized
}
/// @brief Prepends the device name to the topic if it starts with a '/' character.
void InsertTopicOwner(String *topic)
{
    if (topic->startsWith("/"))
        *topic = topic->substring(1);
    *topic = DEVICE_NAME + String("/") + *topic;
}
/// @brief  Sends an MQTT message to the specified topic.
/// @param topic The topic to publish the message to.
/// @param message The message payload to send.
/// @param insertOwner If true, the device name will be prepended to the topic.
/// @param retained If true, the message will be retained by the broker.
void MQTT_Send(String topic, String message, bool insertOwner, bool retained)
{
    if (!mqttClient)
    {
        printf("MQTT client not initialized. Cannot send message.\n");
        return;
    }

    if (insertOwner)
        InsertTopicOwner(&topic);

    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), message.c_str(),
                                         message.length(), 0, retained ? 1 : 0);
    printf("<<[%d][%s]:%s\n", msg_id, topic.c_str(), message.c_str());
}
/// @brief Sends a raw MQTT message without any modifications on topic name.
/// @param topic The topic to publish the message to.
/// @param message The message payload to send.
void MQTT_Send_Raw(String topic, String message)
{
    MQTT_Send(topic, message, false, false);
}

/// @brief Changes the initialization of the MQTT client to local or remote.
/// @param local if true, initializes as local; if false, initializes as remote.
void MQTT_change_to(bool local)
{
    if (local_initialized == local)
    {
        printf("MQTT client already initialized as %s. No changes made.\n", local ? "local" : "remote");
        return;
    }
    MQTT_End();
    MQTT_Init(local);
}

/// @brief Gets the connection state of the MQTT client.
/// @return -1 if the MQTT client is not initialized.
///         0 if not connected to any MQTT broker.
///         1 if connected to local MQTT broker.
///         2 if connected to remote MQTT broker.
///         3 if connecting.
int8_t MQTT_Connected()
{
    return mqtt_state;
}

/// @brief Checks if the MQTT client is initialized as local.
bool MQTT_isLocal()
{
    return local_initialized;
}
/// @deprecated Use MQTT_Send_Raw; this is only for backward compatibility.
/// @brief Sends a message to MQTT with the device name prepended to the topic.
/// @param topic The topic to publish the message to.
/// @param message The message payload to send.
void Send_to_MQTT(String topic, String message)
{
    MQTT_Send_Raw(topic, message);
}