#include <NightMare/Resources/ResourceManager.h>
#include <ArduinoJson.h>
#include <stdlib.h>

namespace NightMare {

ResourceManager::ResourceManager(const char* device, Transport& transport, const char* nameSpace)
    : _device(device), _nameSpace(nameSpace), _transport(transport) {}

bool ResourceManager::add(NetResource& resource, PublishPolicy policy) {
    if (!_registry.add(resource, ResourceRole::LOCAL, nullptr, policy)) return false;
    if (_transport.connected()) {
        auto* entry = _registry.find(resource);
        publishSchema(*entry);
        publishState(*entry);
    }
    return true;
}

bool ResourceManager::mirror(NetResource& resource, const char* owner) {
    return _registry.add(resource, ResourceRole::REMOTE, owner);
}

ResourceManager::Binding* ResourceManager::binding(NetResource& resource) {
    for (auto& item : _bindings) if (item.resource == &resource) return &item;
    for (auto& item : _bindings) if (!item.resource) { item.resource = &resource; return &item; }
    return nullptr;
}

void ResourceManager::onWrite(NetResource& resource, WriteHandler handler, void* context) {
    if (auto* item = binding(resource)) { item->write = handler; item->writeContext = context; }
}
void ResourceManager::onAction(NetResource& resource, ActionHandler handler, void* context) {
    if (auto* item = binding(resource)) { item->action = handler; item->actionContext = context; }
}
void ResourceManager::onUpdate(NetResource& resource, Notification handler, void* context) {
    if (auto* item = binding(resource)) { item->update = handler; item->updateContext = context; }
}
void ResourceManager::onResult(NetResource& resource, ResultHandler handler, void* context) {
    if (auto* item = binding(resource)) { item->result = handler; item->resultContext = context; }
}
void ResourceManager::onEvent(NetResource& resource, EventHandler handler, void* context) {
    if (auto* item = binding(resource)) { item->event = handler; item->eventContext = context; }
}

bool ResourceManager::publish(const ResourceRegistry::Entry& entry, const char* operation,
                              const String& payload, bool retained) {
    if (!_transport.connected()) return false;
    const char* owner = entry.role == ResourceRole::LOCAL ? _device.c_str() : entry.owner;
    return _transport.publish("nm/" + _nameSpace + "/" + owner + "/r/" +
                              entry.resource->id() + "/" + operation, payload, retained);
}

void ResourceManager::publishState(ResourceRegistry::Entry& entry) {
    if (!_transport.connected() || entry.resource->kind() != NetResourceKind::VALUE || entry.role != ResourceRole::LOCAL ||
        !static_cast<NetValueBase*>(entry.resource)->hasValue()) return;
    if (publish(entry, "state", static_cast<NetValueBase*>(entry.resource)->encode(), true))
        entry.lastPublishedMs = millis();
}

void ResourceManager::publishSchema(const ResourceRegistry::Entry& entry) {
    StaticJsonDocument<768> doc;
    doc["kind"] = static_cast<uint8_t>(entry.resource->kind());
    doc["type"] = static_cast<uint8_t>(entry.resource->type());
    if (entry.resource->kind() == NetResourceKind::VALUE)
        doc["access"] = static_cast<uint8_t>(entry.resource->access());
    if (entry.resource->kind() == NetResourceKind::ACTION)
        doc["response"] = static_cast<uint8_t>(entry.resource->actionResponse());
    if (const auto* meta = entry.resource->metadata()) {
        if (meta->label) doc["label"] = meta->label;
        if (meta->unit) doc["unit"] = meta->unit;
        if (meta->description) doc["description"] = meta->description;
        if (meta->group) doc["group"] = meta->group;
        if (meta->minimum) doc["min"] = meta->minimum;
        if (meta->maximum) doc["max"] = meta->maximum;
        if (meta->fields && meta->fieldCount) {
            JsonArray fields = doc.createNestedArray("fields");
            for (uint8_t i = 0; i < meta->fieldCount; ++i) {
                JsonObject field = fields.createNestedObject();
                field["id"] = meta->fields[i].id;
                field["type"] = static_cast<uint8_t>(meta->fields[i].type);
            }
        }
    }
    if (doc.overflowed()) return;
    String json;
    serializeJson(doc, json);
    if (json.length() > 1024) return;
    publish(entry, "schema", json, true);
}

void ResourceManager::connected() {
    for (auto& entry : _registry.entries()) {
        if (!entry.resource || entry.role != ResourceRole::LOCAL) continue;
        publishSchema(entry);
        publishState(entry);
    }
}

void ResourceManager::tick(uint32_t nowMs) {
    for (auto& pending : _pending) {
        if (pending.resource && static_cast<uint32_t>(nowMs - pending.sentMs) > 10000) {
            auto* item = binding(*pending.resource);
            if (item && item->result)
                item->result(item->resultContext, *pending.resource, pending.id, ActionStatus::ERROR, "timeout");
            pending = {};
        }
    }
    if (!_transport.connected()) return;
    for (auto& entry : _registry.entries()) {
        if (!entry.resource || entry.role != ResourceRole::LOCAL ||
            entry.resource->kind() != NetResourceKind::VALUE || !entry.policy.periodMs) continue;
        if (static_cast<uint32_t>(nowMs - entry.lastPublishedMs) >= entry.policy.periodMs)
            publishState(entry);
    }
}

bool ResourceManager::invoke(NetResource& action, const String& arguments) {
    auto* entry = _registry.find(action);
    if (!entry || action.kind() != NetResourceKind::ACTION) return false;
    if (entry->role == ResourceRole::LOCAL) { dispatchAction(*entry, arguments, true); return true; }
    // A response requires a small correlation ID and a reply device. NONE has no envelope.
    const auto response = action.actionResponse();
    if (response == ActionResponse::NONE) return publish(*entry, "invoke", arguments);
    ++_nextRequestId;
    if (!_nextRequestId) ++_nextRequestId;
    for (auto& pending : _pending) {
        if (!pending.resource) {
            const bool sent = publish(*entry, "invoke", String(_nextRequestId) + "|" + _device + "|" + arguments);
            if (sent) { pending.id = _nextRequestId; pending.resource = &action; pending.sentMs = millis(); }
            return sent;
        }
    }
    return false;
}

bool ResourceManager::emit(NetResource& event, const String& payload) {
    auto* entry = _registry.find(event);
    return entry && entry->role == ResourceRole::LOCAL && event.kind() == NetResourceKind::EVENT &&
           publish(*entry, "emit", payload);
}

void ResourceManager::dispatchAction(ResourceRegistry::Entry& entry, const String& payload, bool localCall) {
    auto* item = binding(*entry.resource);
    const auto response = entry.resource->actionResponse();
    uint16_t requestId = 0;
    String caller, arguments = payload;
    if (response != ActionResponse::NONE && !localCall) {
        int first = payload.indexOf('|');
        int second = payload.indexOf('|', first + 1);
        if (first <= 0 || second <= first + 1) return;
        requestId = static_cast<uint16_t>(payload.substring(0, first).toInt());
        caller = payload.substring(first + 1, second);
        arguments = payload.substring(second + 1);
        if (!requestId || !caller.length() || caller.indexOf('/') >= 0) return;
    }
    String result;
    ActionStatus status = item && item->action
        ? item->action(item->actionContext, *entry.resource, arguments, result)
        : ActionStatus::REJECTED;
    if (localCall || response == ActionResponse::NONE || !caller.length()) return;
    const String reply = String(requestId) + "|" + String(static_cast<uint8_t>(status)) + "|" +
                         (response == ActionResponse::RESULT ? result : "");
    _transport.publish("nm/" + _nameSpace + "/" + caller + "/reply", reply);
}

bool ResourceManager::invokeLocal(const String& id, const String& arguments) {
    auto* entry = _registry.find(ResourceRole::LOCAL, nullptr, id);
    if (!entry || entry->resource->kind() != NetResourceKind::ACTION) return false;
    dispatchAction(*entry, arguments, true);
    return true;
}

void ResourceManager::receive(const String& owner, const String& id, Operation operation,
                              const String& payload) {
    const bool isLocal = owner == _device;
    auto* entry = _registry.find(isLocal ? ResourceRole::LOCAL : ResourceRole::REMOTE,
                                 owner.c_str(), id);
    if (!entry) return;
    NetResource& resource = *entry->resource;
    auto* item = binding(resource);
    if (isLocal) {
        if (operation == Operation::VALUE_WRITE && resource.kind() == NetResourceKind::VALUE &&
            resource.access() == NetAccess::READ_WRITE && item && item->write)
            item->write(item->writeContext, resource, payload);
        else if (operation == Operation::ACTION_INVOKE && resource.kind() == NetResourceKind::ACTION)
            dispatchAction(*entry, payload);
        return;
    }
    if (operation == Operation::VALUE_STATE && resource.kind() == NetResourceKind::VALUE) {
        if (static_cast<NetValueBase&>(resource).decodeAndSet(payload) && item && item->update)
            item->update(item->updateContext, resource);
    } else if (operation == Operation::EVENT_EMIT && resource.kind() == NetResourceKind::EVENT &&
               item && item->event) {
        item->event(item->eventContext, resource, payload);
    }
}

void ResourceManager::receiveReply(const String& payload) {
    int first = payload.indexOf('|');
    int second = payload.indexOf('|', first + 1);
    if (first <= 0 || second <= first + 1) return;
    uint16_t id = static_cast<uint16_t>(payload.substring(0, first).toInt());
    int statusNumber = payload.substring(first + 1, second).toInt();
    if (!id || statusNumber < 0 || statusNumber > static_cast<int>(ActionStatus::ERROR)) return;
    for (auto& pending : _pending) {
        if (pending.resource && pending.id == id) {
            auto* item = binding(*pending.resource);
            if (item && item->result)
                item->result(item->resultContext, *pending.resource, id,
                             static_cast<ActionStatus>(statusNumber), payload.substring(second + 1));
            pending = {};
            return;
        }
    }
}

} // namespace NightMare
