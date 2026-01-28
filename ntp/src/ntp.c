/*
 * ntp.c
 *
 *  Created on: Jan 21, 2025
 *      Author: Daruin Solano
 */

#include <string.h>
#include "net_internal.h"
#include "ntp.h"
#include <time.h>

#define NTP_MAX_POOL     4  // Maximum number of configured pools.
#define NTP_MAX_SERVER   6  // Maximum number of monitored servers.
#define NTP_SERVER "pool.ntp.org"  // NTP server address
#define NTP_PORT 123               // NTP server port
#define LOCAL_PORT 2390            // Local port for UDP
#define NTP_PACKET_SIZE 48         // NTP packet size
#define SECONDS_SINCE_1970				2208988800UL
#define NTP_ERA_SECONDS       4294967296ULL  /* 2^32 */


char* ntp_servers[] = {
		//"us.pool.ntp.org",
		"north-america.pool.ntp.org",
		"pool.ntp.org",
		"time.google.com",
		"time.windows.com",
		"time.apple.com",
		"time.cloudflare.com",
		"time.nist.gov",
		"time.nist.gov"		//Public access to accurate time.
};
uint8_t ntp_packet[NTP_PACKET_SIZE]; // NTP request packet
uint8_t ntp_rx_packet[NTP_PACKET_SIZE];
NTP_t ntp;
RTC_TimeTypeDef rtcTime;
RTC_DateTypeDef rtcDate;
extern RTC_HandleTypeDef hrtc;
extern net_hnd_t 	 hnet;
timestamp_t ts;

net_sockhnd_t udp_sock;

static int  ntp_get_network_time(void);
static void ntp_init_packet(void);



static void ntp_init_packet(void) {
    memset(ntp_packet, 0, NTP_PACKET_SIZE);  // Clear the packet
    ntp_packet[0] = (0 << 6) | (4 << 3) | 3; // LI=0, VN=4, Mode=3
    ntp_packet[1] = 0; // Stratum, or type of clock
    ntp_packet[2] = 0;                      // Polling interval
    ntp_packet[3] = 0;//0xEC;                   // Peer clock precision
    // Rest of the packet can remain zero for a basic request
}

static int ntp_get_network_time(void) {
	int rx_len = 0;
	int ret = NET_ERR;
	net_ipaddr_t 	stage_ntp_ip;
	char* 			stage_ntp_server;
	int 			stage_ntp_port;

	// Set up the NTP query packet
	ntp_init_packet();

	// Start UDP socket on the selected NTP server and Port
	if (net_sock_create(hnet, &udp_sock, NET_PROTO_UDP) != NET_OK){
		ret = NET_ERR;
		goto end;
	}

	// Connect to the actual NTP server
	for (int i = 0; i < sizeof(ntp_servers) / sizeof(ntp_servers[0]); i++) {
		ret = NET_ERR;
		msg_debug("NTP server connect try %d/%d", i+1, sizeof(ntp_servers)/sizeof(ntp_servers[0]));
		if (net_sock_open(udp_sock, ntp_servers[i], NULL, NTP_PORT, LOCAL_PORT) == NET_OK)
		{
			stage_ntp_server = ntp.ntp_server = ntp_servers[i];
			ntp.ntp_port = NTP_PORT;
			net_get_hostaddress(hnet, &stage_ntp_ip, stage_ntp_server);
			msg_debug("NTP server open socket success...\n\t-Server: %s IP:%d.%d.%d.%d",
												ntp.ntp_server,
												stage_ntp_ip.ip[12],
												stage_ntp_ip.ip[13],
												stage_ntp_ip.ip[14],
												stage_ntp_ip.ip[15]);
			memcpy(&ntp.ntp_ip.ip[12], &stage_ntp_ip.ip[12], 4);	// copy ntp ip to structure IP
			memset(&stage_ntp_ip, 0, sizeof(stage_ntp_ip));		// clean stage IP
			stage_ntp_port = 0;
			ret = NET_OK;
			break;
		}
	}
	if (ret != NET_OK) {
		msg_error("NTP servers not reachable... after %d attemps",
				sizeof(ntp_servers) / sizeof(ntp_servers[0]));
		goto end;
	}


	/*Drop the first packet in queue*/
	net_sock_recvfrom(udp_sock, (uint8_t*) ntp_rx_packet,
				NTP_PACKET_SIZE, &stage_ntp_ip, &stage_ntp_port);
	memset(ntp_rx_packet, 0, sizeof(ntp_rx_packet));	// clean the buffer
	memset(&stage_ntp_ip, 0, sizeof(stage_ntp_ip));		// clean stage IP
	stage_ntp_port = 0;

	//Send the configured packet to NTP
	rx_len = net_sock_sendto(udp_sock, (const uint8_t*) ntp_packet,
			NTP_PACKET_SIZE, &ntp.ntp_ip, ntp.ntp_port);

	if (rx_len != NTP_PACKET_SIZE){
		ret = NET_ERR;
		goto end;
	}

	memset(ntp_rx_packet, 0, sizeof(ntp_rx_packet));	// clean the buffer
	// Get the time data from NTP
	rx_len = net_sock_recvfrom(udp_sock, (uint8_t*) ntp_rx_packet,
			NTP_PACKET_SIZE, &stage_ntp_ip, &stage_ntp_port);

	msg_debug("UDP received from IP: %d.%d.%d.%d Port: %u Packet Size = %d",
			stage_ntp_ip.ip[12], stage_ntp_ip.ip[13], stage_ntp_ip.ip[14], stage_ntp_ip.ip[15],
	          stage_ntp_port, rx_len);

	/* Must come from UDP/123 */
	if (stage_ntp_port != NTP_PORT) {
	    msg_error("NTP: got non-NTP UDP packet from port %d (discard)", stage_ntp_port);
	    ret =  NET_ERR;
	    goto end;
	}

	if (rx_len < NTP_PACKET_SIZE){
		ret = NET_ERR;
		goto end;
	}

	/* Verify packet validity*/
	uint8_t li_vn_mode = ntp_rx_packet[0];
	uint8_t vn   = (li_vn_mode >> 3) & 0x07;
	uint8_t mode = (li_vn_mode >> 0) & 0x07;
	uint8_t stratum = ntp_rx_packet[1];

	if (mode != 4 || (vn < 3 || vn > 4) || stratum == 0) {
	    msg_error("NTP invalid: mode=%u vn=%u stratum=%u", mode, vn, stratum);
	    ret = NET_ERR;
	    goto end;
	}

	if (rx_len >= NTP_PACKET_SIZE) {
		// Extract the timestamp (offset 40 in the response)
		ntp.timezone_offset = -5 * 3600; // Example: UTC-5
		//dump_memory(ntp_rx_packet, NTP_PACKET_SIZE);

		uint32_t sec1900 =   ((uint32_t) ntp_rx_packet[40] << 24)
										| ((uint32_t) ntp_rx_packet[41] << 16)
										| ((uint32_t) ntp_rx_packet[42] << 8)
										| ((uint32_t) ntp_rx_packet[43] << 0);
		ts.ntp_epoch = sec1900;

		if (sec1900 < SECONDS_SINCE_1970) {
		    msg_error("NTP bogus sec1900=%lu", (unsigned long)sec1900);
		    ret = NET_ERR;
		    goto end;
		}

		ntp.epoch_time = (uint32_t) (sec1900 - SECONDS_SINCE_1970); // Convert to UNIX epoch (1970)
		ts.unix_timestamp = ntp.epoch_time;

		if (ntp.epoch_time < 1609459200UL || ntp.epoch_time > 2000000000UL) {
		    msg_error("NTP epoch out of range: %lu", (unsigned long)ntp.epoch_time);
		    ret = NET_ERR;
		    goto end;
		}

		// Print the epoch time
		msg_debug("Valid Epoch: %lu", ntp.epoch_time);

		ret = NET_OK;
	}
	end:
	net_sock_close(udp_sock);
	net_sock_destroy(udp_sock);
	return ret;
}

time_t ntp_get_epoch(void){
	if (ntp_get_network_time() == NET_OK)
		return ntp.epoch_time;
	else
		return -1;
}


