---
title: Protocol
description: Current MQTT topic mapping, schema and operation payloads.
section: network
order: 1
---

# Protocol and MQTT mapping

The Resource model carries an address of namespace, device and resource. The current MQTT mapping uses the default namespace unless a Network is constructed with another one. Namespace routing, authorization and bridge filtering remain separate future topics.

Namespace and device IDs use the same 1–64 character ASCII segment rule as resource IDs. The ESP32 transport rejects invalid IDs when started.

| Operation | MQTT topic | Payload | Retained |
| --- | --- | --- | --- |
| Schema | `nm/<ns>/<device>/r/<id>/schema` | JSON descriptor | yes |
| Value state | `nm/<ns>/<device>/r/<id>/state` | scalar text | yes |
| Value write | `nm/<ns>/<owner>/r/<id>/write` | requested scalar | no |
| Action invoke, `NONE` | `nm/<ns>/<owner>/r/<id>/invoke` | encoded arguments | no |
| Action invoke, `ACK` or `RESULT` | same | `<requestId>|<caller>|<arguments>` | no |
| Action reply | `nm/<ns>/<caller>/reply` | `<requestId>|<status>|<result>` | no |
| Event emit | `nm/<ns>/<device>/r/<id>/emit` | optional encoded payload | no |
| Device status | `nm/<ns>/<device>/status` | `online` or `offline` | yes |

An Action result uses one reply endpoint per caller. IDs are small unsigned 16-bit numbers scoped to the caller's pending requests. `ActionStatus` numbers are `OK=0`, `REJECTED=1`, `INVALID_ARGUMENT=2`, `BUSY=3`, `ERROR=4`. A `NONE` Action adds no envelope. Empty Event payloads are valid MQTT messages.

Schema descriptors use numeric enum values to keep device code small: Resource kind `VALUE=0`, `ACTION=1`, `EVENT=2`; Value type `NONE=0`, `BOOL=1`, `INT8=2`, `UINT8=3`, `INT16=4`, `UINT16=5`, `INT32=6`, `UINT32=7`, `INT64=8`, `UINT64=9`, `FLOAT32=10`, `FLOAT64=11`, `STRING=12`, `STRUCT=13`; Value access `READ=0`, `READ_WRITE=1`. Action schemas include response policy `NONE=0`, `ACK=1`, `RESULT=2`. Optional metadata fields appear only when supplied.

For example, a temperature schema can be `{"kind":0,"type":10,"access":0,"label":"Room temperature","unit":"C"}`. Its state payload can simply be `23.5`. A write is a request: the owner publishes a state update only after applying it. A remote mirror never republishes received state as authority.

An initialized `STRING` Value prefixes its wire payload with `~`: empty string is `~`, and `hello` is `~hello`. Other scalar Values use their plain encoded text. A Value with no initial state has a schema but no retained state message.

Structured Action and Event payloads use a compact JSON array in the order listed by schema `fields`. A color Action with `red`, `green` and `blue` fields can send `[255,120,0]`. This format keeps field names in discovery rather than repeating them in each invocation.

MQTT subscribes to `nm/#`, `Control/time`, and the device's optional `<device>/console/in` adapter. Commands received there are parsed by CommandRouter and answered on `<device>/console/out`. `>` invokes registered Actions; `< owner [value]` inspects locally known Values; `config`, `system`, `network` and `resources` are operator commands. Serial Console uses the same router. Console text is an adapter format, not the NM-NW Action protocol. See [command grammar](qol-restoration.md#command-grammar).

## Time source

On reconnect, a device with an invalid wall clock sends `time` to `Control/request`. A `Control/time` JSON reply containing `timestamp` sets the system UTC epoch. Millisecond timestamps are accepted. The reply's offset is presentation information and does not change the canonical UTC clock.

## Transport bounds

The active ESP32 adapter uses MQTT QoS 0. It accepts complete single MQTT data events up to 1024 bytes with topics up to 192 bytes; fragmented or oversized incoming payloads are discarded. Keep Action and Event payloads small. The Network has an eight-message inbound queue; `droppedMessages()` reports queue drops. Schema and state are republished after reconnect, while Events are transient.
