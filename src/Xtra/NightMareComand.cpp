#include "NightMareCommand.h"

#ifdef COMPILE_SERIAL
#define COMMAND_RESOLVER_LOGF(fmt, ...) Serial.printf("%s " fmt "\n", COMMAND_RESOLVER_TAG, ##__VA_ARGS__)
#define COMMAND_RESOLVER_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_TAG, COMMAND_RESOLVER_TAG, ##__VA_ARGS__)
#else
#define COMMAND_RESOLVER_LOGF(fmt, ...)
#define COMMAND_RESOLVER_ERRORF(fmt, ...)
#endif

/// @brief  Global function pointer to the command resolver function. This function should be set by the user of the library to handle incoming commands.
NightMareResults (*resolveCommand)(const NightMareMessage &message) = nullptr;

void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message))
{
    resolveCommand = resolver;
}

/// @brief Parses a command string into a NightMareMessage struct.
/// @param message The command string to parse.
/// @return A NightMareMessage struct containing the parsed command and its arguments.
NightMareMessage parseNightMareMessage(const String &message)
{
    /* NightMare Message Parser:

    input: "COMMAND SUBCOMMAND ARG0 ARG1 ARG2 ARG3 ARG4"
    output: NightMareMessage {
        command: "COMMAND",
        subcommand: "SUBCOMMAND", //Equals to args[0]
        args: ["ARG0", "ARG1", "ARG2", "ARG3", "ARG4"]
    }
        notes:
        1- command and subcommand are converted to uppercase, args are kept as is.
        2- when we meet a " in the beginning of a word, we will consider everything until the next " as a single argument, and we will remove the " from the argument.
        3- if we meet a " in the middle of a word, it is considered as part of the argument, and we will not remove it.

    */
    NightMareMessage parsedMsg;
    String current_string = "";
    int index = 0;
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
            if (in_quotes)
                quote = "";
            else
            {
                if (quote.length() == 0)
                    quote = "\"";
                if (index == 0)
                    parsedMsg.command += quote;
                else if (index < 5)
                    parsedMsg.args[index - 1] += quote;
                quote = "";
            }
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
    return parsedMsg;
}

/// @brief Checks a string against a length limit, saying by how much it overran.
/// Takes a const reference so it can be pointed at an incoming payload directly, which is the
/// point: MQTT, HTTP, WS and TCP hand the parser an unbounded buffer they never sized themselves.
/// @param str The string to check.
/// @param maxLength The limit to enforce.
/// @param error Set to the reason when this returns false; left alone on success.
/// @return True when `str` is within the limit, false when it overran.
bool ensureSize(const String &str, size_t maxLength, String &error)
{
    if (str.length() <= maxLength)
        return true;
    error = "input too long: " + String((uint32_t)str.length()) + " chars, limit is " + String((uint32_t)maxLength);
    return false;
}

/// @brief Every ASCII blank separates tokens, not just ' '. MQTT, HTTP, WS and TCP pass their
/// payload straight through untrimmed, so a trailing \r or \n is routine on those transports.
static inline bool isTokenSeparator(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/// @brief Files one finished token into the message. Slot 0 is the command, the rest are args.
/// @return False when there are more tokens than slots, having marked `msg` invalid.
static bool storeToken(NightMareMessage &msg, uint8_t slot, const String &token)
{
    if (slot == 0)
    {
        msg.command = token;
        return true;
    }
    if (slot > NM_MAX_ARGS)
    {
        msg.valid = false;
        msg.error = "too many arguments, limit is " + String(NM_MAX_ARGS);
        return false;
    }
    msg.args[slot - 1] = token;
    return true;
}

/// Where the scan currently is. Keeping this explicit is what lets the quoted and unquoted paths
/// share a single flush point, so they cannot drift apart the way the original parser's two
/// bounds checks did.
enum NightMareParseState
{
    NM_PARSE_SEP,   // between tokens
    NM_PARSE_WORD,  // inside an unquoted token
    NM_PARSE_QUOTED // inside a "..." section
};

/// @brief Parses a raw command line into command / subcommand / args.
///
/// Grammar: `COMMAND ARG0 ARG1 ...`, where ARG0 doubles as the subcommand. Command and subcommand
/// are uppercased; args keep their case.
///   - Any run of whitespace separates tokens, so extra spaces never shift an argument's position.
///   - A `"` toggles quoting and is removed. Adjacent sections join, so `a"b c"d` is one token
///     `ab cd`, and `""` is an argument that is present but empty.
///   - A `\` escapes only the two delimiters: \" is a literal " and \` a literal `. Before anything
///     else the backslash is kept exactly as typed, so \b stays \b and paths survive a round trip.
///     A backslash is therefore never doubled, and never removable either -- for content that must
///     contain a literal \" use a fence, where nothing is interpreted at all.
///   - A backtick fence takes everything up to its closing fence verbatim -- no escapes, no quote
///     handling. An opening run of N backticks closes on a run of exactly N, so content containing
///     a backtick just opens wider. This is what makes a nested command readable:
///       TASK t `WS "hi"` 20        instead of        TASK t "WS \"hi\"" 20
///     An empty literal is not expressible; use "" for that.
///
/// Malformed input is reported rather than guessed at: check `valid` before using the result.
/// @param message The command string to parse.
/// @return The parsed message, or one with `valid` false and `error` set.
NightMareMessage parseNightMareMessage2(const String &message)
{
    NightMareMessage msg;

    if (!ensureSize(message, NM_MAX_MESSAGE_LEN, msg.error))
    {
        msg.valid = false;
        return msg;
    }

    // One allocation, reused for every token, so the scan itself never reallocates. Checked because
    // token += c discards its own failure: on a dead heap it would truncate silently and hand back
    // a confidently wrong parse, which is the one thing this parser exists to prevent.
    String token;
    if (!token.reserve(message.length()))
    {
        msg.valid = false;
        msg.error = "out of memory reserving " + String((uint32_t)message.length()) + " chars";
        return msg;
    }

    NightMareParseState state = NM_PARSE_SEP;
    uint8_t slot = 0; // 0 is the command word; slot N lands in args[N - 1]

    for (size_t i = 0; i < message.length(); i++)
    {
        char c = message.charAt(i);

        // A backslash escapes only the two delimiters. Before anything else it is kept as typed and
        // the next character is left to the normal rules, so /a\b.json and C:\tmp round-trip
        // unchanged and a backslash never has to be doubled.
        if (c == '\\')
        {
            char next = (i + 1 < message.length()) ? message.charAt(i + 1) : '\0';
            if (next == '"' || next == '`')
                token += message.charAt(++i);
            else
                token += '\\';
            if (state == NM_PARSE_SEP)
                state = NM_PARSE_WORD; // a token that opens with a backslash has still started
            continue;
        }
        // A backtick fence takes its contents verbatim: no escapes, no quote handling, whitespace
        // exactly as typed. An opening run of N backticks is closed by a run of exactly N, so a
        // literal that must itself contain a backtick just opens with more of them -- the common
        // case still costs one byte a side. Inside "..." a backtick is only a character.
        if (c == '`' && state != NM_PARSE_QUOTED)
        {
            size_t fence = 1;
            while (i + fence < message.length() && message.charAt(i + fence) == '`')
                fence++;
            size_t j = i + fence; // first content character
            bool closed = false;
            while (j < message.length())
            {
                if (message.charAt(j) != '`')
                {
                    token += message.charAt(j++);
                    continue;
                }
                size_t run = 1;
                while (j + run < message.length() && message.charAt(j + run) == '`')
                    run++;
                if (run == fence)
                {
                    i = j + fence - 1; // the outer i++ lands just past the closing fence
                    closed = true;
                    break;
                }
                for (size_t k = 0; k < run; k++) // a run of the wrong length is content
                    token += '`';
                j += run;
            }
            if (!closed)
            {
                msg.valid = false;
                msg.error = "unterminated ` literal";
                return msg;
            }
            if (state == NM_PARSE_SEP)
                state = NM_PARSE_WORD;
            continue;
        }
        if (c == '"')
        {
            // Entering a quote from SEP starts the token, which is what makes "" come back as an
            // empty argument rather than being lost or turning into a literal quote.
            state = (state == NM_PARSE_QUOTED) ? NM_PARSE_WORD : NM_PARSE_QUOTED;
            continue;
        }
        if (state != NM_PARSE_QUOTED && isTokenSeparator(c))
        {
            // Only a WORD -> SEP transition advances the slot, so a run of blanks counts once.
            if (state == NM_PARSE_WORD)
            {
                if (!storeToken(msg, slot++, token))
                    return msg;
                token = ""; // keeps the reserved buffer, so this costs no allocation
                state = NM_PARSE_SEP;
            }
            continue;
        }
        token += c;
        if (state == NM_PARSE_SEP)
            state = NM_PARSE_WORD;
    }

    if (state == NM_PARSE_QUOTED)
    {
        msg.valid = false;
        msg.error = "unterminated quote";
        return msg;
    }
    if (state == NM_PARSE_WORD && !storeToken(msg, slot++, token))
        return msg;
    if (slot == 0)
    {
        // Nothing but separators. Reachable from any transport that does not trim its payload,
        // where a bare "\r\n" would otherwise parse "successfully" into an empty command.
        msg.valid = false;
        msg.error = "empty command";
        return msg;
    }

    msg.argc = slot - 1; // slot counts the command word, argc does not
    msg.command.toUpperCase();
    msg.subcommand = msg.args[0];
    msg.subcommand.toUpperCase();
    return msg;
}

#ifdef ENABLE_PREPROCESSING
/// Maximum directory depth walked by FS LIST; bounds the recursion on a fixed-size stack.
#define FS_LIST_MAX_DEPTH 4
/// JSON capacity for the FS LIST response. Overflow is reported instead of silently truncated.
#define FS_LIST_JSON_CAPACITY 4096

/// @brief Recursively appends every entry under a directory to `files` as {name, size} objects.
/// Names are full paths from the FS root, so the flat array still describes the whole tree;
/// directories are listed with a trailing slash and a size of 0.
/// @param files The array to append the entries to.
/// @param path Directory to walk, with a leading slash and no trailing slash ("" for the root).
/// @param depth Remaining levels to descend; recursion stops at 0.
static void listFileTree(JsonArray &files, const String &path, uint8_t depth)
{
    File dir = LittleFS.open(path.length() ? path.c_str() : "/");
    if (!dir || !dir.isDirectory())
        return;
    File entry = dir.openNextFile();
    while (entry)
    {
        // File::name() is the bare entry name on newer cores and the full path on older ones,
        // so build the path from the parent rather than trusting either.
        String name = entry.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0)
            name = name.substring(slash + 1);
        String fullPath = path + "/" + name;
        bool isDir = entry.isDirectory();
        JsonObject entryObj = files.createNestedObject();
        entryObj["name"] = isDir ? fullPath + "/" : fullPath;
        entryObj["size"] = isDir ? 0 : entry.size();
        entry.close();
        if (isDir && depth > 0)
            listFileTree(files, fullPath, depth - 1);
        entry = dir.openNextFile();
    }
    dir.close();
}
#endif

/// @brief Core synchronous command executor: parses the message, runs it through the built-in
/// preprocessor, and falls back to the registered resolver. Always runs on the calling task.
/// Most callers want handleNightMareCommand() instead; this is exposed for the async worker to
/// invoke without re-triggering async dispatch.
/// @param message The input command message as a string.
/// @param context The context of the command, including its source and identifier.
/// @return A NightMareResults struct containing the result of the command execution, the response
NightMareResults executeNightMareCommand(const String &message, NightmareContext context)
{
    NightMareResults result;
    result.response = "No command resolved";
    result.result = true;
    result.context = context;
    if (message.length() == 0)
    {
        result.result = false;
        result.response = "Empty command";
        return result;
    }
    // ensureSize returns true when the message fits, so the rejection is the negated case.
    if (!ensureSize(message, NM_MAX_MESSAGE_LEN, result.response))
    {
        result.result = false;
        return result;
    }

    NightMareMessage parsedMsg = parseNightMareMessage2(message);
    if (!parsedMsg.valid)
    {
        // Without this the whole point of the new parser is lost: a malformed line would fall
        // through every branch and come back as "unrecognized" instead of saying what was wrong.
        result.result = false;
        result.response = "Parse error: " + parsedMsg.error;
        return result;
    }
    COMMAND_RESOLVER_LOGF("Received: '%s'", message.c_str());
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
    else if (parsedMsg.command == "TEST")
    {
        NightMareMessage testMsg = parseNightMareMessage(parsedMsg.args[0]);
        String res = "Parsed Test Message: \n";
        res += "Command: " + testMsg.command + "\n";
        res += "Subcommand: " + testMsg.subcommand + "\n";
        res += "Args: [";
        for (int i = 0; i < 5; i++)
        {
            res += testMsg.args[i];
            if (i < 4)
                res += ", ";
        }
        res += "]";
        result.response = res;
        result.result = true;
    }
    // else if (parsedMsg.command == "TIME")
    // {
    //     istime
    // }
    else if (parsedMsg.command == "SYSTEMINFO")
    {
        result.response = getSystemStatus();
    }
    else if (parsedMsg.command == "FS")
    {
        if (parsedMsg.subcommand == "LIST")
        {
            auto doc = DynamicJsonDocument(FS_LIST_JSON_CAPACITY);
            JsonArray files = doc.createNestedArray("files");
            listFileTree(files, "", FS_LIST_MAX_DEPTH);
            if (doc.overflowed())
            {
                result.response = "Too many files to list: the tree exceeds " + String(FS_LIST_JSON_CAPACITY) + " bytes of JSON.";
                result.result = false;
            }
            else
            {
                String resStr = "";
                serializeJson(doc, resStr);
                result.response = resStr;
                result.result = true;
            }
        }
        else if (parsedMsg.subcommand == "READ")
        {
            String filename = parsedMsg.args[1];
            if (filename.length() == 0)
            {
                result.response = "No filename provided to READ.";
                result.result = false;
            }
            else
            {
                if (LittleFS.exists(filename))
                {
                    File file = LittleFS.open(filename, "r");
                    if (file)
                    {
                        result.response = file.readString();
                        file.close();
                        result.result = true;
                    }
                    else
                    {
                        result.response = "Failed to open file '" + filename + "' for reading.";
                        result.result = false;
                    }
                }
                else
                {
                    result.response = "File '" + filename + "' does not exist.";
                    result.result = false;
                }
            }
        }
        else if (parsedMsg.subcommand == "STATUS")
        {
            auto doc = DynamicJsonDocument(512);
            doc["totalBytes"] = LittleFS.totalBytes();
            doc["usedBytes"] = LittleFS.usedBytes();
            doc["initialized"] = SystemSettings.getFlag("LittleFS_mounted");
            String resStr = "";
            serializeJson(doc, resStr);
            result.response = resStr;
            result.result = true;
        }
        else if (parsedMsg.subcommand == "DELETE")
        {
            String filename = parsedMsg.args[1];
            if (filename.length() == 0)
            {
                result.response = "No filename provided to DELETE.";
                result.result = false;
            }
            else
            {
                if (LittleFS.exists(filename))
                {
                    if (LittleFS.remove(filename))
                    {
                        result.response = "File '" + filename + "' deleted successfully.";
                        result.result = true;
                    }
                    else
                    {
                        result.response = "Failed to delete file '" + filename + "'.";
                        result.result = false;
                    }
                }
                else
                {
                    result.response = "File '" + filename + "' does not exist.";
                    result.result = false;
                }
            }
        }
        else if (parsedMsg.subcommand == "FORMAT")
        {
            if (parsedMsg.args[1] != "-p")
            {
                result.response = "Filesystem format denied.";
                result.result = false;
            }
            else if (!SystemSettings.getFlag("LittleFS_mounted"))
            {
                result.response = "Filesystem not mounted.";
                result.result = false;
            }
            else if (LittleFS.format())
            {
                result.response = "Filesystem formatted successfully.";
                result.result = true;
            }
            else
            {
                result.response = "To format the filesystem, use: FS FORMAT CONFIRM";
                result.result = false;
            }
        }
        else
        {
            result.response = "Unknown FS subcommand available: [LIST, DELETE <filename>, READ <filename>, FORMAT].";
            result.result = false;
        }
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
                    COMMAND_RESOLVER_LOGF("ScanResults: %d networks found", res);
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
    /// Schedules a command to be run after a specific delay (in seconds).
    // Format: SCHEDULER <subcommand> : LIST, CLEAR
    else if (parsedMsg.command == "SCHEDULER")
    {
        if (parsedMsg.subcommand == "LIST")
        {
            bool onlyTasks = parsedMsg.args[1] == "1" || parsedMsg.args[1] == "-p" || parsedMsg.args[1] == "-t";
            result.response = scheduler.listTasks(onlyTasks);
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
        else if (parsedMsg.subcommand == "ADD")
        {
        }
        else if (parsedMsg.subcommand == "EDIT")
        {
        }
        else
        {
            result.response = "Unknown SCHEDULER subcommand.";
            result.result = false;
        }
    }

    else if (parsedMsg.command == "TASK")
    {
        uint32_t executionTime = strtoul(parsedMsg.args[3].c_str(), nullptr, 10);
        uint32_t interval = strtoul(parsedMsg.args[2].c_str(), nullptr, 10);
        int id = scheduler.addTask(parsedMsg.args[0], parsedMsg.args[1], interval, executionTime, true);
        if (id != -1)
        {
            result.response = "Task scheduled with ID: " + String(id);
            result.result = true;
        }
        else
        {
            result.response = "Failed to schedule task.";
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

/// @brief Single entrypoint for running a NightMare command. Runs synchronously unless
/// `context.async` is set, in which case the command is queued for the async worker (started
/// automatically on first use) and this returns immediately; the worker delivers the real response
/// later via the context's source. If the async dispatch itself fails (queue full, worker couldn't
/// start, ...), a failure result is returned rather than silently falling back to a blocking call.
/// @param message The raw command string. Callers with only a message string can omit context,
/// e.g. `handleNightMareCommand("PING")`.
/// @param context Execution context; defaults to an anonymous synchronous context.
/// @return The command result, or the dispatch outcome when queued asynchronously.
NightMareResults handleNightMareCommand(const String &message, NightmareContext context)
{
#ifdef COMPILE_ASYNC_COMMANDS
    if (context.async)
    {
        NightMareResults result;
        result.context = context;
        uint8_t dispatchStatus = dispatchAsyncCommand(message, context);
        result.result = (dispatchStatus == ASYNC_CMD_SUCCESS);
        if (result.result)
        {
            result.response = "Command dispatched for asynchronous execution.";
            result.context.msgSource = NM_CMD_ANS_DO_NOT_RESPOND; // the worker delivers the real response
        }
        else
        {
            result.response = asyncCommandErrorMessage(dispatchStatus);
        }
        return result;
    }
#endif
    return executeNightMareCommand(message, context);
}

#ifdef COMPILE_SERIAL_COMMAND_RESOLVER

/// @brief Listens to Serial input and resolves commands using the NightMare command resolver.
/// This function uses Serial.readStringUntil to read input until the specified character is encountered.
/// @param _Serial A HardwareSerial Object Pointer.
/// @param readUntilChar The character to read until (default is '\n')
/// @note: If readUntilChar is set to 0, it will read until no more data is available in the buffer, allowing for multi-line commands.
void NightMareCommand_SerialResolver(SERIALTYPE *_Serial, char readUntilChar)
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
