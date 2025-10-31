#include "newmodule.example.h"
#ifdef COMPILE_NEW_MODULE

#ifdef COMPILE_SERIAL
#define NEWMODULE_LOGF(fmt, ...) Serial.printf("%s %s " fmt "\n", MILLIS_LOG, NEW_MODULE_LOG, ##__VA_ARGS__)
#define NEWMODULE_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_LOG, NEW_MODULE_LOG, ##__VA_ARGS__)
#else
#define NEWMODULE_LOGF(fmt, ...)
#define NEWMODULE_ERRORF(fmt, ...)
#endif
// Module code goes here
#endif