#include <NightMare/Features.h>
#if NM_ENABLE_HTTP
#include "http.h"
#include <Core/NightMareCommand.h>

static HTTP_Server_State http_state = HTTP_STOPPED;
httpd_handle_t server = NULL;

// Root handler
esp_err_t root_handler(httpd_req_t *req)
{

#ifdef USE_REDIRECT
    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", REDIRECT_URL);
    httpd_resp_send(req, NULL, 0);
#else
    const char *html = "<!DOCTYPE html><html><head><title>ESP32</title></head>"
                       "<body><h1>ESP32 Web Server</h1>"
                       "<p>Server is running!</p></body></html>";
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
#endif
    return ESP_OK;
}

esp_err_t error_handler(httpd_req_t *req, httpd_err_code_t error)
{

    String logMsg = String("404 Not Found: ") + String(req->uri);

    httpd_resp_send(req, logMsg.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t nm_server(httpd_req_t *req)
{
    char buf[256];
    if (req->content_len > NM_MAX_MESSAGE_LEN)
    {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, "Command too long", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    String command;
    if (req->content_len > 0 && !command.reserve(req->content_len))
    {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Out of memory", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    int ret, remaining = req->content_len;
    while (remaining > 0)
    {
        const size_t chunk = remaining < static_cast<int>(sizeof(buf) - 1)
                                 ? remaining : sizeof(buf) - 1;
        ret = httpd_req_recv(req, buf, chunk);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            return ESP_FAIL;
        }
        buf[ret] = '\0';
        command += buf;
        remaining -= ret;
    }
    NightMareResults res = handleNightMareCommand(command);
    httpd_resp_set_hdr(req, "Content-Type", "application/json");
    char origin[128];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK)
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", origin);
    httpd_resp_send(req, res.response.c_str(), HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// URI handlers
httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL};

httpd_uri_t uri_nm = {
    .uri = "/nm",
    .method = HTTP_POST,
    .handler = nm_server,
    .user_ctx = NULL};


/** @brief Sets HTTP server priority and (re)start server
    @param useHighPriority If true, sets higher priority for the HTTP server task.
    @return ESP_OK if the server was started successfully, otherwise an error code.
 */
esp_err_t setHttpHighPriority(bool useHighPriority)
{
    if (server)
    {
        http_stop();
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = HTTP_TASK_STACK_SIZE;
    config.task_priority = useHighPriority ? HTTP_TASK_HIGHER_PRIORITY : HTTP_TASK_PRIORITY;
#if NM_ENABLE_WEBSOCKET
    config.max_open_sockets = HTTPD_MAX_OPEN_SOCKETS;
    config.close_fn = http_close_cb;
#endif
    esp_err_t res = httpd_start(&server, &config);
    if (res == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_nm);
#if NM_ENABLE_WEBSOCKET
        startWebsocketServer(server);
#endif
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, error_handler);
        http_state = useHighPriority ? HTTP_RUNNING_HIGH_PRIORITY : HTTP_RUNNING_NORMAL_PRIORITY;
    }
    else
    {
        http_state = HTTP_STOPPED;
    }
    return res;
}

// Start HTTP server
httpd_handle_t http_init(void)
{
    esp_err_t res = setHttpHighPriority(false);
    return res == ESP_OK ? server : NULL;
}

// Get HTTP server state
HTTP_Server_State getHttpState()
{
    return http_state;
}

// Stops HTTP server
void http_stop(void)
{
    if (server)
    {
        httpd_stop(server);
        server = NULL;
        http_state = HTTP_STOPPED;
    }
}
#endif // NM_ENABLE_HTTP
