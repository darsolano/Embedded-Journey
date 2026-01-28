#ifndef _RTC_DEFINED
#define _RTC_DEFINED

#include <stdint.h>
#include "integer.h"
#include "time.h"

#define NETWORK_PRESENT

typedef struct {
	WORD	year;		/* 1..4095 */
	BYTE	month;		/* 1..12 */
	BYTE	mday;		/* 1.. 31 */
	BYTE	wday;		/* 1..7 */
	BYTE	hour;		/* 0..23 */
	BYTE	min;		/* 0..59 */
	BYTE	sec;		/* 0..59 */
    time_t unixtime;	/*Epoch time from NTP raw*/
} RTC_t;

int rtc_initialize (RTC_t* rtc);	/* Initialize RTC */
int rtc_gettime (RTC_t*);			/* Get time */
int rtc_settime (RTC_t*);			/* Set time */
extern DWORD local_get_fattime(void);

#endif
