#include "ws_server.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern net_hnd_t hnet;
bool ws_http_header_has_token(const char *value, const char *token);

static void ws_unmask_local(uint8_t *buf, size_t len, const uint8_t mask_key[4]) {
	for (size_t i = 0; i < len; i++) {
		buf[i] ^= mask_key[i & 3u];
	}
}

bool ws_http_header_has_token(const char *value, const char *token)
{
  if (!value || !token || !*token) return false;

  const char *p = value;
  size_t tlen = strlen(token);

  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    if (!*p) break;

    const char *start = p;
    while (*p && *p != ',') p++;
    const char *stop = p;

    while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;

    size_t len = (size_t)(stop - start);
    if (len == tlen && strncasecmp(start, token, tlen) == 0) return true;

    if (*p == ',') p++;
  }

  return false;
}


/**
 * @brief Perform a minimal RFC6455 WebSocket server handshake.
 *
 * Reads the HTTP Upgrade request until the end of headers, validates required
 * headers for a WebSocket Upgrade (Upgrade + Connection token + Sec-WebSocket-Key),
 * computes Sec-WebSocket-Accept, and replies with HTTP/1.1 101.
 *
 * Notes:
 *  - Accepts both CRLF and LF-only line endings in the HTTP request.
 *  - Ignores Sec-WebSocket-Extensions (e.g., permessage-deflate) for now.
 *  - Does not negotiate subprotocols unless you add Sec-WebSocket-Protocol handling.
 *
 * @param c  Server client context (must contain sock, rxbuf, rxcap).
 * @return WS_OK on success, WS_ERR on failure.
 */
static int ws_server_handshake(ws_server_client_t *c) {
	/* Read until end-of-headers */
	size_t got = 0;
	int rc = ws_recv_until(c->sock, c->rxbuf, c->rxcap, "\r\n\r\n", 4, &got);
	if (rc != WS_OK) {
		/* If your client uses LF-only, ws_http_find_header_value() still works,
		 and ws_recv_until() already uses CRLF delimiter; in practice most clients
		 send CRLF. If you see LF-only clients, you can call ws_recv_until again
		 with "\n\n" as a fallback. */
		msg_error("ws_server_handshake: ws_recv_until failed rc=%d got=%lu\n",
				rc, (unsigned long )got);
		return WS_ERR;
	}

	/* Verify request line starts with GET */
	if (got < 4 || memcmp(c->rxbuf, "GET ", 4) != 0) {
		msg_error("ws_server: not a GET upgrade\n");
		return WS_ERR;
	}

	/* Required headers */
	char key[128];
	char upgrade[64];
	char conn[64];

	if (!ws_http_find_header_value(c->rxbuf, got, "Sec-WebSocket-Key", key,
			sizeof(key))) {
		msg_error("ws_server: missing Sec-WebSocket-Key\n");
		return WS_ERR;
	}

	if (!ws_http_find_header_value(c->rxbuf, got, "Upgrade", upgrade,
			sizeof(upgrade))) {
		msg_error("ws_server: missing Upgrade\n");
		return WS_ERR;
	}

	if (strcasecmp(upgrade, "websocket") != 0) {
		msg_error("ws_server: Upgrade != websocket (%s)\n", upgrade);
		return WS_ERR;
	}

	if (!ws_http_find_header_value(c->rxbuf, got, "Connection", conn,
			sizeof(conn))) {
		msg_error("ws_server: missing Connection\n");
		return WS_ERR;
	}

	/* RFC6455: Connection MUST contain token "Upgrade" (case-insensitive) */
	if (!ws_http_header_has_token(conn, "Upgrade")) {
		msg_error("ws_server: Connection missing Upgrade token (%s)\n", conn);
		return WS_ERR;
	}

	/* Compute Accept */
	char accept[64];
	if (ws_compute_accept(key, accept, sizeof(accept)) != WS_OK) {
		msg_error("ws_server: compute accept failed\n");
		return WS_ERR;
	}

	/* Send 101 Switching Protocols */
	char resp[256];
	int n = snprintf(resp, sizeof(resp), 	"HTTP/1.1 101 Switching Protocols\r\n"
											"Upgrade: websocket\r\n"
											"Connection: Upgrade\r\n"
											"Sec-WebSocket-Accept: %s\r\n"
											"\r\n", accept);
	if (n <= 0 || n >= (int) sizeof(resp))
		return WS_ERR;

	if (ws_send_all(c->sock, (const uint8_t*) resp, (size_t) n) != WS_OK)
		return WS_ERR;

	return WS_OK;
}


int ws_server_start(ws_server_t *s, net_hnd_t hnet, uint16_t port)
{
  if (!s) return WS_ERR;
  memset(s, 0, sizeof(*s));

  s->srv.localport = port;
  s->srv.protocol = NET_PROTO_TCP;
  s->srv.name = "ws";
  s->srv.timeout = 0;

  if (net_srv_bind(hnet, NULL, &s->srv) != NET_OK) {
    msg_error("ws_server_start: net_srv_bind failed\n");
    return WS_ERR;
  }

  s->running = true;
  return WS_OK;
}

static void ws_server_client_init(ws_server_client_t *c)
{
  memset(c, 0, sizeof(*c));
  c->rxcap = 2048;
  c->scratch_cap = 1500;
  c->rxbuf = (uint8_t *)malloc(c->rxcap);
  c->scratch = (uint8_t *)malloc(c->scratch_cap);
  c->open = false;
}

int ws_server_accept(ws_server_t *s, ws_server_client_t *c)
{
  if (!s || !c) return WS_ERR;

  ws_server_client_init(c);

  if (!c->rxbuf || !c->scratch) {
    ws_server_client_close(s, c);
    return WS_ERR;
  }

  /* Wait for a TCP connection */
  int rc = net_srv_listen(&s->srv);
  if (rc != NET_OK) {
    msg_error("ws_server_accept: net_srv_listen rc=%d\n", rc);
    ws_server_client_close(s, c);
    return WS_ERR;
  }

  c->sock = s->srv.sock;

  /* Handshake */
  rc = ws_server_handshake(c);
  if (rc != WS_OK) {
    msg_error("ws_server_accept: handshake failed or client drop the socket rc=%d\n", rc);
    ws_server_client_close(s, c);
    return rc;
  }

  c->open = true;
  msg_info("ws_server: client upgraded to websocket\n");
  return WS_OK;
}

static int ws_server_send(ws_server_client_t *c, ws_opcode_t op, const uint8_t *data, uint32_t len)
{
  if (!c || !c->open) return WS_ERR;
  /* Server MUST NOT mask outgoing frames */
  return ws_send_frame(c->sock, op, data, len, true, false, c->scratch, c->scratch_cap);
}

int ws_server_send_text(ws_server_client_t *c, const uint8_t *data, uint32_t len)
{
  return ws_server_send(c, WS_OPCODE_TEXT, data, len);
}
int ws_server_send_binary(ws_server_client_t *c, const uint8_t *data, uint32_t len)
{
  return ws_server_send(c, WS_OPCODE_BINARY, data, len);
}
int ws_server_send_ping(ws_server_client_t *c, const uint8_t *data, uint32_t len)
{
  return ws_server_send(c, WS_OPCODE_PING, data, len);
}
int ws_server_send_pong(ws_server_client_t *c, const uint8_t *data, uint32_t len)
{
  return ws_server_send(c, WS_OPCODE_PONG, data, len);
}


/**
 * @brief Receive a single WebSocket message on the server side (RFC6455).
 *
 * Behavior / policy:
 *  - Incoming frames (client->server) MUST be masked. If not masked -> protocol error (1002).
 *  - Server does NOT support fragmentation for now: FIN must be 1 and opcode must not be 0x0 continuation.
 *  - RSV1/2/3 must be 0 (no extensions negotiated; we ignore Sec-WebSocket-Extensions in handshake).
 *  - Control frames:
 *      - PING  (0x9): respond with PONG (0xA) carrying same payload, then continue receiving.
 *      - PONG  (0xA): ignore and continue receiving.
 *      - CLOSE (0x8): respond with CLOSE, mark c->open=false, return 0 (clean close).
 *
 * Return:
 *  - >0  : number of payload bytes copied into buffer for TEXT/BINARY frames.
 *  -  0  : clean close (peer closed or CLOSE frame processed).
 *  - <0  : WS_ERR / protocol failure (caller should close the socket/client).
 */
int ws_server_recv(ws_server_client_t *c, uint8_t *buffer, uint32_t buffer_size, ws_opcode_t *out_opcode)
{
    if (!c || !c->open || !buffer || buffer_size == 0) {
        return WS_ERR;
    }

    while (1) {
        ws_frame_hdr_t h;
        int rc = ws_read_frame_hdr(c->sock, &h);
        if (rc != WS_OK) {
            /* Underlying socket likely closed */
            c->open = false;
            return 0;
        }

        /* RFC6455 basic validation (server receiving from client) */
        if (h.rsv1 || h.rsv2 || h.rsv3) {
            /* No extensions negotiated => RSV must be 0 */
            uint8_t close_payload[2] = { 0x03, 0xEA }; /* 1002 */
            (void)ws_send_frame(c->sock, WS_OPCODE_CLOSE, close_payload, 2, true, false,
                                c->scratch, c->scratch_cap);
            c->open = false;
            return WS_ERR;
        }

        if (!h.fin || h.opcode == WS_OPCODE_CONT) {
            /* Fragmentation / continuation not supported in this build */
            uint8_t close_payload[2] = { 0x03, 0xEA }; /* 1002 */
            (void)ws_send_frame(c->sock, WS_OPCODE_CLOSE, close_payload, 2, true, false,
                                c->scratch, c->scratch_cap);
            c->open = false;
            return WS_ERR;
        }

        if (!h.masked) {
            /* Client->server frames MUST be masked */
            uint8_t close_payload[2] = { 0x03, 0xEA }; /* 1002 */
            (void)ws_send_frame(c->sock, WS_OPCODE_CLOSE, close_payload, 2, true, false,
                                c->scratch, c->scratch_cap);
            c->open = false;
            return WS_ERR;
        }

        /* Control frames must have payload <= 125 and must not be fragmented (FIN already checked) */
        if ((h.opcode == WS_OPCODE_CLOSE || h.opcode == WS_OPCODE_PING || h.opcode == WS_OPCODE_PONG) &&
            (h.payload_len > 125)) {
            uint8_t close_payload[2] = { 0x03, 0xEA }; /* 1002 */
            (void)ws_send_frame(c->sock, WS_OPCODE_CLOSE, close_payload, 2, true, false,
                                c->scratch, c->scratch_cap);
            c->open = false;
            return WS_ERR;
        }

        /* Read payload (and unmask) */
        if (h.payload_len > buffer_size) {
            /* Drain to keep stream aligned */
            uint64_t to_drain = h.payload_len;
            while (to_drain) {
                uint8_t tmp[64];
                size_t chunk = (to_drain > sizeof(tmp)) ? sizeof(tmp) : (size_t)to_drain;
                int rr = ws_recv_exact(c->sock, tmp, chunk);
                if (rr != WS_OK) { c->open = false; return 0; }
                to_drain -= chunk;
            }
            /* Too big for user buffer */
            return WS_ERR;
        }

        if (h.payload_len > 0) {
            int rr = ws_recv_exact(c->sock, buffer, (size_t)h.payload_len);
            if (rr != WS_OK) {
                c->open = false;
                return 0;
            }
            ws_unmask_local(buffer, (size_t)h.payload_len, h.mask_key);
        }

        /* Handle control frames */
        if (h.opcode == WS_OPCODE_PING) {
            /* Server must reply unmasked PONG */
            (void)ws_send_frame(c->sock, WS_OPCODE_PONG, buffer, (size_t)h.payload_len,
                                true, false, c->scratch, c->scratch_cap);
            continue;
        }

        if (h.opcode == WS_OPCODE_PONG) {
            continue;
        }

        if (h.opcode == WS_OPCODE_CLOSE) {
            /* Reply CLOSE (unmasked) and close locally */
            /* If peer provided a code, mirror it; else send 1000 */
            uint8_t close_payload[2] = { 0x03, 0xE8 }; /* 1000 */
            if (h.payload_len >= 2) {
                close_payload[0] = buffer[0];
                close_payload[1] = buffer[1];
            }
            (void)ws_send_frame(c->sock, WS_OPCODE_CLOSE, close_payload, 2, true, false,
                                c->scratch, c->scratch_cap);
            c->open = false;
            return 0;
        }

        /* Data frames */
        if (h.opcode == WS_OPCODE_TEXT || h.opcode == WS_OPCODE_BINARY) {
            if (out_opcode) *out_opcode = h.opcode;
            return (int)h.payload_len;
        }

        /* Unknown/unsupported opcode -> protocol error */
        {
            uint8_t close_payload[2] = { 0x03, 0xEA }; /* 1002 */
            (void)ws_send_frame(c->sock, WS_OPCODE_CLOSE, close_payload, 2, true, false,
                                c->scratch, c->scratch_cap);
            c->open = false;
            return WS_ERR;
        }
    }
}

int ws_server_client_close(ws_server_t *s, ws_server_client_t *c)
{
  (void)s;
  if (!c) return WS_ERR;

  if (c->open) {
    /* Send normal close 1000 */
    uint8_t close_payload[2] = {0x03, 0xE8};
    ws_send_frame(c->sock, WS_OPCODE_CLOSE, close_payload, 2, true, false, c->scratch, c->scratch_cap);
    c->open = false;
  }

  /* Close underlying server connection and prepare for next */
  if (s && s->srv.sock) {
    net_srv_next_conn(&s->srv);
  }

  if (c->rxbuf) free(c->rxbuf);
  if (c->scratch) free(c->scratch);
  memset(c, 0, sizeof(*c));
  return WS_OK;
}

int ws_server_stop(ws_server_t *s)
{
  if (!s) return WS_ERR;
  s->running = false;
  net_srv_close(&s->srv);
  return WS_OK;
}

void ws_server_run(void)
{
	ws_server_t s;
	ws_server_start(&s, hnet, 81);   // ws://board-ip:81/

	while (1) {
	  ws_server_client_t cli;
	  if (ws_server_accept(&s, &cli) == WS_OK) {

	    uint8_t buf[512];
	    ws_opcode_t op;

	    while (cli.open) {
	      int n = ws_server_recv(&cli, buf, sizeof(buf), &op);
	      if (n > 0 && op == WS_OPCODE_TEXT) {
	        ws_server_send_text(&cli, buf, (uint32_t)n); // echo
	      } else if (n == 0) {
	        break; // clean close
	      }
	    }

	    ws_server_client_close(&s, &cli);
	  }
	}
}
