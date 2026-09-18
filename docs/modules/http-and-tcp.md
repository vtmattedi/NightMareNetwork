---
title: HTTP, WebSockets and TCP
description: The transports that predate MQTT on the network — an HTTP server with a WebSocket channel, and the NightMare TCP client/server — and where they still earn their place.
section: modules
order: 130
---

# HTTP, WebSockets and TCP

MQTT is the network's transport. These three are older, still shipped, and
used where a device serves its own UI or talks on a LAN without a broker.
None of them is part of the [wire contract](/docs/protocols/topics) a
consumer relies on.

## HTTP server — `HTTP/http.h`

ESP-IDF's `esp_http_server`, wrapped. Compiled with `COMPILE_HTTP_SERVER`.

```cpp
httpd_handle_t http_init();
void           http_stop();
esp_err_t      setHttpHighPriority(bool useHighPriority);   // HTTP_TASK_PRIORITY 5 -> 10
HTTP_Server_State getHttpState();   // HTTP_STOPPED, HTTP_RUNNING_NORMAL_PRIORITY, HTTP_RUNNING_HIGH_PRIORITY
```

`USE_REDIRECT` sends a request for `/` to `REDIRECT_URL`. The task stack is
8 KB. Telemetry reports `direct_http` from `getHttpState()`. Console:
`HTTPSERVER PRIORITY | STATUS | RESET | ENABLE`.

The command resolver accepts commands over HTTP with
`NM_CMD_SRC_HTTP` as the source, which is how Turing's web terminal works.

## WebSockets — `HTTP/websockets.h`

A WebSocket endpoint on the same server. Compiled with
`COMPILE_WEBSOCKET_SERVER`.

```cpp
void startWebsocketServer(httpd_handle_t server);
void ws_broadcast(const char *msg);
extern WebsocketList ws_clients;      // up to HTTPD_MAX_OPEN_SOCKETS (10)
```

Each client carries a `scheduler_requests` bitmask (`WS_SENSORS_REQUEST_MASK`,
`WS_MUX_REQUEST_MASK`) — a subscription to periodic pushes, from the garden
controller's live pages. Console: `WS LIST`.

## NightMare TCP — `TCP/`

A line-oriented TCP client and server with keep-alive, from before the MQTT
client existed. Compiled with `COMPILE_NIGHTMARE_TCP_CLIENT` and/or
`COMPILE_NIGHTMARE_TCP_SERVER`.

| constant | value |
| --- | --- |
| `DEFAULT_PORT` | 4100 |
| `KEEP_ALIVE_MESSAGE` / `KEEP_ALIVE_RESPONSE` | `***keep alive***` / `***ack***`, every 60 s by default |
| `DEFAULT_TIMEOUT` | 180 s of silence drops the peer |

Framing is chosen per connection:

```cpp
enum TransmissionMode {
    AllAvailable = 0,     // raw: whatever arrived is the message
    SizeColon = 1,        // "{length}:{message}" -- read the length, then exactly that many bytes
    ThreeCharHeaders = 2  // not fully implemented
};
String PrepareMsg(String msg, TransmissionMode mode);
```

`SizeColon` is the one to use for anything that can be split across packets.
The Sherlock light and Turing garden devices talk to each other this way;
newer devices do not use it. The client and server classes are in
`TCP/NightMareTCPClient.h` and `TCP/NightMareTCPServer.h` — ask the MCP for
`get_api NightMareTCPClient` for the current constructors.
