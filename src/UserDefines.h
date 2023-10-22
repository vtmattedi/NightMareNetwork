/*---------------------------------------------*/
/*CALLBACK INDENTIFIERS FOR IMPROVED READBILITY*/
static unsigned char Identifier_0b0000 = 0x01;
static unsigned char Identifier_0b0001 = 0x01;
static unsigned char Identifier_0b0010 = 0x02;
static unsigned char Identifier_0b0011 = 0x03;
static unsigned char Identifier_0b0100 = 0x04;
static unsigned char Identifier_0b0101 = 0x05;

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
#define COLOR_WHELL_VAR Identifier_0b0001
#define COLOR_BUTTON_ON_VAR Identifier_0b0010
#define COLOR_BUTTON_OFF_VAR Identifier_0b0011
#define COLOR_BUTTON_AUTO_VAR Identifier_0b0100
#define CONFIG_LED_ENABLE_VAR Identifier_0b0001
#define CONFIG_RESET_VAR Identifier_0b0010

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
#define COLOR_LIGHT_GREEN 0x08a11a
#define COLOR_WHITE 0xFFFFFF
#define COLOR_GREY 0x808080

/*---------------------------------------------*/