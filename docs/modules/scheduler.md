---
title: Scheduler
description: Persisted wall-clock tasks that fire console commands — the network's cron, editable from the console, re-aligned when the clock syncs.
section: modules
order: 40
---

# Scheduler — `Xtra/Scheduler.h`

A task is a **command string** and a time to run it, with an optional repeat
interval. When the time comes the string goes through the command resolver
exactly as if it had been typed on the console. Tasks persist in
`/scheduleTasks.json` and survive reboots. Compiled with `COMPILE_SCHEDULER`;
`USE_NIGHTMARE_COMMAND` routes the fired command through the resolver, which
is what every device wants.

This is the tool for "every day at 05:30". A `Timers` callback comparing the
time every minute does the same job with no persistence, no console, and no
way to see what is armed.

## API

```cpp
extern Scheduler scheduler;

void    scheduler.onCommand(void (*run)(String cmd));       // set by the resolver with USE_NIGHTMARE_COMMAND
void    scheduler.onLogResult(void (*log)(NightMareResults));
int32_t scheduler.addTask(String label, String cmd, uint32_t interval_seconds, uint32_t executionTime,
                          bool repeat = false, bool skipSave = false);
void    scheduler.run();                                    // from loop()

SchedulerTask *scheduler.getByLabel(String label);
SchedulerTask *scheduler.getByID(uint16_t id);
bool    scheduler.deleteTask(uint16_t id);
bool    scheduler.deleteTaskByLabel(String label);
bool    scheduler.killByID(uint16_t id);                    // disarm without deleting
bool    scheduler.taskExists(String label);
String  scheduler.listTasks(bool onlyPersistent = false);
void    scheduler.clear();
```

- `executionTime` is an epoch timestamp (`now()` from TimeLib). With
  `SCHEDULER_USE_MILLIS` it is `millis()` instead, for devices with no time
  sync.
- `repeat` with `interval_seconds` re-arms after each run: `86400` is daily.
- `MAX_SCHEDULER_TASKS` is 10.
- A task's `id` is assigned at creation and stable across reboots; `label` is
  the human handle.

```cpp
void startAcController()
{
    if (!scheduler.taskExists("ac_morning_off"))
        scheduler.addTask("ac_morning_off", "AC MORNINGOFF", 86400,
                          timestampOfNextOccurrence("05:30"), true);   // Core/Misc.h
}
```

## Time sync

A task armed before the clock is synced holds an execution time in the wrong
epoch. `onSync(oldTime)` runs when the time arrives and shifts every task by
the difference; `executionTimeSynced` marks the ones already adjusted. A device
with no time sync should use `SCHEDULER_USE_MILLIS` and accept that tasks
restart from zero on reboot.

## Console

```
TASK <label> <command> <interval_s> <execution_time>   schedule; repeats every interval_s
SCHEDULER LIST [-t]                                     every task, or only persisted ones
SCHEDULER KILL <id>                                     disarm
SCHEDULER CLEAR                                         delete all
```

`SCHEDULER ADD` and `SCHEDULER EDIT` are accepted but do nothing yet — the
branches exist in the resolver and are empty. `TASK` is the working command.

Because the command is itself an argument, quoting has to survive the trip
through the parser and back. Fences make it readable:

```
TASK morning `AC MORNINGOFF` 86400 1758182200
```

A nested command costs one more backtick per level — see the grammar in
[Commands](/docs/protocols/commands).

## Policy in the command, time in the scheduler

The pattern that keeps a controller honest: the scheduler knows *when*; the
command knows *what to do about it*. An `AC MORNINGOFF` command that checks
whether the AC is on and whether tonight's sleep-in was armed is testable from
the console at any hour, and `SCHEDULER LIST` shows an operator exactly what
will happen and when — neither of which is true of a time comparison buried in
a timer callback.
