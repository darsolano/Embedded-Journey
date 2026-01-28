#include "ws_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* GUID required by RFC 6455 for Sec-WebSocket-Accept */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* ---------- transport helpers ---------- */

int ws_send_all(net_sockhnd_t sock, const uint8_t *buf, size_t len) {
	size_t sent = 0;
	while (sent < len) {
		int rc = net_sock_send(sock, (uint8_t*) (buf + sent),
				(int) (len - sent));
		if (rc <= 0) {
			msg_error("ws_send_all: rc=%d sent=%lu/%lu\n", rc,
					(unsigned long )sent, (unsigned long )len);
			return WS_ERR;
		}
		sent += (size_t) rc;
	}
	return WS_OK;
}

int ws_recv_exact(net_sockhnd_t sock, uint8_t *buf, size_t len) {
	size_t got = 0;
	while (got < len) {
		int rc = net_sock_recv(sock, buf + got, (int) (len - got));
		if (rc == 0) {
			/* peer closed */
			return WS_CLOSED;
		}
		if (rc == NET_NO_DATA) {
			/* treat as timeout/no-data */
			return WS_TIMEOUT;
		}
		if (rc < 0) {
			msg_error("ws_recv_exact: rc=%d got=%lu/%lu\n", rc,
					(unsigned long )got, (unsigned long )len);
			return WS_ERR;
		}
		got += (size_t) rc;
	}
	return WS_OK;
}

/**
 * @brief Receive data from a socket until a delimiter sequence is found.
 *
 * This function accumulates received bytes into @p buf until the byte sequence
 * @p delim (length @p delim_len) is found anywhere inside the accumulated data.
 * The buffer is always kept NUL-terminated (buf[len] = 0) so that callers may
 * safely treat it as a C-string for HTTP header parsing/debug.
 *
 * IMPORTANT BEHAVIOR:
 * - NET_NO_DATA / NET_TIMEOUT are treated as "no bytes available yet" and DO NOT
 *   modify the buffer length. The function continues waiting for more bytes.
 * - rc == 0 or rc == NET_EOF is treated as a clean socket close (WS_CLOSED).
 * - Any other rc < 0 is treated as a fatal receive error (WS_ERR).
 *
 * Why this matters on STM32 WiFi modules:
 * Some drivers return a negative code (e.g., -3) to indicate "no data available"
 * rather than blocking. The previous implementation incorrectly did `len += rc`
 * even when rc was negative, corrupting `len` and causing header parsing failures
 * such as "missing Sec-WebSocket-Key" even though the header was present.
 *
 * @param sock       Socket handle to read from.
 * @param buf        Destination buffer to accumulate data into.
 * @param buf_cap    Capacity of @p buf in bytes.
 * @param delim      Delimiter byte sequence to search for (e.g. "\r\n\r\n").
 * @param delim_len  Length of delimiter sequence in bytes.
 * @param out_len    Optional pointer that receives total accumulated length.
 *
 * @return WS_OK if delimiter found, WS_CLOSED if peer closed, WS_TIMEOUT if
 *         timeout/no-data is surfaced (not used here unless your net layer does),
 *         or WS_ERR on error/buffer full.
 */
int ws_recv_until(net_sockhnd_t sock, uint8_t *buf, size_t buf_cap,
		const char *delim, size_t delim_len, size_t *out_len) {
	if (!buf || buf_cap == 0 || !delim || delim_len == 0)
		return WS_ERR;

	size_t len = 0;
	memset(buf,0, buf_cap);

	while (len < buf_cap - 1) {

		/* 1) If we already have the delimiter, stop immediately. */
		if (len >= delim_len) {
			for (size_t i = 0; i + delim_len <= len; i++) {
				if (memcmp(buf + i, delim, delim_len) == 0) {
					if (out_len)
						*out_len = len;
					return WS_OK;
				}
			}
		}

		/* 2) Receive more data */
		int rc = net_sock_recv(sock, buf + len, (int) (buf_cap - 1 - len));

		/* Debug: print only the bytes accumulated so far (safe) */
		if (rc > 0) {
			LOG_DEBUG(
					"ws_recv_until: rc=%d <---> Buffer Content (%d bytes):\n%s",
					(int )rc, strlen((char*)buf), (char* )buf);
		}

		/* 3) No data available yet -> keep waiting, DO NOT change len */
		if (rc == NET_NO_DATA || rc == NET_TIMEOUT) {
			continue;
		}

		/* Peer closed (some stacks report close as rc==0, others as NET_EOF, others as NET_ERR but with 0 bytes) */
		if (rc == 0 || rc == NET_EOF) {
			if (out_len)
				*out_len = len;
			return WS_CLOSED;
		}

		/* If your driver maps "client disconnected" to NET_ERR, treat it as CLOSED when no bytes were read */
		if (rc < 0) {
			/* If we have not accumulated anything and the socket says error, it is very often a disconnect */
			if (len == 0) {
				if (out_len)
					*out_len = len;
				return WS_CLOSED;
			}
			msg_error("ws_recv_until: rc=%d\n", rc);
			if (out_len)
				*out_len = len;
			return WS_ERR;
		}

		/* 6) We received bytes; advance length and NUL-terminate */
		len += (size_t) rc;
		buf[len] = 0;
	}

	msg_error("ws_recv_until: buffer full before delimiter\n");
	if (out_len)
		*out_len = len;
	return WS_ERR;
}

/* ---------- minimal HTTP handshake parsing ---------- */

int ws_http_status_is_101(const uint8_t *hdr, size_t len) {
	/* Accept either "HTTP/1.1 101" or "HTTP/1.0 101" */
	if (!hdr || len < 12)
		return 0;
	if (memcmp(hdr, "HTTP/1.", 7) != 0)
		return 0;

	/* Find first space and then status code */
	const char *s = (const char*) hdr;
	const char *end = (const char*) hdr + len;
	const char *sp = memchr(s, ' ', (size_t) (end - s));
	if (!sp || (end - sp) < 5)
		return 0;
	/* sp+1..sp+3 are status digits */
	return (sp[1] == '1' && sp[2] == '0' && sp[3] == '1');
}

const char* ws_http_find_header_value(const uint8_t *hdr, size_t len,
		const char *name, char *out, size_t out_cap) {
	if (!hdr || !name || !out || out_cap == 0)
		return NULL;

	const char *p = (const char*) hdr;
	const char *end = p + len;

	/* Skip request/status line (accept CRLF or LF-only) */
	const char *line = strstr(p, "\r\n");
	size_t eol_advance = 2;
	if (!line) {
		line = strchr(p, '\n');
		eol_advance = 1;
	}
	if (!line)
		return NULL;
	p = line + eol_advance;

	size_t name_len = strlen(name);

	while (p < end) {
		const char *eol = strstr(p, "\r\n");
		size_t adv = 2;
		if (!eol) {
			eol = strchr(p, '\n');
			adv = 1;
		}
		if (!eol)
			eol = end;

		/* Empty line => end of headers */
		if (eol == p)
			break;

		if ((size_t) (eol - p) > name_len + 1) {
			if (strncasecmp(p, name, name_len) == 0 && p[name_len] == ':') {
				const char *v = p + name_len + 1;
				while (v < eol && (*v == ' ' || *v == '\t'))
					v++;

				size_t vlen = (size_t) (eol - v);
				if (vlen >= out_cap)
					vlen = out_cap - 1;
				memcpy(out, v, vlen);
				out[vlen] = '\0';

				/* Trim trailing whitespace */
				while (vlen > 0
						&& (out[vlen - 1] == ' ' || out[vlen - 1] == '\t')) {
					out[--vlen] = '\0';
				}
				return out;
			}
		}

		p = eol + adv;
	}
	return NULL;
}

/* ---------- Accept compute ---------- */

int ws_compute_accept(const char *client_key_b64, char *accept_out,
		size_t accept_out_len) {
	if (!client_key_b64 || !accept_out || accept_out_len == 0)
		return WS_ERR;

	uint8_t sha_in[128];
	uint8_t sha_out[20];
	char b64[64];

	size_t klen = strlen(client_key_b64);
	size_t glen = strlen(WS_GUID);
	if (klen + glen > sizeof(sha_in))
		return WS_ERR;

	memcpy(sha_in, client_key_b64, klen);
	memcpy(sha_in + klen, WS_GUID, glen);

	ws_sha1(sha_in, klen + glen, sha_out);

	if (ws_base64(sha_out, sizeof(sha_out), b64, sizeof(b64)) < 0)
		return WS_ERR;

	size_t blen = strlen(b64);
	if (blen + 1 > accept_out_len)
		return WS_ERR;
	memcpy(accept_out, b64, blen + 1);
	return WS_OK;
}

/* ---------- frame parsing/sending ---------- */

int ws_read_frame_hdr(net_sockhnd_t sock, ws_frame_hdr_t *h) {
	if (!h)
		return WS_ERR;
	uint8_t b[2];
	int rc = ws_recv_exact(sock, b, 2);
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
		rc = ws_recv_exact(sock, ext, 2);
		if (rc != WS_OK)
			return rc;
		plen = ((uint64_t) ext[0] << 8) | (uint64_t) ext[1];
	} else if (plen == 127) {
		uint8_t ext[8];
		rc = ws_recv_exact(sock, ext, 8);
		if (rc != WS_OK)
			return rc;
		plen = 0;
		for (int i = 0; i < 8; i++) {
			plen = (plen << 8) | (uint64_t) ext[i];
		}
	}
	h->payload_len = plen;

	if (h->masked) {
		rc = ws_recv_exact(sock, h->mask_key, 4);
		if (rc != WS_OK)
			return rc;
	} else {
		memset(h->mask_key, 0, 4);
	}
	return WS_OK;
}

static void ws_unmask(uint8_t *buf, size_t len, const uint8_t mask_key[4]) {
	for (size_t i = 0; i < len; i++) {
		buf[i] ^= mask_key[i & 3u];
	}
}

int ws_read_frame_payload(net_sockhnd_t sock, const ws_frame_hdr_t *h,
		uint8_t *dst, size_t dst_cap, size_t *out_len, uint8_t *scratch,
		size_t scratch_cap) {
	if (!h)
		return WS_ERR;
	if (out_len)
		*out_len = 0;

	/* If payload fits in dst, read it there; otherwise drain to scratch */
	if (h->payload_len > dst_cap) {
		/* Drain (and discard) to keep stream sane */
		uint64_t to_drain = h->payload_len;
		while (to_drain) {
			size_t chunk =
					(to_drain > scratch_cap) ? scratch_cap : (size_t) to_drain;
			int rc = ws_recv_exact(sock, scratch, chunk);
			if (rc != WS_OK)
				return rc;
			to_drain -= chunk;
		}
		return WS_ERR;
	}

	if (h->payload_len == 0) {
		if (out_len)
			*out_len = 0;
		return WS_OK;
	}

	int rc = ws_recv_exact(sock, dst, (size_t) h->payload_len);
	if (rc != WS_OK)
		return rc;

	if (h->masked) {
		ws_unmask(dst, (size_t) h->payload_len, h->mask_key);
	}
	if (out_len)
		*out_len = (size_t) h->payload_len;
	return WS_OK;
}

static void ws_make_mask_key(uint8_t key[4]) {
	/* Simple rand-based; seed rand() in main */
	key[0] = (uint8_t) (rand() & 0xFF);
	key[1] = (uint8_t) (rand() & 0xFF);
	key[2] = (uint8_t) (rand() & 0xFF);
	key[3] = (uint8_t) (rand() & 0xFF);
}

int ws_send_frame(net_sockhnd_t sock, ws_opcode_t opcode,
		const uint8_t *payload, size_t payload_len, bool fin,
		bool mask_outgoing, uint8_t *scratch, size_t scratch_cap)
{
	uint8_t hdr[14];
	size_t hdr_len = 0;

	hdr[0] = (uint8_t) ((fin ? 0x80 : 0x00) | ((uint8_t) opcode & 0x0F));

	if (payload_len <= 125) {
		hdr[1] = (uint8_t) (payload_len & 0x7F);
		hdr_len = 2;
	} else if (payload_len <= 0xFFFF) {
		hdr[1] = 126;
		hdr[2] = (uint8_t) ((payload_len >> 8) & 0xFF);
		hdr[3] = (uint8_t) (payload_len & 0xFF);
		hdr_len = 4;
	} else {
		hdr[1] = 127;
		uint64_t L = (uint64_t) payload_len;
		hdr[2] = (uint8_t) ((L >> 56) & 0xFF);
		hdr[3] = (uint8_t) ((L >> 48) & 0xFF);
		hdr[4] = (uint8_t) ((L >> 40) & 0xFF);
		hdr[5] = (uint8_t) ((L >> 32) & 0xFF);
		hdr[6] = (uint8_t) ((L >> 24) & 0xFF);
		hdr[7] = (uint8_t) ((L >> 16) & 0xFF);
		hdr[8] = (uint8_t) ((L >> 8) & 0xFF);
		hdr[9] = (uint8_t) (L & 0xFF);
		hdr_len = 10;
	}

	uint8_t mask_key[4] = { 0 };
	if (mask_outgoing) {
		hdr[1] |= 0x80;
		ws_make_mask_key(mask_key);
		memcpy(&hdr[hdr_len], mask_key, 4);
		hdr_len += 4;
	}

	if (mask_outgoing) {
		msg_warning("[WS TX] mask_key=%02X %02X %02X %02X\r\n",
				hdr[hdr_len - 4], hdr[hdr_len - 3], hdr[hdr_len - 2],
				hdr[hdr_len - 1]);
	}

	/* Send header (all) */
	if (ws_send_all(sock, hdr, hdr_len) != WS_OK)
		return WS_ERR;

	/* Send payload */
	if (!payload || payload_len == 0)
		return WS_OK;

	if (!mask_outgoing) {
		return ws_send_all(sock, payload, payload_len);
	}

	/* Masked send: XOR into scratch in chunks */
	size_t off = 0;
	if (!scratch || scratch_cap == 0)
		return WS_ERR;

	while (off < payload_len) {
		size_t chunk = payload_len - off;
		if (chunk > scratch_cap)
			chunk = scratch_cap;

		for (size_t i = 0; i < chunk; i++) {
			scratch[i] = payload[off + i] ^ mask_key[(off + i) & 3u];
		}
		if (ws_send_all(sock, scratch, chunk) != WS_OK)
			return WS_ERR;
		off += chunk;
	}

	return WS_OK;
}

int ws_validate_frame_hdr(const ws_frame_hdr_t *h, bool expect_masked,
bool reject_fragmentation) {
	if (!h)
		return WS_ERR;

	/* RSV bits must be 0 unless an extension negotiated (we do not support any) */
	if (h->rsv1 || h->rsv2 || h->rsv3)
		return WS_ERR;

	/* Enforce masking direction */
	if (expect_masked) {
		if (!h->masked)
			return WS_ERR;
	} else {
		if (h->masked)
			return WS_ERR;
	}

	/* Reject fragmentation / continuation if requested */
	if (reject_fragmentation) {
		if (!h->fin)
			return WS_ERR;
		if (((uint8_t) h->opcode) == 0x00)
			return WS_ERR; /* CONTINUATION */
	}

	/* Valid opcodes: 0x1 text, 0x2 bin, 0x8 close, 0x9 ping, 0xA pong */
	switch ((uint8_t) h->opcode) {
	case 0x1:
	case 0x2:
		break;
	case 0x8:
	case 0x9:
	case 0xA:
		break;
	case 0x0: /* continuation */
		return WS_ERR;
	default:
		return WS_ERR;
	}

	/* Control frame rules */
	if (((uint8_t) h->opcode) >= 0x8) {
		if (!h->fin)
			return WS_ERR;
		if (h->payload_len > 125)
			return WS_ERR;
		/* CLOSE payload length cannot be 1 */
		if (((uint8_t) h->opcode) == 0x8 && h->payload_len == 1)
			return WS_ERR;
	}

	return WS_OK;
}
