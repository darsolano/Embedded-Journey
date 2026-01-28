/*
 * ntp.h
 *
 *  Created on: Jan 21, 2025
 *      Author: Daruin Solano
 */

#ifndef ISM43362_WIFI_NTP_H_
#define ISM43362_WIFI_NTP_H_

#include <stdint.h>
#include <stdbool.h>
#include "net.h"

typedef struct{
	net_ipaddr_t ntp_ip;
	struct tm *ntp_time;
	uint32_t epoch_time;	// Is the same as UNIX time
	int timezone_offset;
	char* ntp_server;
	int ntp_port;
}NTP_t;

typedef struct{
	char aws_ts[17];			/* formatted timestamp data: 20251227T050811Z */
	char ts[24];				/* formatted timestamp data: 2025-12-27T05:08:11Z */
	time_t unix_timestamp;		/* seconds from 1970*/
	uint32_t ntp_epoch;			/* seconds from 1900*/
}timestamp_t;

time_t ntp_get_epoch(void);


#endif /* ISM43362_WIFI_NTP_H_ */
