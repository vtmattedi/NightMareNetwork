#include "http.h"
#ifdef COMPILE_HTTP_SERVER
#define COMPILE_SERIAL
#define HTTP_LOG "\x1B[32m[HTTP]\x1B[0m"
#ifdef COMPILE_SERIAL
#define HTTP_LOGF(fmt, ...) Serial.printf("%s %s " fmt "\n", MILLIS_LOG, HTTP_LOG, ##__VA_ARGS__)
#define HTTP_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_LOG, HTTP_LOG, ##__VA_ARGS__)
#else
#define HTTP_LOGF(fmt, ...)
#define HTTP_ERRORF(fmt, ...)
#endif

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
    HTTP_LOGF("%s", logMsg.c_str());
    HTTP_LOGF("METHOD: %d", req->method);
    return ESP_OK;
}

esp_err_t nm_server(httpd_req_t *req)
{
    unsigned long _micros = micros();
    char buf[256];
    int ret, remaining = req->content_len;
    while (remaining > 0)
    {
        ret = httpd_req_recv(req, buf, _min(remaining, sizeof(buf)));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
                continue;
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    NightMareResults res = handleNightMareCommand(String(buf, req->content_len));
    httpd_resp_set_hdr(req, "Content-Type", "application/json");
    char origin[128];
    httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
    // HTTP_LOGF("Origin: %s\n", origin);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", origin);
    httpd_resp_send(req, res.response.c_str(), HTTPD_RESP_USE_STRLEN);
    _micros = micros() - _micros;
    // Log the processing time
    // Serial.printf("[HTTP] NM Command processed in %lu microseconds\n", _micros);
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
#ifdef COMPILE_WEBSOCKET_SERVER
    config.max_open_sockets = HTTPD_MAX_OPEN_SOCKETS;
    config.close_fn = http_close_cb;
#endif
    esp_err_t res = httpd_start(&server, &config);
    if (res == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_nm);
#ifdef COMPILE_WEBSOCKET_SERVER
        startWebsocketServer(server);
#endif
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, error_handler);
        http_state = useHighPriority ? HTTP_RUNNING_HIGH_PRIORITY : HTTP_RUNNING_NORMAL_PRIORITY;
    }
    else
    {
        http_state = HTTP_STOPPED;
    }
    Serial.printf("%s HTTP server initialized with %s\n", OK_LOG(res == ESP_OK), useHighPriority ? "high priority" : "normal priority");
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
        esp_err_t res = httpd_stop(server);
        server = NULL;
        http_state = HTTP_STOPPED;
        Serial.printf("%s HTTP server stopped\n", OK_LOG(res == ESP_OK));
    }
}

#endif