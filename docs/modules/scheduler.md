---
title: Scheduler
description: Schedule command and callback work using wall or monotonic time.
section: modules
order: 30
---

# Scheduler

The NightMare Scheduler uses one timing engine for both operator command jobs and direct C++ callbacks.

The public singleton is:

```cpp
gScheduler
```

A job has independent dimensions:

```text
clock
    Wall
    Monotonic

repeat
    one-shot
    recurring

target
    String command
    callback

scope
    MANAGED
    USER
```

The Scheduler itself can run in:

```text
TASK
MANUAL
```

## Capacity

The current maximum number of jobs is:

```cpp
Scheduler::MaxJobs == 30
```

IDs are global within the Scheduler and begin at 1.

Labels are unique **within a scope**.

This means these can coexist:

```text
MANAGED  nm.telemetry.system
USER     nm.telemetry.system
```

## MANAGED jobs

C++ jobs are MANAGED by default.

Examples:

```cpp
gScheduler.after("startup-check", callback, 5000);

gScheduler.everyMonotonic(
    "sensor-poll",
    callback,
    1000);
```

Framework jobs also use MANAGED scope.

Current examples include telemetry publication and pending identity-cleanup retry.

## USER jobs

USER jobs are created by the `JOB` command family.

Examples:

```text
JOB AFTER once 5000 "PING"
JOB EVERY hourly WALL 3600 "INFO PUBLISH"
```

The command surface can only see USER jobs.

Therefore:

```text
JOB LIST
JOB DELETE
JOB CLEAR
```

cannot enumerate or remove MANAGED jobs.

## Command jobs

A command job stores a String and executes it through:

```cpp
handleNightMareCommand(...)
```

with command source:

```text
NM_CMD_SRC_JOB
```

Example:

```cpp
gScheduler.after(
    "reconnect",
    "MQTT CONNECT REMOTE",
    5000);
```

C++ command jobs default to MANAGED scope.

An explicit scope may be supplied:

```cpp
gScheduler.after(
    "user-like-job",
    "PING",
    5000,
    SchedulerJobScope::USER);
```

Normally only the JOB command implementation should create USER jobs.

## Callback jobs

A callback is:

```cpp
using SchedulerCallback = void (*)();
```

Supported:

```cpp
void poll()
{
    // ...
}

gScheduler.timer("poll", poll, 1000);
```

and non-capturing lambdas:

```cpp
gScheduler.timer(
    "poll",
    []() {
        // ...
    },
    1000);
```

Not supported:

```cpp
int value = 1;

gScheduler.timer(
    "poll",
    [value]() {
        // capturing lambda: not a function pointer
    },
    1000);
```

Callback jobs are always MANAGED.

## Exactly one execution target

Every job must contain exactly one of:

```text
command
callback
```

Never both.

Never neither.

The Scheduler rejects an invalid target configuration.

## Wall clock

Wall-clock deadlines use Unix seconds.

Use:

```cpp
gScheduler.atWall(label, command, epochSeconds);
gScheduler.atWall(label, callback, epochSeconds);
```

Example:

```cpp
gScheduler.atWall(
    "midnight-task",
    callback,
    1790000000);
```

Wall jobs do not execute until NightMare wall time is valid.

## One-shot monotonic delay

Use:

```cpp
gScheduler.after(label, command, delayMs);
gScheduler.after(label, callback, delayMs);
```

Example:

```cpp
gScheduler.after(
    "startup-check",
    callback,
    5000);
```

The deadline is calculated from the current `millis()` value.

## Recurring wall interval

Use:

```cpp
gScheduler.everyWall(label, command, intervalSeconds);
gScheduler.everyWall(label, callback, intervalSeconds);
```

Example:

```cpp
gScheduler.everyWall(
    "hourly",
    callback,
    3600);
```

The interval is in seconds.

Wall recurring jobs require valid wall time before their first due time can be established/executed.

## Recurring monotonic interval

Use:

```cpp
gScheduler.everyMonotonic(label, command, intervalMs);
gScheduler.everyMonotonic(label, callback, intervalMs);
```

Example:

```cpp
gScheduler.everyMonotonic(
    "poll",
    callback,
    1000);
```

The interval is in milliseconds.

## `timer()`

`timer()` is the convenience form for a recurring monotonic callback:

```cpp
gScheduler.timer(
    "poll",
    callback,
    1000);
```

Its first execution is one interval from the time it is scheduled.

## `setTimeout()`

`setTimeout()` creates a one-shot monotonic callback with an internally generated label:

```cpp
gScheduler.setTimeout(callback, 5000);
```

Use an explicitly labeled `after()` job when later removal by a stable label matters.

## Persistence

Only jobs satisfying all of these persist:

```text
active
Wall clock
String command target
```

Therefore:

| Job | Persists |
|---|---:|
| `atWall(..., command, ...)` | yes |
| `everyWall(..., command, ...)` | yes |
| `after(..., command, ...)` | no |
| `everyMonotonic(..., command, ...)` | no |
| any callback job | no |

This is intentional.

A function pointer is runtime firmware state.

A monotonic deadline only has meaning within the current boot.

## Persistence file

Persisted jobs are stored in:

```text
/jobs.json
```

Each persisted job includes its scope.

Entries without a valid current scope are not imported through a backwards-compatibility path.

## Storage availability

The Scheduler initializes persistent storage through `PersistentSettings`.

A persisted job cannot be added while Scheduler storage is unavailable.

Runtime-only jobs can still be represented independently of persistent wall-job storage.

The Scheduler retries storage initialization periodically.

## Run mode

Start the Scheduler explicitly with:

```cpp
gScheduler.begin(SchedulerRunMode::TASK);
```

or:

```cpp
gScheduler.begin(SchedulerRunMode::MANUAL);
```

The first successful `begin()` fixes the selected mode.

A later `begin()` requesting the other mode fails instead of silently changing execution ownership.

Calling `begin()` again with the same mode is allowed.

## TASK mode

In TASK mode the Scheduler creates its own FreeRTOS task.

Current defaults are:

```text
poll interval:   100 ms
stack size:      8192 bytes
priority:        1
```

The task repeatedly calls:

```cpp
gScheduler.tick();
```

Applications using `startNightMareESP()` normally do not need to manage this directly.

## MANUAL mode

In MANUAL mode no Scheduler task is created.

The application must service:

```cpp
gScheduler.tick();
```

The standard framework lifecycle already does this from:

```cpp
tickNightMareESP();
```

when:

```cpp
NM_SCHEDULER_OWN_TASK == 0
```

## Direct `tick()`

`tick()` is public and can also be used directly for cooperative/manual execution.

The Scheduler uses a recursive mutex plus a `dispatching_` guard to prevent overlapping dispatch inside the Scheduler itself.

Choosing TASK mode and then also manually driving `tick()` is application-level misuse; the standard `tickNightMareESP()` path avoids that by only ticking in MANUAL mode.

## Dispatch behavior

At each tick, the Scheduler snapshots the highest job ID that existed when dispatch began.

Jobs created by a running callback/command are therefore not immediately executed in the same dispatch pass.

Before executing a due target, the Scheduler updates/removes the job state.

Then it temporarily releases the job lock while the command/callback runs.

This allows a callback to interact with the Scheduler without deadlocking the job table.

## One-shot persisted jobs

For a persisted one-shot wall command, removal from persisted storage happens before the target is executed.

If saving that removal fails, the live job is restored and execution is skipped so the Scheduler does not claim a persisted job was consumed when the storage record still says otherwise.

## Recurring scheduling

After a recurring job runs, the next deadline is based on the current tick time:

```text
Wall:
    wallNow + interval

Monotonic:
    monotonicNow + interval
```

The Scheduler does not attempt to replay every missed interval after a long pause.

## Remove

Remove a MANAGED job by label:

```cpp
gScheduler.remove("sensor-poll");
```

or ID:

```cpp
gScheduler.remove(id);
```

The default scope is MANAGED.

Remove a USER job explicitly:

```cpp
gScheduler.remove(
    id,
    SchedulerJobScope::USER);
```

For persisted jobs, removal is transactional: if saving the new persisted list fails, the job is restored.

## Clear

Clear one scope:

```cpp
gScheduler.clear(SchedulerJobScope::USER);
```

or:

```cpp
gScheduler.clear(SchedulerJobScope::MANAGED);
```

Clearing one scope preserves persisted jobs from the other scope.

`JOB CLEAR` always uses USER.

## List

```cpp
String json =
    gScheduler.list(SchedulerJobScope::USER);
```

Listing is scope-specific.

The returned JSON describes active jobs and current clock state.

Callback jobs are reported as callback targets; function-pointer addresses are not part of the public listing.

## JOB command mapping

The operator command family maps to Scheduler calls as follows:

```text
JOB AT
    wall one-shot String command

JOB AFTER
    monotonic one-shot String command

JOB EVERY ... WALL
    wall recurring String command

JOB EVERY ... MONO
    monotonic recurring String command

JOB LIST
    USER list

JOB DELETE
    USER remove

JOB CLEAR
    USER clear
```

See [Commands](../protocols/commands.md) for exact command syntax.
