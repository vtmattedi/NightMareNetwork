#pragma once
#ifndef NIGHTMARE_CORE_HTTP_H
#define NIGHTMARE_CORE_HTTP_H

#include <Modules.config.h>
#ifdef COMPILE_HTTP_SERVER
#ifdef COMPILE_WEBSOCKET_SERVER
#include "websockets.h"
#endif
#include <WiFi.h>
#include <esp_http_server.h>
#include <Xtra/NightMareTypes.h>

#define HTTP_TASK_PRIORITY 5
#define HTTP_TASK_HIGHER_PRIORITY 10
#define HTTP_TASK_STACK_SIZE 8192

#define USE_REDIRECT // If defined, redirects root requests to REDIRECT_URL
#define REDIRECT_URL "https://www.mattedidev.com.br" // URL to redirect root requests to

enum HTTP_Server_State {
    HTTP_STOPPED = 0,
    HTTP_RUNNING_NORMAL_PRIORITY = 1,
    HTTP_RUNNING_HIGH_PRIORITY = 2
};

httpd_handle_t http_init();
void http_stop();
esp_err_t setHttpHighPriority(bool useHighPriority);
HTTP_Server_State getHttpState();
#endif
#endif
