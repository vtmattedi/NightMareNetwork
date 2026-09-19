#pragma once
#include <Arduino.h>

#ifndef NIGHTMARE_MAX_JOBS
#define NIGHTMARE_MAX_JOBS 16
#endif

namespace NightMare {

enum class JobClock : uint8_t { MONOTONIC, WALL_CLOCK };

class Scheduler {
public:
    using JobHandler = void (*)(void* context);
    struct Job {
        const char* id = nullptr;
        JobClock clock = JobClock::MONOTONIC;
        uint32_t intervalMs = 0;
        uint32_t nextMs = 0;
        uint32_t lastDay = 0;
        uint32_t secondOfDay = 0;
        JobHandler handler = nullptr;
        void* context = nullptr;
        bool repeating = false;
        bool initialized = false;
    };
    bool after(const char* id, uint32_t delayMs, JobHandler handler, void* context = nullptr);
    bool every(const char* id, uint32_t intervalMs, JobHandler handler, void* context = nullptr);
    bool dailyAt(const char* id, uint8_t hourUtc, uint8_t minuteUtc,
                 JobHandler handler, void* context = nullptr);
    bool cancel(const char* id);
    void tick(uint32_t nowMs = millis());
    const Job* jobs() const { return _jobs; }
private:
    bool insert(Job job);
    Job _jobs[NIGHTMARE_MAX_JOBS];
};

} // namespace NightMare
