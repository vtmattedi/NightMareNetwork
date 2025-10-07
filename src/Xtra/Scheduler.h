#pragma once
#include <Arduino.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#define MAX_SCHEDULER_TASKS 10
struct SchedulerTask
{
    u_int32_t id;
    bool repeat = false;
    uint32_t interval = 0;
    bool armed = false;
    String command;
    uint32_t executionTime;
};

// List implementation
// struct node {
//     SchedulerTask task;
//     node* next;
// };

// class List
// {
// private:
//     node* head;
// public:
//     List() : head(nullptr) {}
//     void addTask(SchedulerTask task);
//     SchedulerTask* getTask(uint16_t id);
//     void removeTask(uint16_t id);
// };
// void List::addTask(SchedulerTask task) {
//     node* newNode = new node();
//     newNode->task = task;
//     newNode->next = nullptr;
//     node* current = head;
//     while (current && current->next)
//     {
//         current = current->next;
//     }
//     if (current) {
//         current->next = newNode;
//     } else {
//         head = newNode;
//     }
// }

// void List::removeTask(uint16_t id) {
//     node* current = head;
//     node* previous = nullptr;
//     while (current) {
//         if (current->task.id == id) {
//             if (previous) {
//                 previous->next = current->next;
//             } else {
//                 head = current->next;
//             }
//             delete current;
//             return;
//         }
//         previous = current;
//         current = current->next;
//     }
// }

// SchedulerTask* List::getTask(uint16_t id) {
//     node* current = head;
//     while (current) {
//         if (current->task.id == id) {
//             return &current->task;
//         }
//         current = current->next;
//     }
//     return nullptr;
// }


// Usage:
// Scheduler scheduler;
// on setup():
//  - scheduler.onCommand(your_function_to_run_commands);
// on loop():
//  - scheduler.run();

class Scheduler
{
    private:
    SchedulerTask tasks[MAX_SCHEDULER_TASKS];
    uint8_t currentTasks = 0;
    uint8_t nextTaskIndex = 0;
    void (*runCmd)(String cmd);
public:
    Scheduler();
    void onCommand(void (*runCommand)(String cmd));
    uint32_t add(String cmd, uint32_t timestamp);
    void run();
    void clear();
    SchedulerTask* getByID(uint16_t id);
    bool killByID(uint16_t id);
    String listTasks();
};

extern Scheduler scheduler;

