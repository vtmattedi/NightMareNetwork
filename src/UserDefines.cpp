#include "UserDefines.h"
unsigned char get_var_by_num(unsigned char num)
{
    {
        switch (num)
        {
        case 0x01:
            return Identifier_0b0001;
            break;
        case 0x02:
            return Identifier_0b0010;
            break;
        case 0x03:
            return Identifier_0b0011;
            break;
        case 0x04:
            return Identifier_0b0100;
            break;
        case 0x05:
            return Identifier_0b0101;
            break;
        case 0x06:
            return Identifier_0b0110;
            break;
        case 0x07:
            return Identifier_0b0111;
            break;
        case 0x08:
            return Identifier_0b1000;
            break;
        case 0x09:
            return Identifier_0b1001;
            break;
        case 0x0A:
            return Identifier_0b1010;
            break;
        case 0x0B:
            return Identifier_0b1011;
            break;
        case 0x0C:
            return Identifier_0b1100;
            break;
        case 0x0D:
            return Identifier_0b1101;
            break;
        case 0x0E:
            return Identifier_0b1110;
            break;
        case 0x0F:
            return Identifier_0b1111;
            break;
        default:
            return 0x00;
            break;
        }
    }
}