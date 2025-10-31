#pragma once
#ifndef NIGHTMARE_CORE_HTTP_H
#define NIGHTMARE_CORE_HTTP_H

#include <Modules.config.h>
#ifdef COMPILE_HTTP_SERVER
#include <WiFi.h>
#include <esp_http_server.h>
#include <Xtra/NightMareCommand.h>


// Module header code goes here
httpd_handle_t http_init();
#endif
#endif
