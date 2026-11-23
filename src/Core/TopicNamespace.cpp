#include "TopicNamespace.h"
#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <atomic>

namespace NightMare::TopicNamespace
{
namespace
{
std::atomic<uint32_t> foreignDropped{0};
}

std::string applyPrefix(std::string_view relative)
{
    return applyPrefix(NightMare::TopicRoot(), NightMare::Namespace(), relative);
}

std::string applyFilterPrefix(std::string_view filter)
{
    return applyPrefix(NightMare::TopicRoot(), NightMare::Namespace(), filter);
}

bool stripPrefix(std::string_view full, std::string &relative)
{
    std::string_view view;
    if (!stripView(NightMare::TopicRoot(), NightMare::Namespace(), full, view))
        return false;
    relative.assign(view);
    return true;
}

void noteForeignDropped()
{
    const uint32_t total = foreignDropped.fetch_add(1, std::memory_order_relaxed) + 1;
    LOG_DEBUG("MQTT", "Dropped a message outside the namespace (%lu so far)",
              static_cast<unsigned long>(total));
}

uint32_t foreignDroppedCount() { return foreignDropped.load(std::memory_order_relaxed); }
}
