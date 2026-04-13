#include "NightMareCommand.h"

NightMareResults (*resolveCommand)(const NightMareMessage &message) = nullptr;

void asyncSend(const String &msg, NightmareContext context)
{
    // This function can be used by commands to send messages asynchronously, for example to send progress updates. It will send the message to the source of the command, for example if the command was received via MQTT it will send the message back to the MQTT topic it was received from.
    if (context.msgSource == NM_CMD_SRC_MQTT)
    {
        MQTT_Send(context.sourceIdentifier, msg, false, false);
    }
    else if (context.msgSource == NM_CMD_SRC_SERIAL)
    {
        HardwareSerial *_Serial = reinterpret_cast<HardwareSerial *>(context.userContext);
        if (_Serial)
            _Serial->println(msg);
    }
}

void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message))
{
    resolveCommand = resolver;
}

#ifdef ENABLE_PREPROCESSING
const char *getBootReason(int reason)
{
    switch (reason)
    {
    case 1:
        return "Power on";
    case 2:
        return "External pin";
    case 3:
        return "Software reset";
    case 4:
        return "Panic";
    case 5:
        return "Interrupt watchdog";
    case 6:
        return "Task watchdog";
    case 7:
        return "Other watchdog";
    case 8:
        return "Deep sleep exit";
    case 9:
        return "Brownout";
    case 10:
        return "SDIO reset";
    default:
        return "Unknown";
    }
}
#endif

NightMareResults handleNightMareCommand(const String &message, NightmareContext context)
{
    const char delimiter = ' ';
    NightMareResults result;
    result.response = "No command resolved";
    result.result = true;
    result.context = context;
    NightMareMessage parsedMsg;
    String current_string = "";
#ifdef COMPILE_SERIAL
    Serial.printf("%s Received: '%s'\n", COMMAND_RESOLVER_LOG, message.c_str());
#endif
    int index = 0;
    // gets command and args using the delimiters
    bool in_quotes = false;
    String quote = "";
    for (size_t i = 0; i < message.length(); i++)
    {
        char c = message.charAt(i);
        if (c == ' ' && !in_quotes)
        {
            index++;
            continue;
        }
        if (c == '\"')
        {
            in_quotes = !in_quotes;
            // start of quote
            if (in_quotes)
                quote = "";
            else
            {
                if (quote.length() == 0)
                    quote = "\"";
                // end of quote
                if (index == 0)
                    parsedMsg.command += quote;
                else if (index < 5)
                    parsedMsg.args[index - 1] += quote;
                quote = "";
            }
            // go to next char
            continue;
        }
        if (in_quotes)
        {
            quote += c;
        }
        else
        {
            if (index == 0)
                parsedMsg.command += c;
            else if (index < 6)
                parsedMsg.args[index - 1] += c;
        }
    }
    parsedMsg.command.toUpperCase();
    parsedMsg.subcommand = parsedMsg.args[0];
    parsedMsg.subcommand.toUpperCase();
#ifdef ENABLE_PREPROCESSING
    bool prehandled = true;
    // Basic commands that can be handled without a resolver
    if (parsedMsg.command == "PING")
    {
        result.response = "PONG";
    }
    else if (parsedMsg.command == "REBOOT")
    {
        ESP.restart();
        result.response = "Rebooting...";
    }
    else if (parsedMsg.command == "BOOTINFO")
    {
        auto doc = DynamicJsonDocument(256);
        doc["ResetReason"] = SystemSettings.get("boot_reason", "Unknown");
        doc["IsTimeSynced"] = SystemSettings.getFlag("time_synced");
        doc["CurrentTime"] = now();
        doc["Uptime"] = millis() / 1000;
        doc["BootTime"] = SystemSettings.get("boot_time", "0").toInt();
        String res = "";
        serializeJson(doc, res);
        result.response = res;
    }
    else if (parsedMsg.command == "HARDWAREINFO")
    {
        auto doc = DynamicJsonDocument(512);
        doc["ChipModel"] = ESP.getChipModel();
        doc["ChipCores"] = ESP.getChipCores();
        doc["ChipRevision"] = ESP.getChipRevision();
        doc["FlashSizeMB"] = ESP.getFlashChipSize() / (1024 * 1024);
        doc["HeapSize"] = ESP.getHeapSize();
        doc["PsramSize"] = ESP.getPsramSize();
        doc["MACAddress"] = WiFi.macAddress();
        String res = "";
        serializeJson(doc, res);
        result.response = res;
    }
#ifdef COMPILE_MQTT
    else if (parsedMsg.command == "MQTT")
    {
        result.result = true;
        if (parsedMsg.subcommand == "STATE")
        {
            int8_t state = MQTT_State();
            switch (state)
            {
            case -2:
                result.response = "MQTT Connecting";
                break;
            case -1:
                result.response = "MQTT Not Initialized";
                break;
            case 0:
                result.response = "MQTT Disconnected";
                break;
            case 1:
                result.response = "MQTT Connected Local";
                break;
            case 2:
                result.response = "MQTT Connected Remote";
                break;
            }
        }
        else if (parsedMsg.subcommand == "CONNECT")
        {
            String dest = parsedMsg.args[1];
            dest.toUpperCase();
            if (dest == "LOCAL" || dest == "1")
            {
                MQTT_change_to(true);
            }
            else if (dest == "REMOTE" || dest == "2")
            {
                MQTT_change_to(false);
            }
            else
            {
                // connect to current
                MQTT_change_to(MQTT_isLocal());
            }
        }
        else if (parsedMsg.subcommand == "DISCONNECT")
        {
            MQTT_End();
        }
        else if (parsedMsg.subcommand == "SWAP")
        {
            MQTT_change_to(!MQTT_isLocal());
        }
        else
        {
            result.response = "Unknown MQTT subcommand available: [CONNECT <Local|Remote>, STATE, DISCONNECT, SWAP].";
            result.result = false;
        }
        if (result.result)
        {
            result.response = MQTTStateJson();
        }
    }
#endif

#ifdef COMPILE_CONFIGS
    else if (parsedMsg.command == "CONFIG")
    {
        String name = parsedMsg.args[1];
        String value = parsedMsg.args[2];
        bool save = parsedMsg.args[3] == "1" || parsedMsg.args[3] == "-s" || parsedMsg.args[3] == "save";
        bool get_privileged = parsedMsg.args[2] == "-p";
        if (parsedMsg.subcommand == "GET")
        {
            if (name == "" || name == "all" || name == "ALL")
                result.response = Config.getAllSettings(get_privileged);
            else
            {
                if (Config.exists(name))
                {
                    result.response = "{\"" + name + "\":\"" + Config.get(name, "", get_privileged) + "\"}";
                }
                else
                {
                    result.response = "{\"error\":\"Configuration '" + name + "' does not exist.\"}";
                }
            }
        }
        else if (parsedMsg.subcommand == "SET" && name != "" && value != "")
        {
            bool set_privileged = parsedMsg.args[3] == "-p";
            Config.set(name, value, set_privileged);
            bool saved = Config.get(name, "", set_privileged) == value;
            result.response = "{\"" + name + "\":\"" + Config.get(name, "", set_privileged) + "\", \"saved\":" + String(saved ? "true" : "false") + "}";
        }
        else if (parsedMsg.subcommand == "SAVE")
        {
            if (Config.save())
                result.response = "Configurations saved successfully.";
            else
                result.response = "Failed to save configurations.";
        }
        else
        {
            result.response = "Unknown CONFIG subcommand available: [GET <name | all>, SET <name> <value>].";
        }
    }
    else if (parsedMsg.command == "SYSTEMCONFIGS")
    {
        String name = parsedMsg.args[1];
        String value = parsedMsg.args[2];
        if (parsedMsg.subcommand == "GET")
        {
            if (name == "" || name == "ALL")
                result.response = SystemSettings.getAllSettings();
            else
            {
                if (SystemSettings.exists(name))
                {
                    result.response = "{\"" + name + "\":\"" + SystemSettings.get(name) + "\"}";
                }
                else
                {
                    result.response = "{\"error\":\"Configuration '" + name + "' does not exist.\"}";
                }
            }
        }
        else if (parsedMsg.subcommand == "SET" && name != "" && value != "")
        {
            SystemSettings.set(name, value);
            result.response = "{\"" + name + "\":\"" + SystemSettings.get(name) + "\"}";
        }
        else
        {
            result.response = SystemSettings.getAllSettings();
        }
    }
#endif

#ifdef COMPILE_WIFI_MODULE
    else if (parsedMsg.command == "WIFI")
    {
        if (parsedMsg.subcommand == "IP")
        {
            result.response = WiFi.localIP().toString();
        }
        else if (parsedMsg.subcommand == "STATE")
        {
            wl_status_t status = WiFi.status();
            result.response += formatString("WiFi Status Code: %d - ", status);
            switch (status)
            {
            case WL_NO_SHIELD:
                result.response += "No Shield";
                break;
            case WL_IDLE_STATUS:
                result.response += "Idle";
                break;
            case WL_NO_SSID_AVAIL:
                result.response += "SSID Unavailable";
                break;
            case WL_SCAN_COMPLETED:
                result.response += "Scan Completed";
                break;
            case WL_CONNECTED:
                result.response += "Connected";
                break;
            case WL_CONNECT_FAILED:
                result.response += "Connect Failed";
                break;
            case WL_CONNECTION_LOST:
                result.response += "Connection Lost";
                break;
            case WL_DISCONNECTED:
                result.response += "Disconnected";
                break;
            default:
                result.response += "Unknown Status";
                break;
            }
        }
        else if (parsedMsg.subcommand == "RECONNECT")
        {
            result.response = "not implemented yet";
        }

        else if (parsedMsg.subcommand == "SCAN")
        {
            bool start = parsedMsg.args[1] == "-s" || parsedMsg.args[1] == "start";
            int16_t res = WiFi.scanComplete();
            auto doc = DynamicJsonDocument(2560);
            if (start || res == -2)
            {
                int16_t res = WiFi.scanNetworks(true);
                if (res == -1)
                {
                    doc["control"] = "scan_started";
                }
                else
                {
                    doc["control"] = "scan_start_failed";
                }
                // if we are on an async context we can wait for the scan to complete and send the results in one go, otherwise user must pool.
                if (context.async)
                {
                    result.context.msgSource = NM_CMD_ANS_DO_NOT_RESPOND; // Do not respond immediately, will respond after scan is complete
                    res = WiFi.scanComplete();
                    while (res == -1)
                    {
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        res = WiFi.scanComplete();
                    }
                    if (res == -2)
                    {
                        doc["control"] = "scan_failed";
                    }
                    else
                    {
                        // After scan is complete, get results and respond
                        doc["control"] = "scan_done";
                        JsonArray networks = doc.createNestedArray("networks");
                        for (int i = 0; i < res; i++)
                        {
                            JsonObject net = networks.createNestedObject();
                            net["ssid"] = WiFi.SSID(i);
                            net["rssi"] = WiFi.RSSI(i);
                            net["mac"] = WiFi.BSSIDstr(i);
                            net["channel"] = WiFi.channel(i);
                            net["encryptionType"] = WiFi_getAuthTypeName(WiFi.encryptionType(i));
                        }
                    }
                    Serial.printf("ScanResults: %d networks found\n", res);
                    String resStr = "";
                    // Serial.printf("doc size: %lu\n", doc.memoryUsage());
                    serializeJson(doc, resStr);
                    asyncSend(resStr, context);
                }
            }
            else
            {
                if (res == -1)
                {
                    doc["control"] = "scan_in_progress";
                }
                else
                {
                    doc["control"] = "scan_done";
                    JsonArray networks = doc.createNestedArray("networks");
                    for (int i = 0; i < res; i++)
                    {
                        JsonObject net = networks.createNestedObject();
                        net["ssid"] = WiFi.SSID(i);
                        net["rssi"] = WiFi.RSSI(i);
                        net["mac"] = WiFi.BSSIDstr(i);
                        net["channel"] = WiFi.channel(i);
                        net["encryptionType"] = WiFi_getAuthTypeName(WiFi.encryptionType(i));
                    }
                }
            }
            String resStr = "";
            // Serial.printf("doc size: %lu\n", doc.memoryUsage());
            serializeJson(doc, resStr);
            result.response = resStr;
        }

        else if (parsedMsg.subcommand == "CHANGE")
        {
            if (parsedMsg.args[1].length() == 0)
            {
                result.response = "No SSID provided to CHANGE.";
            }
            else
            {
                String ssid = parsedMsg.args[1];
                String password = parsedMsg.args[2];
                bool changeResult = WiFi_ChangeCredentials(ssid, password);
                result.response = formatString("WiFi credentials change %s.", changeResult ? "successful" : "failed");
#ifdef COMPILE_MQTT
                if (context.msgSource == NM_CMD_SRC_MQTT)
                {
                    context.msgSource = NM_CMD_ANS_DO_NOT_RESPOND; // Do not respond immediately, will respond after reconnecting to MQTT with the new credentials
                    MQTT_Queue_Async_Message(context.sourceIdentifier, result.response, false, false);
                };
#endif
            }
        }
        else
        {
            result.response = "Unknown WIFI subcommand available: [IP, STATE, SCAN <-s|-start>, CHANGE <ssid> <password>, RECONNECT].";
            result.result = false;
        }
    }
#endif

#ifdef COMPILE_HTTP_SERVER
    else if (parsedMsg.command == "HTTPSERVER")
    {
        if (parsedMsg.subcommand == "PRIORITY")
        {
            bool priority = parsedMsg.args[1] == "1" || parsedMsg.args[1] == "high";
            esp_err_t res = setHttpHighPriority(priority);
            if (res == ESP_OK)
            {
                result.response = "HTTP server priority set to " + String(priority ? "HIGH" : "NORMAL") + ".";
                result.result = true;
            }
            else
            {
                result.response = "Failed to set HTTP server priority to " + String(priority ? "HIGH" : "NORMAL") + ".";
                result.result = false;
            }
        }
        else if (parsedMsg.subcommand == "STATUS")
        {
            HTTP_Server_State state = getHttpState();
            switch (state)
            {
            case HTTP_STOPPED:
                result.response = "HTTP Server is STOPPED.";
                break;
            case HTTP_RUNNING_NORMAL_PRIORITY:
                result.response = "HTTP Server is RUNNING at NORMAL PRIORITY.";
                break;
            case HTTP_RUNNING_HIGH_PRIORITY:
                result.response = "HTTP Server is RUNNING at HIGH PRIORITY.";
                break;
            default:
                result.response = "HTTP Server state is UNKNOWN.";
                break;
            }
            result.result = true;
        }
        else if (parsedMsg.subcommand == "RESET")
        {
            http_stop();
            if (http_init())
            {
                result.response = "HTTP server reset to NORMAL priority and restarted.";
                result.result = true;
            }
            else
            {
                result.response = "Failed to restart HTTP server.";
                result.result = false;
            }
        }
        else if (parsedMsg.subcommand == "ENABLE")
        {
            bool value = parsedMsg.args[1] == "1" || parsedMsg.args[1] == "true" || parsedMsg.args[1] == "on";
            HTTP_Server_State state = getHttpState();
            if (value && state == HTTP_STOPPED)
            {
                if (http_init())
                {
                    result.response = "HTTP server enabled.";
                }
                else
                {
                    result.response = "Failed to enable HTTP server.";
                }
            }
            else if (!value && state != HTTP_STOPPED)
            {
                http_stop();
                result.response = "HTTP server disabled.";
            }
            else
            {
                result.response = "HTTP server already in the desired state.";
            }
        }
        else
        {
            result.response = "Unknown HTTPSERVER subcommand available: [PRIORITY <high|normal>, STATUS, RESET, ENABLE <1|0>].";
        }
    }
#endif

#ifdef SCHEDULER_AWARE
    /// Format SCHEDULE <command> <delta seconds> [interval]
    /// Schedules a command to be run after a specific delay (in seconds).
    else if (parsedMsg.command == "SCHEDULE")
    {
        unsigned long timestamp = now();
        timestamp += strtoul(parsedMsg.args[1].c_str(), NULL, 10);
        int id = scheduler.add(parsedMsg.args[0], timestamp);
        if (id != -1)
        {
            if (parsedMsg.args[2].toInt() > 0)
            {
                SchedulerTask *task = scheduler.getByID(id);
                if (task)
                {
                    task->repeat = true;
                    task->interval = parsedMsg.args[2].toInt();
                }
            }
            result.response = "Task scheduled with ID: " + String(id);
            result.result = true;
        }
        else
        {
            result.response = "Failed to schedule task.";
            result.result = false;
        }
    }
    // Format: SCHEDULER <subcommand> : LIST, CLEAR
    else if (parsedMsg.command == "SCHEDULER")
    {
        if (parsedMsg.subcommand == "LIST")
        {
            result.response = scheduler.listTasks();
        }
        else if (parsedMsg.subcommand == "CLEAR")
        {
            scheduler.clear();
            result.response = "All scheduled tasks cleared.";
        }
        else if (parsedMsg.subcommand == "KILL")
        {
            if (parsedMsg.args[1].length() == 0)
            {
                result.response = "No task ID provided to KILL.";
            }
            else
            {
                uint16_t id = parsedMsg.args[1].toInt();
                if (scheduler.killByID(id))
                {
                    result.response = "Task ID " + String(id) + " killed.";
                    result.result = true;
                }
                else
                {
                    result.response = "Task ID " + String(id) + " not found.";
                }
            }
        }
        else
        {
            result.response = "Unknown SCHEDULER subcommand.";
            result.result = false;
        }
    }
#endif

#ifdef COMPILE_TIMERS

    else if (parsedMsg.command == "TIMERS")
    {
        DynamicJsonDocument doc(512);
        JsonArray tasks = doc.createNestedArray("tasks");
        JsonArray timeouts = doc.createNestedArray("timeouts");
        for (size_t i = 0; i < TIMER_MAX_TASKS; i++)
        {
            if (Timers._tasks[i].label != "unused")
            {
                JsonObject task = Timers._tasks[i].is_timeout ? timeouts.createNestedObject() : tasks.createNestedObject();
                task["label"] = Timers._tasks[i].label;
                task["interval"] = Timers._tasks[i].interval;
                task["timeLeft"] = Timers.timeleft(Timers._tasks[i].label);
            }
        }
        String resStr = "";
        serializeJson(doc, resStr);
        result.response = resStr;
    }
#endif

#ifdef COMPILE_WEBSOCKET_SERVER
    else if (parsedMsg.command == "WS")
    {
        if (parsedMsg.subcommand == "LIST")
        {
            result.response += formatString("Total WS active clients: %d\n", ws_clients.count);
            for (size_t i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
            {
                if (ws_clients.wsList[i].active)
                {
                    result.response += formatString("Client %d: Socket %d\n", i, ws_clients.wsList[i].sockfd);
                }
            }
        }
        else
        {
            ws_broadcast(parsedMsg.subcommand.c_str());
            result.response = "Broadcasted: \'";
            result.response += parsedMsg.subcommand;
            result.response += "\' message to all WebSocket clients.";
        }
    }

#endif
    else
    {
        result.result = false;
        prehandled = false;
    }
    // If not handled, pass to resolver
    if (resolveCommand && !prehandled)
    {
        auto res = resolveCommand(parsedMsg);
        result.response = res.response;
        result.result = res.result;
    }
#else
    if (resolveCommand)
        result = resolveCommand(parsedMsg);
#endif

#if defined(COMPILE_SERIAL) && defined(DEBUG_CMD_RESOLVER)
    Serial.printf("\tmessage = <%s> | \n\tcommand = <%s> | \n\t -args[0] = <%s> | \n\t -args[1] = <%s> | \n\t -args[2] = <%s> |  \n\t -args[3] = <%s>  \n\t -args[4] = <%s> \n\t\n", message.c_str(), parsedMsg.command.c_str(), parsedMsg.args[0].c_str(), parsedMsg.args[1].c_str(), parsedMsg.args[2].c_str(), parsedMsg.args[3].c_str(), parsedMsg.args[4].c_str());

#endif

    if (result.response.length() == 0)
    {
        char buffer[256];
        if (result.result)
            snprintf(buffer, sizeof(buffer), "Command \'%s\' executed successfully.", parsedMsg.command.c_str());
        else
            snprintf(buffer, sizeof(buffer), "Command \'%s\' unrecognized.", parsedMsg.command.c_str());
        result.response = String(buffer);
    }
    result.context = context;
    return result;
}

#ifdef COMPILE_SERIAL_COMMAND_RESOLVER

/// @brief Listens to Serial input and resolves commands using the NightMare command resolver.
/// This function uses Serial.readStringUntil to read input until the specified character is encountered.
/// @param _Serial A HardwareSerial Object Pointer.
/// @param readUntilChar The character to read until (default is '\n')
/// @note: If readUntilChar is set to 0, it will read until no more data is available in the buffer, allowing for multi-line commands.
void NightMareCommand_SerialResolver(HardwareSerial *_Serial, char readUntilChar)
{
    if (_Serial == nullptr)
        return;
    if (_Serial->available())
    {
        String cmd = "";
        if (readUntilChar != 0)
        {
            cmd = _Serial->readStringUntil(readUntilChar);
        }
        else
        {
            while (_Serial->available())
            {
                cmd += (char)_Serial->read();
                delay(10); // Small delay to allow buffer to fill
            }
        }
        cmd.trim();
        _Serial->printf("<\x1b[90m%s\x1b[0m>%s\n", cmd.c_str(), "processing...");
        NightMareResults res = handleNightMareCommand(cmd, NightmareContext(NM_CMD_SRC_SERIAL, "Serial", _Serial, true));
        _Serial->printf("<\x1b[90m%s\x1b[0m>%s\n", cmd.c_str(), OK_LOG(res.result));
        if (res.context.msgSource != NM_CMD_ANS_DO_NOT_RESPOND)
        {
            _Serial->printf("%s\n", res.response.c_str());
        }
    }
}

#endif

#ifdef COMPILE_ASYNC_COMMANDS
#ifdef ASYNC_COMMANDS_SINGLE_TASK

QueueHandle_t asyncCommandQueue;
static bool asyncWorkerTaskRunning = false;

void xCommandWorkerTask(void *param)
{
    NightMareAsyncParam *taskParam;
    for (;;)
    {
        if (xQueueReceive(asyncCommandQueue, &taskParam, portMAX_DELAY) == pdPASS)
        {

            String command = taskParam->command;
            NightmareContext context = taskParam->context;
            Serial.print(ASYNC_TAG);
            Serial.print(" Worker task received id: ");
            Serial.println(context.sourceIdentifier);
            delete taskParam;
            Serial.print(ASYNC_TAG);
            Serial.print(" [");
            Serial.print(context.sourceIdentifier);
            Serial.println("] starting execution.");
            unsigned long startTime = millis();
            NightMareResults res = handleNightMareCommand(command, context);
            Serial.print(ASYNC_TAG);
            Serial.print(" [");
            Serial.print(context.sourceIdentifier);
            Serial.print("] executed in ");
            Serial.print(millis() - startTime);
            Serial.println("ms.");
            Serial.printf("src: %02x\n", res.context.msgSource);
            // If the response is meant to be sent via MQTT
            if (res.context.msgSource == NM_CMD_SRC_MQTT && res.context.sourceIdentifier.length() > 0)
            {
                MQTT_Send(context.sourceIdentifier, res.response, false, false);
                Serial.print(ASYNC_TAG);
                Serial.print(" [");
                Serial.print(context.sourceIdentifier);
                Serial.println("] MQTT response sent.");
            }
            // if the command handler indicated that it will handle the MQTT response itself we end the async response here.
            if (context.msgSource == NM_CMD_SRC_MQTT && res.context.sourceIdentifier.length() > 0)
            {
                MQTT_Send(context.sourceIdentifier, ASYNC_COMMAND_END_TAG, false, false);
                Serial.print(ASYNC_TAG);
                Serial.print(" [");
                Serial.print(context.sourceIdentifier);
                Serial.println("] MQTT finished sent.");
            }
        }
        // Using the task blocker instead
        // vTaskDelay(ASYNC_COMMANDS_SINGLE_TASK_DELAY_MS / portTICK_PERIOD_MS);
    }
}

void startAsyncCommandWorker()
{
    asyncCommandQueue = xQueueCreate(ASYNC_COMMANDS_QUEUE_SIZE, sizeof(NightMareAsyncParam *));
    if (asyncCommandQueue == NULL)
    {
        Serial.printf("%s Failed to create async command queue.\n", ERR_TAG);
        return;
    }
    BaseType_t res = xTaskCreate(
        xCommandWorkerTask,
        "AsyncCmdWorker",
        ASYNC_COMMANDS_TASK_STACK,
        nullptr,
        ASYNC_COMMANDS_TASK_PRIORITY,
        nullptr);
    if (res != pdPASS)
    {
        Serial.printf("%s Error creating async handler task.\n", ERR_TAG);
        return;
    }
    Serial.printf("%s Async handler Task Created.\n", OK_TAG);
    asyncWorkerTaskRunning = true;
    return;
}
#else
void xCommandWorkerTask(void *param)
{
    NightMareAsyncParam *taskParam = (NightMareAsyncParam *)param;
    String command = taskParam->command;
    NightmareContext context = taskParam->context;
    delete taskParam;

    NightMareResults res = handleNightMareCommand(command, context);
    if (context.msgSource == NM_CMD_SRC_MQTT && context.sourceIdentifier.length() > 0)
    {
        MQTT_Queue_Async_Message(context.sourceIdentifier, res.response, false, false);
        MQTT_Queue_Async_Message(context.sourceIdentifier, ASYNC_COMMAND_END_TAG, false, false);
    }

    vTaskDelete(NULL);
}
#endif

uint8_t dispatchAsyncCommand(String command, NightmareContext context)
{
    NightMareAsyncParam *param = new NightMareAsyncParam();

    if (!param)
        return ASYNC_CMD_FAILED_TO_MALLOC_PARAMS;

    param->command = command;
    param->context = context;
    param->context.async = true; // Mark the context as async so handlers can know to respond with async message format if needed.
#ifdef ASYNC_COMMANDS_SINGLE_TASK
    // if task has not been init, init it.
    if (!asyncWorkerTaskRunning)
        startAsyncCommandWorker();
    // if task is still not init i.e. init failled
    if (!asyncWorkerTaskRunning)
    {
        delete param;
        return ASYNC_CMD_SINGLE_TASK_NOT_INIT;
    }

    if (xQueueSend(asyncCommandQueue, &param, 0) != pdPASS)
    {
        delete param;
        Serial.printf("%s Async command queue is full. Failed to dispatch command.\n", ERR_TAG);
        return ASYNC_CMD_QUEUE_FULL;
    }
    Serial.printf("%s Dispatched async command to worker task: %s\n", ASYNC_TAG, command.c_str());
#else
    BaseType_t res = xTaskCreate(
        xCommandWorkerTask,
        "AsyncCmdWorker",
        ASYNC_COMMANDS_TASK_STACK,
        param,
        ASYNC_COMMANDS_TASK_PRIORITY,
        nullptr);
    if (res != pdPASS)
    {
        delete param;
        Serial.printf("%s Error creating dispatch task.\n", ERR_TAG);
        return ASYNC_CMD_TASK_CREATION_FAILED;
    }
#endif
    return ASYNC_CMD_SUCCESS;
}
#endif