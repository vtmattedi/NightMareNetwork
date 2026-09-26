#include <NightMare/Features.h>
#if NM_ENABLE_CONSOLE
#include "NightMareCommand.h"
#include "ConfigManager.h"
#include "DeviceIdentity.h"
#include "Time.h"
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#endif
#if NM_ENABLE_SCHEDULER
#include "Scheduler.h"
#endif
#if NM_ENABLE_TELEMETRY
#include "Telemetry.h"
#endif
#if NM_CONSOLE_BUILTINS
#include <LittleFS.h>
#endif

#if NM_ENABLE_WIFI
// "AUTO" or a dBm value such as "8.5" -> the driver's quarter-dBm level. Exact
// only: 9 is rejected rather than rounded to 8.5.
static bool parseTxPowerArg(String arg, int &quarterDbm)
{
    arg.trim();
    arg.toUpperCase();
    if (arg == "AUTO")
    {
        quarterDbm = NightMare::NM_TX_POWER_AUTO;
        return true;
    }
    if (arg.length() == 0)
        return false;
    const float dbm = arg.toFloat();
    const int quarter = static_cast<int>(dbm * 4.0f + (dbm < 0 ? -0.5f : 0.5f));
    if (quarter == NightMare::NM_TX_POWER_AUTO || fabsf(dbm * 4.0f - quarter) > 0.01f ||
        !WiFi_isValidTxPower(quarter))
        return false;
    quarterDbm = quarter;
    return true;
}
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
///       JOB AFTER t 20 `WS "hi"`        instead of        JOB AFTER t 20 "WS \"hi\""
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

#if NM_CONSOLE_BUILTINS
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
        JsonObject entryObj = files.add<JsonObject>();
        entryObj["name"] = isDir ? fullPath + "/" : fullPath;
        entryObj["size"] = isDir ? 0 : entry.size();
        entry.close();
        if (isDir && depth > 0)
            listFileTree(files, fullPath, depth - 1);
        entry = dir.openNextFile();
    }
    dir.close();
}

static NightMareResults executeAdoptCommand(const String &newName, NightmareContext context)
{
    NightMareResults result;
    result.result = false;
    result.context = context;
    if (!DeviceIdentity::validDeviceName(newName))
    {
        result.response = "Invalid device name.";
        return result;
    }
    if (!gDeviceIdentity.beginAdoption(newName))
    {
        result.response = gDeviceIdentity.hasPendingIdentityCleanup()
                              ? "Cannot adopt while identity cleanup is pending."
                              : "Could not persist the new device name.";
        return result;
    }

    JsonDocument doc;
    doc["name"] = newName;
    const bool rebootRequired = gDeviceIdentity.getDeviceName() != newName;
    doc["active_name"] = gDeviceIdentity.getDeviceName();
    doc["reboot_required"] = rebootRequired;
    serializeJson(doc, result.response);
    result.result = true;
    return result;
}

static void refreshIdentityDocuments()
{
#if NM_ENABLE_MQTT
    if (MQTT_Connected())
    {
        MQTT_Publish("status", deviceStatusJson(true), true, true);
#if NM_ENABLE_TELEMETRY
        Telemetry.publishInfo(InfoType::INFO);
#endif
    }
#endif
}

static NightMareResults executeTimezoneCommand(const String &timezone,
                                               NightmareContext context)
{
    NightMareResults result;
    result.result = false;
    result.context = context;
    if (!DeviceIdentity::validTimezone(timezone))
    {
        result.response = "Invalid timezone: use a non-empty POSIX TZ string up to 128 characters.";
        return result;
    }
    if (!gDeviceIdentity.setTimezone(timezone))
    {
        result.response = "Could not persist or apply timezone.";
        return result;
    }
    refreshIdentityDocuments();
    JsonDocument doc;
    doc["timezone"] = gDeviceIdentity.getTimezone();
    serializeJson(doc, result.response);
    result.result = true;
    return result;
}
#endif

#if NM_ENABLE_JOBS
/// Parse a decimal job time or ID without accepting signs or trailing text.
static bool parseJobNumber(const String &text, uint32_t &value)
{
    if (text.length() == 0)
        return false;
    uint64_t parsed = 0;
    for (size_t i = 0; i < text.length(); ++i)
    {
        char digit = text[i];
        if (digit < '0' || digit > '9')
            return false;
        parsed = parsed * 10 + static_cast<uint8_t>(digit - '0');
        if (parsed > UINT32_MAX)
            return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

// Every JOB command works on USER jobs only: what arrives as text can neither
// see nor remove the jobs the framework and application schedule in C++.
static NightMareResults executeJobCommand(const NightMareMessage &message, NightmareContext context)
{
    constexpr SchedulerJobScope Scope = SchedulerJobScope::USER;
    NightMareResults result;
    result.result = false;
    result.context = context;

    if (message.subcommand == "LIST")
    {
        result.response = gScheduler.list(Scope);
        result.result = result.response.length() != 0;
        if (!result.result)
            result.response = "Could not list jobs.";
    }
    else if (message.subcommand == "CLEAR")
    {
        result.result = gScheduler.clear(Scope);
        result.response = result.result ? "All user jobs cleared." : "Could not clear persisted jobs.";
    }
    else if (message.subcommand == "DELETE")
    {
        const String &target = message.args[1];
        if (target.length() == 0)
            result.response = "Usage: JOB DELETE <label|#id>";
        else if (target[0] == '#')
        {
            uint32_t id = 0;
            if (!parseJobNumber(target.substring(1), id))
                result.response = "Invalid job ID.";
            else
            {
                result.result = gScheduler.remove(id, Scope);
                result.response = result.result ? "Job deleted." : "Job not found.";
            }
        }
        else
        {
            result.result = gScheduler.remove(target, Scope);
            result.response = result.result ? "Job deleted." : "Job not found.";
        }
    }
    else if (message.subcommand == "AT" || message.subcommand == "AFTER" ||
             message.subcommand == "EVERY")
    {
        const String &label = message.args[1];
        uint32_t when = 0;
        String command;
        String clock;
        if (message.subcommand == "EVERY")
        {
            clock = message.args[2];
            clock.toUpperCase();
            command = message.args[4];
            if (clock != "WALL" && clock != "MONO")
            {
                result.response = "Clock must be WALL or MONO.";
                return result;
            }
            if (!parseJobNumber(message.args[3], when) || when == 0)
            {
                result.response = "Invalid job interval.";
                return result;
            }
        }
        else
        {
            command = message.args[3];
            if (!parseJobNumber(message.args[2], when))
            {
                result.response = "Invalid job time or delay.";
                return result;
            }
        }
        if (label.length() == 0 || command.length() == 0)
        {
            result.response = "A job needs a label and a command.";
            return result;
        }

        int32_t id = -1;
        if (message.subcommand == "AT")
            id = gScheduler.atWall(label, command, when, Scope);
        else if (message.subcommand == "AFTER")
            id = gScheduler.after(label, command, when, Scope);
        else if (clock == "WALL")
            id = gScheduler.everyWall(label, command, when, Scope);
        else
            id = gScheduler.everyMonotonic(label, command, when, Scope);
        result.result = id >= 0;
        if (result.result)
            result.response = String("Job created with ID ") + String(id);
        else
            result.response = "Could not create job (invalid time, duplicate label, full list, or storage unavailable).";
    }
    else
    {
        result.response = "Usage: JOB LIST | CLEAR | DELETE <label|#id> | AT <label> <epoch_s> \"<command>\" | AFTER <label> <delay_ms> \"<command>\" | EVERY <label> <WALL|MONO> <interval> \"<command>\"";
    }
    return result;
}
#endif // NM_ENABLE_JOBS

/// Parse and run one command through the built-in handler or registered resolver.
NightMareResults handleNightMareCommand(const String &message, NightmareContext context)
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

#if NM_ENABLE_RESOURCES
    // Resource commands deliberately bypass the generic command parser: the
    // text after the routing separator is an opaque MQTT-compatible payload,
    // not a list of console arguments, and may use the resource payload limit.
    size_t commandStart = 0;
    while (commandStart < message.length() && isTokenSeparator(message[commandStart]))
        ++commandStart;
    if (commandStart < message.length() && message[commandStart] == '>')
    {
        const String expression = message.substring(commandStart + 1);
        if (!ensureSize(expression, NetResourceMaxCommandLength, result.response))
        {
            result.result = false;
            return result;
        }
        const ActionResult resourceResult = gResourcesManager.executeCommand(expression);
        result.result = resourceResult.success;
        result.response = resourceResult.result;
        return result;
    }
#endif

    // ensureSize returns true when the message fits, so the rejection is the negated case.
    if (!ensureSize(message, NM_MAX_MESSAGE_LEN, result.response))
    {
        result.result = false;
        return result;
    }

    // CONFIG is a raw namespace adapter into ConfigManager. It bypasses the
    // generic tokenizer so everything after `set <name>` reaches NetCodec and
    // the change handler as one unchanged payload.
    size_t configCommandStart = 0;
    while (configCommandStart < message.length() &&
           isTokenSeparator(message[configCommandStart]))
        ++configCommandStart;
    size_t configCommandEnd = configCommandStart;
    while (configCommandEnd < message.length() &&
           !isTokenSeparator(message[configCommandEnd]))
        ++configCommandEnd;
    String commandName = message.substring(configCommandStart, configCommandEnd);
    if (commandName.equalsIgnoreCase("CONFIG"))
    {
        size_t configStart = configCommandEnd;
        while (configStart < message.length() && isTokenSeparator(message[configStart]))
            ++configStart;
        result.response = gConfigManager.handle(message.substring(configStart));
        result.result = !result.response.startsWith("ERROR:");
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
#if NM_ENABLE_JOBS
    if (parsedMsg.command == "JOB")
        return executeJobCommand(parsedMsg, context);
#endif
#if NM_ENABLE_TELEMETRY
    // HW returns the readable configuration; HW PUBLISH republishes it.
    if (parsedMsg.command == "HW")
    {
        bool publish = false;
        if (parsedMsg.subcommand == "PUBLISH")
            publish = true;

        if (parsedMsg.argc > 1 || (!publish && parsedMsg.subcommand.length() != 0 &&
                                   parsedMsg.subcommand != "JSON"))
        {
            result.result = false;
            result.response = "Usage: HW [PUBLISH]";
            return result;
        }

        if (publish)
            result.result = Telemetry.publishHardware();
        else
        {
            const TelemetryResult hardware = Telemetry.getHardware();
            result.result = hardware.valid;
            result.response = hardware.valid ? hardware.data : "Could not serialize hardware configuration.";
            return result;
        }
        result.response = result.result ? "Republished to MQTT." : "Hardware publish failed.";
        return result;
    }

    // INFO [section]            query: the aggregate, or one section of it
    // INFO PUBLISH [document]   publish INFO (default), SYSTEM or NETWORK
    if (parsedMsg.command == "INFO")
    {
        if (parsedMsg.subcommand == "PUBLISH")
        {
            const InfoType type = getInfoType(parsedMsg.args[1]);
            if (type != InfoType::INFO && type != InfoType::SYSTEM && type != InfoType::NETWORK)
            {
                result.result = false;
                result.response = type == InfoType::INVALID
                                      ? "Unknown INFO section."
                                      : "Only INFO, SYSTEM and NETWORK are published documents.";
            }
            else
            {
                result.result = Telemetry.publishInfo(type);
                result.response = result.result ? "OK" : "INFO publish failed.";
            }
        }
        else
        {
            const TelemetryResult info = Telemetry.getInfo(parsedMsg.subcommand);
            result.result = info.valid;
            result.response = info.valid
                                  ? info.data
                                  : "Usage: INFO [IDENTITY|HARDWARE|BUILD|BOOT|SYSTEM|NETWORK]"
                                    " | INFO PUBLISH [SYSTEM|NETWORK]";
        }
        return result;
    }
#endif
#if NM_CONSOLE_BUILTINS
    bool prehandled = true;
    // Basic commands that can be handled without a resolver
    if (parsedMsg.command == "ADOPT")
    {
        if (parsedMsg.argc != 1)
        {
            result.result = false;
            result.response = "Usage: ADOPT <device-name>";
        }
        else
            return executeAdoptCommand(parsedMsg.args[0], context);
    }
    else if (parsedMsg.command == "CHANGE" && parsedMsg.subcommand == "NAME")
    {
        if (parsedMsg.argc != 2)
        {
            result.result = false;
            result.response = "Usage: CHANGE NAME <device-name>";
        }
        else
            return executeAdoptCommand(parsedMsg.args[1], context);
    }
    else if (parsedMsg.command == "TIMEZONE")
    {
        if (parsedMsg.argc == 0)
        {
            JsonDocument doc;
            doc["timezone"] = gDeviceIdentity.getTimezone();
            serializeJson(doc, result.response);
            result.result = true;
        }
        else if (parsedMsg.argc == 2 && parsedMsg.subcommand == "SET")
            return executeTimezoneCommand(parsedMsg.args[1], context);
        else
        {
            result.result = false;
            result.response = "Usage: TIMEZONE [SET <posix-tz>]";
        }
    }
    else if (parsedMsg.command == "CHANGE" && parsedMsg.subcommand == "TIMEZONE")
    {
        if (parsedMsg.argc != 2)
        {
            result.result = false;
            result.response = "Usage: CHANGE TIMEZONE <posix-tz>";
        }
        else
            return executeTimezoneCommand(parsedMsg.args[1], context);
    }
    else if (parsedMsg.command == "PING")
    {
        result.response = "PONG";
    }
    else if (parsedMsg.command == "REBOOT")
    {
        ESP.restart();
        result.response = "Rebooting...";
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
    else if (parsedMsg.command == "TIME")
    {
        if (parsedMsg.argc > 1 || (parsedMsg.argc == 1 && parsedMsg.subcommand != "STATUS"))
        {
            result.result = false;
            result.response = "Usage: TIME [STATUS]";
        }
        else
        {
            const time_t epoch = NightMare::Time::now();
            const bool clockValid = NightMare::Time::valid();
            JsonDocument doc;
            doc["synced"] = clockValid && SystemState.get(SystemFlag::TimeSynced);
            doc["valid"] = clockValid;
            doc["epoch"] = clockValid ? static_cast<uint64_t>(epoch) : 0;
            doc["local"] = clockValid
                               ? NightMare::Time::timestampToDateString(
                                     epoch, NightMare::Time::DateAndTime)
                               : String();
            doc["timezone"] = gDeviceIdentity.getTimezone();
            doc["uptime_ms"] = millis();
            serializeJson(doc, result.response);
            result.result = true;
        }
    }
    else if (parsedMsg.command == "FS")
    {
        if (parsedMsg.subcommand == "LIST")
        {
            JsonDocument doc;
            JsonArray files = doc["files"].to<JsonArray>();
            listFileTree(files, "", FS_LIST_MAX_DEPTH);
            // ArduinoJson 7 documents have no capacity to overflow, so the
            // ceiling applies to the finished document instead. Building it
            // first cannot run away: the tree is bounded by FS_LIST_MAX_DEPTH
            // and by the size of the filesystem it is listing.
            if (measureJson(doc) > FS_LIST_JSON_CAPACITY)
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
            JsonDocument doc;
            doc["totalBytes"] = LittleFS.totalBytes();
            doc["usedBytes"] = LittleFS.usedBytes();
            doc["usedPercentage"] = (double)(LittleFS.usedBytes() * 100) / (double)LittleFS.totalBytes();
            doc["initialized"] = SystemState.get(SystemFlag::PersistentStorageReady);
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
            else if (!SystemState.get(SystemFlag::PersistentStorageReady))
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
#if NM_ENABLE_MQTT
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

#if NM_ENABLE_WIFI
    else if (parsedMsg.command == "WIFI")
    {
        if (parsedMsg.subcommand == "IP")
        {
            result.response = WiFi.localIP().toString();
        }
        else if (parsedMsg.subcommand == "STATE")
        {
            wl_status_t status = WiFi.status();
            result.response += "WiFi Status Code: " + String(static_cast<int>(status)) + " - ";
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
        else if (parsedMsg.subcommand == "TXPOWER")
        {
            if (parsedMsg.args[1].length() == 0)
            {
                const int cfg = WiFi_getProfile().txPower;
                result.response = "TX power: " + String(WiFi_getTxPowerDbm()) + " dBm (" +
                                  (cfg == NightMare::NM_TX_POWER_AUTO ? String("auto") : "configured " + String(cfg / 4.0f) + " dBm") + ")";
            }
            else
            {
                int quarter = 0;
                if (!parseTxPowerArg(parsedMsg.args[1], quarter))
                {
                    result.response = "Invalid TX power. Use AUTO or one of: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5, 20, 20.5, 21 dBm.";
                    result.result = false;
                }
                else if (WiFi_setTxPower(quarter))
                    result.response = "TX power set.";
                else
                {
                    result.response = "TX power change failed; previous settings restored.";
                    result.result = false;
                }
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
            JsonDocument doc;
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
                    JsonArray networks = doc["networks"].to<JsonArray>();
                    for (int i = 0; i < res; i++)
                    {
                        JsonObject net = networks.add<JsonObject>();
                        net["ssid"] = WiFi.SSID(i);
                        net["rssi"] = WiFi.RSSI(i);
                        net["mac"] = WiFi.BSSIDstr(i);
                        net["channel"] = WiFi.channel(i);
                        net["encryptionType"] = WiFi_getAuthTypeName(WiFi.encryptionType(i));
                    }
                }
            }
            String resStr = "";
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
                NightMare::WiFiProfile profile = WiFi_getProfile();
                profile.ssid = ssid;
                profile.password = parsedMsg.args[2];
                if (parsedMsg.args[3].length() > 0 &&
                    !parseTxPowerArg(parsedMsg.args[3], profile.txPower))
                {
                    result.response = "Invalid TX power. Use AUTO or one of: -1, 2, 5, 7, 8.5, 11, 13, 15, 17, 18.5, 19, 19.5 dBm.";
                    result.result = false;
                    return result;
                }
                bool changeResult = WiFi_changeProfile(profile);
                result.response = String("WiFi credentials change ") +
                                  (changeResult ? "successful." : "failed.");
#if NM_ENABLE_MQTT
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
            result.response = "Unknown WIFI subcommand available: [IP, STATE, SCAN <-s|-start>, CHANGE <ssid> <password> [dBm|AUTO], TXPOWER [dBm|AUTO], RECONNECT].";
            result.result = false;
        }
    }
#endif

#if NM_ENABLE_HTTP
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

#if NM_ENABLE_WEBSOCKET
    else if (parsedMsg.command == "WS")
    {
        if (parsedMsg.subcommand == "LIST")
        {
            result.response += "Total WS active clients: " + String(ws_clients.count) + "\n";
            for (size_t i = 0; i < HTTPD_MAX_OPEN_SOCKETS; i++)
            {
                if (ws_clients.wsList[i].active)
                {
                    result.response += "Client " + String(i) + ": Socket " +
                                       String(ws_clients.wsList[i].sockfd) + "\n";
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

#if NM_CONSOLE_SERIAL

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
                delayMicroseconds(10); // Small delay to allow buffer to fill
            }
        }
        cmd.trim();
        NightMareResults res = handleNightMareCommand(cmd, NightmareContext(NM_CMD_SRC_SERIAL, "Serial", _Serial));
        if (res.context.msgSource != NM_CMD_ANS_DO_NOT_RESPOND)
        {
            _Serial->println(res.response);
        }
    }
}

#endif
#endif // NM_ENABLE_CONSOLE
