
/*----------------------------------------------------------*/
///
///@file Timers.h -
/// Implements Timers for the NightMare Network.
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.1
///         Structure of the lib changed.
/*----------------------------------------------------------*/

#pragma once
#include <Modules.config.h>
#ifdef COMPILE_TIMERS
#include <Arduino.h>
#include <TimeLib.h>
#define HOUR 3600
#define MINUTE 60

#define USE_MS true
#define USE_SECONDS false
/// Max of tasks handled by the Timer Handler
#define TIMER_MAX_TASKS 20

/// @brief Struct to a single timer task
struct TimerTask
{
public:
  /// @brief The label of the task.
  String label = "unused";
  /// @brief Flag to enable/disable.
  bool enable = false;
  /// @brief Flag to use millis/seconds.
  bool use_millis = false;
  /// @brief Interval between callbacks.
  uint16_t interval = 100;
  /// @brief Last time the callback was called.
  uint32_t last_time = 0;
  /// @brief Flag to indicate if the task is timeout (will self delete after one trigger).
  bool is_timeout = false;
  /// @brief The pointer to the callback function.
  void (*callback)() = NULL;
  void run();
  void reset();
  uint16_t timeLeft();
};

/// @brief Struct to finda
struct indexresult
{
  /// @brief Indicates that the array is full.
  bool full = false;
  /// @brief Indicates that a task was found.
  bool found = false;
  /// @brief the index of the found or the next free index.
  uint8_t index = 0;
};

/// @brief Class to handle multiple Timers
class TimersHandler
{
private:
  unsigned int timeout_index = 0;

public:
  /// @brief Flag to print debug info
  bool debug = false;
  /// @brief Array holding all the individual tasks
  TimerTask _tasks[TIMER_MAX_TASKS];
  bool create(String label, uint16_t interval, void (*callback)(void), bool use_millis = false);
  indexresult getIndex(String label);
  indexresult getIndex();
  String setTimeout(void (*callback)(void), uint16_t interval, bool use_millis = false);
  bool cancelTimeout(String label);
  void run();
  String timeleft(String label);
  bool deleteTask(String label);
};

extern TimersHandler Timers;
#endif