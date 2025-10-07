#include "NightMareCommand.h"
NightMareResults (*resolveCommand)(const NightMareMessage &message) = nullptr;

void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message))
{
    resolveCommand = resolver;
}
NightMareResults handleNightMareCommand(const String &message)
{
    const char delimiter = ' ';
    NightMareResults result;
    result.response = "No command resolved";
    result.result = true;
    NightMareMessage parsedMsg;
    String current_string = "";

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
#ifdef SCHEDULER_AWARE
    /// Format SCHEDULE <command> <timestamp> [interval]
    /// Schedules a command to be run at a specific timestamp (in seconds since epoch).
    else if (parsedMsg.command == "SCHEDULE")
    {
        int id = scheduler.add(parsedMsg.args[0], now() + parsedMsg.args[1].toInt());
        if (id != -1)
        {
            if (parsedMsg.args[2].toInt() > 0) {
                SchedulerTask* task = scheduler.getByID(id);
                if (task) {
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
            if (parsedMsg.args[1].length() == 0) {
                result.response = "No task ID provided to KILL.";
            } else {
                uint16_t id = parsedMsg.args[1].toInt();
                if (scheduler.killByID(id)) {
                    result.response = "Task ID " + String(id) + " killed.";
                    result.result = true;
                } else {
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

 Serial.printf("\tmessage = <%s> | \n\tcommand = <%s> | \n\t -args[0] = <%s> | \n\t -args[1] = <%s> | \n\t -args[2] = <%s> |  \n\t -args[3] = <%s>  \n\t -args[4] = <%s> \n\t\n", message.c_str(), parsedMsg.command.c_str(), parsedMsg.args[0].c_str(), parsedMsg.args[1].c_str(), parsedMsg.args[2].c_str(), parsedMsg.args[3].c_str(), parsedMsg.args[4].c_str());
#if defined(COMPILE_SERIAL) && defined(DEBUG)
#endif
    return result;
}
