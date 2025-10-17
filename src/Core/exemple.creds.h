#define REMOTE_MQTT_URL "<your_remote_mqtt_url>" //do not include mqtts://
#define REMOTE_MQTT_PORT remoteport usually 8883 for TLS
#define LOCAL_MQTT_HOST "<your_local_mqtt_ip>" // do not include mqtt://
#define LOCAL_MQTT_PORT localport usually 1883 for non-TLS
#define MQTT_USER "yourusername"
#define MQTT_PASSWD "yourpassword"
#define MQTT_CREDS_H

static const char *root_ca PROGMEM = R"EOF(
remote mqtt broker root ca certificate here
)EOF";