/*
 * datetime.h
 *
 *  Created on: 16/12/2014
 *      Author: dsolano
 */

#ifndef DATETIME_H_
#define DATETIME_H_

/*
lpcRTC.h - Header file for the lpcRTC Real-Time Clock

Version: 1.0.1
(c) 2014 Korneliusz Jarzebski
www.jarzebski.pl

This program is free software: you can redistribute it and/or modify
it under the terms of the version 3 GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stdbool.h>
#include "rtc.h"


/**
 * @brief  Initializes the real time clock RTC
 * @brief  do not return any value
 * @param  None
 * @retval None
 */
bool utils_initialize(void);


/**
 * @brief  Set date and time
 * @brief  set date and time given each on particular vars
 * @param  year = (year in decimal-2022), month = (month in decimal-12),
 * 			day= (day in the month 0-31), hour=0-23,
 * 			minute = 0-59, 	second = 0-59
 * @retval None
 */
void dateutils_setDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
void dateutils_setDateTime_word(uint32_t t);
void dateutils_setDateTime_array(const char* date, const char* time);
char* dateutils_dateFormat(const char* dateFormat, RTC_t* dt);




#endif /* DATETIME_H_ */
