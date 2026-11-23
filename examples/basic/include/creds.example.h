#pragma once

#define MQTT_CREDS_H

#define DEFAULT_SSID "your_wifi_ssid"
#define DEFAULT_PASSWORD "your_wifi_password"

#define LOCAL_MQTT_HOST "192.168.1.10"
#define LOCAL_MQTT_PORT 1883

// Host only; NmMqttEsp adds mqtts://.
#define REMOTE_MQTT_URL "mqtt.example.com"
#define REMOTE_MQTT_PORT 8883

#define MQTT_USER "yourusername"
#define MQTT_PASSWD "yourpassword"

// Replace with the CA PEM before using Remote MQTT.
// Empty is only a compile-time placeholder for these local-first examples.
#define ROOT_CA ""

// Namespace (docs/architecture/namespaces.md). Empty (the default) keeps the legacy unprefixed
// topic layout. 1-16 characters of [a-z0-9-], starting with a letter.
// #define NM_NAMESPACE "home"
// First topic segment that separates NightMare from other users of the broker (default "nm").
// #define NM_TOPIC_ROOT "nm"
