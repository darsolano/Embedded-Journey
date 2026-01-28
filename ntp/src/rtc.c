/*------------------------------------------------------------------------/
/RTC control module
/-------------------------------------------------------------------------/
/
/  Copyright (C) 2011, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-------------------------------------------------------------------------*/

#include "rtc.h"
#include "stm32l4xx_hal.h"
#include "msg.h"



#ifdef NETWORK_PRESENT
#include <time.h>
#include "ntp.h"
#endif

static void rtc_getNTPtime(RTC_t * rtc);
extern uint32_t unixtime(RTC_t *t);

static void rtc_getNTPtime(RTC_t * rtc){

	rtc->unixtime = ntp_get_epoch();	// The Unix timestamp from January 1, 1970, at 00:00 UTC.
	if (rtc->unixtime < 0)
		msg_error("error getting unixtime\n");

    time_t raw_time = rtc->unixtime;
    struct tm* ptm = gmtime(&raw_time); // Convert to UTC time

    rtc->year = ptm->tm_year + 1900;
    rtc->month = ptm->tm_mon + 1;	// month goes from 0 to 11
    rtc->mday = ptm->tm_mday;
    rtc->wday = ptm->tm_wday;
    rtc->hour = ptm->tm_hour;
    rtc->min = ptm->tm_min;
    rtc->sec = (ptm->tm_sec > 60 ? ptm->tm_sec-60:ptm->tm_sec);
}

extern RTC_HandleTypeDef hrtc;

const uint8_t rtc_dowArray[]  = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

static uint8_t rtc_dow(uint16_t y, uint8_t m, uint8_t d)
{
    uint8_t dow;

    y -= m < 3;
    dow = ((y + y/4 - y/100 + y/400 + *(rtc_dowArray+(m-1)) + d) % 7);

    if (dow == 0)
    {
        return 7;
    }

    return dow;
}



int rtc_initialize (RTC_t* rtc)
{
	  /** Initialize RTC Only
	  */
	  hrtc.Instance = RTC;
	  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
	  hrtc.Init.AsynchPrediv = 127;
	  hrtc.Init.SynchPrediv = 255;
	  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
	  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
	  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
	  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
	  if (HAL_RTC_Init(&hrtc) != HAL_OK)
	  {
	    return -1;
	  }
	  rtc_settime(rtc);
	return 0;
}

int rtc_gettime (RTC_t *rtc)
{
	RTC_TimeTypeDef sTime = {0};
	RTC_DateTypeDef sDate = {0};

	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

	rtc->sec 	= sTime.Seconds;
	rtc->min 	= sTime.Minutes;
	rtc->hour 	= sTime.Hours;
	rtc->wday 	= rtc_dow(sDate.Year, sDate.Month, sDate.Date);
	rtc->mday 	= sDate.Date;
	rtc->month 	= sDate.Month;
	rtc->year 	= sDate.Year + 2000;
	rtc->unixtime = unixtime(rtc);
	return 1;
}

int rtc_settime (RTC_t *rtc)
{
	RTC_TimeTypeDef sTime = {0};
	RTC_DateTypeDef sDate = {0};

#ifdef NETWORK_PRESENT
	rtc_getNTPtime(rtc); // Get time from the network in 1970 UNIX format
#endif

	sTime.Hours = rtc->hour;
	sTime.Minutes = rtc->min;
	sTime.Seconds = rtc->sec;

	sDate.Year = rtc->year - 2000;
	sDate.Month = rtc->month;
	sDate.Date = rtc->mday;
	sDate.WeekDay = rtc->wday;

	HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
	return 1;
}

/**
 * @brief	User Provided Timer Function for FatFs module
 * @return	Nothing
 * @note	This is a real time clock service to be called from FatFs module.
 * Any valid time must be returned even if the system does not support a real time clock.
 * This is not required in read-only configuration.
 */
DWORD local_get_fattime()
{
	RTC_t rtc;

	/* Get local time */
	rtc_gettime(&rtc);

	/* Pack date and time into a DWORD variable */
	return ((DWORD) (rtc.year - 1980) << 25)
		   | ((DWORD) rtc.month << 21)
		   | ((DWORD) rtc.mday << 16)
		   | ((DWORD) rtc.hour << 11)
		   | ((DWORD) rtc.min << 5)
		   | ((DWORD) rtc.sec >> 1);
}
