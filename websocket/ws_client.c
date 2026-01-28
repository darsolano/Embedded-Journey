#include "ws_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern net_hnd_t hnet;
extern RNG_HandleTypeDef hrng;

/* Client context */
typedef struct {
	net_sockhnd_t sock;
	ws_client_cfg_t cfg;
	bool open;
	bool handshake_ok;

	uint8_t *rxbuf;
	size_t rxcap;

	uint8_t *scratch;
	size_t scratch_cap;
	char key_b64[64];
	/* Bytes read beyond the HTTP header terminator ("\r\n\r\n") during the
	 * opening handshake. Some servers send a first WS frame immediately after
	 * the 101 response; we must preserve those bytes for the frame parser.
	 */
	uint8_t pending[256];
	size_t pending_len;
	size_t pending_off;
} ws_client_ctx_t;


static int ws_client_recv_exact(ws_client_ctx_t *ctx, uint8_t *buf, size_t len)
{
    if (!ctx || !buf) return WS_ERR;
    if (len == 0) return WS_OK;

    size_t got = 0;
    const uint32_t timeout_ms = 5000;
    const uint32_t start = HAL_GetTick();

    /* 1) Consume any pending bytes first */
    while (got < len && ctx->pending_off < ctx->pending_len) {
        buf[got++] = ctx->pending[ctx->pending_off++];
    }

    /* If we've consumed all pending bytes, reset to avoid stale state */
    if (ctx->pending_off >= ctx->pending_len) {
        ctx->pending_off = 0;
        ctx->pending_len = 0;
    }

    /* 2) Read remaining bytes from socket */
    while (got < len) {

        int rc = net_sock_recv(ctx->sock, buf + got, (int)(len - got));

        if (rc > 0) {
            got += (size_t)rc;
            continue;
        }

        /* IMPORTANT:
           rc==0 means "peer closed" only if net_sock_recv() follows BSD recv semantics.
           If your WiFi driver returns 0 for "no data yet", you MUST fix net_sock_recv()
           to return NET_NO_DATA instead of 0. */
        if (rc == 0) {
            msg_warning("[WS RX] recv_exact EOF/CLOSED need=%lu got=%lu\r\n",
                        (unsigned long)len, (unsigned long)got);
            return WS_CLOSED;
        }

        /* Treat all transient/no-data conditions the same */
        if (rc == NET_NO_DATA || rc == NET_TIMEOUT) {
            const uint32_t now = HAL_GetTick();
            if ((uint32_t)(now - start) > timeout_ms) {
                msg_warning("[WS RX] recv_exact TIMEOUT need=%lu got=%lu last_rc=%d\r\n",
                            (unsigned long)len, (unsigned long)got, rc);
                return WS_TIMEOUT;
            }
            HAL_Delay(1);
            continue;
        }

        /* Fatal error */
        msg_error("[WS RX] recv_exact ERROR rc=%d need=%lu got=%lu\r\n",
                  rc, (unsigned long)len, (unsigned long)got);
        return WS_ERR;
    }

    return WS_OK;
}


static int ws_client_read_frame_hdr(ws_client_ctx_t *ctx, ws_frame_hdr_t *h) {
	if (!ctx || !h)
		return WS_ERR;

	uint8_t b[2];
	int rc = ws_client_recv_exact(ctx, b, 2);
	if (rc != WS_OK)
		return rc;

	uint8_t b0 = b[0], b1 = b[1];
	h->fin = (b0 & 0x80) != 0;
	h->rsv1 = (b0 & 0x40) != 0;
	h->rsv2 = (b0 & 0x20) != 0;
	h->rsv3 = (b0 & 0x10) != 0;
	h->opcode = (ws_opcode_t) (b0 & 0x0F);
	h->masked = (b1 & 0x80) != 0;

	uint64_t plen = (uint64_t) (b1 & 0x7F);
	if (plen == 126) {
		uint8_t ext[2];
		rc = ws_client_recv_exact(ctx, ext, 2);
		if (rc != WS_OK)
			return rc;
		plen = ((uint64_t) ext[0] << 8) | (uint64_t) ext[1];
	} else if (plen == 127) {
		uint8_t ext[8];
		rc = ws_client_recv_exact(ctx, ext, 8);
		if (rc != WS_OK)
			return rc;
		plen = 0;
		for (int i = 0; i < 8; i++)
			plen = (plen << 8) | (uint64_t) ext[i];
	}
	h->payload_len = plen;

	/* Reject RSV (no extensions negotiated) */
	if (h->rsv1 || h->rsv2 || h->rsv3)
		return WS_ERR;

	/* Reject fragmentation/continuation for now */
	if (!h->fin || h->opcode == WS_OPCODE_CONT)
		return WS_ERR;

	/* Control frames must be FIN=1 and payload <=125 */
	if (h->opcode == WS_OPCODE_CLOSE || h->opcode == WS_OPCODE_PING
			|| h->opcode == WS_OPCODE_PONG) {
		if (!h->fin)
			return WS_ERR;
		if (h->payload_len > 125)
			return WS_ERR;
	}

	if (h->masked) {
		rc = ws_client_recv_exact(ctx, h->mask_key, 4);
		if (rc != WS_OK)
			return rc;
	} else {
		memset(h->mask_key, 0, 4);
	}
	return WS_OK;
}

static void ws_unmask_local(uint8_t *buf, size_t len, const uint8_t mask_key[4])
{
  for (size_t i = 0; i < len; i++) buf[i] ^= mask_key[i & 3u];
}

static int ws_client_read_frame_payload(ws_client_ctx_t *ctx, const ws_frame_hdr_t *h,
                                       uint8_t *dst, size_t dst_cap, size_t *out_len,
                                       uint8_t *scratch, size_t scratch_cap)
{
  if (!ctx || !h) return WS_ERR;
  if (out_len) *out_len = 0;

  if (h->payload_len > dst_cap) {
    /* Drain and discard to keep stream aligned */
    uint64_t to_drain = h->payload_len;

    while (to_drain) {
      size_t chunk = (to_drain > scratch_cap) ? scratch_cap : (size_t)to_drain;
      int rc = ws_client_recv_exact(ctx, scratch, chunk);
      if (rc != WS_OK) return rc;
      to_drain -= chunk;
    }
    return WS_ERR;
  }


  if (h->payload_len == 0) {
    if (out_len) *out_len = 0;
    return WS_OK;
  }

//  int rc = ws_client_recv_exact(ctx, dst, (size_t)h->payload_len);
/*Deletes this after the test*/
  /* probe 1 byte first */
  int rc = ws_client_recv_exact(ctx, dst, 1);
  if (rc != WS_OK) return rc;

  /* then read the remaining payload */
  if (h->payload_len > 1) {
      rc = ws_client_recv_exact(ctx, dst + 1, (size_t)h->payload_len - 1);
      if (rc != WS_OK) return rc;
  }
/*end of delete this*/

  if (rc != WS_OK) return rc;

  if (h->masked) ws_unmask_local(dst, (size_t)h->payload_len, h->mask_key);
  if (out_len) *out_len = (size_t)h->payload_len;

  return WS_OK;
}


static void ws_client_send_close_code(ws_client_ctx_t *ctx, uint16_t code)
{
  if (!ctx) return;
  uint8_t payload[2] = { (uint8_t)(code >> 8), (uint8_t)(code & 0xFF) };
  (void)ws_send_frame(ctx->sock, WS_OPCODE_CLOSE, payload, 2, true, true,
                      ctx->scratch, ctx->scratch_cap);
}

static int ws_client_handshake(ws_client_ctx_t *ctx) {
	/* Build Sec-WebSocket-Key: 16 random bytes base64 */
	uint8_t key_raw[16];
	for (int i = 0; i < 16; i++)
		key_raw[i] = (uint8_t) (rand() & 0xFF);

	if (ws_base64(key_raw, sizeof(key_raw), ctx->key_b64, sizeof(ctx->key_b64))
			< 0) {
		msg_error("ws_client: base64 key failed\n");
		return WS_ERR;
	}

	char req[512];
	int n = 0;

	n += snprintf(req + n, sizeof(req) - (size_t) n, "GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"Sec-WebSocket-Version: 13\r\n", ctx->cfg.resource, ctx->cfg.host,
			ctx->key_b64);

	if (ctx->cfg.origin) {
		n += snprintf(req + n, sizeof(req) - (size_t) n, "Origin: %s\r\n",
				ctx->cfg.origin);
	}
	if (ctx->cfg.subprotocol) {
		n += snprintf(req + n, sizeof(req) - (size_t) n,
				"Sec-WebSocket-Protocol: %s\r\n", ctx->cfg.subprotocol);
	}

	if (ctx->cfg.extra_headers) {
		n += snprintf(req + n, sizeof(req) - (size_t) n, "%s",
				ctx->cfg.extra_headers);
	}

	n += snprintf(req + n, sizeof(req) - (size_t) n, "\r\n");

	if (n <= 0 || n >= (int) sizeof(req)) {
		msg_error("ws_client: handshake req too big\n");
		return WS_ERR;
	}

	if (ws_send_all(ctx->sock, (const uint8_t*) req, (size_t) n) != WS_OK) {
		msg_error("ws_client: send handshake failed\n");
		return WS_ERR;
	}

	/* Read until end of headers */
	size_t got = 0;
	int rc = ws_recv_until(ctx->sock, ctx->rxbuf, ctx->rxcap, "\r\n\r\n", 4,
			&got);
	if (rc != WS_OK) {
		msg_error("ws_client: recv handshake failed rc=%d\n", rc);
		return rc;
	}

	/* ws_recv_until() returns total bytes read. It may read past the end of the
	 * HTTP headers and include the beginning of the first WebSocket frame.
	 * Preserve those "extra" bytes so the frame parser does not desync.
	 */
	size_t hdr_end = 0;
	for (size_t j = 0; j + 3 < got; j++) {
		if (ctx->rxbuf[j] == '\r' && ctx->rxbuf[j + 1] == '\n'
				&& ctx->rxbuf[j + 2] == '\r' && ctx->rxbuf[j + 3] == '\n') {
			hdr_end = j + 4;
			break;
		}
	}

	ctx->pending_len = 0;
	ctx->pending_off = 0;

	if (hdr_end && got > hdr_end) {
		size_t extra = got - hdr_end;
		if (extra > sizeof(ctx->pending))
			extra = sizeof(ctx->pending);
		memcpy(ctx->pending, ctx->rxbuf + hdr_end, extra);
		ctx->pending_len = extra;
	}

	/* Only parse the HTTP header block (not any extra WS bytes). */
	size_t http_len = hdr_end ? hdr_end : got;

	if (!ws_http_status_is_101(ctx->rxbuf, http_len)) {
		msg_error("ws_client: handshake status not 101\n%.*s\n", (int )http_len,
				(char* )ctx->rxbuf);
		return WS_ERR;
	}

	char accept_hdr[128];
	if (!ws_http_find_header_value(ctx->rxbuf, http_len, "Sec-WebSocket-Accept",
			accept_hdr, sizeof(accept_hdr))) {
		msg_error("ws_client: missing Sec-WebSocket-Accept\n");
		return WS_ERR;
	}

	char expected[64];
	if (ws_compute_accept(ctx->key_b64, expected, sizeof(expected)) != WS_OK) {
		msg_error("ws_client: compute accept failed\n");
		return WS_ERR;
	}

	if (strcmp(accept_hdr, expected) != 0) {
		msg_error("ws_client: accept mismatch\nexp=%s\ngot=%s\n", expected,
				accept_hdr);
		return WS_ERR;
	}

	return WS_OK;
}

static int ws_client_send(ws_client_ctx_t *ctx, ws_opcode_t op,
		const uint8_t *data, uint32_t len) {
	if (!ctx || !ws_client_is_open((ws_client_t) ctx))
		return WS_ERR;
	/* Client MUST mask outgoing frames */
	return ws_send_frame(ctx->sock, op, data, len, true, true, ctx->scratch,
			ctx->scratch_cap);
}

int ws_client_create(ws_client_t *out, const ws_client_cfg_t *cfg) {
	if (!out || !cfg || !cfg->host || !cfg->resource)
		return WS_ERR;

	ws_client_ctx_t *ctx = (ws_client_ctx_t*) calloc(1,
			sizeof(ws_client_ctx_t));
	if (!ctx)
		return WS_ERR;

	ctx->cfg = *cfg;
	ctx->open = false;
	ctx->handshake_ok = false;

	ctx->pending_len = 0;
	ctx->pending_off = 0;

	/* Buffers (handshake + scratch) */
	ctx->rxcap = 2048;
	ctx->rxbuf = (uint8_t*) malloc(ctx->rxcap);
	ctx->scratch_cap = 1500;
	ctx->scratch = (uint8_t*) malloc(ctx->scratch_cap);
	if (!ctx->rxbuf || !ctx->scratch) {

		ws_client_close((ws_client_t) ctx);
		return WS_ERR;
	}

	bool tls = (cfg->proto == WS_PROTO_WSS);

	if (net_sock_create(hnet, &ctx->sock,
			tls ? NET_PROTO_TLS : NET_PROTO_TCP) != NET_OK) {
		ws_client_close((ws_client_t) ctx);
		return WS_ERR;
	}

	if (tls) {
		if (cfg->tls_ca_certs) {
			(void) net_sock_setopt(ctx->sock, "tls_ca_certs",
					(const uint8_t*) cfg->tls_ca_certs,
					strlen(cfg->tls_ca_certs) + 1);
		}

		if (cfg->tls_dev_cert) {
			(void) net_sock_setopt(ctx->sock, "tls_dev_cert",
					(const uint8_t*) cfg->tls_dev_cert,
					strlen(cfg->tls_dev_cert) + 1);
		}

		/* ASCII timeout strings (include the null terminator or pass strlen) */
		(void) net_sock_setopt(ctx->sock, "tls_server_name",
				(const uint8_t*) cfg->host, strlen(cfg->host));


		(void) net_sock_setopt(ctx->sock, "tls_server_verification", NULL, 0);
	}

	net_sock_setopt(ctx->sock, "sock_read_timeout", (const uint8_t*) "5000",
			strlen("5000"));
	net_sock_setopt(ctx->sock, "sock_write_timeout", (const uint8_t*) "5000",
			strlen("5000"));

	*out = (ws_client_t) ctx;
	return WS_OK;
}

int ws_client_connect(ws_client_t c) {
	ws_client_ctx_t *ctx = (ws_client_ctx_t*) c;
	if (!ctx)
		return WS_ERR;

	if (net_sock_open(ctx->sock, ctx->cfg.host, NULL, ctx->cfg.port,
			0) != NET_OK) {
		msg_error("ws_client_connect: net_sock_open failed");
		return WS_ERR;
	}
	ctx->open = true;

	int rc = ws_client_handshake(ctx);
	if (rc != WS_OK) {
		ws_client_close(c);
		return WS_ERR;
	}
	ctx->handshake_ok = true;
	return WS_OK;
}

int ws_client_is_open(ws_client_t c) {
	ws_client_ctx_t *ctx = (ws_client_ctx_t*) c;
	return (ctx && ctx->open && ctx->handshake_ok) ? 1 : 0;
}

int ws_client_send_text(ws_client_t c, const uint8_t *data, uint32_t len)
{
  return ws_client_send((ws_client_ctx_t*)c, WS_OPCODE_TEXT, data, len);
}

int ws_client_send_binary(ws_client_t c, const uint8_t *data, uint32_t len) {
	return ws_client_send((ws_client_ctx_t*) c, WS_OPCODE_BINARY, data, len);
}

int ws_client_send_ping(ws_client_t c, const uint8_t *data, uint32_t len) {
	return ws_client_send((ws_client_ctx_t*) c, WS_OPCODE_PING, data, len);
}

int ws_client_send_pong(ws_client_t c, const uint8_t *data, uint32_t len) {
	return ws_client_send((ws_client_ctx_t*) c, WS_OPCODE_PONG, data, len);
}

int ws_client_recv(ws_client_t c, uint8_t *buffer, uint32_t buffer_size,
		ws_opcode_t *out_opcode)
{
	ws_client_ctx_t *ctx = (ws_client_ctx_t*) c;
	if (!ctx || !ws_client_is_open(c) || !buffer || buffer_size == 0)
		return WS_ERR;

	while (1) {
		ws_frame_hdr_t h;
		int rc = ws_client_read_frame_hdr(ctx, &h);


		if (rc == WS_CLOSED) {
			ctx->open = false;
			return rc;
		}
		if (rc == WS_TIMEOUT)
			continue;

		if (rc != WS_OK)
			return WS_ERR;

		/* Server->client frames MUST NOT be masked (usually). If masked, we can still unmask, but log it. */
		/* RFC6455 validation: server frames must be unmasked; reject fragmentation/continuations; reject RSV */
		if (ws_validate_frame_hdr(&h, false, true) != WS_OK) {
			msg_error("[WS RX] protocol error: fin=%d rsv=%d%d%d op=0x%x masked=%d len=%lu\r\n",
			       (int)h.fin, (int)h.rsv1,(int)h.rsv2,(int)h.rsv3,(unsigned)h.opcode,(int)h.masked,(unsigned long)h.payload_len);
			ws_client_send_close_code(ctx, WS_CLOSE_PROTOCOL_ERROR);
			net_sock_close(ctx->sock);
			net_sock_destroy(ctx->sock);
			ctx->open = false;
			return WS_ERR;
		}

		size_t payload_len = 0;
		rc = ws_client_read_frame_payload(ctx, &h, buffer, buffer_size,
				&payload_len, ctx->scratch, ctx->scratch_cap);

		if (h.opcode == 0x8) { // CLOSE
		    uint16_t code = 0;
		    if (h.payload_len >= 2) code = (uint16_t)((buffer[0] << 8) | buffer[1]);
		    msg_warning("[WS RX] CLOSE code=%u\r\n", (unsigned)code);
		    if (h.payload_len > 2) {
		        size_t rlen = h.payload_len - 2;
		        char tmp[128];
		        size_t n = (rlen < sizeof(tmp)-1) ? rlen : (sizeof(tmp)-1);
		        memcpy(tmp, buffer+2, n);
		        tmp[n] = '\0';
		        msg_warning("[WS RX] CLOSE reason: %s\r\n", tmp);
		    }
		}

		if (rc != WS_OK) return WS_ERR;

		/* Control frames */
		if (h.opcode == WS_OPCODE_CLOSE) {
		    uint16_t code = 0;

		    /* RFC6455: close payload can be 0 bytes, or >=2 bytes.
		       If 1 byte, it’s protocol error (but we already validate that). */
		    if (payload_len >= 2) {
		        code = (uint16_t)(((uint16_t)buffer[0] << 8) | (uint16_t)buffer[1]);
		    }

		    /* Print close code + optional reason (bytes after the 2-byte code) */
		    msg_warning("[WS RX] CLOSE code=%u payload_len=%lu\r\n",
		           (unsigned)code, (unsigned long)payload_len);

		    if (payload_len > 2) {
		        /* Reason is UTF-8 per RFC. We’ll print it safely as a bounded string. */
		        const uint8_t *reason = buffer + 2;
		        size_t reason_len = payload_len - 2;

		        /* Make a printable copy and NUL terminate */
		        char tmp[128];
		        size_t n = (reason_len < (sizeof(tmp) - 1)) ? reason_len : (sizeof(tmp) - 1);
		        memcpy(tmp, reason, n);
		        tmp[n] = '\0';

		        msg_warning("[WS RX] CLOSE reason: %s\r\n", tmp);
		    }

		    /* Also dump first few payload bytes in hex (helps if reason contains non-printables) */
		    msg_warning("[WS RX] CLOSE payload hex:");
		    for (size_t i = 0; i < payload_len && i < 16; i++) {
		    	msg_warning(" %02X", buffer[i]);
		    }
		    msg_warning("\r\n");

		    /* Reply with CLOSE 1000 (normal closure) and close locally */
		    uint8_t close_payload[2] = { 0x03, 0xE8 }; /* 1000 */
		    ws_send_frame(ctx->sock, WS_OPCODE_CLOSE, close_payload, 2,
		                  true,  /* fin */
		                  true,  /* mask_outgoing (client must mask) */
		                  ctx->scratch, ctx->scratch_cap);

		    ctx->open = false;
		    return WS_CLOSED;
		}

		if (h.opcode == WS_OPCODE_PING) {
			ws_send_frame(ctx->sock, WS_OPCODE_PONG, buffer, payload_len, true,
					true, ctx->scratch, ctx->scratch_cap);
			continue;
		}
		if (h.opcode == WS_OPCODE_PONG) {
			continue;
		}

		/* Data frames */
		if (h.opcode == WS_OPCODE_TEXT || h.opcode == WS_OPCODE_BINARY) {
			if (out_opcode)
				*out_opcode = h.opcode;
			return (int) payload_len;
		}

		/* Ignore continuation / unsupported for now */
	}
}

int ws_client_close(ws_client_t c)
{
    ws_client_ctx_t *ctx = (ws_client_ctx_t*)c;
    if (!ctx) return WS_ERR;

    /* If already closed, just free */
    if (!ctx->open) {
        if (ctx->rxbuf) free(ctx->rxbuf);
        if (ctx->scratch) free(ctx->scratch);
        free(ctx);
        return WS_OK;
    }

    /* 1) Send CLOSE(1000) masked */
    uint8_t close_payload[2] = { 0x03, 0xE8 }; /* 1000 */
    (void)ws_send_frame(ctx->sock, WS_OPCODE_CLOSE,
                        close_payload, sizeof(close_payload),
                        true,  /* fin */
                        true,  /* client must mask */
                        ctx->scratch, ctx->scratch_cap);

    /* 2) Wait briefly for peer CLOSE (best effort) */
    const uint32_t timeout_ms = 2000;
    const uint32_t start = HAL_GetTick();

    while ((uint32_t)(HAL_GetTick() - start) < timeout_ms) {

        ws_frame_hdr_t h;
        int hr = ws_client_read_frame_hdr(ctx, &h);

        if (hr == WS_TIMEOUT) {
            HAL_Delay(10);
            continue;
        }
        if (hr == WS_CLOSED) {
            /* transport already closed */
            break;
        }
        if (hr != WS_OK) {
            /* protocol/IO error while waiting */
            break;
        }

        /* Read payload (drain if too big) */
        uint8_t tmp[128];
        size_t out_len = 0;

        int pr = ws_client_read_frame_payload(ctx, &h, tmp, sizeof(tmp), &out_len,
                                              ctx->scratch, ctx->scratch_cap);
        if (pr != WS_OK) {
            break;
        }

        if (h.opcode == WS_OPCODE_CLOSE) {
            /* If payload_len == 1 => protocol error; ignore and break */
            break;
        }

        if (h.opcode == WS_OPCODE_PING) {
            /* reply pong while closing */
            (void)ws_send_frame(ctx->sock, WS_OPCODE_PONG,
                                tmp, out_len,
                                true, true,
                                ctx->scratch, ctx->scratch_cap);
            continue;
        }

        /* ignore any other frames while closing */
    }

    /* 3) Now close transport */
    net_sock_close(ctx->sock);
    net_sock_destroy(ctx->sock);
    ctx->open = false;

    /* 4) Free */
    if (ctx->rxbuf) free(ctx->rxbuf);
    if (ctx->scratch) free(ctx->scratch);
    free(ctx);

    return WS_OK;
}

void ws_client_run(void) {
	int rc = WS_ERR;
	ws_client_t c;
	ws_client_cfg_t cfg = { .host = "ws.postman-echo.com", .port = 80,
			.resource = "/raw", .proto = WS_PROTO_WS, .origin = NULL,
			.subprotocol = NULL };
	const char *msg = "hello from IOT board";

	if ((rc = ws_client_create(&c, &cfg)) != WS_OK) {
		msg_error("error creating websocket connection...");
	}
	if (rc == WS_OK) {
		rc = ws_client_connect(c);
	}

	if (rc == WS_OK) {
		rc = ws_client_send_text(c, (uint8_t*) msg, strlen(msg));
	} else {
		msg_error("websocket connect failed...");
	}

	uint8_t rx[256];
	ws_opcode_t op;
	if (rc == WS_OK) {
		int n = ws_client_recv(c, rx, sizeof(rx) - 1, &op);
		if (n > 0) {
			rx[n] = 0;
			msg_info("RX: %s\n", rx);
		}
	}
	ws_client_close(c);
}
