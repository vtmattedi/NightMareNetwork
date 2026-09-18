---
title: Server variables
description: A value mirrored between a client and the device that owns it — optimistic on change, asserted after a delay, rolled back if the device never agreed, stale after silence.
section: modules
order: 50
---

# ServerVariable\<T\> — `Core/ServerVariables.h`

The mechanism under every [Service](/docs/modules/services). A
`ServerVariable<T>` holds a value the *device* owns and the *client* wants to
change. It gives a client three behaviours for free:

- **optimistic** — `change()` updates the local value immediately and
  publishes, so a UI flips at the tap rather than after a round trip;
- **asserted** — `ASSERT_DELAY` (10 s) later, `sync()` compares the local value
  with what the device last reported; if they differ, the local value is
  rolled back and `on_assert_result(false)` fires, so a command that went
  nowhere is visible instead of silently lost;
- **stale** — no report for `MILLIS_TO_STALE` (10 min) sets `stale`, which a
  UI shows as `?` rather than as "off".

Compiled with `COMPILE_SERVERVARIABLES`, plus one `USE_<TYPE>_TEMPLATE` switch
per instantiated type (`int`, `uint32_t`, `uint16_t`, `uint8_t`, `double`,
`bool`, `String`). The template is explicitly instantiated in the `.cpp`, so a
type without its switch is a link error.

## API

```cpp
ServerVariable<int> temp("Ac Temp");     // label is for debug prints

temp.change(24);            // client side: set, publish, schedule the assert
temp.force(24);             // set without publishing; assert delayed 4x
temp.handleServer(23);      // device reported a value
temp.sync();                // from loop(): stale check, assert, delayed send

temp.value;                 // current (client-visible) value
temp.stale;                 // no report within millis_to_stale
```

Callbacks, all optional:

| hook | fires when |
| --- | --- |
| `on_send(T)` | the client changed the value and it should be published |
| `on_send_with_info(uint8_t userid, String value, String userinfo)` | same, with the variable's `userid` and `userinfo` — how a Service routes several variables through one send function |
| `on_value_changed()` | the value changed, from either side, or went stale |
| `on_assert_result(bool)` | an assert completed; `false` means the device disagreed and the value was rolled back |

`millis_to_delay_before_send` batches rapid changes: a setpoint stepped five
times in a second publishes once, 500 ms after the last step.

## How a Service uses it

```cpp
AcTarget.userid = ACTARGET_ID;
AcTarget.userinfo = hostname;                 // which device
AcTarget.on_send_with_info = AcControllerSendById;   // formats "TARGET <v>" to <hostname>/console/in
AcTarget.millis_to_delay_before_send = 500;
```

`ParseServerState()` calls `handleServer()` on each variable with the field
from the device's `state` document; `Sync()` calls `sync()` on each from
`loop()`.

## Feeding one by hand

A `ServerVariable` does not need a `state` document. The Dashboard's plain
light reads its state from a *sensor reading* and still gets all three
behaviours: readings are pushed in with `handleServer()` from the sensor
ingest path, a tap calls `change()` and publishes the command. Staleness is
then measured from when a reading actually arrived, which is what it should
mean.

## Semantics worth knowing

- `handleServer()` while an assert is pending does **not** overwrite the local
  value — the client's change is given its 10 s to land. It updates the server
  copy the assert will compare against.
- `force()` is for corrections the client knows are true (a manual sync);
  it publishes nothing and pushes the assert out to 40 s.
- Rollback is silent to the value's consumers except through
  `on_value_changed` / `on_assert_result`. A UI that wants to say "that did not
  take" hooks the latter.
