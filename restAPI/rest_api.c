/*
 * rest_api.c
 *
 *  Created on: Dec 15, 2025
 *      Author: Daruin Solano
 */


#include "rest_api.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ---------- small helpers ---------- */

static bool method_eq(const char *a, const char *b) { return a && b && strcmp(a,b)==0; }
static bool path_eq(const char *a, const char *b)   { return a && b && strcmp(a,b)==0; }

static const char *find_header_value(const char *headers, uint32_t headers_len, const char *name)
{
    /* Very small, case-insensitive header lookup.
       Returns pointer into headers at start of value (after ':', spaces). */
    if (!headers || !name) return NULL;

    size_t nlen = strlen(name);
    const char *p = headers;
    const char *end = headers + headers_len;

    while (p < end) {
        /* line end */
        const char *line_end = strstr(p, "\r\n");
        if (!line_end) line_end = end;

        /* find ':' */
        const char *colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon) {
            size_t key_len = (size_t)(colon - p);
            /* compare header name case-insensitively */
            if (key_len == nlen) {
                bool match = true;
                for (size_t i=0;i<nlen;i++) {
                    if (tolower((unsigned char)p[i]) != tolower((unsigned char)name[i])) {
                        match = false; break;
                    }
                }
                if (match) {
                    const char *v = colon + 1;
                    while (v < line_end && (*v==' ' || *v=='\t')) v++;
                    return v; /* value start (not null-terminated) */
                }
            }
        }

        p = (line_end < end) ? (line_end + 2) : end;
    }
    return NULL;
}

static bool header_value_starts_with(const char *val, const char *prefix)
{
    if (!val || !prefix) return false;
    while (*prefix) {
        if (tolower((unsigned char)*val++) != tolower((unsigned char)*prefix++)) return false;
    }
    return true;
}

/* URL decode: converts '+' to space and %XX hex.
   Writes decoded string into dst (dst_len includes terminator).
   Returns decoded length, or -1 on error. */
static int url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    if (!dst || dst_len == 0 || !src) return -1;

    size_t di = 0;
    for (size_t si = 0; si < src_len; ++si) {
        if (di + 1 >= dst_len) break;

        char c = src[si];
        if (c == '+') {
            dst[di++] = ' ';
        } else if (c == '%' && (si + 2) < src_len) {
            char h1 = src[si+1], h2 = src[si+2];
            if (isxdigit((unsigned char)h1) && isxdigit((unsigned char)h2)) {
                int v1 = isdigit((unsigned char)h1) ? (h1 - '0') : (tolower((unsigned char)h1) - 'a' + 10);
                int v2 = isdigit((unsigned char)h2) ? (h2 - '0') : (tolower((unsigned char)h2) - 'a' + 10);
                dst[di++] = (char)((v1 << 4) | v2);
                si += 2;
            } else {
                dst[di++] = c; /* keep '%' if malformed */
            }
        } else {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
    return (int)di;
}

/* Parse application/x-www-form-urlencoded into cJSON object of strings. */
static cJSON *parse_form_urlencoded(const char *body, uint32_t body_len)
{
    if (!body || body_len == 0) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    const char *p = body;
    const char *end = body + body_len;

    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *pair_end = amp ? amp : end;

        const char *eq = memchr(p, '=', (size_t)(pair_end - p));
        const char *k_end = eq ? eq : pair_end;
        const char *v_start = eq ? (eq + 1) : pair_end;

        /* decode key */
        char kdec[64];
        size_t klen = (size_t)(k_end - p);
        if (klen >= sizeof(kdec)) klen = sizeof(kdec) - 1;
        url_decode(kdec, sizeof(kdec), p, klen);

        /* decode value */
        char vdec[256];
        size_t vlen = (size_t)(pair_end - v_start);
        if (vlen >= sizeof(vdec)) vlen = sizeof(vdec) - 1;
        url_decode(vdec, sizeof(vdec), v_start, vlen);

        if (kdec[0]) {
            /* If a key appears multiple times, last one wins (simple) */
            cJSON_ReplaceItemInObject(root, kdec, cJSON_CreateString(vdec));
        }

        p = amp ? (amp + 1) : end;
    }

    return root;
}

/* Decide how to parse body:
   - JSON -> parse
   - form -> parse into object
   Returns cJSON* or NULL on failure. */
static cJSON *rest_parse_body(const http_srv_request_t *req)
{
    if (!req || !req->body || req->body_len == 0) return NULL;

    /* find Content-Type */
    const char *ct = find_header_value(req->headers, req->headers_len, "Content-Type");

    if (ct && header_value_starts_with(ct, "application/json")) {
        return cJSON_ParseWithLength(req->body, (size_t)req->body_len);
    }

    if (ct && header_value_starts_with(ct, "application/x-www-form-urlencoded")) {
        return parse_form_urlencoded(req->body, req->body_len);
    }

    /* If no content-type, you can choose a default:
       Many embedded clients send JSON without header; you can attempt JSON first. */
    cJSON *j = cJSON_ParseWithLength(req->body, (size_t)req->body_len);
    if (j) return j;

    /* fallback to form */
    return parse_form_urlencoded(req->body, req->body_len);
}

/* ---------- public API ---------- */

void rest_api_init(rest_api_t *api, const rest_route_t *routes, size_t route_count)
{
    if (!api) return;
    api->routes = routes;
    api->route_count = route_count;
    api->auth = NULL;
    api->pretty_json = false;
}

void rest_api_set_auth(rest_api_t *api, rest_auth_fn auth_cb)
{
    if (!api) return;
    api->auth = auth_cb;
}

void rest_api_set_pretty(rest_api_t *api, bool pretty)
{
    if (!api) return;
    api->pretty_json = pretty;
}

static int send_json_string(http_srv_t *hs, uint32_t status, const char *json_str)
{
    const char *hdr_extra =
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n";

    return http_srv_send_response(
        hs,
        status,
        (status == 200) ? "OK" :
        (status == 201) ? "Created" :
        (status == 204) ? "No Content" :
        (status == 400) ? "Bad Request" :
        (status == 401) ? "Unauthorized" :
        (status == 404) ? "Not Found" :
        (status == 415) ? "Unsupported Media Type" :
        (status == 500) ? "Internal Server Error" : "OK",
        "application/json",
        (const uint8_t *)json_str,
        (json_str ? strlen(json_str) : 0),
        hdr_extra
    );
}

int rest_send_json(http_srv_t *hs, uint32_t status, cJSON *obj, bool pretty)
{
    if (!hs) return HTTP_ERR;

    if (!obj) {
        return send_json_string(hs, status, "{}");
    }

    char *json_str = pretty ? cJSON_Print(obj) : cJSON_PrintUnformatted(obj);
    if (!json_str) return HTTP_ERR;

    int rc = send_json_string(hs, status, json_str);
    free(json_str);
    return rc;
}

int rest_send_error(http_srv_t *hs, uint32_t status, const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return HTTP_ERR;

    cJSON_AddStringToObject(root, "error", code ? code : "error");
    cJSON_AddStringToObject(root, "message", message ? message : "");

    int rc = rest_send_json(hs, status, root, false);
    cJSON_Delete(root);
    return rc;
}

const char *rest_query_get(const http_srv_request_t *req, const char *key,
                           char *out, size_t out_len)
{
    if (!req || !key || !out || out_len == 0) return NULL;
    out[0] = 0;

    const char *q = req->query;
    if (!q || !q[0]) return NULL;

    const size_t klen = strlen(key);
    const char *p = q;

    while (*p) {
        const char *tok = p;
        const char *amp = strchr(tok, '&');
        size_t tok_len = amp ? (size_t)(amp - tok) : strlen(tok);

        if (tok_len > klen + 1 && strncmp(tok, key, klen) == 0 && tok[klen] == '=') {
            const char *val = tok + klen + 1;
            size_t val_len = tok_len - (klen + 1);
            if (val_len >= out_len) val_len = out_len - 1;
            memcpy(out, val, val_len);
            out[val_len] = 0;

            /* URL decode query value too (nice improvement) */
            char tmp[256];
            size_t copy = strlen(out);
            if (copy >= sizeof(tmp)) copy = sizeof(tmp) - 1;
            memcpy(tmp, out, copy);
            tmp[copy] = 0;
            url_decode(out, out_len, tmp, copy);

            return out;
        }

        p = amp ? (amp + 1) : (tok + tok_len);
        if (!amp) break;
    }
    return NULL;
}

int rest_api_dispatch(rest_api_t *api, http_srv_t *hs, const http_srv_request_t *req)
{
    if (!api || !hs || !req) return HTTP_ERR;

    if (api->auth && !api->auth(req)) {
        rest_send_error(hs, 401, "unauthorized", "Missing/invalid credentials");
        http_srv_next_conn(hs);
        return HTTP_OK;
    }

    const rest_route_t *rt = NULL;
    for (size_t i = 0; i < api->route_count; ++i) {
        if (method_eq(req->method, api->routes[i].method) &&
            path_eq(req->path, api->routes[i].path)) {
            rt = &api->routes[i];
            break;
        }
    }

    if (!rt) {
        rest_send_error(hs, 404, "not_found", "Unknown endpoint");
        http_srv_next_conn(hs);
        return HTTP_OK;
    }

    cJSON *body_in = NULL;
    if (rt->parse_body) {
        body_in = rest_parse_body(req);
        if (!body_in) {
            /* If body expected but missing/invalid */
            rest_send_error(hs, 400, "bad_request", "Missing or invalid body");
            http_srv_next_conn(hs);
            return HTTP_OK;
        }
    }

    cJSON *json_out = NULL;
    uint32_t status = 200;

    int hrc = rt->fn(hs, req, body_in, &json_out, &status);

    if (body_in) cJSON_Delete(body_in);

    if (hrc != HTTP_OK) {
        if (json_out) cJSON_Delete(json_out);
        rest_send_error(hs, 500, "handler_error", "Internal handler error");
        http_srv_next_conn(hs);
        return HTTP_OK;
    }

    int src = rest_send_json(hs, status, json_out, api->pretty_json);
    if (json_out) cJSON_Delete(json_out);

    http_srv_next_conn(hs);
    return (src == HTTP_OK) ? HTTP_OK : HTTP_ERR;
}
