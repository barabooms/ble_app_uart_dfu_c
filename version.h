#ifndef VERSION_H
#define VERSION_H

#pragma once

/**----- Макросы -----------------------------------------------------------------------------------------*/
// __DATE__ "Jan 27 2012"
//           01234567890

// __TIME__ "21:06:19"
//           01234567

#define VERSION_Y0 (__DATE__[ 7] - '0')
#define VERSION_Y1 (__DATE__[ 8] - '0')
#define VERSION_Y2 (__DATE__[ 9] - '0')
#define VERSION_Y3 (__DATE__[10] - '0')

#define VERSION_M_JAN (__DATE__[0] == 'J' && __DATE__[1] == 'a' && __DATE__[2] == 'n')
#define VERSION_M_FEB (__DATE__[0] == 'F')
#define VERSION_M_MAR (__DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'r')
#define VERSION_M_APR (__DATE__[0] == 'A' && __DATE__[1] == 'p')
#define VERSION_M_MAY (__DATE__[0] == 'M' && __DATE__[1] == 'a' && __DATE__[2] == 'y')
#define VERSION_M_JUN (__DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'n')
#define VERSION_M_JUL (__DATE__[0] == 'J' && __DATE__[1] == 'u' && __DATE__[2] == 'l')
#define VERSION_M_AUG (__DATE__[0] == 'A' && __DATE__[1] == 'u')
#define VERSION_M_SEP (__DATE__[0] == 'S')
#define VERSION_M_OCT (__DATE__[0] == 'O')
#define VERSION_M_NOV (__DATE__[0] == 'N')
#define VERSION_M_DEC (__DATE__[0] == 'D')

#define VERSION_M0 ((VERSION_M_OCT || VERSION_M_NOV || VERSION_M_DEC) ? 1 : 0)
#define VERSION_M1 \
    ( \
        (VERSION_M_JAN) ? 1 : \
        (VERSION_M_FEB) ? 2 : \
        (VERSION_M_MAR) ? 3 : \
        (VERSION_M_APR) ? 4 : \
        (VERSION_M_MAY) ? 5 : \
        (VERSION_M_JUN) ? 6 : \
        (VERSION_M_JUL) ? 7 : \
        (VERSION_M_AUG) ? 8 : \
        (VERSION_M_SEP) ? 9 : \
        (VERSION_M_OCT) ? 0 : \
        (VERSION_M_NOV) ? 1 : \
        (VERSION_M_DEC) ? 2 : \
                          0   \
    )

#define VERSION_D0 ((__DATE__[4] >= '0') ? (__DATE__[4] - '0') : 0)
#define VERSION_D1 (__DATE__[ 5] - '0')

#define VERSION_HOUR0 (__TIME__[0] - '0')
#define VERSION_HOUR1 (__TIME__[1] - '0')

#define VERSION_MIN0 (__TIME__[3] - '0')
#define VERSION_MIN1 (__TIME__[4] - '0')

#define VERSION_SEC0 (__TIME__[6] - '0')
#define VERSION_SEC1 (__TIME__[7] - '0')

#define VERSION_DAY       (10U * VERSION_D0 + VERSION_D1)
#define VERSION_MONTH     (10U * VERSION_M0 + VERSION_M1)
#define VERSION_YEAR      (10U * VERSION_Y2 + VERSION_Y3)

#define VERSION_HOUR      (10U * VERSION_HOUR0 + VERSION_HOUR1)
#define VERSION_MIN       (10U * VERSION_MIN0 + VERSION_MIN1)
#define VERSION_SEC       (10U * VERSION_SEC0 + VERSION_SEC1)

#define VERSION_BCD_DAY   (16U * VERSION_D0 + VERSION_D1)
#define VERSION_BCD_MONTH (16U * VERSION_M0 + VERSION_M1)
#define VERSION_BCD_YEAR  (16U * VERSION_Y2 + VERSION_Y3)

#define VERSION_BCD_HOUR  (16U * VERSION_HOUR0 + VERSION_HOUR1)
#define VERSION_BCD_MIN   (16U * VERSION_MIN0 + VERSION_MIN1)
#define VERSION_BCD_SEC   (16U * VERSION_SEC0 + VERSION_SEC1)

/** 
Формат данных 0xDDMYYmmm:
    DD - День = 0x00..0x1F
    M - месяц = 0x0..0xC
    YY - Год = 0x00..0x63
    mmm - минуты = часы * минуты = 000...5A0
*/
#define VERSION_INT     ( VERSION_DAY << 24U | VERSION_MONTH << 20U | VERSION_YEAR << 12U | (VERSION_HOUR * VERSION_MIN) ) 

/**----- Проверка макросов -------------------------------------------------------------------------------*/

/**----- КОНЕЦ ФАЙЛА -------------------------------------------------------------------------------------*/
#endif
