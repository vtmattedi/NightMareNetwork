
/*----------------------------------------------------------*/
///
///@file ServerVariables.h -
/// Implements a server variable.
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.1
///         Structure of the lib changed.
/*----------------------------------------------------------*/


#ifndef NIGHTMARE_SERVERVARIABLES
#define NIGHTMARE_SERVERVARIABLES
#include <Modules.config.h>
#include <Arduino.h>
#include <TimeLib.h>



#define MIN_TO_MS 60 * 1000

/// Default time (in ms) between asserts between client and server
#define ASSERT_DELAY 10000
#define MILLIS_TO_STALE 10 * MIN_TO_MS

/// @brief class to store and sync a varible between one or more
/// clients and a server. It works by periodically recieving info
/// from the server, after a change on the client side we wait for
/// a period of `ASSERT_DELAY` ms an then we assert if the change
/// on the client side has been reflected on the server. If not we
/// refer back to server value. Also it goes stale after
/// `MILLIS_TO_STALE` ms. The class also have some function pointers
/// that if not not NULL will be called when the value has changed or
/// when client value have changed and therefore should be sent to
/// server
/// @tparam T The type of variable stored and synced.
template <typename T>
class ServerVariable
{
public:
  /// @brief The Value of the Variable
  T value;
  /// @brief Millis to delay between the value have been changed by
  /// the client side and the function on_send be called. only appliable
  /// if >0
  uint16_t millis_to_delay_before_send = 0;
  /// @brief Time in ms between server responses to be considered stale
  uint32_t millis_to_stale = MILLIS_TO_STALE; // 10 mins
  /// function called when and server assert is made.
  void (*on_assert_result)(bool) = NULL;
  /// @brief function called when client have changed the value
  void (*on_send)(T) = NULL;
  /// @brief function called when client have changed the value
  /// instead of passing a value on its original form, passes it 
  /// as a string and also some user data
  void (*on_send_with_info)(uint8_t, String, String) = NULL;
  /// @brief Called when the value has been changed either by then server
  /// or by the client
  void (*on_value_changed)() = NULL;
  /// @brief Flags wheather or not the variable is stale
  bool stale = true;
  /// @brief Flag to print some debug info
  bool debug = false;
  /// @brief Label used mainly for debug identification
  const char *label = (char *)"no label";
  /// @brief user info to be passed by `on_send_with_info`;
  uint8_t userid = 0;
  /// @brief user info to be passed by `on_send_with_info`;
  const char* userinfo = "";

private:
  /// @brief Server value of the variable
  T serverValue;
  /// @brief Flags wheather or not the assert has been handled
  bool assert_handled = true;
  /// @brief Flags wheather or not the send has been handled
  bool send_handled = true;
  /// @brief Millis() timestamp to next assert
  uint32_t millis_to_assert_server = ASSERT_DELAY;
  /// @brief Millis() timestamp to send the value
  uint32_t millis_to_send = 0;
  /// @brief Millis() timestamp of last value received
  uint32_t millis_last_server_received = 0;
  /// @brief calls the on send function if they are not NULL
  void sendChanges();

public:
  ServerVariable(T initialValue);
  ServerVariable();
  ServerVariable(const char *name);
  void handleServer(T newServerValue);
  void sync();
  void change(T newValue);
  void force(T newValue, bool skip_send = false);

private:
  void onValueChanged();
};

#endif