#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_GATEWAY && NM_ENABLE_CONSOLE
#include <Core/NightMareTypes.h>

// The GATEWAY command family, routed here by handleNightMareCommand():
//
//   GATEWAY GET                                   status
//   GATEWAY ENABLE | DISABLE                      the whole gateway
//   GATEWAY CLIENTS                               ESP-NOW sessions
//   GATEWAY ESPNOW GET | PAUSE | RESUME           diagnostic pause, MQTT unaffected
//   GATEWAY TRACE GET | ENABLE | DISABLE          the <device>/hardware hop trace
//   GATEWAY MQTT GET | TEST [bytes [count]]       uplink state, PUBACK latency test
//   GATEWAY MQTT LINKDEBUG GET | ENABLE | DISABLE Wi-Fi driver dump on disconnect
//
// Answers are JSON. A refused or unknown request is {"ok":false,"reason":...}.
// ENABLE/DISABLE and PAUSE/RESUME are applied by the next gateway tick: the
// answer confirms the request, and GATEWAY GET shows when it took effect.
NightMareResults executeGatewayCommand(const NightMareMessage &message, NightmareContext context);

#endif
