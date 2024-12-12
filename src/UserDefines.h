#ifndef NIGHTMARE_USERDEFNIES
#define NIGHTMARE_USERDEFNIES

/*---------------------------------------------*/
/*CALLBACK INDENTIFIERS FOR IMPROVED READBILITY*/

static unsigned char Identifier_0b0001 = 0x01;
static unsigned char Identifier_0b0010 = 0x02;
static unsigned char Identifier_0b0011 = 0x03;
static unsigned char Identifier_0b0100 = 0x04;
static unsigned char Identifier_0b0101 = 0x05;
static unsigned char Identifier_0b0110 = 0x06;
static unsigned char Identifier_0b0111 = 0x07;
static unsigned char Identifier_0b1000 = 0x08;
static unsigned char Identifier_0b1001 = 0x09;
static unsigned char Identifier_0b1010 = 0x0A;
static unsigned char Identifier_0b1011 = 0x0B;
static unsigned char Identifier_0b1100 = 0x0C;
static unsigned char Identifier_0b1101 = 0x0D;
static unsigned char Identifier_0b1110 = 0x0E;
static unsigned char Identifier_0b1111 = 0x0F;

#define AC_BUTTON_POWER 0x01
#define AC_BUTTON_SET 0x02
#define AC_BUTTON_PLUS 0x03
#define AC_BUTTON_MINUS 0x04

#define COLOR_WHELL 0x01
#define COLOR_BUTTON_ON 0x02
#define COLOR_BUTTON_OFF 0x03
#define COLOR_BUTTON_AUTO 0x04

#define CONFIG_LED_ENABLE 0x01
#define CONFIG_RESET 0x02

#define AC_BUTTON_POWER_VAR Identifier_0b0001
#define AC_BUTTON_SET_VAR Identifier_0b0010
#define AC_BUTTON_PLUS_VAR Identifier_0b0011
#define AC_BUTTON_MINUS_VAR Identifier_0b0100
#define AC_BUTTON_SLEEP_IN_VAR Identifier_0b0101
#define AC_BUTTON_INFO_VAR Identifier_0b0110
#define AC_CONFIG_SYNC_VAR Identifier_0b0111
#define AC_BUTTON_SLEEP_VAR Identifier_0b1001



#define COLOR_WHELL_VAR Identifier_0b0001
#define COLOR_BUTTON_ON_VAR Identifier_0b0010
#define COLOR_BUTTON_OFF_VAR Identifier_0b0011
#define COLOR_BUTTON_AUTO_VAR Identifier_0b0100
#define COLOR_BUTTON_CONFIG_VAR Identifier_0b0101
#define COLOR_CONFIG_SPEED_VAR Identifier_0b0110
#define COLOR_CONFIG_BRIGHTNESS_VAR Identifier_0b0111
#define COLOR_CONFIG_MODE_ROLLER_VAR Identifier_0b1000
#define COLOR_MULTIPLE_VAR AC_BUTTON_INFO_VAR
// #define COLOR_MULTIPLE_VAR AC_BUTTON_INFO_VAR

// #define CONFIG_SAVE_VAR Identifier_0b0001
// #define CONFIG_BACK_VAR Identifier_0b0010
#define CONFIG_LED_ENABLE_VAR Identifier_0b0011
#define CONFIG_SAVESCREEN_VAR Identifier_0b0100
#define CONFIG_BRIGHTNESS_VAR Identifier_0b0101
#define CONFIG_RESTART_VAR Identifier_0b0110
#define CONFIG_TOPIC_MONITOR_VAR Identifier_0b0111
#define CONFIG_IGNORE_DOOR_VAR Identifier_0b1000
#define CONFIG_FACTORY_RESET_VAR Identifier_0b0001

#define MOVE_SAVE_VAR Identifier_0b0001
#define MOVE_BACK_VAR Identifier_0b0010
#define MOVE_LAST_VAR Identifier_0b0011
#define MOVE_NEXT_VAR Identifier_0b0100
#define MOVE_WAKEUP_VAR Identifier_0b0101
#define POPUP_OK_VAR Identifier_0b0110
#define POPUP_BACK_VAR Identifier_0b0111
#define POPUP_DEFOCUSED_VAR Identifier_0b1000

#define WC_CONNECT_VAR Identifier_0b0001
#define WC_REFRESH_VAR Identifier_0b0010
#define WC_PWRD_TA_FOCUSED_VAR Identifier_0b0011
#define WC_PWRD_TA_DEFOCUSED_VAR Identifier_0b0100

#define KB_DOOR_TIMER_VAR Identifier_0b0001
#define KB_AC_SHUTDOWN_HOUR_VAR Identifier_0b0010
#define KB_AC_SHUTDOWN_MIN_VAR Identifier_0b0011

#define MENU_AC_VAR Identifier_0b0001
#define MENU_BEDLIGHTS_VAR Identifier_0b0010
#define MENU_AUTO_VAR Identifier_0b0011
#define MENU_SHUTDOWNALL_VAR Identifier_0b0100
#define MENU_SETTINGS_VAR Identifier_0b0101
#define MENU_UTILITIES_VAR Identifier_0b0110
#define MENU_LIGHT_VAR Identifier_0b0111



#define ROTATION_PORTRAIT 0
#define ROTATION_LANDSCAPE 1
#define ROTATION_PORTRAIT_INV 2
#define ROTATION_LANDSCAPE_INV 3

#define ROTATION ROTATION_PORTRAIT
/*---------------------------------------------*/
/*-------COLORS DEFINITIONS--------------------*/
#define COLOR_GREY_DISABLE
#define COLOR_DEFAULT_BLUE 0x2196F7
#define COLOR_LIGHT_BLUE 0x00FFCC
#define COLOR_DARK_RED 0x8B0000
#define COLOR_RED 0xFF0000
#define COLOR_LIGHT_GREEN 0x08A11A
#define COLOR_WHITE 0xFFFFFF
#define COLOR_BLACK 0x000000
#define COLOR_GREY 0x808080
#define COLOR_DARK_YELLOW 0x8B8000
#define COLOR_DARK_GREEN 0x008B00
#define COLOR_YELLOW 0xDDDD00

/*---------------------------------------------*/

#endif