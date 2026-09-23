---
title: MQTTP
description: Correlated NightMare command request/response over MQTT controlled-console topics.
section: protocols
order: 50
---

# MQTTP

MQTTP is NightMare's correlated command request/response pattern over MQTT.

In the current implementation it is the **controlled console** topic family:

```text
<device>/console/controlled/<id>/in
<device>/console/controlled/<id>/out
```

It reuses the normal NightMare command parser and command handler. It is not a second command language.

## Purpose

Ordinary MQTT console uses:

```text
<device>/console/in
<device>/console/out
```

That is convenient for interactive/operator use, but several requests can share the same output topic.

MQTTP gives a caller a request-specific topic pair by supplying an ID.

Example:

```text
request:
bedroom-ac/console/controlled/req-42/in

response:
bedroom-ac/console/controlled/req-42/out
```

The caller can subscribe to the response topic before publishing the request and correlate that response with its own ID.

## Device subscription

With MQTT + Console enabled, every device subscribes to:

```text
<device>/console/controlled/+/in
```

The `+` wildcard represents exactly one request ID segment.

## Request

Publish a normal NightMare command String to:

```text
<device>/console/controlled/<id>/in
```

Example:

```text
topic:
bedroom-ac/console/controlled/req-42/in

payload:
INFO SYSTEM
```

The payload is parsed by the same `handleNightMareCommand()` implementation used by the other command transports.

All normal command grammar rules therefore apply:

- 512-character maximum command line,
- command and subcommand are case-insensitive through uppercasing,
- up to five arguments after the command word,
- quoted arguments,
- backtick literal arguments,
- normal built-in and project-resolver behavior.

See [Commands](commands.md).

## Response

The result String is published to:

```text
<device>/console/controlled/<id>/out
```

Example:

```text
topic:
bedroom-ac/console/controlled/req-42/out

payload:
{"uptime_ms":123456,"cpu_mhz":160,...}
```

The response is transient:

```text
retained: false
```

MQTTP does not define a second JSON response envelope around command output. The response payload is the command handler's `result.response` String.

That means the payload format depends on the command:

```text
PING
    PONG

INFO SYSTEM
    JSON object

JOB LIST
    JSON object

CONFIG GET ...
    JSON/string response defined by CONFIG
```

The caller must know the command's response contract.

## Request ID rules

The `<id>` must:

- contain at least 1 character,
- contain at most 64 characters,
- not contain `/`,
- not contain `+`,
- not contain `#`,
- not contain control characters below `0x20`.

The ID is a topic segment supplied by the caller.

NightMare does not generate IDs for the caller.

## ID uniqueness

The device does not maintain an ID registry.

It simply derives the response topic from the incoming request topic.

Therefore uniqueness is the caller's responsibility.

Two clients using the same:

```text
<device>
<id>
```

at the same time will share the same response topic.

A practical ID should therefore be unique enough for the client/session issuing the request.

## Request lifecycle

The minimal caller flow is:

```text
1. choose id
2. subscribe to <device>/console/controlled/<id>/out
3. publish command to <device>/console/controlled/<id>/in
4. wait for one response
5. correlate it by id
6. unsubscribe when appropriate
```

The device itself does not create client-side timeout or retry policy.

## No retention

Neither request nor response is retained.

MQTTP is for command transactions, not state.

A late subscriber should not receive an old response from the broker as if it belonged to a new request.

## No device-side retry contract

The current protocol defines no device-side:

```text
retry
acknowledgement-of-acknowledgement
request persistence
duplicate suppression
idempotency registry
transaction log
```

If a client times out and retries a command, whether that command is safe to repeat depends on the command itself.

Clients should treat non-idempotent commands accordingly.

## Error responses

Parser errors and command failures still produce normal response payloads.

Examples include:

```text
Parse error: ...
Unknown INFO section.
Job not found.
Command 'X' unrecognized.
```

There is no generic MQTTP status-code envelope around those strings.

Where a command returns JSON containing its own error shape, that is the command's contract rather than an MQTTP-level contract.

## Ordinary console vs MQTTP

| Property | Ordinary console | MQTTP / controlled console |
|---|---|---|
| Request | `<device>/console/in` | `<device>/console/controlled/<id>/in` |
| Response | `<device>/console/out` | `<device>/console/controlled/<id>/out` |
| Correlation | shared output | caller-provided ID |
| Request retained | no | no |
| Response retained | no | no |
| Command grammar | NightMare command grammar | same |
| Built-ins | same | same |

## Broadcast console is not MQTTP

NightMare also accepts:

```text
all/console/in
```

Every subscribed device executes that command and responds at its own:

```text
<device>/console/out
```

There is no controlled ID in that path.

Broadcast console is therefore useful for fan-out commands, but it is not one correlated request/response transaction.

## Relationship to Resource Actions

Raw Resource Action MQTT still uses:

```text
<device>/resource/<action>/invoke
```

and remains fire-and-forget.

The `>` Resource-command adapter gives command transports a correlated path around **local** Resource execution. An MQTTP request such as:

```text
> identify invoke {"mode":"blink"}
```

executes a ManagedAction on the receiving device and returns its `ActionResult.result` on the matching controlled-console response topic.

This does not add a Resource result topic.

If the bound Action is Remote, the command publishes the normal `/invoke` request and can only report whether that publication was accepted. It still cannot report remote execution success. RemoteState writes have the same transport-acceptance limitation.

## Context on the device

Controlled-console requests are executed with MQTT command source context.

Internally, the source identifier is the derived response topic:

```text
<device>/console/controlled/<id>/out
```

This allows the MQTT command runner to publish the command result to the correct correlated topic.

The project command resolver itself currently receives `NightMareMessage`, not the full input `NightmareContext`, so applications should not depend on reading the controlled ID from the resolver API.

## What MQTTP guarantees

For the current implementation, MQTTP guarantees only the protocol mapping:

```text
valid controlled request topic
    -> run one NightMare command
    -> publish its response on the matching controlled output topic
```

It does not guarantee:

- remote business-operation success beyond what the command reports,
- exactly-once execution,
- retry,
- durable requests,
- durable responses,
- cross-device transactions.

Those behaviors must not be inferred from the presence of a request ID.
