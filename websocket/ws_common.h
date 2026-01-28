/*
 * ws_common.h
 *
 * Robust WebSocket helpers for STM32 + net_sock (TCP/TLS).
 * - send_all(): handles partial TCP sends
 * - recv_exact(): reads exact N bytes
 * - recv_until(): reads until a delimiter (e.g., "\r\n\r\n")
 *
 * NOTE:
 *  - SHA1 and Base64 are expected to be provided by your project.
 *    You must provide:
 *      void ws_sha1(const uint8_t *data, size_t len, uint8_t out[20]);
 *      int  ws_base64(const uint8_t *in, size_t in_len, char *out, size_t out_len);
 */
#ifndef WS_COMMON_H
#define WS_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* External crypto helpers (mbedTLS, etc.) */
extern void ws_sha1(const uint8_t *data, size_t len, uint8_t out[20]);
extern int  ws_base64(const uint8_t *in, size_t in_len, char *out, size_t out_len);

#define WS_OK            0
#define WS_ERR          -1
#define WS_TIMEOUT      -2
#define WS_CLOSED        -3   /* for recv() meaning clean close */

#define WS_CLOSE_NORMAL           1000
#define WS_CLOSE_PROTOCOL_ERROR   1002

/* If your WiFi stack returns a specific code for "no data / timeout", set it here.
 * You used -3 earlier in logs. If different, change WS_NET_NO_DATA. */
#ifndef WS_NET_NO_DATA
#define WS_NET_NO_DATA  (-3)
#endif

typedef enum {
  WS_OPCODE_CONT   = 0x0,
  WS_OPCODE_TEXT   = 0x1,
  WS_OPCODE_BINARY = 0x2,
  WS_OPCODE_CLOSE  = 0x8,
  WS_OPCODE_PING   = 0x9,
  WS_OPCODE_PONG   = 0xA
} ws_opcode_t;

typedef enum {
  WS_PROTO_WS  = 0,	// plain TCB
  WS_PROTO_WSS = 1	// TLS
} ws_proto_t;

/* Common helpers */
int  ws_send_all(net_sockhnd_t sock, const uint8_t *buf, size_t len);
int  ws_recv_exact(net_sockhnd_t sock, uint8_t *buf, size_t len);
int  ws_recv_until(net_sockhnd_t sock, uint8_t *buf, size_t buf_cap,
                   const char *delim, size_t delim_len, size_t *out_len);

/* HTTP header helpers for handshake parsing (independent of HTTP server) */
int  ws_http_status_is_101(const uint8_t *hdr, size_t len);
const char *ws_http_find_header_value(const uint8_t *hdr, size_t len,
                                      const char *name,
                                      char *out, size_t out_cap);

/* RFC6455 Accept compute */
int  ws_compute_accept(const char *client_key_b64, char *accept_out, size_t accept_out_len);

/* WebSocket frame helpers (client/server share the parser).
 * - For server receive from browser: expect_masked=true
 * - For client receive from server: expect_masked=false
 * - For send: mask_outgoing=true for client, false for server
 */
typedef struct {
  ws_opcode_t opcode;
  bool        fin;
  bool        rsv1;
  bool        rsv2;
  bool        rsv3;
  uint64_t    payload_len;
  bool        masked;
  uint8_t     mask_key[4];
} ws_frame_hdr_t;

int  ws_read_frame_hdr(net_sockhnd_t sock, ws_frame_hdr_t *h);

int ws_validate_frame_hdr(const ws_frame_hdr_t *h, bool expect_masked, bool reject_fragmentation);
int  ws_read_frame_payload(net_sockhnd_t sock, const ws_frame_hdr_t *h,
                           uint8_t *dst, size_t dst_cap, size_t *out_len,
                           uint8_t *scratch, size_t scratch_cap);
int  ws_send_frame(net_sockhnd_t sock, ws_opcode_t opcode, const uint8_t *payload,
                   size_t payload_len, bool fin, bool mask_outgoing,
                   uint8_t *scratch, size_t scratch_cap);

#ifdef __cplusplus
}
#endif

#endif /* WS_COMMON_H */
