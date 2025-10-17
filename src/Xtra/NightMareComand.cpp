#include "NightMareCommand.h"
NightMareResults (*resolveCommand)(const NightMareMessage &message) = nullptr;

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

NightMareResults handleNightMareCommand(const String &message)
{
    const char delimiter = ' ';
    NightMareResults result;
    result.response = "No command resolved";
    result.result = true;
    NightMareMessage parsedMsg;
    String current_string = "";
#ifdef COMPILE_SERIAL
    Serial.printf("\x1b[96;1m[Command]\x1b[0m Received: '%s'\n", message.c_str());
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
            else if (index < 5)
                parsedMsg.args[index - 1] += c;
        }
    }
    parsedMsg.command.toUpperCase();
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
        doc["ResetReason"] = getBootReason(esp_reset_reason());
        doc["IsTimeSynced"] = now() > 1600000000;
        doc["CurrentTime"] = now();
        doc["Uptime"] = millis() / 1000;
        doc["BootTime"] = now() - (millis() / 1000);
        String res = "";
        serializeJson(doc, res);
        result.response = res;
    }
#ifdef COMPILE_MQTT
    else if (parsedMsg.command == "MQTT")
    {
        String subcmd = parsedMsg.args[0];
        subcmd.toUpperCase();
        if (subcmd == "STATE")
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
        else if (subcmd == "CONNECT")
        {
            String dest = parsedMsg.args[1];
            dest.toUpperCase();
            if (dest == "LOCAL" || dest == "1")
            {
                MQTT_change_to(true);
                result.response = "MQTT Connecting To Local...";
            }
            else if (dest == "REMOTE" || dest == "2")
            {
                MQTT_change_to(false);
                result.response = "MQTT Connecting To Remote...";
            }
            result.result = true;
        }
        else
        {
            result.response = "Unknown MQTT subcommand available: [CONNECT <Local|Remote>, STATUS].";
            result.result = false;
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
        String subcmd = parsedMsg.args[0];
        subcmd.toUpperCase();
        if (subcmd == "LIST")
        {
            result.response = scheduler.listTasks();
        }
        else if (subcmd == "CLEAR")
        {
            scheduler.clear();
            result.response = "All scheduled tasks cleared.";
        }
        else if (subcmd == "KILL")
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
    else
    {
        result.result = false;
        prehandled = false;
    }
    // If not handled, pass to resolver
    if (resolveCommand && !prehandled)
        result = resolveCommand(parsedMsg);

#else
    if (resolveCommand)
        result = resolveCommand(parsedMsg);
#endif

#if defined(COMPILE_SERIAL) && defined(DEBUG)
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
    return result;
}
