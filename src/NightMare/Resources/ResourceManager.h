#pragma once
#include <NightMare/Resources/ResourceRegistry.h>
#include <NightMare/Resources/NetValue.h>
#include <NightMare/Resources/NetAction.h>
#include <NightMare/Resources/NetEvent.h>
#include <NightMare/Network/Transport.h>

namespace NightMare {

enum class Operation : uint8_t { VALUE_STATE, VALUE_WRITE, ACTION_INVOKE, ACTION_RESULT, EVENT_EMIT };

class ResourceManager {
public:
    using WriteHandler = ActionStatus (*)(void* context, NetResource& resource, const String& requested);
    using ActionHandler = ActionStatus (*)(void* context, NetResource& resource,
                                           const String& arguments, String& result);
    using Notification = void (*)(void* context, NetResource& resource);
    using ResultHandler = void (*)(void* context, NetResource& resource, uint16_t requestId,
                                   ActionStatus status, const String& result);
    using EventHandler = void (*)(void* context, NetResource& resource, const String& payload);

    ResourceManager(const char* device, Transport& transport, const char* nameSpace = "default");
    ResourceRegistry& registry() { return _registry; }
    bool add(NetResource& resource, PublishPolicy policy = {});
    bool mirror(NetResource& resource, const char* owner);
    void onWrite(NetResource& resource, WriteHandler handler, void* context = nullptr);
    void onAction(NetResource& resource, ActionHandler handler, void* context = nullptr);
    void onUpdate(NetResource& resource, Notification handler, void* context = nullptr);
    void onResult(NetResource& resource, ResultHandler handler, void* context = nullptr);
    void onEvent(NetResource& resource, EventHandler handler, void* context = nullptr);

    template<typename T> bool set(NetValue<T>& value, const T& next) {
        auto* entry = _registry.find(value);
        if (!entry || entry->role != ResourceRole::LOCAL) return false;
        bool changed = value.set(next);
        if (changed && entry->policy.onChange) publishState(*entry);
        return changed;
    }
    template<typename T> bool request(NetValue<T>& value, const T& next) {
        auto* entry = _registry.find(value);
        if (!entry || entry->role != ResourceRole::REMOTE || value.access() != NetAccess::READ_WRITE) return false;
        return publish(*entry, "write", NetCodec<T>::encode(next));
    }
    bool invoke(NetResource& action, const String& arguments = "");
    template<typename Args> bool invoke(NetAction<Args>& action, const Args& arguments) {
        return invoke(action, action.encode(arguments));
    }
    bool emit(NetResource& event, const String& payload = "");
    template<typename Payload> bool emit(NetEvent<Payload>& event, const Payload& payload) {
        return emit(event, event.encode(payload));
    }
    void receive(const String& owner, const String& id, Operation operation, const String& payload);
    void receiveReply(const String& payload);
    void connected();
    void tick(uint32_t nowMs);
    bool invokeLocal(const String& id, const String& arguments = "");
    const String& device() const { return _device; }
    const String& nameSpace() const { return _nameSpace; }

private:
    struct Binding {
        NetResource* resource = nullptr;
        WriteHandler write = nullptr;
        ActionHandler action = nullptr;
        Notification update = nullptr;
        ResultHandler result = nullptr;
        EventHandler event = nullptr;
        void* writeContext = nullptr;
        void* actionContext = nullptr;
        void* updateContext = nullptr;
        void* resultContext = nullptr;
        void* eventContext = nullptr;
    };
    Binding* binding(NetResource& resource);
    bool publish(const ResourceRegistry::Entry& entry, const char* operation, const String& payload,
                 bool retained = false);
    void publishState(ResourceRegistry::Entry& entry);
    void publishSchema(const ResourceRegistry::Entry& entry);
    void dispatchAction(ResourceRegistry::Entry& entry, const String& payload, bool localCall = false);

    String _device;
    String _nameSpace;
    Transport& _transport;
    ResourceRegistry _registry;
    Binding _bindings[NIGHTMARE_MAX_RESOURCES];
    struct Pending { uint16_t id = 0; NetResource* resource = nullptr; uint32_t sentMs = 0; };
    Pending _pending[8];
    uint16_t _nextRequestId = 0;
};

} // namespace NightMare
