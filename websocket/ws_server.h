/*
 * ws_server.h
 *
 * WebSocket SERVER library (browser connects to MCU).
 * Independent from HTTP server; uses only net_sock + net_srv (for listening).
 *
 * NOTE: browsers will connect with WS over TCP.
 *       WSS requires TLS termination on the MCU (NET_PROTO_TLS) and is heavier.
 */
#ifndef WS_SERVER_H
#define WS_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ws_common.h>

#include "net_srv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  net_srv_conn_t srv;         /* listener (uses your net_srv) */
  bool running;
} ws_server_t;

typedef struct {
  net_sockhnd_t sock;         /* active client socket */
  bool open;
  /* buffers */
  uint8_t *rxbuf;
  size_t   rxcap;
  uint8_t *scratch;
  size_t   scratch_cap;
} ws_server_client_t;

/* Start listening on port (creates server). */
int ws_server_start(ws_server_t *s, net_hnd_t hnet, uint16_t port);

/* Wait for a TCP client, perform WS upgrade handshake.
 * Returns WS_OK and fills out 'c' if successful.
 * Returns WS_TIMEOUT for no-data/timeout while reading handshake.
 * Returns WS_ERR on parse/handshake error.
 */
int ws_server_accept(ws_server_t *s, ws_server_client_t *c);

/* Receive next TEXT/BINARY message from browser.
 * Returns >0 payload length, 0 clean close, <0 error/timeout.
 */
int ws_server_recv(ws_server_client_t *c, uint8_t *buffer, uint32_t buffer_size, ws_opcode_t *out_opcode);

int ws_server_send_text(ws_server_client_t *c, const uint8_t *data, uint32_t len);
int ws_server_send_binary(ws_server_client_t *c, const uint8_t *data, uint32_t len);
int ws_server_send_ping(ws_server_client_t *c, const uint8_t *data, uint32_t len);
int ws_server_send_pong(ws_server_client_t *c, const uint8_t *data, uint32_t len);

/* Close client socket (does not stop server). */
int ws_server_client_close(ws_server_t *s, ws_server_client_t *c);

/* Stop server */
int ws_server_stop(ws_server_t *s);

/*Websocket server demo*/
void ws_server_run(void);

#ifdef __cplusplus
}
#endif

#endif /* WS_SERVER_H */
