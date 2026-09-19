#include <NightMare/Runtime/Scheduler.h>
#include <NightMare/Core/Time.h>
#include <string.h>

namespace NightMare {

bool Scheduler::insert(Job job) {
    if (!job.id || !*job.id || !job.handler) return false;
    for (const auto& existing : _jobs)
        if (existing.id && strcmp(existing.id, job.id) == 0) return false;
    for (auto& existing : _jobs)
        if (!existing.id) { existing = job; return true; }
    return false;
}
bool Scheduler::after(const char* id, uint32_t delayMs, JobHandler handler, void* context) {
    if (delayMs > INT32_MAX) return false;
    Job job;
    job.id = id; job.nextMs = millis() + delayMs; job.handler = handler; job.context = context;
    return insert(job);
}
bool Scheduler::every(const char* id, uint32_t intervalMs, JobHandler handler, void* context) {
    if (!intervalMs || intervalMs > INT32_MAX) return false;
    Job job;
    job.id = id; job.intervalMs = intervalMs; job.nextMs = millis() + intervalMs;
    job.handler = handler; job.context = context; job.repeating = true;
    return insert(job);
}
bool Scheduler::dailyAt(const char* id, uint8_t hourUtc, uint8_t minuteUtc,
                        JobHandler handler, void* context) {
    if (hourUtc > 23 || minuteUtc > 59) return false;
    Job job;
    job.id = id; job.clock = JobClock::WALL_CLOCK;
    job.secondOfDay = hourUtc * 3600UL + minuteUtc * 60UL;
    job.handler = handler; job.context = context; job.repeating = true;
    return insert(job);
}
bool Scheduler::cancel(const char* id) {
    for (auto& job : _jobs) {
        if (job.id && strcmp(job.id, id) == 0) { job = {}; return true; }
    }
    return false;
}
void Scheduler::tick(uint32_t nowMs) {
    for (auto& job : _jobs) {
        if (!job.id) continue;
        if (job.clock == JobClock::MONOTONIC) {
            if (static_cast<int32_t>(nowMs - job.nextMs) < 0) continue;
            auto handler = job.handler;
            void* context = job.context;
            if (job.repeating) job.nextMs = nowMs + job.intervalMs;
            else job = {};
            handler(context);
        } else {
            if (!Time::valid()) continue;
            uint32_t epoch = static_cast<uint32_t>(Time::now());
            uint32_t day = epoch / 86400UL;
            uint32_t seconds = epoch % 86400UL;
            if (!job.initialized) {
                job.lastDay = seconds >= job.secondOfDay ? day : day - 1;
                job.initialized = true;
            }
            if (seconds >= job.secondOfDay && job.lastDay != day) {
                job.lastDay = day;
                job.handler(job.context);
            }
        }
    }
}

} // namespace NightMare
