/*
 * test_ws.c
 *
 *  Created on: Dec 27, 2025
 *      Author: Daruin Solano
 */


#include <string.h>
#include <stdio.h>
#include "ws_client.h"
#include "aws_cert.h"
#include <stdint.h>
#include <stddef.h>

#define WS_HOST   "ws.ifelse.io"
#define WS_PATH   "/"     // this echo server accepts "/"

static void hexdump(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        printf("%02X ", p[i]);
        if ((i % 16) == 15) printf("\n");
    }
    if ((n % 16) != 0) printf("\n");
}

uint32_t crc32_byte(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF; // Initial value
    while (length--) {
        uint32_t byte = *data++;
        crc ^= byte;
        for (int j = 0; j < 8; j++) {
            // Check if LSB is 1 and shift right
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc; // Final XOR
}

int ws_echo_smoketest(int use_tls)
{
    int rc;
    ws_client_t c;

    // ws.ifelse.io
    ws_client_cfg_t cfg = {
    	.host		   = "a1rowpbf3j3tx6-ats.iot.us-east-2.amazonaws.com",
        //.host        = "echo.websocket.org",
    	//.host		   = "ws.ifelse.io",
        .port          = use_tls ? 443 : 80,
        .resource      = use_tls ? "/":"/",
        .proto         = use_tls ? WS_PROTO_WSS : WS_PROTO_WS,
        .origin        = NULL,
        .subprotocol   = NULL,
        .extra_headers = NULL,
		.tls_ca_certs  = AWS_ROOT_CA1
		//.tls_ca_certs  = ISRG_ROOT_X1
		//.tls_ca_certs  = GTS_Root_R4
    };

    msg_info("\n[WS TEST] Connecting to %s://%s:%d%s\n",
             use_tls ? "wss" : "ws", cfg.host, cfg.port, cfg.resource);

    rc = ws_client_create(&c, &cfg);
    if (rc != WS_OK) {
        msg_error("[WS TEST] ws_client_create failed rc=%d\n", rc);
        return rc;
    }

    rc = ws_client_connect(c);
    if (rc != WS_OK) {
        msg_error("[WS TEST] ws_client_connect failed rc=%d\n", rc);
        ws_client_close(c);
        return rc;
    }

    char msg[64];
    /* Receive: your ws_client_recv returns:
       >0 payload length
        0 clean close
       <0 WS_ERR or WS_TIMEOUT
    */
    uint8_t rx[512];
    ws_opcode_t op = WS_OPCODE_CONT; /* anything; will be set */
    int n;

    /* Try a few times in case server pings or timing is slow */
    for (int tries = 0; tries < 20; tries++) {
    	sprintf(msg, "%s - %d", use_tls ? "hello over WSS from STM32" : "hello over WS from STM32", tries);
        rc = ws_client_send_text(c, (uint8_t*)msg, (uint32_t)strlen(msg));
        if (rc != WS_OK) {
            msg_error("[WS TEST] ws_client_send_text failed rc=%d\n", rc);
            ws_client_close(c);
            return rc;
        }

        memset(rx, 0, sizeof(rx));
        n = ws_client_recv(c, rx, sizeof(rx) - 1, &op);

        if (n == WS_TIMEOUT) {
            continue;
        }
        if (n < 0) {
            msg_error("[WS TEST] RX error n=%d\n", n);
            ws_client_close(c);
            return n;
        }
        if (n == WS_CLOSED) {
            msg_error("[WS TEST] Connection closed by peer (no WS frame parsed)\n");
            ws_client_close(c);
            return 0;
        }

        rx[n] = 0;

        if (op == WS_OPCODE_TEXT) {
            msg_info("[WS TEST] RX TEXT (%d): %s\n", n, rx);
            continue;
        } else if (op == WS_OPCODE_BINARY) {
            msg_info("[WS TEST] RX BINARY (%d bytes)\n", n);
            hexdump(rx, n);
            continue;
        } else {
            msg_info("[WS TEST] RX opcode=%d len=%d (ignored)\n", (int)op, n);
        }
    }

    if (ws_client_close(c) != WS_OK){
        msg_error("[WS TEST] ERROR. Closing.\n");
        rc = WS_ERR;
    }else{
        msg_info("[WS TEST] Done. Closing.\n");
        rc = WS_OK;
    }
    return rc;
}
