/*
 * ws_client.h
 *
 * WebSocket CLIENT library (MCU connects to WS/WSS server).
 * Independent from HTTP server; uses only net_sock_* transport.
 *
 * Requires external:
 *   ws_sha1(), ws_base64() (mbedTLS or your crypto helper).
 */
#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <ws_common.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* ws_client_t;

typedef struct {
  const char *host;       /* e.g. "echo.websocket.events" */
  int         port;       /* 80 or 443 */
  const char *resource;   /* e.g. "/ws" */
  ws_proto_t  proto;      /* WS_PROTO_WS or WS_PROTO_WSS */
  const char *origin;     /* optional, may be NULL */
  const char *subprotocol;/* optional, may be NULL */
  const char *extra_headers; /* optional raw header lines ending with \r\n */
  char* 	tls_ca_certs;
  char* 	tls_dev_cert;
  char* 	tls_dev_key;
} ws_client_cfg_t;

int  ws_client_create(ws_client_t *out, const ws_client_cfg_t *cfg);
int  ws_client_connect(ws_client_t c);
int  ws_client_is_open(ws_client_t c);

int  ws_client_send_text(ws_client_t c, const uint8_t *data, uint32_t len);
int  ws_client_send_binary(ws_client_t c, const uint8_t *data, uint32_t len);
int  ws_client_send_ping(ws_client_t c, const uint8_t *data, uint32_t len);
int  ws_client_send_pong(ws_client_t c, const uint8_t *data, uint32_t len);

/* Receive next TEXT/BINARY message.
 * Returns:
 *   >0 : payload length stored in buffer
 *    0 : clean close
 *   <0 : WS_ERR / WS_TIMEOUT
 */
int  ws_client_recv(ws_client_t c, uint8_t *buffer, uint32_t buffer_size, ws_opcode_t *out_opcode);

int  ws_client_close(ws_client_t c);

void ws_client_run(void);


#ifdef __cplusplus
}
#endif

#endif /* WS_CLIENT_H */
