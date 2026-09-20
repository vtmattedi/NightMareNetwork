#pragma once

#include "NetResources.h"

// Implement this at the transport boundary. A successful return means that the
// message was accepted for publishing; it does not acknowledge execution by another device.
class ResourcePublisher
{
public:
    virtual ~ResourcePublisher() = default;
    virtual bool publish(const String &topic, const String &payload, bool retained) = 0;
};

class ResourcesManager
{
public:
    static constexpr int MaxResources = 100;

    // Framework setup: publishing is injected by the MQTT facade.
    void setPublisher(ResourcePublisher *publisher);

    // Project API: resources remain owned by the project and must outlive their binding.
    bool bindResource(NetResource *resource);
    void unbindResource(NetResource *resource);

    // Call after transport reconnection to republish the retained manifest and
    // all known values owned by this device. Binding one also announces it.
    bool announceAll();

    // Accepts exact <device>/resources[/<name>/{state,set,invoke}] topics.
    // Returns true when a known resource accepted the message.
    bool handleIngressMessage(const String &topic, const String &message);

    // Optional fallback handlers. Per-resource handlers take precedence.
    void setValueHandler(NetValueResource::WriteHandler handler) { valueHandler_ = handler; }
    void setActionHandler(NetActionResource::InvokeHandler handler) { actionHandler_ = handler; }

private:
    friend struct NetValueResource;
    friend struct NetActionResource;

    NetResource *resources_[MaxResources] = {};
    int resourceCount_ = 0;
    ResourcePublisher *publisher_ = nullptr; // Non-owning.
    NetValueResource::WriteHandler valueHandler_ = nullptr;
    NetActionResource::InvokeHandler actionHandler_ = nullptr;

    // Resource methods delegate here. Local state changes remain committed if
    // publication fails; announceAll() can retry them. Remote requests succeed
    // only when accepted for publication.
    bool setValue(NetValueResource &resource, const String &newValue);
    bool invoke(NetActionResource &resource, const String &payload);

    static bool validSegment(const String &segment);
    NetResource *findResource(const String &deviceName, const String &name) const;
    String topicFor(const NetResource &resource, const char *suffix) const;
    bool publishManifest();
    bool publishState(const NetValueResource &resource);
    bool applyOtherDeviceManifest(const String &deviceName, const String &message);
};

extern ResourcesManager gResourcesManager;
