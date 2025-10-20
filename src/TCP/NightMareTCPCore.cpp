#include "NightMareTCPCore.h"

// Prepare the msg according to the transmission mode
String PrepareMsg(String msg, TransmissionMode mode)
{
  if (mode == TransmissionMode::AllAvailable)
    return msg; // SendMsg
  else if (mode == TransmissionMode::SizeColon)
  {
    String r_msg = "";
    r_msg += msg.length();
    r_msg += ":";
    r_msg += msg;
    return r_msg;
  }
  else if (mode == TransmissionMode::ThreeCharHeaders)
  {
    // not implemented;
  }
  return msg;
}


int log_f(bool _debug, const char *format, ...)
{
  if (!_debug)
    return 0;
  char loc_buf[64];
  char *temp = loc_buf;
  va_list arg;
  va_list copy;
  va_start(arg, format);
  va_copy(copy, arg);
  int len = vsnprintf(temp, sizeof(loc_buf), format, copy);
  va_end(copy);
  if (len < 0)
  {
    va_end(arg);
    return 0;
  }
  if (len >= (int)sizeof(loc_buf))
  { // comparation of same sign type for the compiler
    temp = (char *)malloc(len + 1);
    if (temp == NULL)
    {
      va_end(arg);
      return 0;
    }
    len = vsnprintf(temp, len + 1, format, arg);
  }
  va_end(arg);
  len = Serial.write((uint8_t *)temp, len);
  if (temp != loc_buf)
  {
    free(temp);
  }
  return len;
}