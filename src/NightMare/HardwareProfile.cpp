#include "HardwareProfile.h"

#if __has_include(<NightMareHardware.h>)
#include <NightMareHardware.h>
#define NM_HAS_PROJECT_HARDWARE 1
#else
#define NM_HAS_PROJECT_HARDWARE 0
#endif

namespace NMHardware
{
Profile getProfile()
{
#if NM_HAS_PROJECT_HARDWARE
    return projectProfile();
#else
    return {"unspecified", nullptr, 0};
#endif
}
}
