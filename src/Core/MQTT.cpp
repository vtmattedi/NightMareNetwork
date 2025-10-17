#include "MQTT.h"
#ifdef COMPILE_MQTT
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
// Control commands for safe MQTT operations
typedef enum
{
    MQTT_CMD_NONE,
    MQTT_CMD_SWITCH_TO_LOCAL,
    MQTT_CMD_SWITCH_TO_REMOTE,
    MQTT_CMD_DISCONNECT,
    MQTT_CMD_RECONNECT
} mqtt_control_cmd_t;

// Global control queue
static QueueHandle_t mqtt_control_queue = NULL;
static TaskHandle_t mqtt_control_task_handle = NULL;
static String local_ip = LOCAL_MQTT_HOST;
#define printc Serial.print(local_initialized ? CLIENT[0] : CLIENT[1])
#define client_str (local_initialized ? CLIENT[0] : CLIENT[1])

#ifdef COMPILE_SERIAL
#define printf(...) \
    printc;         \
    Serial.printf(__VA_ARGS__);
#else
#define printf(...)
#endif
/*Forward Declarations for private functions*/
void MQTT_Config(bool local);
// Control task - handles stop/start/destroy operations safely
void mqtt_control_task(void *arg)
{
    mqtt_control_cmd_t cmd;

    while (1)
    {
        if (xQueueReceive(mqtt_control_queue, &cmd, portMAX_DELAY))
        {
            switch (cmd)
            {
            case MQTT_CMD_SWITCH_TO_LOCAL:
                printf("Control Task: Switching to LOCAL...\n");
                if (mqttClient)
                {
                    esp_mqtt_client_stop(mqttClient);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_mqtt_client_destroy(mqttClient);
                    mqttClient = NULL;
                }
                mqtt_state = -1;
                vTaskDelay(pdMS_TO_TICKS(500));
                MQTT_Config(true);
                break;

            case MQTT_CMD_SWITCH_TO_REMOTE:
                printf("Control Task: Switching to REMOTE...\n");
                if (mqttClient)
                {
                    esp_mqtt_client_stop(mqttClient);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_mqtt_client_destroy(mqttClient);
                    mqttClient = NULL;
                }
                mqtt_state = -1;
                vTaskDelay(pdMS_TO_TICKS(500));
                MQTT_Config(false);
                break;

            case MQTT_CMD_DISCONNECT:
                printf("Control Task: Disconnecting...\n");
                if (mqttClient)
                {
                    esp_mqtt_client_stop(mqttClient);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_mqtt_client_destroy(mqttClient);
                    mqttClient = NULL;
                    mqtt_state = -1;
                }
                
                MQTT_Finish();
                break;
            // This should rarely be used, but is here for completeness
            case MQTT_CMD_RECONNECT:
                printf("Control Task: Reconnecting...\n");
                if (mqttClient)
                {
                    esp_mqtt_client_reconnect(mqttClient);
                }
                break;

            default:
                break;
            }
        }
    }
}
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
#ifdef MQTT_PREPROCESS
        // Send initial messages
        MQTT_Send("/status", "online", true, true);
        static bool first_time = true;
        if (first_time)
        {
            MQTT_Send("console/out", "Booted");
            first_time = false;
        }
#endif
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
            printf("\x1b[97;1m>>>\x1b[0m[%s]:%s\n", topicStr.c_str(), payloadStr.c_str());
#ifdef MQTT_PREPROCESS
            if (topicStr == String(DEVICE_NAME) + "/console/in" || topicStr == "All/console/in")
            {
                // Handle command internally
                NightMareResults res = handleNightMareCommand(payloadStr);
                MQTT_Send(String(DEVICE_NAME) + "/console/out", res.response);
                return; // Do not pass to external handler
            }
            else if (topicStr == "All/connection")
            {
                bool local = (payloadStr != "go_external");
                if (local != local_initialized)
                {
                    if(local)
                    {
                        int _index = payloadStr.indexOf(' ');
                        if(_index > 0)
                        {
                            local_ip = payloadStr.substring(_index + 1);
                            printf("Switching to local with IP: %s\n", local_ip.c_str());
                        }
                    }
                    MQTT_change_to(local);
                }
                return; // Do not pass to external handler
            }
#endif
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

/// @brief Sets the callback function to be called when a new MQTT message is received.
void MQTT_onMessage(void (*cb)(String topic, String message))
{
    if (cb)
    {
        handleMqttMessage = cb;
    }
}

/// @brief Sets the callback function to be called when the MQTT client is connected.
/// @param cb The callback function to be called.
void MQTT_onConnected(void (*cb)(void))
{
    if (cb)
    {
        handleMqttConnected = cb;
    }
}

/// @brief Sets the callback function to be called when the MQTT client is disconnected.
/// @param cb The callback function to be called, with a boolean parameter indicating if it was
void MQTT_onDisconnected(void (*cb)(bool))
{
    if (cb)
    {
        handleMqttDisconnected = cb;
    }
}

/// @brief Initialize the MQTT control system (call once during setup)
void MQTT_Control_Init()
{
    if (!mqtt_control_queue)
    {
        mqtt_control_queue = xQueueCreate(2, sizeof(mqtt_control_cmd_t));

        xTaskCreate(mqtt_control_task,
                    "mqtt_ctrl",
                    4096, // Stack size
                    NULL,
                    5, // Priority
                    &mqtt_control_task_handle);

        printf("MQTT Control Task initialized\n");
    }
}

/// @brief Starts the MQTT client with the specified configuration.
/// @param local If true, initializes as local; if false, initializes as remote.
void MQTT_Config(bool local){
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
    mqtt_cfg.task_stack = 8192;

    if (local)
    {
        // Local connection (non-secure)
        snprintf(uri_buffer, sizeof(uri_buffer), "mqtt://%s:%d", local_ip.c_str(), LOCAL_MQTT_PORT);
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
        mqtt_state = -2; // Connecting
    }
    else
    {
        printf("Failed to initialize MQTT client!\n");
    }

}

/// @brief Initializes the MQTT client.
/// @param local If true, initializes as local; if false, initializes as remote.
void MQTT_Init(bool local)
{
    if (mqtt_control_queue)
    {
        printf("MQTT client already initialized. Use MQTT_change_to.\n");
        return;
    }
    MQTT_Control_Init();
    MQTT_Config(local);
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

/// @brief Finalizes the MQTT system by deleting the control queue and task.
void MQTT_Finish()
{
    MQTT_End();
    if (mqtt_control_queue)
    {
        vQueueDelete(mqtt_control_queue);
        mqtt_control_queue = NULL;
    }
    if (mqtt_control_task_handle)
    {
        vTaskDelete(mqtt_control_task_handle);
        mqtt_control_task_handle = NULL;
    }
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
    if (!mqttClient || mqtt_state <= 0)
    {
        printf("MQTT client not initialized. Cannot send message.\n");
        return;
    }

    if (insertOwner)
        InsertTopicOwner(&topic);

    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), message.c_str(),
                                         message.length(), 0, retained ? 1 : 0);
    printf("\x1b[90;1m<<<\x1b[0m[%s]:%s\n", topic.c_str(), message.c_str());
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
    mqtt_control_cmd_t cmd = local ? MQTT_CMD_SWITCH_TO_LOCAL : MQTT_CMD_SWITCH_TO_REMOTE;
    if (xQueueSend(mqtt_control_queue, &cmd, pdMS_TO_TICKS(100)) != pdPASS)
    {
        printf("ERROR: Failed to queue MQTT switch command\n");
    }
    else
    {
        printf("Queued switch to %s\n", local ? "LOCAL" : "REMOTE");
    }
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

int8_t MQTT_State(){
    return mqtt_state;
}
#endif