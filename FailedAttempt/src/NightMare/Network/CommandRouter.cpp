#include <NightMare/Features.h>
#if NIGHTMARE_ENABLE_CONSOLE
#include <NightMare/Network/CommandRouter.h>
#include <NightMare/Core/DeviceIdentity.h>
#include <NightMare/Platform/Esp32SystemInfo.h>
#include <NightMare/Network/Network.h>
#include <ArduinoJson.h>
#include <stdlib.h>

namespace NightMare {

namespace {
CommandResult success(const String& text) { return {true, text}; }
CommandResult failure(const String& text) { return {false, "error: " + text}; }

const char* kindName(NetResourceKind kind) {
    switch (kind) {
    case NetResourceKind::VALUE: return "value";
    case NetResourceKind::ACTION: return "action";
    case NetResourceKind::EVENT: return "event";
    }
    return "unknown";
}
const char* statusName(ActionStatus status) {
    switch (status) {
    case ActionStatus::OK: return "ok";
    case ActionStatus::REJECTED: return "rejected";
    case ActionStatus::INVALID_ARGUMENT: return "invalid argument";
    case ActionStatus::BUSY: return "busy";
    case ActionStatus::ERROR: return "failed";
    }
    return "failed";
}

bool encodeScalar(NetValueType type, const String& human, String& encoded) {
    switch (type) {
    case NetValueType::NONE: encoded = ""; return !human.length();
    case NetValueType::STRING: encoded = NetCodec<String>::encode(human); return true;
    case NetValueType::BOOL: { bool value; if (!NetCodec<bool>::decode(human, value)) return false;
        encoded = NetCodec<bool>::encode(value); return true; }
#define NM_SCALAR_CASE(kind, cpp) case NetValueType::kind: { cpp value; \
    if (!NetCodec<cpp>::decode(human, value)) return false; \
    encoded = NetCodec<cpp>::encode(value); return true; }
    NM_SCALAR_CASE(INT8, int8_t)
    NM_SCALAR_CASE(UINT8, uint8_t)
    NM_SCALAR_CASE(INT16, int16_t)
    NM_SCALAR_CASE(UINT16, uint16_t)
    NM_SCALAR_CASE(INT32, int32_t)
    NM_SCALAR_CASE(UINT32, uint32_t)
    NM_SCALAR_CASE(INT64, int64_t)
    NM_SCALAR_CASE(UINT64, uint64_t)
    NM_SCALAR_CASE(FLOAT32, float)
    NM_SCALAR_CASE(FLOAT64, double)
#undef NM_SCALAR_CASE
    case NetValueType::STRUCT: return false;
    }
    return false;
}

bool actionPayload(const NetResource& resource, const ParsedCommand& command,
                   uint8_t firstArg, String& payload) {
    if (resource.type() != NetValueType::STRUCT)
        return encodeScalar(resource.type(), command.tail(firstArg), payload);
    const auto* metadata = resource.metadata();
    if (!metadata || !metadata->fields ||
        command.count - firstArg != metadata->fieldCount) return false;
    DynamicJsonDocument doc(512);
    JsonArray fields = doc.to<JsonArray>();
    for (uint8_t i = 0; i < metadata->fieldCount; ++i) {
        const String& word = command.words[firstArg + i];
        String encoded;
        if (!encodeScalar(metadata->fields[i].type, word, encoded)) return false;
        switch (metadata->fields[i].type) {
        case NetValueType::STRING: fields.add(word); break;
        case NetValueType::BOOL: fields.add(encoded == "true"); break;
        case NetValueType::FLOAT32:
        case NetValueType::FLOAT64: fields.add(encoded.toDouble()); break;
        case NetValueType::UINT32: fields.add(static_cast<uint32_t>(strtoul(encoded.c_str(), nullptr, 10))); break;
        case NetValueType::INT64: fields.add(static_cast<int64_t>(strtoll(encoded.c_str(), nullptr, 10))); break;
        case NetValueType::UINT64: fields.add(static_cast<uint64_t>(strtoull(encoded.c_str(), nullptr, 10))); break;
        default: fields.add(encoded.toInt()); break;
        }
    }
    serializeJson(doc, payload);
    return !doc.overflowed();
}

String displayValue(const NetResource& resource) {
    const auto& value = static_cast<const NetValueBase&>(resource);
    if (!value.hasValue()) return "<never received>";
    String encoded = value.encode();
    return resource.type() == NetValueType::STRING ? encoded.substring(1) : encoded;
}
}

CommandResult CommandRouter::execute(const String& line, CommandContext context) {
    ParsedCommand command;
    String error;
    if (!CommandParser::parse(line, command, error)) return failure(error);
    if (!command.count) return failure("empty command; type help");
    if (command.words[0] == ">") return invoke(command);
    if (command.words[0] == "<") return inspect(command);
    return operatorCommand(command, context);
}

bool CommandRouter::registerCommand(const char* name, Handler handler, void* context) {
    if (!name || !*name || !handler || String(name) == "config" ||
        String(name) == "system" || String(name) == "network" ||
        String(name) == "resources" || String(name) == "help" ||
        String(name) == ">" || String(name) == "<") return false;
    for (const auto& item : _handlers) if (item.name && String(item.name) == name) return false;
    for (auto& item : _handlers) if (!item.name) { item = {name, handler, context}; return true; }
    return false;
}

void CommandRouter::tick(uint32_t nowMs) {
    if (_restartAtMs && static_cast<int32_t>(nowMs - _restartAtMs) >= 0) ESP.restart();
}

CommandResult CommandRouter::invoke(const ParsedCommand& command) {
    if (command.count < 2) return failure("usage: > [owner/]action [arguments]");
    String target = command.words[1];
    int slash = target.indexOf('/');
    String owner = slash >= 0 ? target.substring(0, slash) : _resources.device();
    String id = slash >= 0 ? target.substring(slash + 1) : target;
    if (!owner.length() || !id.length() || id.indexOf('/') >= 0)
        return failure("invalid action address");
    ResourceRole role = owner == _resources.device() ? ResourceRole::LOCAL : ResourceRole::REMOTE;
    auto* entry = _resources.registry().find(role, owner.c_str(), id);
    if (!entry || entry->resource->kind() != NetResourceKind::ACTION)
        return failure("action not registered: " + target);
    String payload;
    if (!actionPayload(*entry->resource, command, 2, payload))
        return failure("invalid arguments for " + target);
    if (role == ResourceRole::REMOTE)
        return _resources.invoke(*entry->resource, payload) ? success("sent: " + target)
                                                        : failure("could not send " + target);
    String result;
    ActionStatus status = _resources.invokeLocalResult(id, payload, result);
    return status == ActionStatus::OK ? success(result.length() ? result : "ok")
                                      : failure(String(statusName(status)) +
                                                (result.length() ? ": " + result : ""));
}

CommandResult CommandRouter::inspect(const ParsedCommand& command) {
    if (command.count < 2 || command.count > 4)
        return failure("usage: < owner [value [maxAgeMs]]");
    String owner = command.words[1];
    if (owner == "local") owner = _resources.device();
    ResourceRole role = owner == _resources.device() ? ResourceRole::LOCAL : ResourceRole::REMOTE;
    if (command.count == 2) {
        String lines;
        for (const auto& entry : _resources.registry().entries()) {
            if (!entry.resource || entry.resource->kind() != NetResourceKind::VALUE ||
                entry.role != role || (role == ResourceRole::REMOTE && owner != entry.owner)) continue;
            if (lines.length()) lines += '\n';
            lines += entry.resource->id() + " = " + displayValue(*entry.resource);
        }
        return lines.length() ? success(lines) : failure("no registered Values for " + owner);
    }
    auto* entry = _resources.registry().find(role, owner.c_str(), command.words[2]);
    if (!entry || entry->resource->kind() != NetResourceKind::VALUE)
        return failure("Value not registered: " + owner + "/" + command.words[2]);
    String result = owner + "/" + entry->resource->id() + " = " + displayValue(*entry->resource);
    if (role == ResourceRole::REMOTE) {
        uint32_t maxAge = 30000;
        if (command.count == 4 && !NetCodec<uint32_t>::decode(command.words[3], maxAge))
            return failure("maxAgeMs must be an unsigned integer");
        ResourceFreshness freshness = _resources.freshness(*entry->resource, maxAge);
        result += freshness == ResourceFreshness::UNKNOWN ? " [unknown]" :
                  freshness == ResourceFreshness::FRESH ? " [fresh]" : " [stale]";
        if (freshness != ResourceFreshness::UNKNOWN)
            result += " age=" + String(_resources.ageMs(*entry->resource)) + "ms";
    }
    return success(result);
}

CommandResult CommandRouter::operatorCommand(const ParsedCommand& command, CommandContext context) {
    const String& verb = command.words[0];
    if (verb == "help") {
        String help =
            "> [owner/]action [args]  invoke registered Action\n"
            "< owner [value [maxAgeMs]]  inspect last known Value\n"
            "resources  list registered resources\n"
            "system info|boot|status|restart  device information and restart";
        if (_settings) help += "\nconfig list|get|set|flag|exists|remove|clear  user settings";
        if (_network) help += "\nnetwork status  link and queue status";
        if (_identity) help += "\ndevice label|adopt  display label and stable ID";
        return success(help);
    }
    if (verb == "resources") {
        if (command.count != 1 && !(command.count == 2 && command.words[1] == "list"))
            return failure("usage: resources [list]");
        String lines;
        for (const auto& entry : _resources.registry().entries()) {
            if (!entry.resource) continue;
            if (lines.length()) lines += '\n';
            lines += String(entry.role == ResourceRole::LOCAL ? _resources.device().c_str() : entry.owner)
                + "/" + entry.resource->id() + " " + kindName(entry.resource->kind());
        }
        return success(lines.length() ? lines : "no resources registered");
    }
    if (verb == "config") {
        if (!_settings) return failure("settings unavailable");
        if (command.count < 2) return failure("usage: config list|get|set|remove|clear");
        const String& op = command.words[1];
        if (op == "list" && command.count == 2) {
            struct State { String lines; } state;
            _settings->visit([](void* p, const String& key, const String& value) {
                auto& lines = static_cast<State*>(p)->lines;
                if (lines.length()) lines += '\n';
                lines += key + " = " + value;
            }, &state, context.access);
            return success(state.lines.length() ? state.lines : "no user settings");
        }
        if (op == "get" && command.count == 3) {
            if (!_settings->exists(command.words[2].c_str(), context.access))
                return failure("setting not found");
            String value = _settings->getString(command.words[2].c_str(), "", context.access);
            return success(value.length() ? value : "\"\"");
        }
        if (op == "exists" && command.count == 3)
            return success(_settings->exists(command.words[2].c_str(), context.access) ? "true" : "false");
        if (op == "flag" && command.count == 3)
            return success(_settings->getBool(command.words[2].c_str(), false, context.access)
                ? "true" : "false");
        if (op == "flag" && command.count == 4) {
            bool flag;
            if (!NetCodec<bool>::decode(command.words[3], flag)) return failure("expected true or false");
            return _settings->setBool(command.words[2].c_str(), flag, context.access)
                ? success("saved") : failure("could not save flag");
        }
        if (op == "set" && command.count >= 4)
            return _settings->setString(command.words[2].c_str(), command.tail(3), context.access)
                ? success("saved; reboot if this changes network identity or connection settings")
                : failure("could not save setting");
        if (op == "remove" && command.count == 3)
            return _settings->remove(command.words[2].c_str(), context.access)
                ? success("removed") : failure("setting not found or could not save");
        if (op == "clear" && command.count == 2)
            return _settings->clear(context.access) ? success("settings cleared")
                                                    : failure("could not clear settings");
        return failure("usage: config list|get key|set key value|remove key|clear");
    }
    if (verb == "system") {
        if (command.count == 2 && command.words[1] == "info") {
            String result = "device=" + _resources.device() + " " + Esp32SystemInfo::hardwareText();
            if (_identity) result += " label=" + _identity->label();
            return success(result);
        }
        if (command.count == 2 && command.words[1] == "boot")
            return success(Esp32SystemInfo::bootText(_firmwareVersion.c_str()));
        if (command.count == 2 && command.words[1] == "status")
            return success(Esp32SystemInfo::statusText());
        if (command.count == 2 && (command.words[1] == "reboot" || command.words[1] == "restart")) {
            _restartAtMs = millis() + 250;
            return success("restarting");
        }
        return failure("usage: system info|boot|status|restart");
    }
    if (verb == "network") {
        if (command.count != 2 || command.words[1] != "status")
            return failure("usage: network status");
        if (!_network) return failure("network status unavailable");
        return success(String(_network->connected() ? "connected" : "disconnected") +
            " droppedMessages=" + String(_network->droppedMessages()));
    }
    if (verb == "device") {
        if (!_identity || command.count < 2) return failure("usage: device label|adopt");
        if (command.words[1] == "label") {
            if (command.count == 2)
                return success(_identity->label().length() ? _identity->label() : "\"\"");
            return _identity->setLabel(command.tail(2)) ? success("label saved")
                                                        : failure("could not save label");
        }
        if (command.words[1] == "adopt" && command.count == 3)
            return _identity->adoptId(command.words[2])
                ? success("ID saved; restart required") : failure("invalid ID or could not save");
        return failure("usage: device label [text]|adopt id");
    }
    if (verb == "boot_info" && command.count == 1)
        return success(Esp32SystemInfo::bootText(_firmwareVersion.c_str()));
    if (verb == "hardware_info" && command.count == 1) return success(Esp32SystemInfo::hardwareText());
    for (const auto& item : _handlers)
        if (item.name && verb == item.name) return item.handler(item.context, command, context);
    // Registered local Actions can also be invoked by their bare ID.
    auto* entry = _resources.registry().find(ResourceRole::LOCAL, nullptr, verb);
    if (entry && entry->resource->kind() == NetResourceKind::ACTION) {
        if (command.count == ParsedCommand::MAX_WORDS) return failure("too many arguments");
        ParsedCommand prefixed;
        prefixed.words[0] = ">";
        for (uint8_t i = 0; i < command.count; ++i) prefixed.words[i + 1] = command.words[i];
        prefixed.count = command.count + 1;
        return invoke(prefixed);
    }
    return failure("unknown command; type help");
}

} // namespace NightMare
#endif
