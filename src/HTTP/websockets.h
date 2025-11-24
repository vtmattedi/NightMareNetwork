#pragma once
#ifndef NIGHTMARE_CORE_WEBSOCKETS_H
#define NIGHTMARE_CORE_WEBSOCKETS_H

#include <Modules.config.h>
#ifdef COMPILE_WEBSOCKET_SERVER
#include <WiFi.h>
#include <esp_http_server.h>
#include <Xtra/NightMareTypes.h>
#include <Core/Timers.h>
static portMUX_TYPE ws_clients_lock = portMUX_INITIALIZER_UNLOCKED; // optional safety if multitasking
void ws_broadcast(const char *msg);
void http_close_cb(httpd_handle_t hd, int fd);
void startWebsocketServer(httpd_handle_t server_handle);

#define WS_SENSORS_REQUEST_MASK 1 << 0
#define WS_MUX_REQUEST_MASK     1 << 1

#define HTTPD_MAX_OPEN_SOCKETS 10
struct websockets
{
    int sockfd;
    uint8_t scheduler_requests = 0;
    bool active = false;
};

class WebsocketList
{
private:
    int nextIndex = 0;
    void handleSchedulerRequests();

public:
    uint8_t scheduler_requests = 0;
    int count = 0;
    websockets wsList[HTTPD_MAX_OPEN_SOCKETS];
    void addSocket(int sockfd);
    void removeSocket(int sockfd);
    void updateSchedulerRequests(int sockfd, uint8_t requests);
};

extern WebsocketList ws_clients;

#endif

#endif
