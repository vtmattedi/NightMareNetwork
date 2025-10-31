

#include "http.h"
#ifdef COMPILE_HTTP_SERVER

#ifdef COMPILE_SERIAL
#define HTTP_LOGF(fmt, ...) Serial.printf("%s %s " fmt "\n", MILLIS_LOG, HTTP_LOG, ##__VA_ARGS__)
#define HTTP_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_LOG, HTTP_LOG, ##__VA_ARGS__)
#else
#define HTTP_LOGF(fmt, ...)
#define HTTP_ERRORF(fmt, ...)
#endif
// Module code goes here

httpd_handle_t server = NULL;

// Root handler
esp_err_t root_handler(httpd_req_t *req)
{
    const char *html = "<!DOCTYPE html><html><head><title>ESP32</title></head>"
                       "<body><h1>ESP32 Web Server</h1>"
                       "<p>Server is running!</p></body></html>";
    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", "https://nightmarenetwork.io");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// API handler
esp_err_t api_handler(httpd_req_t *req)
{
    char json[64];
    snprintf(json, sizeof(json), "{\"status\":\"ok\",\"uptime\":%lu}", millis() / 1000);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// POST example handler
esp_err_t post_handler(httpd_req_t *req)
{
    char buf[100];
    req->content_len;
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

    httpd_resp_send(req, "Data received", HTTPD_RESP_USE_STRLEN);
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
    int m = micros();
    char buf[100];
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
    httpd_resp_send(req, res.response.c_str(), HTTPD_RESP_USE_STRLEN);
    Serial.printf("%s NM command processed in %lu us\n", "[HTTP]", micros() - m);
    return ESP_OK;
}

// URI handlers
httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL};

httpd_uri_t uri_api = {
    .uri = "/api",
    .method = HTTP_GET,
    .handler = api_handler,
    .user_ctx = NULL};

httpd_uri_t uri_post = {
    .uri = "/post",
    .method = HTTP_POST,
    .handler = post_handler,
    .user_ctx = NULL};

httpd_uri_t uri_nm = {
    .uri = "/nm",
    .method = HTTP_POST,
    .handler = nm_server,
    .user_ctx = NULL};

// Start HTTP server
httpd_handle_t http_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    esp_err_t res = httpd_start(&server, &config);
    if (res == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_api);
        httpd_register_uri_handler(server, &uri_post);
        httpd_register_uri_handler(server, &uri_nm);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, error_handler);
    }
    Serial.printf("%s HTTP server initialization\n", OK_LOG(res == ESP_OK));

    return res == ESP_OK ? server : NULL;
}

// void setup() {
//   Serial.begin(115200);

//   // Connect to WiFi
//   Serial.println("Connecting to WiFi...");
//   WiFi.begin(ssid, password);

//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }

//   Serial.println("\nWiFi connected!");
//   Serial.print("IP address: ");
//   Serial.println(WiFi.localIP());

//   // Start server
//   start_webserver();
// }

#endif