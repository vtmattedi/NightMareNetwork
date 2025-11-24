#include "websockets.h"
#ifdef COMPILE_WEBSOCKET_SERVER
#define HTTP_LOG "\x1B[32;1m[WS]\x1B[0m"
#ifdef COMPILE_SERIAL
#define HTTP_LOGF(fmt, ...) Serial.printf("%s %s " fmt "\n", MILLIS_LOG, HTTP_LOG, ##__VA_ARGS__)
#define HTTP_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_LOG, HTTP_LOG, ##__VA_ARGS__)
#else
#define HTTP_LOGF(fmt, ...)
#define HTTP_ERRORF(fmt, ...)
#endif

WebsocketList ws_clients;
httpd_handle_t ws_server = NULL;

void ws_scheduler_task(void *param)
{
    for (;;)
    {
        HTTP_LOGF("WebSocket scheduler task running, requests=0x%02X", ws_clients.scheduler_requests);
        if (ws_clients.scheduler_requests != 0)
        {
            bool sensors_requested = ws_clients.scheduler_requests & WS_SENSORS_REQUEST_MASK;
            bool mux_requested = ws_clients.scheduler_requests & WS_MUX_REQUEST_MASK;
            String sensors_data = "";
            String mux_data = "";
            if (sensors_requested)
            {
                sensors_data = handleNightMareCommand("SENSORSDATA").response;
            }
            if (mux_requested)
            {
                mux_data = handleNightMareCommand("MUXDATA -q").response;
            }
            for (size_t i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
            {
                if (!ws_clients.wsList[i].active)
                    continue;
                int sock = ws_clients.wsList[i].sockfd;
                uint8_t requests = ws_clients.wsList[i].scheduler_requests;

                if (requests & WS_SENSORS_REQUEST_MASK)
                {
                    String message = "1:" + sensors_data;
                    httpd_ws_frame_t frame = {
                        .type = HTTPD_WS_TYPE_TEXT,
                        .payload = (uint8_t *)message.c_str(),
                        .len = message.length()};
                    esp_err_t ret = httpd_ws_send_frame_async(ws_server, sock, &frame);
                    if (ret != ESP_OK)
                    {
                        HTTP_ERRORF("Sensors sched send to %d failed (%d), removing client", sock, ret);
                        ws_clients.removeSocket(sock);
                        continue;
                    }
                }
                if (requests & WS_MUX_REQUEST_MASK)
                {
                    String message = "2:" + mux_data;
                    httpd_ws_frame_t frame = {
                        .type = HTTPD_WS_TYPE_TEXT,
                        .payload = (uint8_t *)message.c_str(),
                        .len = message.length()};
                    esp_err_t ret = httpd_ws_send_frame_async(ws_server, sock, &frame);
                    if (ret != ESP_OK)
                    {
                        HTTP_ERRORF("Mux sched send to %d failed (%d), removing client", sock, ret);
                        ws_clients.removeSocket(sock);
                        continue;
                    }
                }
            }
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS); // check every second
    }
}

esp_err_t ws_handler(httpd_req_t *req)
{
    HTTP_LOGF("ws_handler called, method=%d", req->method);

    if (req->method == HTTP_GET)
    {
        // WebSocket handshake: must NOT send a body or call httpd_resp_send
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); // optional
        int fd = httpd_req_to_sockfd(req);
        ws_clients.addSocket(fd);

        return ESP_OK;
    }

    // For non-GET, treat as incoming ws frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        HTTP_ERRORF("ws_recv_frame (meta) failed: %d", ret);
        return ret;
    }
    HTTP_LOGF("ws_handler called, frame=%d", ws_pkt.type);
    // Close frame -> remove client
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
    {
        int fd = httpd_req_to_sockfd(req);
        ws_clients.removeSocket(fd);
        return ESP_OK;
    }

    // Ping/pong or empty
    if (ws_pkt.len == 0)
    {
        return ESP_OK;
    }

    uint8_t *buf = (uint8_t *)malloc(ws_pkt.len + 1);
    if (!buf)
        return ESP_ERR_NO_MEM;

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK)
    {
        free(buf);
        HTTP_ERRORF("ws_recv_frame (data) failed: %d", ret);
        return ret;
    }

    buf[ws_pkt.len] = 0;
    HTTP_LOGF("WS Received (%d): %s", ws_pkt.len, (char *)buf);
    if (ws_pkt.len == 3)
    {
        if (buf[2] == ';')
        {
            // Special case: client is updating scheduler requests
            int fd = httpd_req_to_sockfd(req);
            uint8_t requests = atoi((char *)buf);
            ws_clients.updateSchedulerRequests(fd, requests);
            free(buf);
            if (requests & WS_SENSORS_REQUEST_MASK)
            {
                String message = "1:";
                message += handleNightMareCommand("sensors").response;
                httpd_ws_frame_t out = {
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t *)message.c_str(),
                    .len = message.length()};
                httpd_ws_send_frame(req, &out);
            }
            else if (requests & WS_MUX_REQUEST_MASK)
            {
                String message = "2:";
                message += handleNightMareCommand("mux").response;
                httpd_ws_frame_t out = {
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t *)message.c_str(),
                    .len = message.length()};
                httpd_ws_send_frame(req, &out);
            }
            return ESP_OK;
        }
    }
    String message = "0:";
    message += handleNightMareCommand(String((char *)buf, ws_pkt.len)).response;

    httpd_ws_frame_t out = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)message.c_str(),
        .len = message.length()};
    httpd_ws_send_frame(req, &out);

    free(buf);
    return ESP_OK;
}

httpd_uri_t ws_handshake = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler, // WebSocket handler to be set during WebSocket initialization
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = true};

void ws_broadcast(const char *message)
{
    if (!ws_server)
        return;

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)message,
        .len = strlen(message)};

    for (size_t i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
    {
        if (!ws_clients.wsList[i].active)
            continue;
        HTTP_LOGF("Broadcasting to socket %d", ws_clients.wsList[i].sockfd);
        int sock = ws_clients.wsList[i].sockfd;
        esp_err_t ret = httpd_ws_send_frame_async(ws_server, sock, &frame);
        if (ret != ESP_OK)
        {
            HTTP_ERRORF("Broadcast to %d failed (%d), removing client", sock, ret);
            ws_clients.removeSocket(sock);
        }
    }
}

void startWebsocketServer(httpd_handle_t server_handle)
{
    ws_server = server_handle;
    httpd_register_uri_handler(server_handle, &ws_handshake);
    auto res = xTaskCreatePinnedToCore(ws_scheduler_task, "ws_scheduler_task", 4096, NULL, 5, NULL, 1);
    Serial.printf("%s WebSocket Server Task Started\n", OK_LOG(res == pdPASS));
}

void http_close_cb(httpd_handle_t hd, int fd)
{
    // server closed or socket dropped
    ws_clients.removeSocket(fd);
}

void broadcastMessage() {}

void WebsocketList::addSocket(int sockfd)
{
    if (count >= HTTPD_MAX_OPEN_SOCKETS)
    {
        HTTP_ERRORF("Max WS clients reached (%d)", HTTPD_MAX_OPEN_SOCKETS);
        return;
    }
    wsList[nextIndex].sockfd = sockfd;
    wsList[nextIndex].active = true;
    count++;
    HTTP_LOGF("Added WS client %d, total=%d", sockfd, count);
    // find next available slot
    int found = -1;
    for (int i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
    {
        if (!wsList[i].active)
        {
            found = i;
            break;
        }
    }
    nextIndex = found;
    handleSchedulerRequests();
}

void WebsocketList::removeSocket(int sockfd)
{
    HTTP_LOGF("Removing WS client %d", sockfd);
    for (int i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
    {
        if (wsList[i].active && wsList[i].sockfd == sockfd)
        {
            HTTP_LOGF("Found and removed WS client %d", sockfd);
            wsList[i].active = false;
            count--;
            // if there is no more slots or the removed slot is before nextIndex, update nextIndex
            if (nextIndex == -1 || i < nextIndex)
            {
                nextIndex = i;
            } // cleared slot is now the next available
        }
    }
    handleSchedulerRequests();
}

void WebsocketList::handleSchedulerRequests()
{
    int8_t requests = 0;
    uint8_t current_requests = scheduler_requests;
    for (int i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
    {
        if (wsList[i].active)
        {
            requests |= wsList[i].scheduler_requests;
        }
    }
    if (requests == current_requests)
        return; // no change
    scheduler_requests = requests;
}

void WebsocketList::updateSchedulerRequests(int sockfd, uint8_t requests)
{
    for (int i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
    {
        if (wsList[i].active && wsList[i].sockfd == sockfd)
        {
            wsList[i].scheduler_requests = requests;
            HTTP_LOGF("Updated scheduler requests for socket %d to %d", sockfd, requests);
            break;
        }
    }
    handleSchedulerRequests();
}

#endif